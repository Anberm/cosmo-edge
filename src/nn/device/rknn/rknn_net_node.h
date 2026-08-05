#pragma once

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <mutex>
#include <string>
#include <vector>

#include "nn/node/net_node.h"
#include "rknn_api.h"

namespace cosmo::nn {

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
    std::vector<int> TensorShape(const rknn_tensor_attr& attr) const;
    size_t TensorElementCount(const rknn_tensor_attr& attr) const;
    void DestroyContext();

    rknn_context context_{0};
    rknn_input_output_num io_count_{};
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<unsigned char> model_data_;
    bool yolov8_heads_{false};
    int yolov8_class_count_{0};
    int yolov8_point_count_{0};
    mutable std::mutex mutex_;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
