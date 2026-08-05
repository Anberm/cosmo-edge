#include "catch_amalgamated.hpp"
#include "nn/core/inference_pipeline_metrics.h"

TEST_CASE("Inference pipeline metrics expose host, graph, and RKNN stage timings",
          "[nn][rknn][metrics]") {
    cosmo::nn::InferencePipelineMetrics metrics;

    metrics.RecordColorConvert(2'000'000, 2);
    metrics.RecordBlobConvert(3'000'000, 2);
    metrics.RecordGraphForward(8'000'000, 2, true);
    metrics.RecordGraphForward(1'000'000, 1, false);
    metrics.RecordResultParse(4'000'000, 2, true);
    metrics.RecordResultParse(500'000, 1, false);
    metrics.RecordRknnPrepare(1'000'000);
    metrics.RecordRknnInputsSet(2'000'000);
    metrics.RecordRknnRun(3'000'000);
    metrics.RecordRknnOutputsGet(4'000'000);
    metrics.RecordRknnOutputTransform(5'000'000);
    metrics.RecordRknnForward(15'000'000, true);
    metrics.RecordRknnForward(7'000'000, false);

    const auto snapshot = metrics.Snapshot();
    CHECK(snapshot.color_convert_frames == 2);
    CHECK(snapshot.color_convert_nanoseconds == 2'000'000);
    CHECK(snapshot.blob_convert_frames == 2);
    CHECK(snapshot.graph_forward_frames == 3);
    CHECK(snapshot.graph_forward_nanoseconds == 9'000'000);
    CHECK(snapshot.graph_forward_failures == 1);
    CHECK(snapshot.result_parse_frames == 3);
    CHECK(snapshot.result_parse_failures == 1);
    CHECK(snapshot.rknn_forwards == 2);
    CHECK(snapshot.rknn_forward_nanoseconds == 22'000'000);
    CHECK(snapshot.rknn_forward_failures == 1);
    CHECK(snapshot.rknn_prepare_calls == 1);
    CHECK(snapshot.rknn_inputs_set_calls == 1);
    CHECK(snapshot.rknn_run_calls == 1);
    CHECK(snapshot.rknn_outputs_get_calls == 1);
    CHECK(snapshot.rknn_output_transform_calls == 1);
}
