#pragma once

#include <atomic>
#include <cstdint>

namespace cosmo::media {

struct PreviewPipelineMetricsSnapshot {
    uint64_t active_publishers{0};
    uint64_t active_preview_streams{0};
    uint64_t active_raw_preview_streams{0};
    uint64_t active_algorithm_preview_streams{0};
    uint64_t preview_stream_starts{0};
    uint64_t preview_stream_stops{0};
    uint64_t preview_stream_failures{0};
    uint64_t osd_frames{0};
    uint64_t osd_nanoseconds{0};
    uint64_t published_frames{0};
    uint64_t publish_nanoseconds{0};
    uint64_t first_frames{0};
    uint64_t first_frame_nanoseconds{0};
    uint64_t first_frame_max_nanoseconds{0};
};

class PreviewPipelineMetrics {
public:
    void PublisherOpened();
    void PublisherClosed();
    void PreviewStarted(bool algorithm_preview, uint64_t first_frame_nanoseconds);
    void PreviewStopped(bool algorithm_preview);
    void PreviewFailed();
    void RecordOsdFrame(uint64_t nanoseconds);
    void RecordPublishedFrame(uint64_t nanoseconds);

    [[nodiscard]] PreviewPipelineMetricsSnapshot Snapshot() const;

private:
    std::atomic<uint64_t> active_publishers_{0};
    std::atomic<uint64_t> active_preview_streams_{0};
    std::atomic<uint64_t> active_raw_preview_streams_{0};
    std::atomic<uint64_t> active_algorithm_preview_streams_{0};
    std::atomic<uint64_t> preview_stream_starts_{0};
    std::atomic<uint64_t> preview_stream_stops_{0};
    std::atomic<uint64_t> preview_stream_failures_{0};
    std::atomic<uint64_t> osd_frames_{0};
    std::atomic<uint64_t> osd_nanoseconds_{0};
    std::atomic<uint64_t> published_frames_{0};
    std::atomic<uint64_t> publish_nanoseconds_{0};
    std::atomic<uint64_t> first_frames_{0};
    std::atomic<uint64_t> first_frame_nanoseconds_{0};
    std::atomic<uint64_t> first_frame_max_nanoseconds_{0};
};

PreviewPipelineMetrics& GetPreviewPipelineMetrics();

}  // namespace cosmo::media
