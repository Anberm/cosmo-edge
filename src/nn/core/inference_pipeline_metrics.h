#pragma once

#include <atomic>
#include <cstdint>

namespace cosmo::nn {

enum class RknnModelScope : uint8_t {
    Other = 0,
    Detector,
};

struct InferencePipelineMetricsSnapshot {
    uint64_t color_convert_frames{0};
    uint64_t color_convert_nanoseconds{0};
    uint64_t blob_convert_frames{0};
    uint64_t blob_convert_nanoseconds{0};
    uint64_t graph_forward_frames{0};
    uint64_t graph_forward_nanoseconds{0};
    uint64_t graph_forward_failures{0};
    uint64_t result_parse_frames{0};
    uint64_t result_parse_nanoseconds{0};
    uint64_t result_parse_failures{0};
    uint64_t rknn_forwards{0};
    uint64_t rknn_forward_nanoseconds{0};
    uint64_t rknn_forward_failures{0};
    uint64_t rknn_prepare_calls{0};
    uint64_t rknn_prepare_nanoseconds{0};
    uint64_t rknn_inputs_set_calls{0};
    uint64_t rknn_inputs_set_nanoseconds{0};
    uint64_t rknn_run_calls{0};
    uint64_t rknn_run_nanoseconds{0};
    uint64_t rknn_outputs_get_calls{0};
    uint64_t rknn_outputs_get_nanoseconds{0};
    uint64_t rknn_output_transform_calls{0};
    uint64_t rknn_output_transform_nanoseconds{0};
    uint64_t rknn_mutex_wait_calls{0};
    uint64_t rknn_mutex_wait_nanoseconds{0};
    uint64_t rknn_detector_forwards{0};
    uint64_t rknn_detector_forward_nanoseconds{0};
    uint64_t rknn_detector_forward_failures{0};
    uint64_t rknn_detector_prepare_calls{0};
    uint64_t rknn_detector_prepare_nanoseconds{0};
    uint64_t rknn_detector_inputs_set_calls{0};
    uint64_t rknn_detector_inputs_set_nanoseconds{0};
    uint64_t rknn_detector_run_calls{0};
    uint64_t rknn_detector_run_nanoseconds{0};
    uint64_t rknn_detector_outputs_get_calls{0};
    uint64_t rknn_detector_outputs_get_nanoseconds{0};
    uint64_t rknn_detector_output_transform_calls{0};
    uint64_t rknn_detector_output_transform_nanoseconds{0};
    uint64_t rknn_detector_mutex_wait_calls{0};
    uint64_t rknn_detector_mutex_wait_nanoseconds{0};
    uint64_t rknn_preprocess_fast_hits{0};
    uint64_t rknn_rga_fill_calls{0};
    uint64_t rknn_rga_fill_nanoseconds{0};
    uint64_t rknn_rga_resize_color_calls{0};
    uint64_t rknn_rga_resize_color_nanoseconds{0};
    uint64_t rknn_rga_failures{0};
    uint64_t rknn_cpu_resize_fallback_calls{0};
    uint64_t rknn_cpu_resize_fallback_nanoseconds{0};
    uint64_t rknn_cpu_normalize_fallback_calls{0};
    uint64_t rknn_cpu_normalize_fallback_nanoseconds{0};
    uint64_t rknn_native_input_map_calls{0};
    uint64_t rknn_native_input_map_nanoseconds{0};
    uint64_t rknn_native_int8_inputs{0};
    uint64_t rknn_float_inputs{0};
    uint64_t rknn_input_compatibility_fallbacks{0};
    uint64_t yolov8_postprocess_calls{0};
    uint64_t yolov8_postprocess_nanoseconds{0};
    uint64_t yolov8_nms_calls{0};
    uint64_t yolov8_nms_nanoseconds{0};
};

// Process-wide cumulative counters. Consumers derive interval averages from two
// snapshots, which keeps sampling independent from the inference threads.
class InferencePipelineMetrics {
public:
    void RecordColorConvert(uint64_t nanoseconds, uint64_t frames = 1);
    void RecordBlobConvert(uint64_t nanoseconds, uint64_t frames);
    void RecordGraphForward(uint64_t nanoseconds, uint64_t frames, bool success);
    void RecordResultParse(uint64_t nanoseconds, uint64_t frames, bool success);
    void RecordRknnForward(uint64_t nanoseconds, bool success,
                           RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnPrepare(uint64_t nanoseconds,
                           RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnInputsSet(uint64_t nanoseconds,
                             RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnRun(uint64_t nanoseconds, RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnOutputsGet(uint64_t nanoseconds,
                              RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnOutputTransform(uint64_t nanoseconds,
                                   RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnMutexWait(uint64_t nanoseconds,
                             RknnModelScope scope = RknnModelScope::Other);
    void RecordRknnPreprocessFastHit();
    void RecordRknnRgaFill(uint64_t nanoseconds);
    void RecordRknnRgaResizeColor(uint64_t nanoseconds);
    void RecordRknnRgaFailure();
    void RecordRknnCpuResizeFallback(uint64_t nanoseconds);
    void RecordRknnCpuNormalizeFallback(uint64_t nanoseconds);
    void RecordRknnNativeInputMap(uint64_t nanoseconds);
    void RecordRknnInputFormat(bool native_int8, bool compatibility_fallback = false);
    void RecordYolov8Postprocess(uint64_t nanoseconds);
    void RecordYolov8Nms(uint64_t nanoseconds);

    [[nodiscard]] InferencePipelineMetricsSnapshot Snapshot() const;

private:
    static void RecordStage(std::atomic<uint64_t>& count, std::atomic<uint64_t>& duration,
                            uint64_t samples, uint64_t nanoseconds);

    std::atomic<uint64_t> color_convert_frames_{0};
    std::atomic<uint64_t> color_convert_nanoseconds_{0};
    std::atomic<uint64_t> blob_convert_frames_{0};
    std::atomic<uint64_t> blob_convert_nanoseconds_{0};
    std::atomic<uint64_t> graph_forward_frames_{0};
    std::atomic<uint64_t> graph_forward_nanoseconds_{0};
    std::atomic<uint64_t> graph_forward_failures_{0};
    std::atomic<uint64_t> result_parse_frames_{0};
    std::atomic<uint64_t> result_parse_nanoseconds_{0};
    std::atomic<uint64_t> result_parse_failures_{0};
    std::atomic<uint64_t> rknn_forwards_{0};
    std::atomic<uint64_t> rknn_forward_nanoseconds_{0};
    std::atomic<uint64_t> rknn_forward_failures_{0};
    std::atomic<uint64_t> rknn_prepare_calls_{0};
    std::atomic<uint64_t> rknn_prepare_nanoseconds_{0};
    std::atomic<uint64_t> rknn_inputs_set_calls_{0};
    std::atomic<uint64_t> rknn_inputs_set_nanoseconds_{0};
    std::atomic<uint64_t> rknn_run_calls_{0};
    std::atomic<uint64_t> rknn_run_nanoseconds_{0};
    std::atomic<uint64_t> rknn_outputs_get_calls_{0};
    std::atomic<uint64_t> rknn_outputs_get_nanoseconds_{0};
    std::atomic<uint64_t> rknn_output_transform_calls_{0};
    std::atomic<uint64_t> rknn_output_transform_nanoseconds_{0};
    std::atomic<uint64_t> rknn_mutex_wait_calls_{0};
    std::atomic<uint64_t> rknn_mutex_wait_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_forwards_{0};
    std::atomic<uint64_t> rknn_detector_forward_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_forward_failures_{0};
    std::atomic<uint64_t> rknn_detector_prepare_calls_{0};
    std::atomic<uint64_t> rknn_detector_prepare_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_inputs_set_calls_{0};
    std::atomic<uint64_t> rknn_detector_inputs_set_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_run_calls_{0};
    std::atomic<uint64_t> rknn_detector_run_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_outputs_get_calls_{0};
    std::atomic<uint64_t> rknn_detector_outputs_get_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_output_transform_calls_{0};
    std::atomic<uint64_t> rknn_detector_output_transform_nanoseconds_{0};
    std::atomic<uint64_t> rknn_detector_mutex_wait_calls_{0};
    std::atomic<uint64_t> rknn_detector_mutex_wait_nanoseconds_{0};
    std::atomic<uint64_t> rknn_preprocess_fast_hits_{0};
    std::atomic<uint64_t> rknn_rga_fill_calls_{0};
    std::atomic<uint64_t> rknn_rga_fill_nanoseconds_{0};
    std::atomic<uint64_t> rknn_rga_resize_color_calls_{0};
    std::atomic<uint64_t> rknn_rga_resize_color_nanoseconds_{0};
    std::atomic<uint64_t> rknn_rga_failures_{0};
    std::atomic<uint64_t> rknn_cpu_resize_fallback_calls_{0};
    std::atomic<uint64_t> rknn_cpu_resize_fallback_nanoseconds_{0};
    std::atomic<uint64_t> rknn_cpu_normalize_fallback_calls_{0};
    std::atomic<uint64_t> rknn_cpu_normalize_fallback_nanoseconds_{0};
    std::atomic<uint64_t> rknn_native_input_map_calls_{0};
    std::atomic<uint64_t> rknn_native_input_map_nanoseconds_{0};
    std::atomic<uint64_t> rknn_native_int8_inputs_{0};
    std::atomic<uint64_t> rknn_float_inputs_{0};
    std::atomic<uint64_t> rknn_input_compatibility_fallbacks_{0};
    std::atomic<uint64_t> yolov8_postprocess_calls_{0};
    std::atomic<uint64_t> yolov8_postprocess_nanoseconds_{0};
    std::atomic<uint64_t> yolov8_nms_calls_{0};
    std::atomic<uint64_t> yolov8_nms_nanoseconds_{0};
};

InferencePipelineMetrics& GetInferencePipelineMetrics();

}  // namespace cosmo::nn
