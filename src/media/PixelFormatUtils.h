#pragma once

#include <cstddef>
#include <optional>

#include "media/PixelFormat.h"
#include "util/Rect.h"

namespace cosmo {
namespace media {

    class PixelFormatUtils {
    public:
        // format
        static PixelBaseType PixelFormatBaseType(PixelFormat);

        static size_t PixelFormatChannels(PixelFormat);

        static size_t PixelFormatDepth(PixelFormat);

        [[nodiscard]] static std::optional<size_t> CalculateFrameSize(int width, int height,
                                                                      PixelFormat format);

        [[nodiscard]] static std::optional<util::Box> NormalizeCropRoi(int source_width, int source_height,
                                                                       PixelFormat format,
                                                                       const util::Box& requested_roi,
                                                                       int min_dimension, int max_dimension);

        static bool PixelFormatIsRGB(PixelFormat);

        static bool PixelFormatIsBGR(PixelFormat);

        static bool PixelFormatIsYUV(PixelFormat);
    };

}  // namespace media
}  // namespace cosmo
