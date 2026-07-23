#pragma once

#include <cstdint>
#include <vector>

#include "util/VideoInfo.h"

namespace cosmo::media {

struct EncodedImageInfo {
    int width{0};
    int height{0};
    int channels{0};
    std::uint64_t pixel_count{0};
};

/// Reads only the encoded image header; it does not allocate a decoded frame.
[[nodiscard]] bool InspectEncodedImage(const std::vector<std::uint8_t>& data,
                                       EncodedImageInfo& info) noexcept;

/// Largest decoded RGB frame supported by the shared media pipeline.
[[nodiscard]] constexpr std::uint64_t MaxDecodedImagePixels() noexcept {
    return static_cast<std::uint64_t>(kVideoMaxWidth) * static_cast<std::uint64_t>(kVideoMaxHeight);
}

/// Applies the actual decoded-frame allocation boundary before a JPEG/PNG/BMP
/// decoder can allocate from attacker-controlled dimensions.
[[nodiscard]] bool IsEncodedImageWithinFrameCapability(const EncodedImageInfo& info) noexcept;

}  // namespace cosmo::media
