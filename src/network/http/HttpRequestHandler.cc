// HttpRequestHandler — Http Request Handler implementation.

#include "network/http/HttpRequestHandler.h"

#include <limits>

#include "util/Log.h"

namespace cosmo::network::http {

bool HttpRequestHandler::Finalize() {
    Flush();
    return true;
}

size_t HttpStringHandler::AppendData(const char* data, size_t size) {
    if (size > max_size_ || data_.size() > max_size_ - size) {
        LOG_WARN("HTTP string response exceeds {} bytes", max_size_);
        return 0;
    }
    data_.append(data, size);
    return size;
}

HttpFileHandler::HttpFileHandler(const std::string& filename) : file_(filename) {}

size_t HttpFileHandler::AppendData(const char* data, size_t size) {
    if (!file_.is_open() || (data == nullptr && size != 0) ||
        size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        return 0;
    }
    file_.write(data, static_cast<std::streamsize>(size));
    return file_.good() ? size : 0;
}

void HttpFileHandler::Flush() {
    file_.flush();
}

bool HttpFileHandler::Finalize() {
    Flush();
    return file_.is_open() && file_.good();
}

size_t HttpImageHandler::AppendData(const char* data, size_t size) {
    if (size > max_size_ || data_.size() > max_size_ - size) {
        LOG_WARN("HTTP image response exceeds {} bytes", max_size_);
        return 0;
    }
    data_.insert(data_.end(), data, data + size);
    return size;
}

const std::string& HttpStringHandler::GetData() const {
    return data_;
}

const std::vector<u_char>& HttpImageHandler::GetImageData() const {
    return data_;
}

}  // namespace cosmo::network::http
