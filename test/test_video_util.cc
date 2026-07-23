#include <array>
#include <cstddef>
#include <cstdint>

#include "catch_amalgamated.hpp"
#include "media/VideoCodecType.h"
#include "media/VideoUtil.h"

namespace cosmo::media {

TEST_CASE("VideoUtil frame classification is bounds checked", "[media][video-util][security]") {
    const std::array<std::uint8_t, 1> short_data{0};
    CHECK(GetFrameType(VideoCodecType::kH264, nullptr, 0) == HFrameType::UNKNOWN);
    CHECK(GetFrameType(VideoCodecType::kH264, short_data.data(), short_data.size()) == HFrameType::UNKNOWN);
    CHECK(GetFrameType(VideoCodecType::kH265, short_data.data(), short_data.size()) == HFrameType::UNKNOWN);

    const std::array<std::uint8_t, 5> h264_i{0x00, 0x00, 0x00, 0x01, 0x65};
    const std::array<std::uint8_t, 4> h264_sps{0x00, 0x00, 0x01, 0x67};
    const std::array<std::uint8_t, 6> h265_i{0x00, 0x00, 0x00, 0x01, 0x26, 0x01};
    CHECK(GetFrameType(VideoCodecType::kH264, h264_i.data(), h264_i.size()) == HFrameType::I);
    CHECK(GetFrameType(VideoCodecType::kH264, h264_sps.data(), h264_sps.size()) == HFrameType::SPS);
    CHECK(GetFrameType(VideoCodecType::kH265, h265_i.data(), h265_i.size()) == HFrameType::I);
}

TEST_CASE("VideoUtil separator helpers honor supplied buffer lengths", "[media][video-util][security]") {
    std::size_t removed = 99;
    const std::array<std::uint8_t, 2> short_data{0, 0};
    CHECK(RemoveHFrameSeparator(short_data.data(), short_data.size(), removed) == nullptr);
    CHECK(removed == 0);
    CHECK(SeparateHVideoFrame(nullptr, 10) == 0);

    const std::array<std::uint8_t, 7> prefixed{0x7f, 0x00, 0x00, 0x01, 0x65, 0xaa, 0xbb};
    const auto* payload = RemoveHFrameSeparator(prefixed.data(), prefixed.size(), removed);
    REQUIRE(payload != nullptr);
    CHECK(removed == 4);
    CHECK(payload == prefixed.data() + 4);

    const std::array<std::uint8_t, 10> two_nalus{0x00, 0x00, 0x00, 0x01, 0x67, 0x00, 0x00, 0x01, 0x68, 0xaa};
    CHECK(SeparateHVideoFrame(two_nalus.data(), two_nalus.size()) == 5);
}

}  // namespace cosmo::media
