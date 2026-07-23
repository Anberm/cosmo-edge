#include "catch_amalgamated.hpp"
#include "media/PreviewPipelineMetrics.h"

TEST_CASE("Preview pipeline metrics expose lifecycle and stage timings", "[media][preview][metrics]") {
    cosmo::media::PreviewPipelineMetrics metrics;

    metrics.PublisherOpened();
    metrics.PreviewStarted(false, 10'000'000);
    metrics.PreviewStarted(true, 25'000'000);
    metrics.RecordOsdFrame(2'000'000);
    metrics.RecordPublishedFrame(3'000'000);
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
