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

void InferencePipelineMetrics::RecordRknnForward(uint64_t nanoseconds, bool success,
                                                 RknnModelScope scope) {
    RecordStage(rknn_forwards_, rknn_forward_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_forward_failures_.fetch_add(1, std::memory_order_relaxed);
    if (scope == RknnModelScope::Detector) {
        RecordStage(rknn_detector_forwards_, rknn_detector_forward_nanoseconds_, 1, nanoseconds);
        if (!success)
            rknn_detector_forward_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void InferencePipelineMetrics::RecordRknnPrepare(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_prepare_calls_, rknn_prepare_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_prepare_calls_, rknn_detector_prepare_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnInputsSet(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_inputs_set_calls_, rknn_inputs_set_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_inputs_set_calls_, rknn_detector_inputs_set_nanoseconds_, 1,
                    nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRun(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_run_calls_, rknn_run_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_run_calls_, rknn_detector_run_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnOutputsGet(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_outputs_get_calls_, rknn_outputs_get_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_outputs_get_calls_, rknn_detector_outputs_get_nanoseconds_, 1,
                    nanoseconds);
}

void InferencePipelineMetrics::RecordRknnOutputTransform(uint64_t nanoseconds,
                                                         RknnModelScope scope) {
    RecordStage(rknn_output_transform_calls_, rknn_output_transform_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_output_transform_calls_,
                    rknn_detector_output_transform_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnMutexWait(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_mutex_wait_calls_, rknn_mutex_wait_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_mutex_wait_calls_, rknn_detector_mutex_wait_nanoseconds_, 1,
                    nanoseconds);
}

void InferencePipelineMetrics::RecordRknnPreprocessFastHit() {
    rknn_preprocess_fast_hits_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaFill(uint64_t nanoseconds) {
    RecordStage(rknn_rga_fill_calls_, rknn_rga_fill_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRgaResizeColor(uint64_t nanoseconds) {
    RecordStage(rknn_rga_resize_color_calls_, rknn_rga_resize_color_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRgaFailure() {
    rknn_rga_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnCpuResizeFallback(uint64_t nanoseconds) {
    RecordStage(rknn_cpu_resize_fallback_calls_, rknn_cpu_resize_fallback_nanoseconds_, 1,
                nanoseconds);
}

void InferencePipelineMetrics::RecordRknnCpuNormalizeFallback(uint64_t nanoseconds) {
    RecordStage(rknn_cpu_normalize_fallback_calls_, rknn_cpu_normalize_fallback_nanoseconds_, 1,
                nanoseconds);
}

void InferencePipelineMetrics::RecordRknnNativeInputMap(uint64_t nanoseconds) {
    RecordStage(rknn_native_input_map_calls_, rknn_native_input_map_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnInputFormat(bool native_int8,
                                                     bool compatibility_fallback) {
    (native_int8 ? rknn_native_int8_inputs_ : rknn_float_inputs_)
        .fetch_add(1, std::memory_order_relaxed);
    if (compatibility_fallback)
        rknn_input_compatibility_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordYolov8Postprocess(uint64_t nanoseconds) {
    RecordStage(yolov8_postprocess_calls_, yolov8_postprocess_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordYolov8Nms(uint64_t nanoseconds) {
    RecordStage(yolov8_nms_calls_, yolov8_nms_nanoseconds_, 1, nanoseconds);
}

InferencePipelineMetricsSnapshot InferencePipelineMetrics::Snapshot() const {
    InferencePipelineMetricsSnapshot snapshot;
#define SNAPSHOT_FIELD(field) snapshot.field = field##_.load(std::memory_order_relaxed)
    SNAPSHOT_FIELD(color_convert_frames);
    SNAPSHOT_FIELD(color_convert_nanoseconds);
    SNAPSHOT_FIELD(blob_convert_frames);
    SNAPSHOT_FIELD(blob_convert_nanoseconds);
    SNAPSHOT_FIELD(graph_forward_frames);
    SNAPSHOT_FIELD(graph_forward_nanoseconds);
    SNAPSHOT_FIELD(graph_forward_failures);
    SNAPSHOT_FIELD(result_parse_frames);
    SNAPSHOT_FIELD(result_parse_nanoseconds);
    SNAPSHOT_FIELD(result_parse_failures);
    SNAPSHOT_FIELD(rknn_forwards);
    SNAPSHOT_FIELD(rknn_forward_nanoseconds);
    SNAPSHOT_FIELD(rknn_forward_failures);
    SNAPSHOT_FIELD(rknn_prepare_calls);
    SNAPSHOT_FIELD(rknn_prepare_nanoseconds);
    SNAPSHOT_FIELD(rknn_inputs_set_calls);
    SNAPSHOT_FIELD(rknn_inputs_set_nanoseconds);
    SNAPSHOT_FIELD(rknn_run_calls);
    SNAPSHOT_FIELD(rknn_run_nanoseconds);
    SNAPSHOT_FIELD(rknn_outputs_get_calls);
    SNAPSHOT_FIELD(rknn_outputs_get_nanoseconds);
    SNAPSHOT_FIELD(rknn_output_transform_calls);
    SNAPSHOT_FIELD(rknn_output_transform_nanoseconds);
    SNAPSHOT_FIELD(rknn_mutex_wait_calls);
    SNAPSHOT_FIELD(rknn_mutex_wait_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_forwards);
    SNAPSHOT_FIELD(rknn_detector_forward_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_forward_failures);
    SNAPSHOT_FIELD(rknn_detector_prepare_calls);
    SNAPSHOT_FIELD(rknn_detector_prepare_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_inputs_set_calls);
    SNAPSHOT_FIELD(rknn_detector_inputs_set_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_run_calls);
    SNAPSHOT_FIELD(rknn_detector_run_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_outputs_get_calls);
    SNAPSHOT_FIELD(rknn_detector_outputs_get_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_output_transform_calls);
    SNAPSHOT_FIELD(rknn_detector_output_transform_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_mutex_wait_calls);
    SNAPSHOT_FIELD(rknn_detector_mutex_wait_nanoseconds);
    SNAPSHOT_FIELD(rknn_preprocess_fast_hits);
    SNAPSHOT_FIELD(rknn_rga_fill_calls);
    SNAPSHOT_FIELD(rknn_rga_fill_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_resize_color_calls);
    SNAPSHOT_FIELD(rknn_rga_resize_color_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_failures);
    SNAPSHOT_FIELD(rknn_cpu_resize_fallback_calls);
    SNAPSHOT_FIELD(rknn_cpu_resize_fallback_nanoseconds);
    SNAPSHOT_FIELD(rknn_cpu_normalize_fallback_calls);
    SNAPSHOT_FIELD(rknn_cpu_normalize_fallback_nanoseconds);
    SNAPSHOT_FIELD(rknn_native_input_map_calls);
    SNAPSHOT_FIELD(rknn_native_input_map_nanoseconds);
    SNAPSHOT_FIELD(rknn_native_int8_inputs);
    SNAPSHOT_FIELD(rknn_float_inputs);
    SNAPSHOT_FIELD(rknn_input_compatibility_fallbacks);
    SNAPSHOT_FIELD(yolov8_postprocess_calls);
    SNAPSHOT_FIELD(yolov8_postprocess_nanoseconds);
    SNAPSHOT_FIELD(yolov8_nms_calls);
    SNAPSHOT_FIELD(yolov8_nms_nanoseconds);
#undef SNAPSHOT_FIELD
    return snapshot;
}

InferencePipelineMetrics& GetInferencePipelineMetrics() {
    static InferencePipelineMetrics metrics;
    return metrics;
}

}  // namespace cosmo::nn
