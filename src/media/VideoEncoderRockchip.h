#pragma once

#include <memory>

#include "media/VideoEncoder.h"

namespace cosmo::media {

class VideoEncoderCpu;
struct RockchipEncoderState;

/// Rockchip MPP encoder with a bounded host-to-MPP copy boundary.
///
/// The caller still owns compact I420 host memory. Each frame is copied into a
/// reusable, stride-aligned MPP buffer before hardware encoding. DMA-BUF
/// zero-copy ownership is intentionally outside this phase.
class VideoEncoderRockchip final : public VideoEncoder {
public:
    VideoEncoderRockchip();
    ~VideoEncoderRockchip() override;

    bool Open() override;
    VideoPacketPtr SendYUVFrame(void* data) override;

    static VideoEncoderCapability Probe(VideoCodecType type);

private:
    bool OpenMpp();
    void Clean();
    void CleanMpp();

    std::unique_ptr<RockchipEncoderState> state_;
    std::unique_ptr<VideoEncoderCpu> fallback_;
};

}  // namespace cosmo::media
