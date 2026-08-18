#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_SOPHON_BACKEND

#include <type_traits>
#include <utility>

#include "nn/device/sophon/sophon_net_node.h"

namespace cosmo::nn {
namespace {

    static_assert(std::is_nothrow_move_constructible_v<OwnedBmrt>);
    static_assert(!std::is_copy_constructible_v<OwnedBmrt>);

    TEST_CASE("Sophon BMRuntime ownership rejects an empty handle", "[nn][sophon][ownership]") {
        SophonNetNode node;
        OwnedBmrt runtime;

        auto status = node.AttachOwnedBmrt(std::move(runtime));
        REQUIRE(static_cast<int>(status) == static_cast<int>(COSMO_NN_ERR_LOAD_MODEL));
    }

}  // namespace
}  // namespace cosmo::nn

#endif
