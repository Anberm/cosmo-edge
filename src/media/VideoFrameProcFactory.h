/// @file VideoFrameProcFactory.h
/// @brief Factory function for creating the correct backend VideoFrameProc.
///
/// The concrete implementation (Sophon, CPU, or Rockchip) is selected at
/// CMake time; exactly one factory implementation is compiled into the binary.
#pragma once

#include <memory>

#include "media/IVideoFrameProc.h"

namespace cosmo {
namespace mem {
    class IDeviceContext;
}
namespace media {
    class IOsdTextRenderer;

    /// Creates the configured backend VideoFrameProc.
    /// @param ctx  Device context providing hardware handles
    /// @param osd  OSD text rendering service
    std::unique_ptr<IVideoFrameProc> CreateVideoFrameProc(mem::IDeviceContext& ctx, IOsdTextRenderer& osd);

}  // namespace media
}  // namespace cosmo
