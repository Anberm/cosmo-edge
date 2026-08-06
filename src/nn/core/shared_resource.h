#pragma once

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmruntime_cpp.h"
#include "bmruntime_interface.h"
#endif

#include <string>
#include <vector>

#include "nn/core/abstract_context.h"

namespace cosmo::nn {

struct Yolov8DirectPostprocessConfig {
    bool configured{false};
    float confidence_threshold{0.0f};
    int input_width{0};
    int input_height{0};
};

struct Yolov8Candidate {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
    float confidence{0.0f};
    int class_id{-1};
};

struct Yolov8CandidateBatch {
    bool ready{false};
    std::vector<Yolov8Candidate> candidates;

    void Reset() {
        ready = false;
        candidates.clear();
    }
};

class SharedResource {
public:
    explicit SharedResource(int i = 0) noexcept(false);

    ~SharedResource();

public:
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    bm_handle_t m_handle;
#endif

    int current_device_id = 0;

    // dino
    void* tokenizer_handle = nullptr;
    std::string tokenizer_path{};
    std::vector<int32_t> prompt_token_ids{};
    float text_threshold = 0.0f;
    float box_threshold  = 0.0f;

    // sophon
    int net_input_w         = 0;
    int net_input_h         = 0;
    float model_input_scale = 1.0f;  // for INT8 quantized models

    // Optional producer/consumer hint. Only compatible RKNN YOLOv8 native-output
    // graphs set this; every other backend keeps the established box-major scan.
    bool prefer_yolov8_class_major_scan = false;

    // Optional producer/consumer capability for a backend that can decode native
    // YOLOv8 heads directly into candidates. The graph instance is exclusively
    // leased by InstancePool while Forward runs, so this sidecar has the same
    // lifetime and concurrency boundary as the graph's ordinary BlobStore.
    Yolov8DirectPostprocessConfig yolov8_direct_postprocess{};
    Yolov8CandidateBatch yolov8_candidate_batch{};
};

}  // namespace cosmo::nn
