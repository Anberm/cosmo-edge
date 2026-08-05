#include "media/PreviewPipelineMetrics.h"

namespace cosmo::media {
namespace {
    void DecrementWithoutUnderflow(std::atomic<uint64_t>& value) {
        auto current = value.load(std::memory_order_relaxed);
        while (current != 0 &&
               !value.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
        }
    }

    void UpdateMaximum(std::atomic<uint64_t>& value, uint64_t sample) {
        auto current = value.load(std::memory_order_relaxed);
        while (sample > current && !value.compare_exchange_weak(current, sample, std::memory_order_relaxed)) {
        }
    }
}  // namespace

void PreviewPipelineMetrics::PublisherOpened() {
    active_publishers_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::PublisherClosed() {
    DecrementWithoutUnderflow(active_publishers_);
}

void PreviewPipelineMetrics::PreviewStarted(bool algorithm_preview, uint64_t first_frame_nanoseconds) {
    active_preview_streams_.fetch_add(1, std::memory_order_relaxed);
    auto& active_by_type =
        algorithm_preview ? active_algorithm_preview_streams_ : active_raw_preview_streams_;
    active_by_type.fetch_add(1, std::memory_order_relaxed);
    preview_stream_starts_.fetch_add(1, std::memory_order_relaxed);
    first_frames_.fetch_add(1, std::memory_order_relaxed);
    first_frame_nanoseconds_.fetch_add(first_frame_nanoseconds, std::memory_order_relaxed);
    UpdateMaximum(first_frame_max_nanoseconds_, first_frame_nanoseconds);
}

void PreviewPipelineMetrics::PreviewStopped(bool algorithm_preview) {
    DecrementWithoutUnderflow(active_preview_streams_);
    auto& active_by_type =
        algorithm_preview ? active_algorithm_preview_streams_ : active_raw_preview_streams_;
    DecrementWithoutUnderflow(active_by_type);
    preview_stream_stops_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::PreviewFailed() {
    preview_stream_failures_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordOsdFrame(uint64_t nanoseconds) {
    osd_frames_.fetch_add(1, std::memory_order_relaxed);
    osd_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordPublishedFrame(uint64_t nanoseconds) {
    published_frames_.fetch_add(1, std::memory_order_relaxed);
    publish_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordRgaOperation(bool success, uint64_t nanoseconds) {
    if (success) {
        rga_frames_.fetch_add(1, std::memory_order_relaxed);
        rga_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        rga_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppEncode(bool success, uint64_t nanoseconds) {
    if (success) {
        mpp_encoded_frames_.fetch_add(1, std::memory_order_relaxed);
        mpp_encode_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        mpp_encode_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppDecode(bool success, uint64_t nanoseconds) {
    if (success) {
        mpp_decoded_frames_.fetch_add(1, std::memory_order_relaxed);
        mpp_decode_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        mpp_decode_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppDecodeFallback() {
    mpp_decode_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordMppCopyOut(bool success, uint64_t nanoseconds) {
    if (success) {
        mpp_copy_out_frames_.fetch_add(1, std::memory_order_relaxed);
        mpp_copy_out_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        mpp_copy_out_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppEarlyDrop() {
    mpp_early_dropped_frames_.fetch_add(1, std::memory_order_relaxed);
}

PreviewPipelineMetricsSnapshot PreviewPipelineMetrics::Snapshot() const {
    return {
        active_publishers_.load(std::memory_order_relaxed),
        active_preview_streams_.load(std::memory_order_relaxed),
        active_raw_preview_streams_.load(std::memory_order_relaxed),
        active_algorithm_preview_streams_.load(std::memory_order_relaxed),
        preview_stream_starts_.load(std::memory_order_relaxed),
        preview_stream_stops_.load(std::memory_order_relaxed),
        preview_stream_failures_.load(std::memory_order_relaxed),
        osd_frames_.load(std::memory_order_relaxed),
        osd_nanoseconds_.load(std::memory_order_relaxed),
        published_frames_.load(std::memory_order_relaxed),
        publish_nanoseconds_.load(std::memory_order_relaxed),
        first_frames_.load(std::memory_order_relaxed),
        first_frame_nanoseconds_.load(std::memory_order_relaxed),
        first_frame_max_nanoseconds_.load(std::memory_order_relaxed),
        rga_frames_.load(std::memory_order_relaxed),
        rga_nanoseconds_.load(std::memory_order_relaxed),
        rga_failures_.load(std::memory_order_relaxed),
        mpp_encoded_frames_.load(std::memory_order_relaxed),
        mpp_encode_nanoseconds_.load(std::memory_order_relaxed),
        mpp_encode_failures_.load(std::memory_order_relaxed),
        mpp_decoded_frames_.load(std::memory_order_relaxed),
        mpp_decode_nanoseconds_.load(std::memory_order_relaxed),
        mpp_decode_failures_.load(std::memory_order_relaxed),
        mpp_decode_fallbacks_.load(std::memory_order_relaxed),
        mpp_copy_out_frames_.load(std::memory_order_relaxed),
        mpp_copy_out_nanoseconds_.load(std::memory_order_relaxed),
        mpp_copy_out_failures_.load(std::memory_order_relaxed),
        mpp_early_dropped_frames_.load(std::memory_order_relaxed),
    };
}

PreviewPipelineMetrics& GetPreviewPipelineMetrics() {
    static PreviewPipelineMetrics metrics;
    return metrics;
}

}  // namespace cosmo::media
