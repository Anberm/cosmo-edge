#pragma once

#include <string>

#include "service/modelguard/IModelAuthorizationService.h"

namespace cosmo::service {

class ModelAuthorizationServiceImpl final : public IModelAuthorizationService {
public:
    explicit ModelAuthorizationServiceImpl(
        std::string provision_tool = "/appfs/cosmo_wander/cwai_data/bin/cosmo-model-provision");

    ModelAuthorizationStatus Status() override;
    util::ErrorEnum CreateDeviceRequest(std::string& file_path, std::string& file_name) override;
    util::ErrorEnum InstallCertificate(const std::string& file_path) override;

private:
    bool ToolAvailable() const;
    std::string provision_tool_;
};

}  // namespace cosmo::service
