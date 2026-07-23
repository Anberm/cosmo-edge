#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "service/path/IUploadStagingService.h"

namespace cosmo::service {

struct UploadStagingConfig {
    std::string root_path;
    std::uint64_t max_total_size{500ULL * 1024 * 1024};
    std::uint64_t max_chunk_size{8ULL * 1024 * 1024};
    std::uint32_t max_chunks{128};
    std::size_t max_sessions_per_principal{4};
    std::size_t max_sessions{16};
    std::uint64_t max_reserved_bytes{2ULL * 1024 * 1024 * 1024};
    std::chrono::milliseconds session_ttl{std::chrono::minutes(30)};
    std::chrono::milliseconds cleanup_interval{std::chrono::minutes(1)};
    std::chrono::milliseconds max_session_lifetime{std::chrono::hours(2)};
};

class UploadStagingServiceImpl final : public IUploadStagingService {
public:
    using Clock       = std::chrono::steady_clock;
    using NowFunction = std::function<Clock::time_point()>;

    UploadStagingServiceImpl();
    explicit UploadStagingServiceImpl(
        UploadStagingConfig config, NowFunction now = []() { return Clock::now(); });
    ~UploadStagingServiceImpl() override;

    util::ErrorEnum Begin(const UploadBeginRequest& request, UploadSessionInfo& info) override;
    util::ErrorEnum AppendChunk(const std::string& principal, const std::string& upload_id,
                                std::uint32_t chunk_index, const std::string& source_path,
                                UploadSessionInfo& info) override;
    util::ErrorEnum Complete(const std::string& principal, const std::string& upload_id,
                             UploadSessionInfo& info) override;
    util::ErrorEnum Consume(const std::string& principal, const std::string& upload_id,
                            UploadPurpose expected_purpose, StagedFileLease& lease) override;
    util::ErrorEnum ConsumeMany(const std::string& principal, const std::vector<std::string>& upload_ids,
                                UploadPurpose expected_purpose,
                                std::vector<StagedFileLease>& leases) override;
    util::ErrorEnum ConsumeLegacyPath(const std::string& principal, const std::string& legacy_path,
                                      UploadPurpose expected_purpose, StagedFileLease& lease) override;
    util::ErrorEnum ConsumeLegacyPaths(const std::string& principal,
                                       const std::vector<std::string>& legacy_paths,
                                       UploadPurpose expected_purpose,
                                       std::vector<StagedFileLease>& leases) override;
    util::ErrorEnum GetLegacyPath(const std::string& principal, const std::string& upload_id,
                                  UploadPurpose expected_purpose, std::string& path) override;
    util::ErrorEnum Cancel(const std::string& principal, const std::string& upload_id) override;
    void CleanupExpired() override;

private:
    enum class SessionState { kOpen, kComplete, kConsuming, kRetired };

    struct ChunkRecord {
        std::uint64_t size{0};
        std::string sha256;
    };

    struct Session {
        std::mutex mutex;
        std::string principal;
        UploadPurpose purpose{UploadPurpose::kModelComponent};
        std::string upload_id;
        std::string client_request_id;
        std::string original_name;
        std::string session_dir;
        std::string payload_path;
        std::string sha256;
        std::string verified_sha256;
        std::uint64_t total_size{0};
        std::uint64_t received_size{0};
        std::uint32_t total_chunks{0};
        std::uint32_t next_chunk_index{0};
        std::uint64_t session_device{0};
        std::uint64_t session_inode{0};
        std::uint64_t payload_device{0};
        std::uint64_t payload_inode{0};
        std::vector<ChunkRecord> chunks;
        Clock::time_point expires_at;
        Clock::time_point absolute_expires_at;
        std::int64_t expires_at_unix_ms{0};
        std::int64_t absolute_expires_at_unix_ms{0};
        SessionState state{SessionState::kOpen};
    };

    static bool IsPurposeValid(UploadPurpose purpose);
    static bool IsSha256Valid(const std::string& sha256);
    static bool IsClientRequestIdValid(const std::string& client_request_id);
    static bool NormalizeOriginalName(const std::string& input, std::string& output);
    static bool IsUuidLike(const std::string& value);
    static std::string MakeOpaqueReference(const Session& session);
    static bool ComputeSha256(int fd, std::string& digest);
    static bool InspectRegularFile(const std::string& path, std::uint64_t max_size, std::uint64_t& size,
                                   std::string& sha256);
    static util::ErrorEnum AppendRegularFile(const std::string& source_path, int destination_fd,
                                             std::uint64_t expected_destination_size, std::uint64_t max_bytes,
                                             std::uint64_t& copied_bytes, std::string& sha256);

    std::shared_ptr<Session> FindSession(const std::string& principal, const std::string& upload_id);
    std::shared_ptr<Session> ResolveSessionLocked(const std::string& principal,
                                                  const std::string& upload_id) const;
    UploadSessionInfo MakeInfo(const Session& session) const;
    bool ImmutableMetadataMatches(const Session& session, const UploadBeginRequest& request,
                                  const std::string& normalized_name) const;
    void RefreshIdleExpiry(Session& session) const;
    bool ValidateRootPath() const;
    bool OpenValidatedPayload(const Session& session, int flags, int& payload_fd) const;
    util::ErrorEnum ValidateCompletedPayload(Session& session, bool establish_digest,
                                             int* verified_fd = nullptr) const;
    util::ErrorEnum ConsumeBatch(const std::string& principal, const std::vector<std::string>& identifiers,
                                 UploadPurpose expected_purpose, bool legacy_references,
                                 std::vector<StagedFileLease>& leases);
    void ReleaseSessionAccountingLocked(const std::shared_ptr<Session>& session);
    void RemoveSessionLocked(const std::shared_ptr<Session>& session);
    void CleanupOrphans();
    void RemoveSessions(const std::vector<std::shared_ptr<Session>>& sessions) const;
    void StartCleanupThread();
    void StopCleanupThread();

    UploadStagingConfig config_;
    NowFunction now_;
    int root_fd_{-1};
    std::uint64_t root_device_{0};
    std::uint64_t root_inode_{0};
    std::mutex sessions_mutex_;
    using SessionMap = std::map<std::string, std::shared_ptr<Session>>;
    SessionMap sessions_;
    // Sessions are hidden from public lookup while ConsumeBatch validates and
    // prepares every lease. Accounting and aliases remain reserved until the
    // batch commits, and failures move the same map nodes back without allocation.
    SessionMap consuming_sessions_;
    std::map<std::pair<std::string, std::string>, std::string> client_request_aliases_;
    std::map<std::string, std::size_t> principal_session_counts_;
    std::size_t active_session_count_{0};
    std::uint64_t reserved_bytes_{0};
    std::mutex cleanup_mutex_;
    std::condition_variable cleanup_condition_;
    bool stopping_{false};
    std::thread cleanup_thread_;
};

}  // namespace cosmo::service
