#include <filesystem>

#include "catch_amalgamated.hpp"
#include "util/ResourceBudget.h"

TEST_CASE("Storage resource budget is derived from the target filesystem", "[resource][storage]") {
    const auto budget =
        cosmo::util::InspectStorageResourceBudget(std::filesystem::temp_directory_path().string(), 0, 0);
    REQUIRE(budget.valid);
    CHECK(budget.capacity_bytes > 0);
    CHECK(budget.available_bytes > 0);
    CHECK(budget.reserve_bytes == 0);
    CHECK(budget.usable_bytes == budget.available_bytes);

    const auto reserved = cosmo::util::InspectStorageResourceBudget(
        std::filesystem::temp_directory_path().string(), budget.available_bytes, 0);
    REQUIRE(reserved.valid);
    CHECK(reserved.usable_bytes == 0);
}
