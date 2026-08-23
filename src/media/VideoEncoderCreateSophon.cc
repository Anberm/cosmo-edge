// VideoEncoderCreateSophon.cc — Sophon backend factory for VideoEncoder.
// Compiled only when COSMO_MEDIA_USE_SOPHON_BACKEND is ON (CMake file-level switching).

#include "media/VideoEncoder.h"
#include "media/VideoEncoderSophon.h"

namespace cosmo {
namespace media {

    std::shared_ptr<VideoEncoder> VideoEncoder::Create(void* mediaHandle) {
        return std::make_shared<VideoEncoderSophon>(mediaHandle);
    }

    VideoEncoderCapability VideoEncoder::Probe(VideoCodecType type) {
        VideoEncoderCapability capability;
        capability.backend = "sophon-vpu";
        if (type == VideoCodecType::kH264) {
            capability.available      = true;
            capability.implementation = "bmvpu-h264";
            capability.detail         = "compiled Sophon VPU encoder";
        } else if (type == VideoCodecType::kH265) {
            capability.available      = true;
            capability.implementation = "bmvpu-h265";
            capability.detail         = "compiled Sophon VPU encoder";
        } else {
            capability.detail = "unsupported codec";
        }
        return capability;
    }

}  // namespace media
}  // namespace cosmo
