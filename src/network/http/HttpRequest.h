#pragma once

#include <memory>
#include <string>
#include <vector>

#include "network/http/HttpRequestHandler.h"

namespace cosmo::network::http {

enum class HttpRequestMethod { kUnspecified, kGet, kPost, kPut, kDelete, kConnect, kOptions, kTrace, kPatch };

class HttpRequest {
public:
    HttpRequest(const std::string& url, HttpRequestHandler& resultHandler);
    explicit HttpRequest(const std::string& url, HttpRequestHandler* result_handler = nullptr);
    ~HttpRequest();

    HttpRequest(const HttpRequest&)            = delete;
    HttpRequest& operator=(const HttpRequest&) = delete;

    void SetPostUrl(const std::string& url);
    void SetContentType(const std::string& contentType);

    void SetTimeout(long seconds);

    void SetConnectTimeout(long seconds);

    // Override the system trust store with a PEM CA bundle for private PKI.
    void SetCaBundlePath(const std::string& ca_bundle_path);

    void AppendHeader(const std::string& key, const std::string& value);

    // Clears existing data and sets new data
    void SetData(const std::string& data);

    void SetDataEx(std::string&& data);
    // Append key=value pair
    void AppendData(const std::string& key, const std::string& value);

    /// Add multipart fields without buffering file bodies in process memory.
    bool AddMimeField(const std::string& name, const std::string& value);
    bool AddMimeFile(const std::string& name, const std::string& path, const std::string& filename,
                     const std::string& content_type);
    // Submit request, returns HTTP status code
    long Submit(HttpRequestMethod method = HttpRequestMethod::kUnspecified);

    const std::string& GetContentType() const;

    // Set proxy
    void SetProxy(const std::string& proxy, const std::string& username = "",
                  const std::string& password = "");

    // Bind to specific network interface
    void SetInterface(const std::string& interfaceName);

private:
    struct MimePart;

    std::string url_;
    std::string data_;
    std::string proxy_;
    std::string proxy_username_;
    std::string proxy_password_;
    std::string post_content_type_;
    std::string result_content_type_;
    std::string interface_name_;
    std::string ca_bundle_path_;
    HttpRequestHandler* result_handler_;
    std::vector<std::string> header_;
    std::vector<std::unique_ptr<MimePart>> mime_parts_;
    long status_;
    long timeout_;
    long connect_timeout_;
    long follow_location_;
};

}  // namespace cosmo::network::http
