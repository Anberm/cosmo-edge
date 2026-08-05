// VideoDecoder — Video Decoder implementation.

#include "media/VideoDecoder.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "util/Log.h"

static constexpr const char* kTag = "[DECODER] ";

namespace cosmo {
namespace media {

    DecodedVideoFrame::DecodedVideoFrame(VideoFramePtr frame) : frame_(std::move(frame)) {
        if (frame_) {
            frame_index_ = frame_->GetFrameIndex();
            width_       = frame_->GetWidth();
            height_      = frame_->GetHeight();
            format_      = frame_->GetPixelFormat();
        }
    }

    DecodedVideoFrame::DecodedVideoFrame(uint64_t frame_index, size_t width, size_t height,
                                         PixelFormat format, Materializer materializer,
                                         DiscardHandler discard_handler)
        : frame_index_(frame_index),
          width_(width),
          height_(height),
          format_(format),
          materializer_(std::move(materializer)),
          discard_handler_(std::move(discard_handler)) {}

    bool DecodedVideoFrame::HasFrame() const {
        return frame_ != nullptr || static_cast<bool>(materializer_);
    }

    bool DecodedVideoFrame::IsDeferred() const {
        return frame_ == nullptr && static_cast<bool>(materializer_);
    }

    uint64_t DecodedVideoFrame::GetFrameIndex() const {
        return frame_ ? frame_->GetFrameIndex() : frame_index_;
    }

    size_t DecodedVideoFrame::GetWidth() const {
        return frame_ ? frame_->GetWidth() : width_;
    }

    size_t DecodedVideoFrame::GetHeight() const {
        return frame_ ? frame_->GetHeight() : height_;
    }

    PixelFormat DecodedVideoFrame::GetPixelFormat() const {
        return frame_ ? frame_->GetPixelFormat() : format_;
    }

    VideoFramePtr DecodedVideoFrame::Materialize() {
        if (frame_) {
            return frame_;
        }
        if (!materializer_) {
            return nullptr;
        }

        auto materializer = std::move(materializer_);
        discard_handler_  = nullptr;
        frame_             = materializer();
        return frame_;
    }

    void DecodedVideoFrame::Discard() {
        if (IsDeferred() && discard_handler_) {
            discard_handler_();
        }
        frame_.reset();
        materializer_    = nullptr;
        discard_handler_ = nullptr;
    }

    VideoDecoder::VideoDecoder(size_t name) : idx_name_("atomicDecoder_" + std::to_string(name)) {
        LOG_INFO("{} Construction", idx_name_);
    }

    VideoDecoder::~VideoDecoder() {}

    void VideoDecoder::SetCodecType(VideoCodecType type, int valWidth, int valHeight) {
        codec_type_ = type;
        width_      = static_cast<size_t>(valWidth);
        height_     = static_cast<size_t>(valHeight);
    }

    size_t VideoDecoder::GetWidth() const {
        return width_;
    }

    size_t VideoDecoder::GetHeight() const {
        return height_;
    }

    DecodedVideoFrame VideoDecoder::DecodeFrame(const uint8_t* pkt, size_t len, int64_t frame_idx,
                                                bool& result) {
        auto send_result = SendPacket(pkt, len, frame_idx);
        send_pkt_cnt_ += 1;
        if (frame_idx % (25 * 60) == 0) {
            LOG_INFO("{} Decode {} Frames. Total Get Output {} Frames.", idx_name_, send_pkt_cnt_,
                     recv_frame_cnt_);
        }

        if (!send_result) {
            result = false;
            std::ostringstream prefix;
            const size_t prefix_size = pkt == nullptr ? 0 : std::min<size_t>(len, 6);
            for (size_t i = 0; i < prefix_size; ++i) {
                if (i != 0) {
                    prefix << ' ';
                }
                prefix << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(pkt[i]);
            }
            LOG_WARN("{}{} Frame Size:{} Decode Failed. frameIndex:{} data:{}", kTag, idx_name_, len,
                     frame_idx, prefix.str());
            return {};
        }

        result = true;

        auto frame = GetDecodedFrame();
        if (frame.HasFrame()) {
            recv_frame_cnt_ += 1;
        }

        return frame;
    }

    VideoFramePtr VideoDecoder::Decode(const uint8_t* pkt, size_t len, int64_t frame_idx, bool& result) {
        auto frame = DecodeFrame(pkt, len, frame_idx, result);
        return frame.Materialize();
    }

    DecodedVideoFrame VideoDecoder::GetDecodedFrame() {
        return DecodedVideoFrame(GetFrame());
    }

}  // namespace media
}  // namespace cosmo
