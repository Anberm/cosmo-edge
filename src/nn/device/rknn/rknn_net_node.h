#pragma once

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <mutex>
#include <string>
#include <vector>

#include "nn/device/rknn/rknn_yolov8_adapter.h"
#include "nn/node/net_node.h"
#include "rknn_api.h"

namespace cosmo::nn {

bool IsRknnNativeInt8InputCompatible(const rknn_tensor_attr& attr, const BlobDesc& desc);
bool IsRknnNativeYolov8OutputCompatible(const std::vector<rknn_tensor_attr>& attrs,
                                        std::string* reason = nullptr);
bool IsRknnBoundInt8InputCompatible(const rknn_tensor_attr& attr, const BlobDesc& desc,
                                    std::string* reason = nullptr);
bool CopyRknnPackedInt8Input(const int8_t* source, size_t source_bytes, int8_t* destination,
                             size_t destination_bytes, int height, int width, int channels, int width_stride,
                             std::string* reason = nullptr);
bool RknnFastOutputEnabled();
bool RknnDirectCandidatesEnabled();
bool RknnBoundInputEnabled();

class RknnNetNode final : public NetNode {
public:
    RknnNetNode();
    ~RknnNetNode() override;

    RknnNetNode(const RknnNetNode&)            = delete;
    RknnNetNode& operator=(const RknnNetNode&) = delete;

    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    Status LoadWeight(const char* data, size_t size) override;
    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status QueryTensorAttributes();
    Status PrepareInput(const Blob& blob, std::vector<float>& nhwc, int& height, int& width) const;
    Status PrepareNativeCompatibilityInput(const Blob& blob, std::vector<float>& nhwc,
                                           int& height, int& width) const;
    std::vector<int> TensorShape(const rknn_tensor_attr& attr) const;
    size_t TensorElementCount(const rknn_tensor_attr& attr) const;
    bool TryBindInputMemory(const BlobDesc& desc, std::string& reason);
    void DestroyContext();

    rknn_context context_{0};
    rknn_input_output_num io_count_{};
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<rknn_output> runtime_outputs_;
    std::vector<RknnYolov8Head> float_yolov8_heads_;
    std::vector<RknnYolov8QuantizedHead> quantized_yolov8_heads_;
    RknnYolov8CandidateScratch yolov8_candidate_scratch_;
    RknnOutputAdapterContract output_adapter_contract_;
    rknn_tensor_attr bound_input_attr_{};
    rknn_tensor_mem* bound_input_memory_{nullptr};
    std::vector<unsigned char> model_data_;
    std::vector<float> input_nhwc_;
    bool yolov8_heads_{false};
    bool native_yolov8_outputs_{false};
    bool detector_model_{false};
    bool bound_input_eligible_{true};
    int yolov8_class_count_{0};
    int yolov8_point_count_{0};
    mutable std::mutex mutex_;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
