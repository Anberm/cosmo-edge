#include "media/IOsdTextRenderer.h"
#include "media/VideoFrameProcFactory.h"
#include "media/VideoFrameProcRockchip.h"
#include "mem/IDeviceContext.h"

namespace cosmo::media {

std::unique_ptr<IVideoFrameProc> CreateVideoFrameProc(mem::IDeviceContext& ctx, IOsdTextRenderer& osd) {
    static_cast<void>(ctx);
    return std::make_unique<VideoFrameProcRockchip>(osd);
}

}  // namespace cosmo::media
