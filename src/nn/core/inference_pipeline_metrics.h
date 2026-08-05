#pragma once

#include <atomic>
#include <cstdint>

namespace cosmo::nn {

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
};

// Process-wide cumulative counters. Consumers derive interval averages from two
// snapshots, which keeps sampling independent from the inference threads.
class InferencePipelineMetrics {
public:
    void RecordColorConvert(uint64_t nanoseconds, uint64_t frames = 1);
    void RecordBlobConvert(uint64_t nanoseconds, uint64_t frames);
    void RecordGraphForward(uint64_t nanoseconds, uint64_t frames, bool success);
    void RecordResultParse(uint64_t nanoseconds, uint64_t frames, bool success);
    void RecordRknnForward(uint64_t nanoseconds, bool success);
    void RecordRknnPrepare(uint64_t nanoseconds);
    void RecordRknnInputsSet(uint64_t nanoseconds);
    void RecordRknnRun(uint64_t nanoseconds);
    void RecordRknnOutputsGet(uint64_t nanoseconds);
    void RecordRknnOutputTransform(uint64_t nanoseconds);

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
};

InferencePipelineMetrics& GetInferencePipelineMetrics();

}  // namespace cosmo::nn
