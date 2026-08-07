#pragma once

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

namespace cosmo::network::http {

class HttpRequestHandler {
public:
    virtual ~HttpRequestHandler() = default;

    /**
     * @return: returning a value != size will abort the download
     */
    virtual size_t AppendData(const char* data, size_t size) = 0;

    /**
     * @return: flush buffered content
     */
    virtual void Flush() {};

    /**
     * Finalize the response sink after a completed transfer.
     * Existing handlers remain compatible through the default Flush-based
     * implementation.
     */
    virtual bool Finalize();
};

class HttpStringHandler : public HttpRequestHandler {
public:
    /// Bounds responses intentionally buffered in memory. Callers handling a
    /// domain payload should pass that domain's resource or protocol budget.
    explicit HttpStringHandler(size_t max_size = 16U * 1024 * 1024) : max_size_(max_size) {}
    size_t AppendData(const char* data, size_t size) override;
    const std::string& GetData() const;

private:
    std::string data_;
    size_t max_size_;
};

class HttpFileHandler : public HttpRequestHandler {
public:
    /// Streams the response directly to disk; no in-memory body-size quota is
    /// applied here. Filesystem admission remains the caller's responsibility.
    explicit HttpFileHandler(const std::string& filename);
    size_t AppendData(const char* data, size_t size) override;
    void Flush() override;
    bool Finalize() override;

private:
    std::ofstream file_;
};

class HttpImageHandler : public HttpRequestHandler {
public:
    /// Bounds encoded image bytes held in memory. Production image downloads
    /// pass a live memory/media capability budget instead of relying on this
    /// compatibility default.
    explicit HttpImageHandler(size_t max_size = 16U * 1024 * 1024) : max_size_(max_size) {}
    size_t AppendData(const char* data, size_t size) override;
    const std::vector<u_char>& GetImageData() const;

private:
    std::vector<u_char> data_;
    size_t max_size_;
};

}  // namespace cosmo::network::http
