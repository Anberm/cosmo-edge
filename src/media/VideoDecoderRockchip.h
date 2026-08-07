#pragma once

#include <memory>

#include "media/VideoDecoder.h"

namespace cosmo::media {

class VideoDecoderCpu;
struct RockchipDecoderState;

/// Rockchip MPP decoder with a bounded device-to-host copy boundary.
///
/// Compressed H.264/H.265 packets are decoded by the VPU. A decoded MPP frame
/// is retained on the decoder thread until the channel sampling decision; only
/// selected outputs are copied into the compact I420 VideoFrame expected by
/// the existing CosmoEdge pipeline. DMA-BUF zero-copy ownership is
/// intentionally outside this phase.
class VideoDecoderRockchip final : public VideoDecoder {
public:
    explicit VideoDecoderRockchip(size_t name);
    ~VideoDecoderRockchip() override;

    bool Open() override;
    bool Close() override;
    bool IsOpened() override;
    bool ReuseForStreamRestart(VideoCodecType type, int width, int height) override;

    bool SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) override;
    VideoFramePtr GetFrame() override;
    DecodedVideoFrame GetDecodedFrame() override;

    static VideoDecoderCapability Probe(VideoCodecType type);

private:
    bool OpenMpp();
    bool ConfigureFrameGroup(size_t buffer_size);
    DecodedVideoFrame ReceiveMppFrame(bool& made_progress);
    void CleanMpp();

    std::unique_ptr<RockchipDecoderState> state_;
    std::unique_ptr<VideoDecoderCpu> fallback_;
};

}  // namespace cosmo::media
