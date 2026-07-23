#include "catch_amalgamated.hpp"
#include "nn/core/common.h"

#ifdef COSMO_NN_USE_HOST_BACKEND
#include "nn/device/host/host_node_factory.h"
#include "nn/node/node_type.h"
#endif

TEST_CASE("backend capabilities identify host memory boundaries", "[nn][backend]") {
    using namespace cosmo::nn;

    CHECK(UsesHostMemory(DEVICE_NAIVE));
    CHECK(UsesHostMemory(DEVICE_CPU));
    CHECK_FALSE(UsesHostMemory(DEVICE_SOPHON_TPU));
}

#ifdef COSMO_NN_USE_HOST_BACKEND
TEST_CASE("host node factory excludes network runtime ownership", "[nn][backend]") {
    using namespace cosmo::nn;

    CHECK(CreateHostNode(NODE_RESIZE) != nullptr);
    CHECK(CreateHostNode(NODE_NORMALIZE) != nullptr);
    CHECK(CreateHostNode(NODE_NET) == nullptr);
    CHECK(CreateHostNode(NODE_UNKNOWN) == nullptr);
}
#endif
