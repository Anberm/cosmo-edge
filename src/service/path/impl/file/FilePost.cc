// FilePost — File Post implementation.

#include "service/path/impl/file/FilePost.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>

#include "util/Log.h"

namespace cosmo::network::http {

static constexpr const char* kTag = "FilePost";

namespace {
    constexpr size_t kMaxMultipartFileBytes = 256ULL * 1024 * 1024;

    class ScopedFd {
    public:
        explicit ScopedFd(int fd) : fd_(fd) {}
        ~ScopedFd() {
            if (fd_ >= 0) {
                ::close(fd_);
            }
        }

        ScopedFd(const ScopedFd&)            = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        int Get() const {
            return fd_;
        }

    private:
        int fd_;
    };
}  // namespace

FilePost::FilePost(const std::string& url) : http_post_(url, str_hnd_) {
    BoundaryGen();
}

void FilePost::SetPostUrl(const std::string& url) {
    http_post_.SetPostUrl(url);
}

bool FilePost::SetFile(const std::string& key, const std::string& filename, const std::string& filetype) {
    const std::string display_name = std::filesystem::path(filename).filename().string();
    const auto invalid             = [](const std::string& value) {
        return value.empty() || value.find_first_of("\r\n\"") != std::string::npos;
    };
    if (invalid(key) || invalid(display_name) || filetype.find_first_of("\r\n") != std::string::npos) {
        LOG_WARN("{} rejected invalid multipart metadata", kTag);
        return false;
    }

    ScopedFd file(::open(filename.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat status {};
    if (file.Get() < 0 || ::fstat(file.Get(), &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size <= 0 || static_cast<std::uintmax_t>(status.st_size) > kMaxMultipartFileBytes) {
        LOG_WARN("{} rejected invalid multipart file [{}]", kTag, display_name);
        return false;
    }

    const auto initial_size = data_.size();
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"; filename=\"" + display_name +
                 "\"\r\n");
    data_.append("Content-Type: " + filetype + "\r\n\r\n");

    std::array<char, 64 * 1024> buffer{};
    size_t total       = 0;
    ssize_t bytes_read = 0;
    while ((bytes_read = ::read(file.Get(), buffer.data(), buffer.size())) > 0) {
        const auto chunk = static_cast<size_t>(bytes_read);
        if (chunk > kMaxMultipartFileBytes - total) {
            data_.resize(initial_size);
            LOG_WARN("{} multipart file grew beyond the size limit [{}]", kTag, display_name);
            return false;
        }
        data_.append(buffer.data(), chunk);
        total += chunk;
    }
    if (bytes_read < 0 || total == 0) {
        data_.resize(initial_size);
        LOG_WARN("{} failed to read multipart file [{}]", kTag, display_name);
        return false;
    }
    data_.append("\r\n");
    return true;
}

void FilePost::AppendData(const std::string& key, const std::string& value) {
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n");
    data_.append(value + "\r\n");
}

long FilePost::Submit() {
    data_.append("--" + boundary_ + "--\r\n");
    http_post_.SetDataEx(std::move(data_));
    http_post_.SetContentType("multipart/form-data; boundary=" + boundary_);
    return http_post_.Submit();
}

std::string FilePost::GetContentType() const {
    return http_post_.GetContentType();
}

std::string FilePost::GetContent() const {
    return str_hnd_.GetData();
}

void FilePost::BoundaryGen() {
    boundary_.reserve(41);
    boundary_.assign(27, '-');
    char buf[15]{};
    snprintf(buf, 15, "0000%10s", std::to_string(std::time(nullptr)).c_str());
    boundary_.append(buf);
}

void FilePost::SetProxy(const std::string& proxy, const std::string& username, const std::string& password) {
    http_post_.SetProxy(proxy, username, password);
}

HttpRequest& FilePost::GetHttpRequest() {
    return http_post_;
}

}  // namespace cosmo::network::http
