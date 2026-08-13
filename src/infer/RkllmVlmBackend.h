#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "infer/AiCommon.h"
#include "media/VideoFrame.h"
#include "util/ErrorCode.h"

namespace cosmo {

struct Qwen3VLGenerationParam;
struct Qwen3VLResult;

class RkllmVlmBackend {
public:
    explicit RkllmVlmBackend(std::string model_path);
    ~RkllmVlmBackend();

    RkllmVlmBackend(const RkllmVlmBackend&)            = delete;
    RkllmVlmBackend& operator=(const RkllmVlmBackend&) = delete;

    util::ErrorEnum Init();
    util::ErrorEnum Generate(const std::vector<VideoFramePtr>& images,
                             const std::vector<std::string>& prompts, const Qwen3VLGenerationParam& gen_param,
                             std::vector<Qwen3VLResult>& results);

private:
    struct Impl;
    Impl* impl_{nullptr};
    std::string model_path_;
};

}  // namespace cosmo
