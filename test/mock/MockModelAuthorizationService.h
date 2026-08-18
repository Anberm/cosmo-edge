#pragma once

#include <trompeloeil.hpp>

#include "service/modelguard/IModelAuthorizationService.h"

namespace cosmo::test {

class MockModelAuthorizationService : public service::IModelAuthorizationService {
public:
    MAKE_MOCK0(Status, service::ModelAuthorizationStatus(), override);
    MAKE_MOCK2(CreateDeviceRequest, util::ErrorEnum(std::string&, std::string&), override);
    MAKE_MOCK1(InstallCertificate, util::ErrorEnum(const std::string&), override);
};

}  // namespace cosmo::test
