#pragma once

#include <string>

#include "util/ErrorCode.h"

namespace cosmo::service {

struct ModelAuthorizationStatus {
    bool supported{false};
    bool authorized{false};
    std::string state{"unsupported"};
};

class IModelAuthorizationService {
public:
    virtual ~IModelAuthorizationService() = default;
    virtual ModelAuthorizationStatus Status() = 0;
    virtual util::ErrorEnum CreateDeviceRequest(std::string& file_path, std::string& file_name) = 0;
    virtual util::ErrorEnum InstallCertificate(const std::string& file_path) = 0;
};

}  // namespace cosmo::service
