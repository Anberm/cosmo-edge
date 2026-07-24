#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/ErrorCode.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::detail {

/// Maximum encoded input retained in memory while the media pipeline decodes
/// one image. This mirrors the largest supported decoded RGB frame and is a
/// media capability boundary, not an upload quota.
[[nodiscard]] std::uint64_t MaxEncodedImageInputBytes() noexcept;

/// Atomically claim authenticated image upload sessions and read their pinned
/// payloads. Legacy Base64 callers do not use this helper.
[[nodiscard]] util::ErrorEnum ConsumeStagedImages(const RequestDispatchContext& context,
                                                  const std::vector<std::string>& upload_ids,
                                                  std::vector<std::vector<std::uint8_t>>& images);

}  // namespace cosmo::detail
