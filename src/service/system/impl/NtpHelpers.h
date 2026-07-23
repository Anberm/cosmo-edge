// NtpHelpers.h — Shared NTP protocol helpers for TimeServiceImpl and TimeServiceConfig.
// Extracted from duplicated anonymous namespaces (DEBT-013).
//
// NOTE: This header is intentionally designed for internal use within the
//       TimeService implementation files only. All functions and types are
//       in an anonymous namespace to preserve internal linkage.

#pragma once

#include <arpa/inet.h>
#include <event2/dns.h>
#include <event2/event.h>
#include <event2/util.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "platform/SystemTime.h"
#include "util/Log.h"

namespace cosmo::service {
namespace {

    // ---------------------------------------------------------------------------
    // Constants
    // ---------------------------------------------------------------------------

    constexpr uint32_t kJan1970       = 0x83aa7e80;
    constexpr double kFracPerMs       = 4.294967296E6;  // 2^32 / 1000
    constexpr int kNtpTimeoutMs       = 10000;          // send/recv timeout
    constexpr int kMinSyncIntervalSec = 60;
    constexpr int kMaxSyncIntervalMin = 1440;
    constexpr int kResolverPollMs     = 20;
    constexpr int kResolverTimeoutMs  = 10000;

    // ---------------------------------------------------------------------------
    // NTP protocol structures
    // ---------------------------------------------------------------------------

    struct NtpTime {
        uint32_t seconds{0};
        uint32_t fraction{0};
    };

    struct NtpPacket {
        int8_t li_vn_mode{0};
        uint8_t stratum{0};
        char poll{0};
        char precision{0};
        int root_delay{0};
        uint32_t root_dispersion{0};
        int reference_id{0};
        NtpTime reference_ts;
        NtpTime originate_ts;
        NtpTime receive_ts;
        NtpTime transmit_ts;
    };

    static_assert(sizeof(NtpPacket) == 48, "NTP wire packet must be exactly 48 bytes");

    // ---------------------------------------------------------------------------
    // Byte-order conversion
    // ---------------------------------------------------------------------------

    // Convert NTP packet fields from network to host byte order
    inline void NtohPacket(NtpPacket& pkt) {
        pkt.root_delay            = ntohl(pkt.root_delay);
        pkt.root_dispersion       = ntohl(pkt.root_dispersion);
        pkt.reference_id          = ntohl(pkt.reference_id);
        pkt.reference_ts.seconds  = ntohl(pkt.reference_ts.seconds);
        pkt.reference_ts.fraction = ntohl(pkt.reference_ts.fraction);
        pkt.originate_ts.seconds  = ntohl(pkt.originate_ts.seconds);
        pkt.originate_ts.fraction = ntohl(pkt.originate_ts.fraction);
        pkt.receive_ts.seconds    = ntohl(pkt.receive_ts.seconds);
        pkt.receive_ts.fraction   = ntohl(pkt.receive_ts.fraction);
        pkt.transmit_ts.seconds   = ntohl(pkt.transmit_ts.seconds);
        pkt.transmit_ts.fraction  = ntohl(pkt.transmit_ts.fraction);
    }

    // Convert NTP packet fields from host to network byte order
    inline void HtonPacket(NtpPacket& pkt) {
        pkt.root_delay            = htonl(pkt.root_delay);
        pkt.root_dispersion       = htonl(pkt.root_dispersion);
        pkt.reference_id          = htonl(pkt.reference_id);
        pkt.reference_ts.seconds  = htonl(pkt.reference_ts.seconds);
        pkt.reference_ts.fraction = htonl(pkt.reference_ts.fraction);
        pkt.originate_ts.seconds  = htonl(pkt.originate_ts.seconds);
        pkt.originate_ts.fraction = htonl(pkt.originate_ts.fraction);
        pkt.receive_ts.seconds    = htonl(pkt.receive_ts.seconds);
        pkt.receive_ts.fraction   = htonl(pkt.receive_ts.fraction);
        pkt.transmit_ts.seconds   = htonl(pkt.transmit_ts.seconds);
        pkt.transmit_ts.fraction  = htonl(pkt.transmit_ts.fraction);
    }

    // ---------------------------------------------------------------------------
    // Timestamp conversion utilities
    // ---------------------------------------------------------------------------

    // Convert NTP timestamp to timeval (microseconds since 1970-01-01)
    inline void TimestampToTimeval(const NtpTime& ts, long& out_sec, long& out_usec) {
        if (ts.seconds >= kJan1970) {
            out_sec  = static_cast<long>(ts.seconds - kJan1970);
            out_usec = static_cast<long>(ts.fraction / kFracPerMs * 1000.0);
        } else {
            out_sec  = 0;
            out_usec = 0;
        }
    }

    // Convert timeval to NTP timestamp
    inline void TimevalToTimestamp(NtpTime& ts, long sec, long usec) {
        ts.seconds  = static_cast<uint32_t>(sec + kJan1970);
        ts.fraction = static_cast<uint32_t>(usec / 1000.0 * kFracPerMs);
    }

    // Convert timeval to 100-nanosecond units
    inline int64_t TimevalToNs100(long sec, long usec) {
        return 10000000LL * sec + 10LL * usec;
    }

    // Convert NTP timestamp to 100-nanosecond units
    inline int64_t TimestampToNs100(const NtpTime& ts) {
        long sec = 0, usec = 0;
        TimestampToTimeval(ts, sec, usec);
        return TimevalToNs100(sec, usec);
    }

    // Get current system time in 100-nanosecond units since epoch
    inline int64_t GetTimevalueNs100() {
        struct timeval tv {};
        gettimeofday(&tv, nullptr);
        return 10000000LL * tv.tv_sec + 10LL * tv.tv_usec;
    }

    // ---------------------------------------------------------------------------
    // NTP packet initialization
    // ---------------------------------------------------------------------------

    // Initialize NTP request packet (client mode, version 3)
    inline void InitPacket(NtpPacket& pkt) {
        pkt                        = NtpPacket{};
        constexpr uint8_t kLeap    = 0;
        constexpr uint8_t kVersion = 3;
        constexpr uint8_t kMode    = 3;  // client
        pkt.li_vn_mode             = static_cast<int8_t>((kLeap << 6) | (kVersion << 3) | kMode);
        pkt.poll                   = 4;
        pkt.precision              = static_cast<char>((-6) & 0xFF);
        pkt.root_delay             = (1 << 16);
        pkt.root_dispersion        = (1 << 16);
    }

    inline bool SameNtpTime(const NtpTime& lhs, const NtpTime& rhs) {
        return lhs.seconds == rhs.seconds && lhs.fraction == rhs.fraction;
    }

    // Validate the response fields that bind the server reply to this request.
    // The packet must already be converted to host byte order.
    inline bool ValidateNtpResponse(const NtpPacket& response, const NtpTime& request_transmit_ts) {
        const auto flags   = static_cast<uint8_t>(response.li_vn_mode);
        const auto leap    = static_cast<uint8_t>((flags >> 6) & 0x03);
        const auto version = static_cast<uint8_t>((flags >> 3) & 0x07);
        const auto mode    = static_cast<uint8_t>(flags & 0x07);

        if (leap == 3 || (version != 3 && version != 4) || mode != 4 || response.stratum == 0 ||
            response.stratum > 15) {
            return false;
        }
        if (!SameNtpTime(response.originate_ts, request_transmit_ts)) {
            return false;
        }
        if (response.receive_ts.seconds < kJan1970 || response.transmit_ts.seconds < kJan1970) {
            return false;
        }
        return TimestampToNs100(response.transmit_ts) >= TimestampToNs100(response.receive_ts);
    }

    // ---------------------------------------------------------------------------
    // Socket helpers
    // ---------------------------------------------------------------------------

    // Set socket send/recv timeout
    inline bool SetSocketTimeout(int sockfd, int timeout_ms) {
        struct timeval tv {};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0 &&
               setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
    }

    inline bool IsValidNtpHost(const std::string& host) {
        if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') {
            return false;
        }

        in_addr numeric_address{};
        if (inet_pton(AF_INET, host.c_str(), &numeric_address) == 1) {
            return true;
        }
        if (host.find_first_not_of("0123456789.") == std::string::npos) {
            return false;
        }

        size_t label_size = 0;
        for (size_t index = 0; index < host.size(); ++index) {
            const auto value = static_cast<unsigned char>(host[index]);
            if (host[index] == '.') {
                if (label_size == 0 || host[index - 1] == '-') {
                    return false;
                }
                label_size = 0;
                continue;
            }
            if ((!std::isalnum(value) && host[index] != '-') || (label_size == 0 && host[index] == '-') ||
                ++label_size > 63) {
                return false;
            }
        }
        return label_size != 0 && host.back() != '-';
    }

    // Resolve an IPv4 address without invoking a shell or creating an untracked
    // resolver thread. libevent performs DNS over non-blocking sockets owned by this
    // worker, so cancellation and destruction remain deterministic.
    inline std::string ResolveDomain(const std::string& host, const std::function<bool()>& is_cancelled = {},
                                     const std::function<void(std::chrono::milliseconds)>& wait = {}) {
        if (!IsValidNtpHost(host)) {
            LOG_ERRO("Invalid NTP host: [{}]", host);
            return "";
        }

        in_addr address{};
        if (inet_pton(AF_INET, host.c_str(), &address) == 1) {
            return host;
        }

        struct ResolutionState {
            bool complete{false};
            int result{EVUTIL_EAI_FAIL};
            std::string ip;
        };

        event_base* event_base = event_base_new();
        if (event_base == nullptr) {
            LOG_ERRO("Failed to create NTP DNS event base for [{}]", host);
            return "";
        }
        evdns_base* dns_base = evdns_base_new(event_base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
        if (dns_base == nullptr) {
            event_base_free(event_base);
            LOG_ERRO("Failed to initialize NTP DNS resolver for [{}]", host);
            return "";
        }

        ResolutionState state;
        evutil_addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        auto* request     = evdns_getaddrinfo(
            dns_base, host.c_str(), nullptr, &hints,
            [](int result, evutil_addrinfo* addresses, void* context) {
                auto& resolution  = *static_cast<ResolutionState*>(context);
                resolution.result = result;
                if (result == 0 && addresses != nullptr) {
                    char value[INET_ADDRSTRLEN] = {};
                    const auto* socket_address  = reinterpret_cast<const sockaddr_in*>(addresses->ai_addr);
                    if (inet_ntop(AF_INET, &socket_address->sin_addr, value, sizeof(value)) != nullptr) {
                        resolution.ip = value;
                    }
                }
                if (addresses != nullptr) {
                    evutil_freeaddrinfo(addresses);
                }
                resolution.complete = true;
            },
            &state);

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kResolverTimeoutMs);
        bool loop_failed = false;
        while (!state.complete && (!is_cancelled || !is_cancelled()) &&
               std::chrono::steady_clock::now() < deadline) {
            if (event_base_loop(event_base, EVLOOP_NONBLOCK) < 0) {
                loop_failed = true;
                break;
            }
            // event_base_loop may synchronously update state through the callback.
            // cppcheck-suppress knownConditionTrueFalse
            if (!state.complete) {
                if (wait) {
                    wait(std::chrono::milliseconds(kResolverPollMs));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kResolverPollMs));
                }
            }
        }
        const bool stopped   = is_cancelled && is_cancelled();
        const bool timed_out = !state.complete && !loop_failed && !stopped;
        if (!state.complete && request != nullptr) {
            evdns_getaddrinfo_cancel(request);
        }
        evdns_base_free(dns_base, 0);
        event_base_free(event_base);

        if (stopped) {
            return "";
        }
        if (loop_failed) {
            LOG_ERRO("DNS event loop failed for NTP host [{}]", host);
            return "";
        }
        if (timed_out) {
            LOG_ERRO("Timed out resolving NTP host [{}]", host);
            return "";
        }
        if (state.result != 0 || state.ip.empty()) {
            LOG_ERRO("Failed to resolve NTP host [{}]: {}", host,
                     state.result == 0 ? "no address" : evutil_gai_strerror(state.result));
            return "";
        }
        return state.ip;
    }

    // Connect UDP socket to NTP server, returns sockfd or -1
    inline int ConnectNtpServer(const std::string& host, int port,
                                const std::function<bool()>& is_cancelled                  = {},
                                const std::function<void(std::chrono::milliseconds)>& wait = {}) {
        LOG_INFO("NTP connecting to host=[{}] port=[{}]", host, port);

        if (port <= 0 || port > 65535) {
            LOG_ERRO("Invalid NTP port: {}", port);
            return -1;
        }

        std::string ip = ResolveDomain(host, is_cancelled, wait);
        if (ip.empty()) {
            if (!is_cancelled || !is_cancelled()) {
                LOG_ERRO("Cannot resolve host: {}", host);
            }
            return -1;
        }
        if (is_cancelled && is_cancelled()) {
            return -1;
        }

        int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockfd < 0) {
            LOG_ERRO("{}", "socket() failed");
            return -1;
        }
        if (!SetSocketTimeout(sockfd, kNtpTimeoutMs)) {
            close(sockfd);
            LOG_ERRO("{}", "setsockopt() failed");
            return -1;
        }

        struct sockaddr_in src {};
        src.sin_family      = AF_INET;
        src.sin_port        = htons(0);
        src.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sockfd, reinterpret_cast<struct sockaddr*>(&src), sizeof(src)) < 0) {
            close(sockfd);
            LOG_ERRO("{}", "bind() failed");
            return -1;
        }

        struct sockaddr_in dst {};
        dst.sin_family = AF_INET;
        dst.sin_port   = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &dst.sin_addr) != 1) {
            close(sockfd);
            LOG_ERRO("Resolved NTP address is invalid: [{}]", ip);
            return -1;
        }

        if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst)) < 0) {
            close(sockfd);
            LOG_ERRO("connect() failed, host: [{}]", host);
            return -1;
        }

        LOG_INFO("NTP server connected, sockfd={}", sockfd);
        return sockfd;
    }

    // ---------------------------------------------------------------------------
    // NTP time application
    // ---------------------------------------------------------------------------

    // Set system time from 100ns-unit timestamp (skip if diff < 1s)
    inline void ApplyNtpTime(int64_t timestamp_ns100) {
        int64_t timestamp_ms = timestamp_ns100 / 10000;

        struct timeval old_tv {};
        gettimeofday(&old_tv, nullptr);
        int64_t old_ms = 1000LL * old_tv.tv_sec + old_tv.tv_usec / 1000;

        int64_t diff_ms = (timestamp_ms > old_ms) ? (timestamp_ms - old_ms) : (old_ms - timestamp_ms);
        if (diff_ms < 1000) {
            return;  // difference too small, skip
        }

        auto result = cosmo::platform::SetSystemTime(timestamp_ms);
        if (result != cosmo::util::ErrorEnum::Success) {
            LOG_WARN("NTP set time failed: [{}] -> [{}] diff:{}ms code:{}", old_ms, timestamp_ms, diff_ms,
                     static_cast<int>(result));
            return;
        }
        LOG_INFO("NTP set time: [{}] -> [{}] diff:{}ms", old_ms, timestamp_ms, diff_ms);
    }

    // ---------------------------------------------------------------------------
    // Timezone parsing
    // ---------------------------------------------------------------------------

    // Parses "+08:00" or "-05:30" format timezone string
    inline bool ParseTimeZoneString(const std::string& tz, uint64_t& is_west, uint64_t& offset) {
        if (tz.length() != 6 || (tz[0] != '+' && tz[0] != '-') || tz[3] != ':') {
            return false;
        }
        if (!std::isdigit(static_cast<unsigned char>(tz[1])) ||
            !std::isdigit(static_cast<unsigned char>(tz[2])) ||
            !std::isdigit(static_cast<unsigned char>(tz[4])) ||
            !std::isdigit(static_cast<unsigned char>(tz[5]))) {
            return false;
        }
        const int h = (tz[1] - '0') * 10 + (tz[2] - '0');
        const int m = (tz[4] - '0') * 10 + (tz[5] - '0');
        if (h > 14 || m > 59 || (h == 14 && m != 0)) {
            return false;
        }
        is_west = (tz[0] == '-') ? 1 : 0;
        offset  = h * 100 + m;
        return true;
    }

}  // namespace
}  // namespace cosmo::service
