#pragma once

#include <cstdint>
#include <filesystem>

#include "service/system/UpgradeSpace.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

struct EventMediaCleanupResult {
    util::ErrorEnum error{util::ErrorEnum::Success};
    std::uint64_t deleted_files{0};
    std::uint64_t deleted_bytes{0};
};

[[nodiscard]] std::uint64_t RequiredUpgradeSpaceBytes(std::uint64_t package_size_bytes);
[[nodiscard]] bool IsEventMediaFile(const std::filesystem::path& path);
[[nodiscard]] EventMediaCleanupResult DeleteEventMediaFiles(const std::filesystem::path& event_root);
[[nodiscard]] util::ErrorEnum CheckUpgradeStorage(const std::filesystem::path& data_root,
                                                  const std::filesystem::path& event_root,
                                                  std::uint64_t package_size_bytes,
                                                  bool cleanup_event_media,
                                                  UpgradeSpaceStatus& status);

}  // namespace cosmo::service
