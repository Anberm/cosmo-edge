// FilePost — File Post implementation.

#include "service/path/impl/file/FilePost.h"

#include <filesystem>

#include "util/Log.h"

namespace cosmo::network::http {

static constexpr const char* kTag = "FilePost";

FilePost::FilePost(const std::string& url) : http_post_(url, str_hnd_) {}

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

    if (!http_post_.AddMimeFile(key, filename, display_name, filetype)) {
        LOG_WARN("{} rejected invalid multipart file [{}]", kTag, display_name);
        return false;
    }
    return true;
}

void FilePost::AppendData(const std::string& key, const std::string& value) {
    if (!http_post_.AddMimeField(key, value)) {
        LOG_WARN("{} rejected invalid multipart field", kTag);
    }
}

long FilePost::Submit() {
    return http_post_.Submit(HttpRequestMethod::kPost);
}

std::string FilePost::GetContentType() const {
    return http_post_.GetContentType();
}

std::string FilePost::GetContent() const {
    return str_hnd_.GetData();
}

void FilePost::SetProxy(const std::string& proxy, const std::string& username, const std::string& password) {
    http_post_.SetProxy(proxy, username, password);
}

HttpRequest& FilePost::GetHttpRequest() {
    return http_post_;
}

}  // namespace cosmo::network::http
