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

HttpRequest::HttpRequest(const std::string& url, HttpRequestHandler& resultHandler)
    : HttpRequest(url, &resultHandler) {}

HttpRequest::HttpRequest(const std::string& url, HttpRequestHandler* result_handler)
    : url_(url),
      post_content_type_("text/plain"),
      result_handler_(result_handler),
      status_(0),
      timeout_(10L),
      connect_timeout_(10L),
      follow_location_(1L) {}

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
    size_t PostCallback(char* ptr, size_t size, size_t nmemb, void* data) {
        auto len = size * nmemb;
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
    CURL* curl = curl_easy_init();
    [[maybe_unused]] std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_easy_init_ptr(
        curl, curl_easy_cleanup);
    if (!curl) {
        LOG_ERRO("{}", "curl easy init fail");
        return -1;
    }
    if (!mime_parts_.empty() && !data_.empty()) {
        LOG_ERRO("{}", "HTTP request cannot combine buffered and MIME bodies");
        return -1;
    }

    CURLcode res = curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_URL fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_WRITEFUNCTION fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, result_handler_);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_WRITEDATA fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    auto method_str = GetMethodString(method);
    if (method_str != nullptr) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_str);
    }

    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_location_);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

#if LIBCURL_VERSION_NUM >= 0x075500
    res = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    res = curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    if (res != CURLE_OK) {
        LOG_ERRO("Failed to restrict request protocols: [{}]", curl_easy_strerror(res));
        return -1;
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    res = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    res = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    if (res != CURLE_OK) {
        LOG_ERRO("Failed to restrict redirect protocols: [{}]", curl_easy_strerror(res));
        return -1;
    }

    res = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_SSL_VERIFYPEER fail : [{}]", curl_easy_strerror(res));
        return -1;
    }
    res = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_SSL_VERIFYHOST fail : [{}]", curl_easy_strerror(res));
        return -1;
    }
    if (!ca_bundle_path_.empty()) {
        res = curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle_path_.c_str());
        if (res != CURLE_OK) {
            LOG_ERRO("CURLOPT_CAINFO fail : [{}]", curl_easy_strerror(res));
            return -1;
        }
    }

    if (!interface_name_.empty()) {
        curl_easy_setopt(curl, CURLOPT_INTERFACE, interface_name_.c_str());
    }

    if (!proxy_.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy_.c_str());
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
        if (!proxy_username_.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, (proxy_username_ + ":" + proxy_password_).c_str());
        }
    }

    struct curl_slist* headers = nullptr;
    if (!data_.empty() && mime_parts_.empty()) {
        std::string type_header = "Content-Type: " + post_content_type_;
        headers                 = curl_slist_append(headers, type_header.c_str());
        std::string size_header = "Content-Length: " + std::to_string(data_.size());
        headers                 = curl_slist_append(headers, size_header.c_str());
    }

    for (auto& header : header_) {
        headers = curl_slist_append(headers, header.c_str());
    }

    [[maybe_unused]] std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> curl_header_free(
        headers, curl_slist_free_all);
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_HTTPHEADER fail : [{}]", curl_easy_strerror(res));
        return -1;
    }
    if (!data_.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data_.size());
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
            if (curl_part == nullptr || curl_mime_name(curl_part, part->name.c_str()) != CURLE_OK) {
                return -1;
            }
            if (part->fd >= 0) {
                part->position = 0;
                if (part->size > static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max()) ||
                    curl_mime_filename(curl_part, part->filename.c_str()) != CURLE_OK ||
                    (!part->content_type.empty() &&
                     curl_mime_type(curl_part, part->content_type.c_str()) != CURLE_OK) ||
                    curl_mime_data_cb(curl_part, static_cast<curl_off_t>(part->size), read_file, seek_file,
                                      nullptr, part.get()) != CURLE_OK) {
                    return -1;
                }
            } else if (curl_mime_data(curl_part, part->value.data(), part->value.size()) != CURLE_OK) {
                return -1;
            }
        }
        if (curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime.get()) != CURLE_OK) {
            return -1;
        }
    }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERRO("curl perform fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    if (result_handler_) {
        result_handler_->Flush();
    }

    char* ct = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
    if (ct) {
        result_content_type_.assign(ct);
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_);
    if (method_str)
        LOG_INFO("HTTP Submit [{}][{}]", method_str, status_);
    else
        LOG_INFO("HTTP Submit [{}]", status_);
    return status_;
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
