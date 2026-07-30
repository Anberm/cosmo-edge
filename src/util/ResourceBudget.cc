#include "util/ResourceBudget.h"

#include <sys/statvfs.h>

#include <algorithm>
#include <limits>

namespace cosmo::util {
namespace {

    std::uint64_t SaturatingMultiply(std::uint64_t lhs, std::uint64_t rhs) {
        if (lhs == 0 || rhs == 0) {
            return 0;
        }
        const auto max_value = std::numeric_limits<std::uint64_t>::max();
        return lhs > max_value / rhs ? max_value : lhs * rhs;
    }

    std::uint64_t FilesystemBytes(fsblkcnt_t blocks, unsigned long fragment_size) {
        return SaturatingMultiply(static_cast<std::uint64_t>(blocks),
                                  static_cast<std::uint64_t>(fragment_size));
    }

}  // namespace

StorageResourceBudget InspectStorageResourceBudget(const std::string& path, std::uint64_t reserve_bytes,
                                                   std::uint32_t reserve_percent) {
    StorageResourceBudget result;
    if (path.empty() || reserve_percent > 100) {
        return result;
    }

    struct statvfs status {};
    if (statvfs(path.c_str(), &status) != 0) {
        return result;
    }
    const auto fragment_size = status.f_frsize != 0 ? status.f_frsize : status.f_bsize;
    if (fragment_size == 0) {
        return result;
    }

    result.capacity_bytes         = FilesystemBytes(status.f_blocks, fragment_size);
    result.available_bytes        = FilesystemBytes(status.f_bavail, fragment_size);
    const auto percentage_reserve = SaturatingMultiply(result.capacity_bytes, reserve_percent) / 100;
    result.reserve_bytes          = std::max(reserve_bytes, percentage_reserve);
    result.usable_bytes =
        result.available_bytes > result.reserve_bytes ? result.available_bytes - result.reserve_bytes : 0;
    result.valid = true;
    return result;
}

std::uint64_t UsableStorageBytesAfterReclaim(const StorageResourceBudget& budget,
                                             std::uint64_t reclaimable_bytes) {
    if (!budget.valid) {
        return 0;
    }
    const auto max_value = std::numeric_limits<std::uint64_t>::max();
    return reclaimable_bytes > max_value - budget.usable_bytes ? max_value
                                                               : budget.usable_bytes + reclaimable_bytes;
}

}  // namespace cosmo::util
