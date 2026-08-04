#include "service/modelguard/impl/ModelAuthorizationServiceImpl.h"

#include "catch_amalgamated.hpp"

TEST_CASE("ModelAuthorizationServiceImpl hides authorization when the controlled tool is absent",
          "[service][model-authorization]") {
    cosmo::service::ModelAuthorizationServiceImpl service("/definitely/not/cosmo-model-provision");

    const auto status = service.Status();
    CHECK_FALSE(status.supported);
    CHECK_FALSE(status.authorized);
    CHECK(status.state == "unsupported");

    std::string path;
    std::string name;
    CHECK(service.CreateDeviceRequest(path, name) == cosmo::util::ErrorEnum::OperationNotSupport);
    CHECK(path.empty());
    CHECK(name.empty());
    CHECK(service.InstallCertificate("/definitely/not/a-certificate") ==
          cosmo::util::ErrorEnum::OperationNotSupport);
}
