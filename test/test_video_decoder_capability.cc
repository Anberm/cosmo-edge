#include "catch_amalgamated.hpp"
#include "media/VideoDecoder.h"

TEST_CASE("Video decoder capability uses a deterministic backend", "[media][decoder][capability]") {
    const auto h264 = cosmo::media::VideoDecoder::Probe(cosmo::media::VideoCodecType::kH264);
    const auto h265 = cosmo::media::VideoDecoder::Probe(cosmo::media::VideoCodecType::kH265);

    CHECK(h264.available);
    CHECK_FALSE(h264.backend.empty());
    CHECK_FALSE(h264.implementation.empty());
    CHECK_FALSE(h264.detail.empty());
    CHECK(h265.available);
    CHECK_FALSE(h265.backend.empty());
    CHECK_FALSE(h265.implementation.empty());
    CHECK_FALSE(h265.detail.empty());

#ifdef COSMO_MEDIA_USE_ROCKCHIP_BACKEND
    CHECK(h264.backend == "rockchip-copy-out");
    CHECK(h264.implementation == "rockchip-mpp-vpu");
    CHECK(h265.backend == "rockchip-copy-out");
    CHECK(h265.implementation == "rockchip-mpp-vpu");
#elif defined(COSMO_MEDIA_USE_SOPHON_BACKEND)
    CHECK(h264.backend == "sophon-vpu");
    CHECK(h265.backend == "sophon-vpu");
#else
    CHECK(h264.backend == "ffmpeg-software");
    CHECK(h265.backend == "ffmpeg-software");
#endif
}
