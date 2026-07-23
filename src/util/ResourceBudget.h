/// @file ResourceBudget.h
/// @brief Resource-driven filesystem admission helpers.
#pragma once

#include <cstdint>
#include <string>

namespace cosmo::util {

constexpr std::uint64_t kDefaultStorageReserveBytes   = 512ULL * 1024 * 1024;
constexpr std::uint32_t kDefaultStorageReservePercent = 5;

struct StorageResourceBudget {
    bool valid{false};
    std::uint64_t capacity_bytes{0};
    std::uint64_t available_bytes{0};
    std::uint64_t reserve_bytes{0};
    std::uint64_t usable_bytes{0};
};

/// Inspect the filesystem containing @p path. The usable budget is the
/// unprivileged available space minus the larger of a fixed and percentage
/// reserve. The path must already exist.
[[nodiscard]] StorageResourceBudget InspectStorageResourceBudget(
    const std::string& path, std::uint64_t reserve_bytes = kDefaultStorageReserveBytes,
    std::uint32_t reserve_percent = kDefaultStorageReservePercent);

}  // namespace cosmo::util
