#pragma once

#include <string>

#include "network/http/HttpRequest.h"

namespace cosmo::network::http {

class FilePost {
public:
    explicit FilePost(const std::string& url);

    FilePost(const FilePost&)            = delete;
    FilePost& operator=(const FilePost&) = delete;

    void SetPostUrl(const std::string& url);
    bool SetFile(const std::string& key, const std::string& filename,
                 const std::string& filetype = "text/plain");
    void AppendData(const std::string& key, const std::string& value);

    // Submit request, returns HTTP status code
    long Submit();

    std::string GetContentType() const;
    std::string GetContent() const;

    // Set proxy
    void SetProxy(const std::string& proxy, const std::string& username = "",
                  const std::string& password = "");
    // Get underlying HTTP request object
    HttpRequest& GetHttpRequest();

private:
    HttpStringHandler str_hnd_;
    HttpRequest http_post_;
};

}  // namespace cosmo::network::http
