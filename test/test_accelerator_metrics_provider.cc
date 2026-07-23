#include "catch_amalgamated.hpp"
#include "service/system/impl/AcceleratorMetricsProvider.h"

#ifdef COSMO_NN_USE_CPU_BACKEND
TEST_CASE("CPU accelerator metrics provider reports no accelerator", "[system][metrics]") {
    auto provider = cosmo::service::detail::CreateAcceleratorMetricsProvider();
    REQUIRE(provider != nullptr);

    const auto metrics = provider->QueryUtilization();
    CHECK(metrics.gpuusage == 0.0);
    CHECK(metrics.gpumemtotal == 0);
    CHECK(metrics.gpudevusage.empty());
    CHECK(provider->QueryAvailableMemoryMB() == 0);
}
#endif
