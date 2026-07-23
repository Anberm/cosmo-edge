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
    };
}

PreviewPipelineMetrics& GetPreviewPipelineMetrics() {
    static PreviewPipelineMetrics metrics;
    return metrics;
}

}  // namespace cosmo::media
