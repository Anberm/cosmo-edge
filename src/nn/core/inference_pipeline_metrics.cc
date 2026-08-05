#include "nn/core/inference_pipeline_metrics.h"

namespace cosmo::nn {

void InferencePipelineMetrics::RecordStage(std::atomic<uint64_t>& count,
                                           std::atomic<uint64_t>& duration, uint64_t samples,
                                           uint64_t nanoseconds) {
    count.fetch_add(samples, std::memory_order_relaxed);
    duration.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordColorConvert(uint64_t nanoseconds, uint64_t frames) {
    RecordStage(color_convert_frames_, color_convert_nanoseconds_, frames, nanoseconds);
}

void InferencePipelineMetrics::RecordBlobConvert(uint64_t nanoseconds, uint64_t frames) {
    RecordStage(blob_convert_frames_, blob_convert_nanoseconds_, frames, nanoseconds);
}

void InferencePipelineMetrics::RecordGraphForward(uint64_t nanoseconds, uint64_t frames,
                                                  bool success) {
    RecordStage(graph_forward_frames_, graph_forward_nanoseconds_, frames, nanoseconds);
    if (!success)
        graph_forward_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordResultParse(uint64_t nanoseconds, uint64_t frames,
                                                 bool success) {
    RecordStage(result_parse_frames_, result_parse_nanoseconds_, frames, nanoseconds);
    if (!success)
        result_parse_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnForward(uint64_t nanoseconds, bool success) {
    RecordStage(rknn_forwards_, rknn_forward_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_forward_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnPrepare(uint64_t nanoseconds) {
    RecordStage(rknn_prepare_calls_, rknn_prepare_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnInputsSet(uint64_t nanoseconds) {
    RecordStage(rknn_inputs_set_calls_, rknn_inputs_set_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRun(uint64_t nanoseconds) {
    RecordStage(rknn_run_calls_, rknn_run_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnOutputsGet(uint64_t nanoseconds) {
    RecordStage(rknn_outputs_get_calls_, rknn_outputs_get_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnOutputTransform(uint64_t nanoseconds) {
    RecordStage(rknn_output_transform_calls_, rknn_output_transform_nanoseconds_, 1, nanoseconds);
}

InferencePipelineMetricsSnapshot InferencePipelineMetrics::Snapshot() const {
    return {
        color_convert_frames_.load(std::memory_order_relaxed),
        color_convert_nanoseconds_.load(std::memory_order_relaxed),
        blob_convert_frames_.load(std::memory_order_relaxed),
        blob_convert_nanoseconds_.load(std::memory_order_relaxed),
        graph_forward_frames_.load(std::memory_order_relaxed),
        graph_forward_nanoseconds_.load(std::memory_order_relaxed),
        graph_forward_failures_.load(std::memory_order_relaxed),
        result_parse_frames_.load(std::memory_order_relaxed),
        result_parse_nanoseconds_.load(std::memory_order_relaxed),
        result_parse_failures_.load(std::memory_order_relaxed),
        rknn_forwards_.load(std::memory_order_relaxed),
        rknn_forward_nanoseconds_.load(std::memory_order_relaxed),
        rknn_forward_failures_.load(std::memory_order_relaxed),
        rknn_prepare_calls_.load(std::memory_order_relaxed),
        rknn_prepare_nanoseconds_.load(std::memory_order_relaxed),
        rknn_inputs_set_calls_.load(std::memory_order_relaxed),
        rknn_inputs_set_nanoseconds_.load(std::memory_order_relaxed),
        rknn_run_calls_.load(std::memory_order_relaxed),
        rknn_run_nanoseconds_.load(std::memory_order_relaxed),
        rknn_outputs_get_calls_.load(std::memory_order_relaxed),
        rknn_outputs_get_nanoseconds_.load(std::memory_order_relaxed),
        rknn_output_transform_calls_.load(std::memory_order_relaxed),
        rknn_output_transform_nanoseconds_.load(std::memory_order_relaxed),
    };
}

InferencePipelineMetrics& GetInferencePipelineMetrics() {
    static InferencePipelineMetrics metrics;
    return metrics;
}

}  // namespace cosmo::nn
