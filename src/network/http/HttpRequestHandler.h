#pragma once

#include <fstream>
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
};

class HttpStringHandler : public HttpRequestHandler {
public:
    explicit HttpStringHandler(size_t max_size = 16U * 1024 * 1024) : max_size_(max_size) {}
    size_t AppendData(const char* data, size_t size) override;
    const std::string& GetData() const;

private:
    std::string data_;
    size_t max_size_;
};

class HttpFileHandler : public HttpRequestHandler {
public:
    explicit HttpFileHandler(const std::string& filename);
    size_t AppendData(const char* data, size_t size) override;
    void Flush() override;

private:
    std::ofstream file_;
};

class HttpImageHandler : public HttpRequestHandler {
public:
    explicit HttpImageHandler(size_t max_size = 16U * 1024 * 1024) : max_size_(max_size) {}
    size_t AppendData(const char* data, size_t size) override;
    const std::vector<u_char>& GetImageData() const;

private:
    std::vector<u_char> data_;
    size_t max_size_;
};

}  // namespace cosmo::network::http
