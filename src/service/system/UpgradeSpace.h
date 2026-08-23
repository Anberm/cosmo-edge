#pragma once

#include <cstdint>

namespace cosmo::service {

struct UpgradeSpaceStatus {
    bool sufficient{false};
    std::uint64_t required_bytes{0};
    std::uint64_t available_bytes{0};
    std::uint64_t event_media_bytes{0};
    std::uint64_t deleted_media_bytes{0};
    std::uint64_t deleted_media_files{0};
};

}  // namespace cosmo::service
