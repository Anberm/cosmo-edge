#include "catch_amalgamated.hpp"

#ifdef COSMO_MEDIA_USE_ROCKCHIP_BACKEND

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "media/IOsdTextRenderer.h"
#include "media/PixelFormat.h"
#include "media/PreviewPipelineMetrics.h"
#include "media/VideoDecoder.h"
#include "media/VideoEncoder.h"
#include "media/VideoFrame.h"
#include "media/VideoFrameProcRockchip.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"

namespace {

class StubOsdTextRenderer final : public cosmo::media::IOsdTextRenderer {
public:
    bool Init(const std::string&) override {
        return true;
    }
    bool IsReady() const override {
        return false;
    }
    TextBitmap RenderString(const std::string&, float) const override {
        return {};
    }
    OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
        return {};
    }
};

bool HasAnnexBStartCode(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 0 && data[1] == 0 &&
           ((data[2] == 1) || (data[2] == 0 && data[3] == 1));
}

}  // namespace

TEST_CASE("Rockchip MPP encodes compact I420 as Annex-B H264",
          "[media][rockchip][encoder][.device]") {
    constexpr int width  = 640;
    constexpr int height = 360;
    std::vector<uint8_t> i420(static_cast<size_t>(width) * height * 3 / 2, 128);
    std::fill_n(i420.begin(), static_cast<size_t>(width) * height, 32);

    auto encoder = cosmo::media::VideoEncoder::Create(nullptr);
    REQUIRE(encoder);
    encoder->Set(cosmo::media::VideoCodecType::kH264, width, height);
    REQUIRE(encoder->Open());

    auto packet = encoder->SendYUVFrame(i420.data());
    REQUIRE(packet);
    CHECK(packet->GetSize() > 0);
    CHECK(packet->IsIFrame());
    CHECK(HasAnnexBStartCode(packet->data));
}

TEST_CASE("Rockchip RGA performs admitted host-buffer conversions and resize",
          "[media][rockchip][rga][.device]") {
    constexpr int width     = 64;
    constexpr int height    = 64;
    constexpr int pool_size = width * height * 3;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(), {pool_size});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;

    const auto before = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    {
        StubOsdTextRenderer osd;
        cosmo::media::VideoFrameProcRockchip processor(osd);
        auto bgr = std::make_shared<cosmo::media::VideoFrame>(
            width, height, cosmo::media::PixelFormat::PIXEL_BGR8);
        REQUIRE(VideoFrameValid(bgr, true));
        std::fill_n(bgr->GetData(), bgr->GetSize(), 96);

        auto i420 = processor.BGR2I420(bgr);
        REQUIRE(VideoFrameValid(i420, true));
        CHECK(i420->GetPixelFormat() == cosmo::media::PixelFormat::PIXEL_I420);

        auto resized = processor.Resize(i420, height / 2, width / 2);
        REQUIRE(VideoFrameValid(resized, true));
        CHECK(resized->GetWidth() == width / 2);
        CHECK(resized->GetHeight() == height / 2);

        auto rgb = processor.I4202RGB(i420);
        REQUIRE(VideoFrameValid(rgb, true));
        CHECK(rgb->GetPixelFormat() == cosmo::media::PixelFormat::PIXEL_RGB8);
    }

    const auto after = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    CHECK(after.rga_frames - before.rga_frames == 3);
    CHECK(after.rga_failures - before.rga_failures == 0);
}

TEST_CASE("Rockchip MPP decodes its H264 output through the Copy-out boundary",
          "[media][rockchip][decoder][.device]") {
    constexpr int width     = 640;
    constexpr int height    = 360;
    constexpr int pool_size = width * height * 3 / 2;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(), {pool_size});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;

    std::vector<uint8_t> i420(static_cast<size_t>(pool_size), 128);
    std::fill_n(i420.begin(), static_cast<size_t>(width) * height, 48);

    auto encoder = cosmo::media::VideoEncoder::Create(nullptr);
    REQUIRE(encoder);
    encoder->Set(cosmo::media::VideoCodecType::kH264, width, height);
    REQUIRE(encoder->Open());

    std::vector<cosmo::media::VideoPacketPtr> packets;
    for (int index = 0; index < 16; ++index) {
        auto packet = encoder->SendYUVFrame(i420.data());
        REQUIRE(packet);
        packets.push_back(std::move(packet));
    }

    auto decoder = cosmo::media::VideoDecoder::Create(0, nullptr);
    REQUIRE(decoder);
    decoder->SetCodecType(cosmo::media::VideoCodecType::kH264, width, height);
    REQUIRE(decoder->Open());

    const auto before = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    cosmo::media::VideoFramePtr decoded;
    bool discarded_deferred_output = false;
    for (size_t index = 0; index < packets.size(); ++index) {
        bool accepted = false;
        auto output = decoder->DecodeFrame(packets[index]->data.data(), packets[index]->data.size(),
                                           static_cast<int64_t>(index + 1), accepted);
        CHECK(accepted);
        if (output.IsDeferred() && !discarded_deferred_output) {
            output.Discard();
            discarded_deferred_output = true;
            continue;
        }
        if (output.HasFrame()) {
            decoded = output.Materialize();
        }
        if (decoded) {
            break;
        }
    }
    for (int attempt = 0; !decoded && attempt < 8; ++attempt) {
        decoded = decoder->GetFrame();
    }

    REQUIRE(decoded);
    CHECK(decoded->GetWidth() == width);
    CHECK(decoded->GetHeight() == height);
    CHECK(decoded->GetPixelFormat() == cosmo::media::PixelFormat::PIXEL_I420);
    CHECK(decoded->Active());

    const auto after = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    CHECK(discarded_deferred_output);
    CHECK(after.mpp_decoded_frames > before.mpp_decoded_frames);
    CHECK(after.mpp_decode_failures == before.mpp_decode_failures);
    CHECK(after.mpp_decode_fallbacks == before.mpp_decode_fallbacks);
    CHECK(after.mpp_copy_out_frames > before.mpp_copy_out_frames);
    CHECK(after.mpp_copy_out_failures == before.mpp_copy_out_failures);
    CHECK(after.mpp_early_dropped_frames == before.mpp_early_dropped_frames + 1);
    CHECK(decoder->Close());
}

#endif
