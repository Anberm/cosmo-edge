#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/rknn/rknn_net_node.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>

#include "nn/core/inference_pipeline_metrics.h"
#include "nn/device/rknn/rknn_yolov8_adapter.h"
#include "nn/node/node_type_utils.h"
#include "util/Log.h"

namespace cosmo::nn {
namespace {

    using MetricsClock = std::chrono::steady_clock;

    uint64_t ElapsedNanoseconds(MetricsClock::time_point started_at) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         MetricsClock::now() - started_at)
                                         .count());
    }

    Status RknnError(const std::string& operation, int code) {
        return Status(COSMO_NN_ERR_NET, operation + " failed with RKNN code " + std::to_string(code));
    }

    class OutputGuard {
    public:
        OutputGuard(rknn_context context, std::vector<rknn_output>& outputs)
            : context_(context), outputs_(outputs) {}
        ~OutputGuard() {
            if (active_)
                rknn_outputs_release(context_, static_cast<uint32_t>(outputs_.size()), outputs_.data());
        }
        void Activate() { active_ = true; }
        int Release() {
            if (!active_)
                return RKNN_SUCC;
            active_ = false;
            return rknn_outputs_release(context_, static_cast<uint32_t>(outputs_.size()),
                                        outputs_.data());
        }

    private:
        rknn_context context_;
        std::vector<rknn_output>& outputs_;
        bool active_{false};
    };

    size_t BlobElementCount(const BlobDesc& desc) {
        if (desc.dims.empty())
            return 0;
        size_t count = 1;
        for (int dim : desc.dims) {
            if (dim <= 0 || count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
                return 0;
            count *= static_cast<size_t>(dim);
        }
        return count;
    }

    size_t TensorAttrElementCount(const rknn_tensor_attr& attr) {
        if (attr.n_elems != 0)
            return attr.n_elems;
        if (attr.n_dims == 0)
            return 0;
        size_t count = 1;
        for (uint32_t index = 0; index < attr.n_dims; ++index) {
            if (attr.dims[index] == 0 ||
                count > std::numeric_limits<size_t>::max() /
                            static_cast<size_t>(attr.dims[index])) {
                return 0;
            }
            count *= static_cast<size_t>(attr.dims[index]);
        }
        return count;
    }

    std::vector<int> TensorAttrShape(const rknn_tensor_attr& attr) {
        std::vector<int> shape;
        shape.reserve(attr.n_dims);
        for (uint32_t index = 0; index < attr.n_dims; ++index) {
            if (attr.dims[index] > static_cast<uint32_t>(std::numeric_limits<int>::max()))
                return {};
            shape.push_back(static_cast<int>(attr.dims[index]));
        }
        return shape;
    }

    bool EnvironmentFlag(const char* name, bool default_value) {
        const char* raw = std::getenv(name);
        if (!raw || *raw == '\0')
            return default_value;
        std::string value(raw);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "0" || value == "false" || value == "off" || value == "no")
            return false;
        if (value == "1" || value == "true" || value == "on" || value == "yes")
            return true;
        return default_value;
    }

}  // namespace

bool IsRknnNativeInt8InputCompatible(const rknn_tensor_attr& attr, const BlobDesc& desc) {
    constexpr float kExpectedScale = 0.00392157f;
    return desc.data_type == DATA_TYPE_INT8 && desc.data_format == DATA_FORMAT_NHWC &&
           desc.dims.size() == 4 && desc.dims[0] == 1 && desc.dims[1] > 0 && desc.dims[2] > 0 &&
           desc.dims[3] == 3 && attr.n_dims == 4 && attr.fmt == RKNN_TENSOR_NHWC &&
           attr.type == RKNN_TENSOR_INT8 && attr.qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
           attr.zp == -128 && std::fabs(attr.scale - kExpectedScale) <= 1e-7f && attr.dims[0] == 1 &&
           attr.dims[1] == static_cast<uint32_t>(desc.dims[1]) &&
           attr.dims[2] == static_cast<uint32_t>(desc.dims[2]) && attr.dims[3] == 3;
}

bool IsRknnNativeYolov8OutputCompatible(const std::vector<rknn_tensor_attr>& attrs,
                                        std::string* reason) {
    const auto reject = [&](const char* message) {
        if (reason)
            *reason = message;
        return false;
    };
    std::vector<std::vector<int>> shapes;
    shapes.reserve(attrs.size());
    for (const auto& attr : attrs) {
        if (attr.type != RKNN_TENSOR_INT8)
            return reject(attr.type == RKNN_TENSOR_FLOAT16
                              ? "FP16 outputs retain the RKNN float compatibility path"
                              : "output type is not INT8");
        if (attr.fmt != RKNN_TENSOR_NCHW)
            return reject("INT8 output format is not NCHW");
        if (attr.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC)
            return reject("INT8 output quantization is not affine asymmetric");
        if (!std::isfinite(attr.scale) || !(attr.scale > 0.0f) || attr.zp < -128 || attr.zp > 127)
            return reject("INT8 output quantization parameters are invalid");
        const size_t element_count = TensorAttrElementCount(attr);
        if (element_count == 0 || attr.size != element_count)
            return reject("INT8 output byte count does not match its compact shape");
        auto shape = TensorAttrShape(attr);
        if (shape.empty())
            return reject("INT8 output shape cannot be represented");
        shapes.push_back(std::move(shape));
    }
    RknnYolov8Layout layout;
    std::string adapter_error;
    if (!DetectRknnYolov8Layout(shapes, layout, adapter_error)) {
        if (reason)
            *reason = adapter_error;
        return false;
    }
    if (reason)
        reason->clear();
    return true;
}

bool RknnFastOutputEnabled() {
    return EnvironmentFlag("COSMO_RKNN_FAST_OUTPUT", true);
}

bool RknnDirectCandidatesEnabled() {
    return EnvironmentFlag("COSMO_RKNN_DIRECT_CANDIDATES", true);
}

RknnNetNode::RknnNetNode() : NetNode() {
    name = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_NET).append("_0");
}

RknnNetNode::~RknnNetNode() {
    std::lock_guard<std::mutex> lock(mutex_);
    DestroyContext();
}

void RknnNetNode::DestroyContext() {
    if (context_ != 0) {
        rknn_destroy(context_);
        context_ = 0;
    }
    input_attrs_.clear();
    output_attrs_.clear();
    std::vector<rknn_output>().swap(runtime_outputs_);
    std::vector<RknnYolov8Head>().swap(float_yolov8_heads_);
    std::vector<RknnYolov8QuantizedHead>().swap(quantized_yolov8_heads_);
    std::vector<int8_t>().swap(yolov8_candidate_scratch_.class_max);
    std::vector<int>().swap(yolov8_candidate_scratch_.class_ids);
    model_data_.clear();
    std::vector<float>().swap(input_nhwc_);
    io_count_              = {};
    yolov8_heads_          = false;
    native_yolov8_outputs_ = false;
    detector_model_        = false;
    yolov8_class_count_    = 0;
    yolov8_point_count_    = 0;
}

size_t RknnNetNode::TensorElementCount(const rknn_tensor_attr& attr) const {
    if (attr.n_elems != 0)
        return attr.n_elems;
    size_t count = 1;
    for (uint32_t index = 0; index < attr.n_dims; ++index) {
        if (attr.dims[index] == 0 ||
            count > std::numeric_limits<size_t>::max() / static_cast<size_t>(attr.dims[index]))
            return 0;
        count *= static_cast<size_t>(attr.dims[index]);
    }
    return count;
}

std::vector<int> RknnNetNode::TensorShape(const rknn_tensor_attr& attr) const {
    std::vector<int> shape;
    shape.reserve(attr.n_dims);
    for (uint32_t index = 0; index < attr.n_dims; ++index) {
        if (attr.dims[index] > static_cast<uint32_t>(std::numeric_limits<int>::max()))
            return {};
        shape.push_back(static_cast<int>(attr.dims[index]));
    }
    return shape;
}

Status RknnNetNode::QueryTensorAttributes() {
    int result = rknn_query(context_, RKNN_QUERY_IN_OUT_NUM, &io_count_, sizeof(io_count_));
    if (result != RKNN_SUCC)
        return RknnError("RKNN_QUERY_IN_OUT_NUM", result);
    if (io_count_.n_input != 1 || io_count_.n_output == 0 || io_count_.n_output > 64)
        return Status(COSMO_NN_ERR_UNSUPPORT_NET,
                      "RKNN backend currently requires one input and 1-64 outputs");

    input_attrs_.resize(io_count_.n_input);
    for (uint32_t index = 0; index < io_count_.n_input; ++index) {
        input_attrs_[index]       = {};
        input_attrs_[index].index = index;
        result = rknn_query(context_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[index],
                            sizeof(input_attrs_[index]));
        if (result != RKNN_SUCC)
            return RknnError("RKNN_QUERY_INPUT_ATTR", result);
    }

    output_attrs_.resize(io_count_.n_output);
    std::vector<std::vector<int>> output_shapes;
    output_shapes.reserve(io_count_.n_output);
    for (uint32_t index = 0; index < io_count_.n_output; ++index) {
        output_attrs_[index]       = {};
        output_attrs_[index].index = index;
        result = rknn_query(context_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[index],
                            sizeof(output_attrs_[index]));
        if (result != RKNN_SUCC)
            return RknnError("RKNN_QUERY_OUTPUT_ATTR", result);
        auto shape = TensorShape(output_attrs_[index]);
        if (shape.empty() || TensorElementCount(output_attrs_[index]) == 0)
            return Status(COSMO_NN_ERR_UNSUPPORT_NET, "RKNN model contains an invalid output shape");
        output_shapes.push_back(std::move(shape));
    }
    runtime_outputs_.resize(output_attrs_.size());
    float_yolov8_heads_.reserve(output_attrs_.size());
    quantized_yolov8_heads_.reserve(output_attrs_.size());

    RknnYolov8Layout yolo_layout;
    std::string adapter_error;
    yolov8_heads_ = DetectRknnYolov8Layout(output_shapes, yolo_layout, adapter_error);
    if (yolov8_heads_) {
        yolov8_class_count_ = yolo_layout.class_count;
        yolov8_point_count_ = yolo_layout.point_count;
    }
    std::string native_output_reason;
    native_yolov8_outputs_ =
        yolov8_heads_ &&
        IsRknnNativeYolov8OutputCompatible(output_attrs_, &native_output_reason);
    if (yolov8_heads_ && !native_yolov8_outputs_) {
        LOG_INFO("RKNN YOLOv8 native INT8 output disabled: {}", native_output_reason);
    }
    const auto& input = input_attrs_.front();
    detector_model_ = yolov8_heads_ && input.n_dims == 4 && input.fmt == RKNN_TENSOR_NHWC &&
                      input.dims[0] == 1 && input.dims[1] == 640 && input.dims[2] == 640 &&
                      input.dims[3] == 3;
    return COSMO_NN_OK;
}

Status RknnNetNode::LoadWeight(const char* data, size_t size) {
    if (!data || size == 0)
        return Status(COSMO_NN_ERR_LOAD_MODEL, "RKNN model data is empty");
    if (size > std::numeric_limits<uint32_t>::max())
        return Status(COSMO_NN_ERR_LOAD_MODEL, "RKNN model exceeds the runtime size limit");

    std::lock_guard<std::mutex> lock(mutex_);
    DestroyContext();
    try {
        model_data_.assign(reinterpret_cast<const unsigned char*>(data),
                           reinterpret_cast<const unsigned char*>(data) + size);
    } catch (const std::bad_alloc&) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Not enough memory to retain RKNN model data");
    }

    int result = rknn_init(&context_, model_data_.data(), static_cast<uint32_t>(model_data_.size()), 0,
                           nullptr);
    if (result != RKNN_SUCC) {
        DestroyContext();
        return RknnError("rknn_init", result);
    }
    result = rknn_set_core_mask(context_, RKNN_NPU_CORE_AUTO);
    if (result != RKNN_SUCC) {
        DestroyContext();
        return RknnError("rknn_set_core_mask", result);
    }

    rknn_sdk_version version{};
    result = rknn_query(context_, RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    if (result != RKNN_SUCC) {
        DestroyContext();
        return RknnError("RKNN_QUERY_SDK_VERSION", result);
    }
    auto status = QueryTensorAttributes();
    if (!status) {
        DestroyContext();
        return status;
    }
    if (network_input_names.size() != io_count_.n_input) {
        DestroyContext();
        return Status(COSMO_NN_ERR_INVALID_CFG, "RKNN input count does not match config.json");
    }
    const size_t logical_outputs = yolov8_heads_ ? 1 : io_count_.n_output;
    if (network_output_names.size() != logical_outputs) {
        DestroyContext();
        return Status(COSMO_NN_ERR_INVALID_CFG, "RKNN logical output count does not match config.json");
    }

    LOG_INFO("RKNN model loaded: api={} driver={} inputs={} runtime_outputs={} logical_outputs={} "
             "native_int8_output={}",
             version.api_version, version.drv_version, io_count_.n_input, io_count_.n_output,
             logical_outputs, native_yolov8_outputs_);
    return COSMO_NN_OK;
}

DeviceType RknnNetNode::GetTopBlobDeviceType() {
    return DEVICE_NAIVE;
}

Status RknnNetNode::InferTopShapes() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_ == 0)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "RKNN context is not initialized");
    top_blob_shapes.clear();
    top_blob_data_types.clear();
    if (yolov8_heads_) {
        top_blob_shapes.push_back({1, 4 + yolov8_class_count_, yolov8_point_count_});
        top_blob_data_types.push_back(DATA_TYPE_FLOAT);
        return COSMO_NN_OK;
    }
    for (const auto& attr : output_attrs_) {
        auto shape = TensorShape(attr);
        if (shape.empty())
            return Status(COSMO_NN_ERR_UNSUPPORT_NET, "RKNN output shape cannot be represented");
        top_blob_shapes.push_back(std::move(shape));
        // rknn_outputs_get(want_float=1) always materializes float output.
        top_blob_data_types.push_back(DATA_TYPE_FLOAT);
    }
    return COSMO_NN_OK;
}

Status RknnNetNode::PrepareInput(const Blob& blob, std::vector<float>& nhwc, int& height,
                                 int& width) const {
    auto& mutable_blob = const_cast<Blob&>(blob);
    const auto desc    = mutable_blob.GetBlobDesc();
    const auto handle  = mutable_blob.GetHandle();
    if (!handle.base || desc.data_type != DATA_TYPE_FLOAT || desc.data_format != DATA_FORMAT_NCHW ||
        desc.dims.size() != 4 || desc.dims[0] != 1 || desc.dims[1] <= 0 || desc.dims[2] <= 0 ||
        desc.dims[3] <= 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "RKNN input must be a non-empty batch-1 NCHW float blob");
    }
    const int channels = desc.dims[1];
    height             = desc.dims[2];
    width              = desc.dims[3];
    if (channels != 3)
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, "RKNN CV backend currently requires three channels");
    if (BlobElementCount(desc) != TensorElementCount(input_attrs_[0]))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN input shape does not match the model");

    const auto& attr = input_attrs_[0];
    if (attr.n_dims != 4)
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, "RKNN CV backend requires a four-dimensional input");
    if (attr.fmt == RKNN_TENSOR_NHWC) {
        if (attr.dims[0] != 1 || attr.dims[1] != static_cast<uint32_t>(height) ||
            attr.dims[2] != static_cast<uint32_t>(width) || attr.dims[3] != 3)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN NHWC model dimensions do not match the graph");
    } else if (attr.fmt == RKNN_TENSOR_NCHW) {
        if (attr.dims[0] != 1 || attr.dims[1] != 3 ||
            attr.dims[2] != static_cast<uint32_t>(height) ||
            attr.dims[3] != static_cast<uint32_t>(width))
            return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN NCHW model dimensions do not match the graph");
    } else {
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, "RKNN input format must be NCHW or NHWC");
    }

    const size_t plane = static_cast<size_t>(height) * static_cast<size_t>(width);
    try {
        nhwc.resize(plane * static_cast<size_t>(channels));
    } catch (const std::bad_alloc&) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Not enough memory for RKNN NCHW-to-NHWC copy");
    }
    const auto* source = static_cast<const float*>(handle.base);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * static_cast<size_t>(width) + x;
            for (int channel = 0; channel < channels; ++channel)
                nhwc[pixel * static_cast<size_t>(channels) + channel] =
                    source[static_cast<size_t>(channel) * plane + pixel];
        }
    }
    return COSMO_NN_OK;
}

Status RknnNetNode::PrepareNativeCompatibilityInput(const Blob& blob, std::vector<float>& nhwc,
                                                    int& height, int& width) const {
    auto& mutable_blob = const_cast<Blob&>(blob);
    const auto desc    = mutable_blob.GetBlobDesc();
    const auto handle  = mutable_blob.GetHandle();
    if (!handle.base || desc.data_type != DATA_TYPE_INT8 || desc.data_format != DATA_FORMAT_NHWC ||
        desc.dims.size() != 4 || desc.dims[0] != 1 || desc.dims[1] <= 0 || desc.dims[2] <= 0 ||
        desc.dims[3] != 3 || BlobElementCount(desc) != TensorElementCount(input_attrs_[0])) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "RKNN native compatibility input must be batch-1 NHWC int8");
    }
    height = desc.dims[1];
    width  = desc.dims[2];
    const auto count = BlobElementCount(desc);
    try {
        nhwc.resize(count);
    } catch (const std::bad_alloc&) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY,
                      "Not enough memory for RKNN native-input compatibility fallback");
    }
    const auto* source = static_cast<const int8_t*>(handle.base);
    constexpr float kNormalizeScale = 0.00392157f;
    for (size_t index = 0; index < count; ++index)
        nhwc[index] = static_cast<float>(static_cast<int>(source[index]) + 128) * kNormalizeScale;
    return COSMO_NN_OK;
}

Status RknnNetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                            std::vector<std::shared_ptr<Blob>>& top_blobs) {
    const auto mutex_wait_started = MetricsClock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    const auto scope = detector_model_ ? RknnModelScope::Detector : RknnModelScope::Other;
    GetInferencePipelineMetrics().RecordRknnMutexWait(ElapsedNanoseconds(mutex_wait_started), scope);
    if (context_ == 0)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "RKNN context is not initialized");
    if (bottom_blobs.size() != 1 || !bottom_blobs[0])
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN backend requires exactly one input blob");
    const size_t logical_outputs = yolov8_heads_ ? 1 : output_attrs_.size();
    if (top_blobs.size() != logical_outputs)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN output blob count mismatch");
    if (yolov8_heads_ && shared_resource)
        shared_resource->yolov8_candidate_batch.Reset();

    timer.Start();
    const auto forward_started = MetricsClock::now();
    const auto finish = [&](Status status) -> Status {
        timer.Stop();
        GetInferencePipelineMetrics().RecordRknnForward(ElapsedNanoseconds(forward_started),
                                                        bool(status), scope);
        return status;
    };
    int input_height = 0, input_width = 0;
    bool native_int8 = false;
    bool compatibility_fallback = false;
    rknn_input input{};
    input.index = 0;
    const auto prepare_started = MetricsClock::now();
    const auto input_desc = bottom_blobs[0]->GetBlobDesc();
    Status prepare_status;
    if (input_desc.data_type == DATA_TYPE_INT8 && input_desc.data_format == DATA_FORMAT_NHWC) {
        native_int8 = IsRknnNativeInt8InputCompatible(input_attrs_[0], input_desc);
        if (native_int8) {
            const auto count = BlobElementCount(input_desc);
            if (!bottom_blobs[0]->GetHandle().base || count == 0 ||
                count > std::numeric_limits<uint32_t>::max()) {
                prepare_status =
                    Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN native input exceeds runtime size limit");
            } else {
                input.buf          = bottom_blobs[0]->GetHandle().base;
                input.size         = static_cast<uint32_t>(count);
                input.pass_through = 1;
                input.type         = RKNN_TENSOR_INT8;
                input.fmt          = RKNN_TENSOR_NHWC;
                input_height       = input_desc.dims[1];
                input_width        = input_desc.dims[2];
                prepare_status     = COSMO_NN_OK;
            }
        } else {
            compatibility_fallback = true;
            prepare_status = PrepareNativeCompatibilityInput(*bottom_blobs[0], input_nhwc_,
                                                              input_height, input_width);
        }
    } else {
        prepare_status = PrepareInput(*bottom_blobs[0], input_nhwc_, input_height, input_width);
    }
    GetInferencePipelineMetrics().RecordRknnPrepare(ElapsedNanoseconds(prepare_started), scope);
    if (!prepare_status)
        return finish(prepare_status);
    if (!native_int8) {
        if (input_nhwc_.size() > std::numeric_limits<uint32_t>::max() / sizeof(float))
            return finish(
                Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN input exceeds the runtime size limit"));
        input.buf          = input_nhwc_.data();
        input.size         = static_cast<uint32_t>(input_nhwc_.size() * sizeof(float));
        input.pass_through = 0;
        input.type         = RKNN_TENSOR_FLOAT32;
        // Runtime 2.3.2 silently rejects source NCHW on this model path while
        // reporting success. The graph boundary copy above makes NHWC explicit.
        input.fmt = RKNN_TENSOR_NHWC;
    }
    GetInferencePipelineMetrics().RecordRknnInputFormat(native_int8, compatibility_fallback);
    const auto inputs_set_started = MetricsClock::now();
    int result = rknn_inputs_set(context_, 1, &input);
    GetInferencePipelineMetrics().RecordRknnInputsSet(ElapsedNanoseconds(inputs_set_started), scope);
    if (result != RKNN_SUCC)
        return finish(RknnError("rknn_inputs_set", result));
    const auto run_started = MetricsClock::now();
    result = rknn_run(context_, nullptr);
    GetInferencePipelineMetrics().RecordRknnRun(ElapsedNanoseconds(run_started), scope);
    if (result != RKNN_SUCC)
        return finish(RknnError("rknn_run", result));

    const bool native_yolov8_output = native_yolov8_outputs_ && RknnFastOutputEnabled();
    const bool direct_yolov8_candidates =
        native_yolov8_output && RknnDirectCandidatesEnabled() && shared_resource &&
        shared_resource->yolov8_direct_postprocess.configured &&
        shared_resource->yolov8_direct_postprocess.input_width == input_width &&
        shared_resource->yolov8_direct_postprocess.input_height == input_height;
    if (shared_resource)
        shared_resource->prefer_yolov8_class_major_scan = native_yolov8_output && !direct_yolov8_candidates;
    auto& outputs = runtime_outputs_;
    std::fill(outputs.begin(), outputs.end(), rknn_output{});
    for (uint32_t index = 0; index < outputs.size(); ++index) {
        outputs[index].index       = index;
        outputs[index].want_float  = native_yolov8_output ? 0 : 1;
        outputs[index].is_prealloc = 0;
    }
    OutputGuard output_guard(context_, outputs);
    const auto outputs_get_started = MetricsClock::now();
    result = rknn_outputs_get(context_, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr);
    GetInferencePipelineMetrics().RecordRknnOutputsGet(ElapsedNanoseconds(outputs_get_started), scope);
    if (result != RKNN_SUCC)
        return finish(RknnError("rknn_outputs_get", result));
    output_guard.Activate();

    uint64_t output_bytes = 0;
    for (const auto& output : outputs)
        output_bytes += output.size;
    GetInferencePipelineMetrics().RecordRknnOutputFormat(
        native_yolov8_output, output_bytes, yolov8_heads_ && !native_yolov8_outputs_);

    const auto output_transform_started = MetricsClock::now();
    const auto release_outputs = [&]() {
        const auto release_started = MetricsClock::now();
        const int release_result = output_guard.Release();
        GetInferencePipelineMetrics().RecordRknnOutputsRelease(
            ElapsedNanoseconds(release_started), scope);
        return release_result;
    };
    const auto finish_output_error = [&](Status status) -> Status {
        GetInferencePipelineMetrics().RecordRknnOutputTransform(
            ElapsedNanoseconds(output_transform_started), scope);
        release_outputs();
        return finish(status);
    };
    if (yolov8_heads_) {
        auto top_desc = top_blobs[0]->GetBlobDesc();
        const size_t top_count = BlobElementCount(top_desc);
        if (!top_blobs[0]->GetHandle().base || top_desc.data_type != DATA_TYPE_FLOAT)
            return finish_output_error(
                Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN YOLOv8 top blob is invalid"));
        std::string adapter_error;
        if (native_yolov8_output) {
            auto& heads = quantized_yolov8_heads_;
            heads.clear();
            for (size_t index = 0; index < outputs.size(); ++index) {
                if (!outputs[index].buf || outputs[index].size != output_attrs_[index].size) {
                    return finish_output_error(Status(
                        COSMO_NN_ERR_NET, "RKNN returned an invalid native YOLOv8 output buffer"));
                }
                heads.push_back({static_cast<const int8_t*>(outputs[index].buf),
                                 outputs[index].size, TensorShape(output_attrs_[index]),
                                 output_attrs_[index].zp, output_attrs_[index].scale});
            }
            if (direct_yolov8_candidates) {
                auto& candidate_batch = shared_resource->yolov8_candidate_batch;
                const auto& config    = shared_resource->yolov8_direct_postprocess;
                RknnYolov8CandidateTiming timing;
                const bool decoded = DecodeRknnYolov8QuantizedCandidates(
                    heads, input_height, input_width, config.confidence_threshold, yolov8_candidate_scratch_,
                    candidate_batch.candidates, adapter_error, &timing);
                GetInferencePipelineMetrics().RecordRknnYolov8Transform(timing.dfl_nanoseconds,
                                                                        timing.class_nanoseconds);
                GetInferencePipelineMetrics().RecordRknnYolov8DirectCandidates(
                    decoded, timing.points_scanned, timing.points_decoded,
                    decoded ? top_count * sizeof(float) : 0);
                if (!decoded)
                    return finish_output_error(Status(COSMO_NN_ERR_NET, adapter_error));
                candidate_batch.ready = true;
            } else {
                RknnYolov8TransformTiming timing;
                const bool reconstructed = ReconstructRknnYolov8Quantized(
                    heads, input_height, input_width, static_cast<float*>(top_blobs[0]->GetHandle().base),
                    top_count, adapter_error, &timing);
                GetInferencePipelineMetrics().RecordRknnYolov8Transform(timing.dfl_nanoseconds,
                                                                        timing.class_nanoseconds);
                if (!reconstructed)
                    return finish_output_error(Status(COSMO_NN_ERR_NET, adapter_error));
            }
        } else {
            auto& heads = float_yolov8_heads_;
            heads.clear();
            for (size_t index = 0; index < outputs.size(); ++index) {
                if (!outputs[index].buf || outputs[index].size % sizeof(float) != 0) {
                    return finish_output_error(Status(
                        COSMO_NN_ERR_NET, "RKNN returned an invalid YOLOv8 output buffer"));
                }
                heads.push_back({static_cast<const float*>(outputs[index].buf),
                                 outputs[index].size / sizeof(float),
                                 TensorShape(output_attrs_[index])});
            }
            if (!ReconstructRknnYolov8(
                    heads, input_height, input_width,
                    static_cast<float*>(top_blobs[0]->GetHandle().base), top_count,
                    adapter_error)) {
                return finish_output_error(Status(COSMO_NN_ERR_NET, adapter_error));
            }
        }
    } else {
        for (size_t index = 0; index < outputs.size(); ++index) {
            auto desc = top_blobs[index]->GetBlobDesc();
            const size_t count = BlobElementCount(desc);
            if (!top_blobs[index]->GetHandle().base || desc.data_type != DATA_TYPE_FLOAT ||
                !outputs[index].buf || outputs[index].size != count * sizeof(float)) {
                return finish_output_error(
                    Status(COSMO_NN_ERR_NET, "RKNN output size does not match the graph blob"));
            }
            std::memcpy(top_blobs[index]->GetHandle().base, outputs[index].buf, outputs[index].size);
        }
    }
    GetInferencePipelineMetrics().RecordRknnOutputTransform(
        ElapsedNanoseconds(output_transform_started), scope);
    result = release_outputs();
    if (result != RKNN_SUCC)
        return finish(RknnError("rknn_outputs_release", result));
    return finish(COSMO_NN_OK);
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
