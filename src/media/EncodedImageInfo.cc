#include "media/EncodedImageInfo.h"

#include <limits>

#include "media/PixelFormatUtils.h"
#include "stb/stb_image.h"

namespace cosmo::media {

bool InspectEncodedImage(const std::vector<std::uint8_t>& data, EncodedImageInfo& info) noexcept {
    info = {};
    if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    int width    = 0;
    int height   = 0;
    int channels = 0;
    if (stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels) == 0 ||
        width <= 0 || height <= 0 || channels <= 0) {
        return false;
    }

    const auto width_u64  = static_cast<std::uint64_t>(width);
    const auto height_u64 = static_cast<std::uint64_t>(height);
    if (width_u64 > std::numeric_limits<std::uint64_t>::max() / height_u64) {
        return false;
    }
    info.width       = width;
    info.height      = height;
    info.channels    = channels;
    info.pixel_count = width_u64 * height_u64;
    return true;
}

bool IsEncodedImageWithinFrameCapability(const EncodedImageInfo& info) noexcept {
    return info.pixel_count <= MaxDecodedImagePixels() &&
           PixelFormatUtils::CalculateFrameSize(info.width, info.height, PixelFormat::PIXEL_RGB8).has_value();
}

}  // namespace cosmo::media
