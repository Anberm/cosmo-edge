#include "catch_amalgamated.hpp"
#include "media/PreviewPipelineMetrics.h"

TEST_CASE("Preview pipeline metrics expose lifecycle and stage timings", "[media][preview][metrics]") {
    cosmo::media::PreviewPipelineMetrics metrics;

    metrics.PublisherOpened();
    metrics.PreviewStarted(false, 10'000'000);
    metrics.PreviewStarted(true, 25'000'000);
    metrics.RecordOsdFrame(2'000'000);
    metrics.RecordPublishedFrame(3'000'000);
    metrics.RecordRgaOperation(true, 4'000'000);
    metrics.RecordRgaOperation(false, 5'000'000);
    metrics.RecordMppEncode(true, 6'000'000);
    metrics.RecordMppEncode(false, 7'000'000);
    metrics.RecordMppDecode(true, 8'000'000);
    metrics.RecordMppDecode(false, 9'000'000);
    metrics.RecordMppDecodeFallback();
    metrics.RecordMppCopyOut(true, 11'000'000);
    metrics.RecordMppCopyOut(false, 12'000'000);
    metrics.RecordMppEarlyDrop();
    metrics.PreviewFailed();

    auto during = metrics.Snapshot();
    CHECK(during.active_publishers == 1);
    CHECK(during.active_preview_streams == 2);
    CHECK(during.active_raw_preview_streams == 1);
    CHECK(during.active_algorithm_preview_streams == 1);
    CHECK(during.preview_stream_starts == 2);
    CHECK(during.preview_stream_failures == 1);
    CHECK(during.osd_frames == 1);
    CHECK(during.osd_nanoseconds == 2'000'000);
    CHECK(during.published_frames == 1);
    CHECK(during.publish_nanoseconds == 3'000'000);
    CHECK(during.first_frames == 2);
    CHECK(during.first_frame_nanoseconds == 35'000'000);
    CHECK(during.first_frame_max_nanoseconds == 25'000'000);
    CHECK(during.rga_frames == 1);
    CHECK(during.rga_nanoseconds == 4'000'000);
    CHECK(during.rga_failures == 1);
    CHECK(during.mpp_encoded_frames == 1);
    CHECK(during.mpp_encode_nanoseconds == 6'000'000);
    CHECK(during.mpp_encode_failures == 1);
    CHECK(during.mpp_decoded_frames == 1);
    CHECK(during.mpp_decode_nanoseconds == 8'000'000);
    CHECK(during.mpp_decode_failures == 1);
    CHECK(during.mpp_decode_fallbacks == 1);
    CHECK(during.mpp_copy_out_frames == 1);
    CHECK(during.mpp_copy_out_nanoseconds == 11'000'000);
    CHECK(during.mpp_copy_out_failures == 1);
    CHECK(during.mpp_early_dropped_frames == 1);

    metrics.PreviewStopped(false);
    metrics.PreviewStopped(true);
    metrics.PublisherClosed();
    metrics.PublisherClosed();

    auto after = metrics.Snapshot();
    CHECK(after.active_publishers == 0);
    CHECK(after.active_preview_streams == 0);
    CHECK(after.active_raw_preview_streams == 0);
    CHECK(after.active_algorithm_preview_streams == 0);
    CHECK(after.preview_stream_stops == 2);
}
