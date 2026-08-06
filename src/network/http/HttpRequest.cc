// HttpRequest — Http Request implementation.

#include "network/http/HttpRequest.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "curl/curl.h"
#include "util/Log.h"

namespace cosmo::network::http {

struct HttpRequest::MimePart {
    std::string name;
    std::string value;
    std::string filename;
    std::string content_type;
    int fd{-1};
    std::uint64_t size{0};
    std::uint64_t position{0};

    ~MimePart() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

namespace {

    const char* GetMethodString(HttpRequestMethod method) {
        switch (method) {
            case HttpRequestMethod::kGet:
                return "GET";
            case HttpRequestMethod::kPost:
                return "POST";
            case HttpRequestMethod::kPut:
                return "PUT";
            case HttpRequestMethod::kDelete:
                return "DELETE";
            case HttpRequestMethod::kConnect:
                return "CONNECT";
            case HttpRequestMethod::kOptions:
                return "OPTIONS";
            case HttpRequestMethod::kTrace:
                return "TRACE";
            case HttpRequestMethod::kPatch:
                return "PATCH";
            default:
                return nullptr;
        }
    }

}  // namespace

HttpRequest::HttpRequest(const std::string& url, HttpRequestHandler& resultHandler)
    : HttpRequest(url, &resultHandler) {}

HttpRequest::HttpRequest(const std::string& url, HttpRequestHandler* result_handler)
    : url_(url),
      post_content_type_("text/plain"),
      result_handler_(result_handler),
      timeout_(10L),
      connect_timeout_(10L) {}

HttpRequest::~HttpRequest() = default;

void HttpRequest::SetPostUrl(const std::string& url) {
    url_ = url;
}

void HttpRequest::AppendHeader(const std::string& key, const std::string& value) {
    header_.emplace_back(key + ": " + value);
}

void HttpRequest::SetData(const std::string& data) {
    data_ = data;
}

void HttpRequest::SetDataEx(std::string&& data) {
    data_ = std::move(data);
}

void HttpRequest::AppendData(const std::string& key, const std::string& value) {
    std::string sep;
    if (!data_.empty()) {
        sep = "&";
    }
    data_ += sep;
    data_ += key;
    data_ += "=";
    data_ += value;
}

bool HttpRequest::AddMimeField(const std::string& name, const std::string& value) {
    if (name.empty() || name.find_first_of("\r\n\"") != std::string::npos) {
        return false;
    }
    auto part   = std::make_unique<MimePart>();
    part->name  = name;
    part->value = value;
    mime_parts_.push_back(std::move(part));
    return true;
}

bool HttpRequest::AddMimeFile(const std::string& name, const std::string& path, const std::string& filename,
                              const std::string& content_type) {
    if (name.empty() || filename.empty() || path.empty() ||
        name.find_first_of("\r\n\"") != std::string::npos ||
        filename.find_first_of("\r\n\"") != std::string::npos ||
        content_type.find_first_of("\r\n") != std::string::npos) {
        return false;
    }

    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat status {};
    if (fd < 0 || fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0) {
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }

    auto part          = std::make_unique<MimePart>();
    part->name         = name;
    part->filename     = filename;
    part->content_type = content_type;
    part->fd           = fd;
    part->size         = static_cast<std::uint64_t>(status.st_size);
    mime_parts_.push_back(std::move(part));
    return true;
}

const std::string& HttpRequest::GetContentType() const {
    return result_content_type_;
}

namespace {
    constexpr long kFollowLocation = 1L;
    constexpr long kMaxRedirects   = 5L;

    bool CurlSucceeded(CURLcode result, const char* operation) {
        if (result == CURLE_OK) {
            return true;
        }
        LOG_ERRO("{} failed: [{}]", operation, curl_easy_strerror(result));
        return false;
    }

    template <typename Value>
    bool SetCurlOption(CURL* curl, CURLoption option, Value value, const char* option_name) {
        return CurlSucceeded(curl_easy_setopt(curl, option, value), option_name);
    }

    template <typename Value>
    bool GetCurlInfo(CURL* curl, CURLINFO info, Value* value, const char* info_name) {
        return CurlSucceeded(curl_easy_getinfo(curl, info, value), info_name);
    }

    class CurlHeaderList {
    public:
        CurlHeaderList() = default;

        ~CurlHeaderList() {
            curl_slist_free_all(headers_);
        }

        CurlHeaderList(const CurlHeaderList&)            = delete;
        CurlHeaderList& operator=(const CurlHeaderList&) = delete;

        bool Append(const std::string& header) {
            auto* updated = curl_slist_append(headers_, header.c_str());
            if (updated == nullptr) {
                LOG_ERRO("{}", "curl_slist_append failed");
                return false;
            }
            headers_ = updated;
            return true;
        }

        curl_slist* Get() const {
            return headers_;
        }

    private:
        curl_slist* headers_{nullptr};
    };

    bool ConfigureRequestMethod(CURL* curl, HttpRequestMethod method, bool has_body) {
        switch (method) {
            case HttpRequestMethod::kUnspecified:
                return true;
            case HttpRequestMethod::kGet:
                if (!has_body) {
                    return SetCurlOption(curl, CURLOPT_HTTPGET, 1L, "CURLOPT_HTTPGET");
                }
                return SetCurlOption(curl, CURLOPT_CUSTOMREQUEST, "GET", "CURLOPT_CUSTOMREQUEST");
            case HttpRequestMethod::kPost:
                if (has_body) {
                    return true;
                }
                return SetCurlOption(curl, CURLOPT_POST, 1L, "CURLOPT_POST") &&
                       SetCurlOption(curl, CURLOPT_POSTFIELDS, "", "CURLOPT_POSTFIELDS") &&
                       SetCurlOption(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(0),
                                     "CURLOPT_POSTFIELDSIZE_LARGE");
            default: {
                const auto* method_name = GetMethodString(method);
                if (method_name == nullptr) {
                    LOG_ERRO("{}", "Unsupported HTTP request method");
                    return false;
                }
                return SetCurlOption(curl, CURLOPT_CUSTOMREQUEST, method_name, "CURLOPT_CUSTOMREQUEST");
            }
        }
    }

    size_t PostCallback(char* ptr, size_t size, size_t nmemb, void* data) {
        if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) {
            return 0;
        }
        const auto len = size * nmemb;
        if (!data) {
            LOG_INFO("{}", "curl callback data is NULL");
            return len;
        }

        HttpRequestHandler* hnd = static_cast<HttpRequestHandler*>(data);
        return hnd->AppendData(ptr, len);
    }

}  // namespace

void HttpRequest::SetContentType(const std::string& contentType) {
    post_content_type_ = contentType;
}

long HttpRequest::Submit(HttpRequestMethod method) {
    result_content_type_.clear();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_handle(curl_easy_init(), curl_easy_cleanup);
    auto* curl = curl_handle.get();
    if (!curl) {
        LOG_ERRO("{}", "curl easy init fail");
        return -1;
    }
    if (!mime_parts_.empty() && !data_.empty()) {
        LOG_ERRO("{}", "HTTP request cannot combine buffered and MIME bodies");
        return -1;
    }

    if (!SetCurlOption(curl, CURLOPT_URL, url_.c_str(), "CURLOPT_URL") ||
        !SetCurlOption(curl, CURLOPT_WRITEFUNCTION, PostCallback, "CURLOPT_WRITEFUNCTION") ||
        !SetCurlOption(curl, CURLOPT_WRITEDATA, result_handler_, "CURLOPT_WRITEDATA") ||
        !SetCurlOption(curl, CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4), "CURLOPT_IPRESOLVE") ||
        !SetCurlOption(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_, "CURLOPT_CONNECTTIMEOUT") ||
        !SetCurlOption(curl, CURLOPT_TIMEOUT, timeout_, "CURLOPT_TIMEOUT") ||
        !SetCurlOption(curl, CURLOPT_VERBOSE, 0L, "CURLOPT_VERBOSE") ||
        !SetCurlOption(curl, CURLOPT_FOLLOWLOCATION, kFollowLocation, "CURLOPT_FOLLOWLOCATION") ||
        !SetCurlOption(curl, CURLOPT_MAXREDIRS, kMaxRedirects, "CURLOPT_MAXREDIRS") ||
        !SetCurlOption(curl, CURLOPT_NOSIGNAL, 1L, "CURLOPT_NOSIGNAL")) {
        return -1;
    }

#if LIBCURL_VERSION_NUM >= 0x075500
    if (!SetCurlOption(curl, CURLOPT_PROTOCOLS_STR, "http,https", "CURLOPT_PROTOCOLS_STR")) {
        return -1;
    }
#else
    if (!SetCurlOption(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS),
                       "CURLOPT_PROTOCOLS")) {
        return -1;
    }
#endif
#if LIBCURL_VERSION_NUM >= 0x075500
    if (!SetCurlOption(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https", "CURLOPT_REDIR_PROTOCOLS_STR")) {
        return -1;
    }
#else
    if (!SetCurlOption(curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS),
                       "CURLOPT_REDIR_PROTOCOLS")) {
        return -1;
    }
#endif

    if (!SetCurlOption(curl, CURLOPT_SSL_VERIFYPEER, 1L, "CURLOPT_SSL_VERIFYPEER") ||
        !SetCurlOption(curl, CURLOPT_SSL_VERIFYHOST, 2L, "CURLOPT_SSL_VERIFYHOST")) {
        return -1;
    }
    if (!ca_bundle_path_.empty() &&
        !SetCurlOption(curl, CURLOPT_CAINFO, ca_bundle_path_.c_str(), "CURLOPT_CAINFO")) {
        return -1;
    }

    if (!interface_name_.empty() &&
        !SetCurlOption(curl, CURLOPT_INTERFACE, interface_name_.c_str(), "CURLOPT_INTERFACE")) {
        return -1;
    }

    std::string proxy_credentials;
    if (!proxy_.empty()) {
        if (!SetCurlOption(curl, CURLOPT_PROXY, proxy_.c_str(), "CURLOPT_PROXY") ||
            !SetCurlOption(curl, CURLOPT_PROXYTYPE, static_cast<long>(CURLPROXY_HTTP), "CURLOPT_PROXYTYPE") ||
            !SetCurlOption(curl, CURLOPT_HTTPPROXYTUNNEL, 1L, "CURLOPT_HTTPPROXYTUNNEL")) {
            return -1;
        }
        if (!proxy_username_.empty()) {
            proxy_credentials = proxy_username_ + ":" + proxy_password_;
            if (!SetCurlOption(curl, CURLOPT_PROXYUSERPWD, proxy_credentials.c_str(),
                               "CURLOPT_PROXYUSERPWD")) {
                return -1;
            }
        }
    }

    CurlHeaderList headers;
    if (!data_.empty() && mime_parts_.empty()) {
        if (!headers.Append("Content-Type: " + post_content_type_) ||
            !headers.Append("Content-Length: " + std::to_string(data_.size()))) {
            return -1;
        }
    }

    for (const auto& header : header_) {
        if (!headers.Append(header)) {
            return -1;
        }
    }

    if (!SetCurlOption(curl, CURLOPT_HTTPHEADER, headers.Get(), "CURLOPT_HTTPHEADER")) {
        return -1;
    }
    if (!data_.empty()) {
        if (static_cast<std::uintmax_t>(data_.size()) >
            static_cast<std::uintmax_t>(std::numeric_limits<curl_off_t>::max())) {
            LOG_ERRO("HTTP request body exceeds curl_off_t capacity: {} bytes", data_.size());
            return -1;
        }
        if (!SetCurlOption(curl, CURLOPT_POSTFIELDS, data_.c_str(), "CURLOPT_POSTFIELDS") ||
            !SetCurlOption(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(data_.size()),
                           "CURLOPT_POSTFIELDSIZE_LARGE")) {
            return -1;
        }
    }

    std::unique_ptr<curl_mime, decltype(&curl_mime_free)> mime(nullptr, curl_mime_free);
    if (!mime_parts_.empty()) {
        mime.reset(curl_mime_init(curl));
        if (!mime) {
            LOG_ERRO("{}", "curl_mime_init failed");
            return -1;
        }
        const auto read_file =
            +[](char* buffer, size_t item_size, size_t item_count, void* user_data) -> size_t {
            auto* part = static_cast<MimePart*>(user_data);
            if (part == nullptr || part->fd < 0 || item_size == 0 || item_count == 0 ||
                item_count > std::numeric_limits<size_t>::max() / item_size) {
                return CURL_READFUNC_ABORT;
            }
            const auto capacity = item_size * item_count;
            if (part->position >= part->size) {
                return 0;
            }
            const auto remaining = part->size - part->position;
            const auto requested =
                static_cast<size_t>(std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(capacity)));
            ssize_t read_count = 0;
            do {
                read_count = pread(part->fd, buffer, requested, static_cast<off_t>(part->position));
            } while (read_count < 0 && errno == EINTR);
            if (read_count < 0) {
                return CURL_READFUNC_ABORT;
            }
            part->position += static_cast<std::uint64_t>(read_count);
            return static_cast<size_t>(read_count);
        };
        const auto seek_file = +[](void* user_data, curl_off_t offset, int origin) -> int {
            auto* part = static_cast<MimePart*>(user_data);
            if (part == nullptr || offset < 0) {
                return CURL_SEEKFUNC_FAIL;
            }
            std::uint64_t base = 0;
            if (origin == SEEK_CUR) {
                base = part->position;
            } else if (origin == SEEK_END) {
                base = part->size;
            } else if (origin != SEEK_SET) {
                return CURL_SEEKFUNC_FAIL;
            }
            const auto delta = static_cast<std::uint64_t>(offset);
            if (delta > part->size || base > part->size - delta) {
                return CURL_SEEKFUNC_FAIL;
            }
            part->position = base + delta;
            return CURL_SEEKFUNC_OK;
        };

        for (const auto& part : mime_parts_) {
            curl_mimepart* curl_part = curl_mime_addpart(mime.get());
            if (curl_part == nullptr) {
                LOG_ERRO("{}", "curl_mime_addpart failed");
                return -1;
            }
            if (!CurlSucceeded(curl_mime_name(curl_part, part->name.c_str()), "curl_mime_name")) {
                return -1;
            }
            if (part->fd >= 0) {
                part->position = 0;
                if (part->size > static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max())) {
                    LOG_ERRO("Multipart file exceeds curl_off_t capacity: {} bytes", part->size);
                    return -1;
                }
                if (!CurlSucceeded(curl_mime_filename(curl_part, part->filename.c_str()),
                                   "curl_mime_filename") ||
                    (!part->content_type.empty() &&
                     !CurlSucceeded(curl_mime_type(curl_part, part->content_type.c_str()),
                                    "curl_mime_type")) ||
                    !CurlSucceeded(curl_mime_data_cb(curl_part, static_cast<curl_off_t>(part->size),
                                                     read_file, seek_file, nullptr, part.get()),
                                   "curl_mime_data_cb")) {
                    return -1;
                }
            } else if (!CurlSucceeded(curl_mime_data(curl_part, part->value.data(), part->value.size()),
                                      "curl_mime_data")) {
                return -1;
            }
        }
        if (!SetCurlOption(curl, CURLOPT_MIMEPOST, mime.get(), "CURLOPT_MIMEPOST")) {
            return -1;
        }
    }

    if (!ConfigureRequestMethod(curl, method, !data_.empty() || !mime_parts_.empty())) {
        return -1;
    }

    if (!CurlSucceeded(curl_easy_perform(curl), "curl_easy_perform")) {
        return -1;
    }

    if (result_handler_ != nullptr && !result_handler_->Finalize()) {
        LOG_ERRO("{}", "HTTP response handler finalization failed");
        return -1;
    }

    char* content_type = nullptr;
    if (!GetCurlInfo(curl, CURLINFO_CONTENT_TYPE, &content_type, "CURLINFO_CONTENT_TYPE")) {
        return -1;
    }
    if (content_type != nullptr) {
        result_content_type_.assign(content_type);
    }

    long status = 0;
    if (!GetCurlInfo(curl, CURLINFO_RESPONSE_CODE, &status, "CURLINFO_RESPONSE_CODE")) {
        return -1;
    }

    const auto* method_name = GetMethodString(method);
    if (method_name != nullptr) {
        LOG_INFO("HTTP Submit [{}][{}]", method_name, status);
    } else {
        LOG_INFO("HTTP Submit [{}]", status);
    }
    return status;
}

void HttpRequest::SetTimeout(long seconds) {
    timeout_ = seconds;
}

void HttpRequest::SetConnectTimeout(long seconds) {
    connect_timeout_ = seconds;
}

void HttpRequest::SetCaBundlePath(const std::string& ca_bundle_path) {
    ca_bundle_path_ = ca_bundle_path;
}

void HttpRequest::SetProxy(const std::string& proxy, const std::string& username,
                           const std::string& password) {
    proxy_          = proxy;
    proxy_username_ = username;
    proxy_password_ = password;
}

void HttpRequest::SetInterface(const std::string& interfaceName) {
    interface_name_ = interfaceName;
}

}  // namespace cosmo::network::http
