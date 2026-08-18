#include "media/VideoEncoder.h"
#include "media/VideoEncoderRockchip.h"

namespace cosmo::media {

std::shared_ptr<VideoEncoder> VideoEncoder::Create(void* media_handle) {
    static_cast<void>(media_handle);
    return std::make_shared<VideoEncoderRockchip>();
}

VideoEncoderCapability VideoEncoder::Probe(VideoCodecType type) {
    return VideoEncoderRockchip::Probe(type);
}

}  // namespace cosmo::media
