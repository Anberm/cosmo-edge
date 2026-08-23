#pragma once

#include <memory>

#include "media/VideoEncoder.h"

namespace cosmo::media {

class VideoEncoderCpu;
struct RockchipEncoderState;

/// Shared Rockchip MPP encoder with an RGA-backed host-to-DMA boundary.
///
/// The caller still owns compact I420 host memory. Each frame is copied into a
/// reusable, stride-aligned MPP DMA-BUF through RGA before hardware encoding.
/// A measured CPU fallback remains available for unsupported RGA layouts.
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
