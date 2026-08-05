#include "catch_amalgamated.hpp"
#include "media/VideoEncoder.h"

#ifdef COSMO_MEDIA_USE_CPU_BACKEND
#include "media/VideoEncoderCpu.h"
#endif

TEST_CASE("Video encoder capability uses a deterministic backend", "[media][encoder][capability]") {
    const auto capability =
        cosmo::media::VideoEncoder::Probe(cosmo::media::VideoCodecType::kH264);

    CHECK_FALSE(capability.backend.empty());
    CHECK_FALSE(capability.detail.empty());

#ifdef COSMO_MEDIA_USE_CPU_BACKEND
    CHECK(capability.backend == "ffmpeg-software");
    CHECK(capability.implementation == "libopenh264");
    CHECK(cosmo::media::VideoEncoderCpu::IsAllowedEncoderName(
        cosmo::media::VideoCodecType::kH264, "libopenh264"));
    CHECK_FALSE(cosmo::media::VideoEncoderCpu::IsAllowedEncoderName(
        cosmo::media::VideoCodecType::kH264, "h264_v4l2m2m"));
    CHECK_FALSE(cosmo::media::VideoEncoderCpu::IsAllowedEncoderName(
        cosmo::media::VideoCodecType::kH264, "h264_nvenc"));
#else
    CHECK(capability.available);
#endif
}
