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

bool IsRknnBoundInt8InputCompatible(const rknn_tensor_attr& attr, const BlobDesc& desc, std::string* reason) {
    const auto reject = [&](const char* message) {
        if (reason)
            *reason = message;
        return false;
    };
    if (desc.data_type != DATA_TYPE_INT8 || desc.data_format != DATA_FORMAT_NHWC || desc.dims.size() != 4 ||
        desc.dims[0] != 1 || desc.dims[1] <= 0 || desc.dims[2] <= 0 || desc.dims[3] <= 0) {
        return reject("bound input requires packed batch-1 INT8 NHWC data");
    }
    if (attr.n_dims != 4 || attr.fmt != RKNN_TENSOR_NHWC || attr.type != RKNN_TENSOR_INT8)
        return reject("RKNN native input is not INT8 NHWC");
    if (attr.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC || attr.zp != -128 || !std::isfinite(attr.scale) ||
        std::fabs(attr.scale - 0.00392157f) > 1e-7f) {
        return reject("RKNN native input quantization does not match the host fast path");
    }
    if (attr.dims[0] != 1 || attr.dims[1] != static_cast<uint32_t>(desc.dims[1]) ||
        attr.dims[2] != static_cast<uint32_t>(desc.dims[2]) ||
        attr.dims[3] != static_cast<uint32_t>(desc.dims[3])) {
        return reject("RKNN native input dimensions do not match the graph blob");
    }
    const uint32_t width_stride = attr.w_stride == 0 ? attr.dims[2] : attr.w_stride;
    if (width_stride < attr.dims[2])
        return reject("RKNN native input width stride is smaller than the tensor width");
    const uint64_t required  = static_cast<uint64_t>(attr.dims[1]) * width_stride * attr.dims[3];
    const uint64_t available = attr.size_with_stride == 0 ? attr.size : attr.size_with_stride;
    if (required == 0 || available < required)
        return reject("RKNN native input stride allocation is too small");
    if (reason)
        reason->clear();
    return true;
}

bool CopyRknnPackedInt8Input(const int8_t* source, size_t source_bytes, int8_t* destination,
                             size_t destination_bytes, int height, int width, int channels, int width_stride,
                             std::string* reason) {
    const auto reject = [&](const char* message) {
        if (reason)
            *reason = message;
        return false;
    };
    if (!source || !destination || height <= 0 || width <= 0 || channels <= 0)
        return reject("bound input copy received an invalid packed tensor");
    const int effective_stride = width_stride == 0 ? width : width_stride;
    if (effective_stride < width)
        return reject("bound input copy width stride is too small");
    const uint64_t row_bytes            = static_cast<uint64_t>(width) * channels;
    const uint64_t source_required      = static_cast<uint64_t>(height) * row_bytes;
    const uint64_t destination_required = static_cast<uint64_t>(height) * effective_stride * channels;
    if (source_required > source_bytes || destination_required > destination_bytes)
        return reject("bound input copy buffer is smaller than its tensor contract");
    if (effective_stride == width) {
        std::memcpy(destination, source, static_cast<size_t>(source_required));
    } else {
        const size_t source_row_bytes = static_cast<size_t>(row_bytes);
        const size_t destination_row_bytes =
            static_cast<size_t>(effective_stride) * static_cast<size_t>(channels);
        for (int row = 0; row < height; ++row) {
            std::memcpy(destination + static_cast<size_t>(row) * destination_row_bytes,
                        source + static_cast<size_t>(row) * source_row_bytes, source_row_bytes);
        }
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

bool RknnBoundInputEnabled() {
    return EnvironmentFlag("COSMO_RKNN_BOUND_INPUT", true);
}

RknnNetNode::RknnNetNode() : NetNode() {
    name = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_NET).append("_0");
}

RknnNetNode::~RknnNetNode() {
    std::lock_guard<std::mutex> lock(mutex_);
    DestroyContext();
}

void RknnNetNode::DestroyContext() {
    if (bound_input_memory_) {
        if (context_ != 0)
            rknn_destroy_mem(context_, bound_input_memory_);
        bound_input_memory_ = nullptr;
    }
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
    std::vector<int>().swap(yolov8_candidate_scratch_.active_points);
    model_data_.clear();
    std::vector<float>().swap(input_nhwc_);
    io_count_                = {};
    output_adapter_contract_ = {};
    bound_input_attr_        = {};
    yolov8_heads_          = false;
    native_yolov8_outputs_ = false;
    detector_model_        = false;
    bound_input_eligible_    = true;
    yolov8_class_count_    = 0;
    yolov8_point_count_    = 0;
}

bool RknnNetNode::TryBindInputMemory(const BlobDesc& desc, std::string& reason) {
    rknn_tensor_attr attr{};
    attr.index = 0;
    int result = rknn_query(context_, RKNN_QUERY_NATIVE_INPUT_ATTR, &attr, sizeof(attr));
    if (result != RKNN_SUCC) {
        reason = "RKNN_QUERY_NATIVE_INPUT_ATTR failed with RKNN code " + std::to_string(result);
        return false;
    }
    if (!IsRknnBoundInt8InputCompatible(attr, desc, &reason))
        return false;

    const uint32_t bytes = attr.size_with_stride == 0 ? attr.size : attr.size_with_stride;
    auto* memory         = rknn_create_mem(context_, bytes);
    if (!memory || !memory->virt_addr || memory->size < bytes) {
        if (memory)
            rknn_destroy_mem(context_, memory);
        reason = "rknn_create_mem failed for the bound input";
        return false;
    }
    std::memset(memory->virt_addr, 0, memory->size);
    attr.pass_through = 1;
    result            = rknn_set_io_mem(context_, memory, &attr);
    if (result != RKNN_SUCC) {
        rknn_destroy_mem(context_, memory);
        reason = "rknn_set_io_mem failed with RKNN code " + std::to_string(result);
        return false;
    }
    bound_input_attr_   = attr;
    bound_input_memory_ = memory;
    reason.clear();
    return true;
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

    std::string adapter_error;
    if (!ResolveRknnOutputAdapter(output_shapes, output_adapter_contract_, adapter_error))
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, adapter_error);
    yolov8_heads_ = IsRknnYolov8DflAdapter(output_adapter_contract_.kind);
    if (yolov8_heads_) {
        yolov8_class_count_ = output_adapter_contract_.class_count;
        yolov8_point_count_ = output_adapter_contract_.point_count;
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

    LOG_INFO(
        "RKNN model loaded: api={} driver={} inputs={} runtime_outputs={} logical_outputs={} "
        "output_adapter={} native_int8_output={}",
        version.api_version, version.drv_version, io_count_.n_input, io_count_.n_output, logical_outputs,
        RknnOutputAdapterName(output_adapter_contract_.kind), native_yolov8_outputs_);
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
    bool use_bound_input = false;
    if (bound_input_memory_ && !native_int8) {
        return finish(
            Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN bound input cannot accept a non-native tensor"));
    }
    if (native_int8 && !bound_input_memory_ && bound_input_eligible_ && RknnBoundInputEnabled()) {
        std::string bind_reason;
        const bool bound = TryBindInputMemory(input_desc, bind_reason);
        GetInferencePipelineMetrics().RecordRknnBoundInputBind(bound);
        if (bound) {
            LOG_INFO("RKNN bound input enabled: bytes={} fd={} width_stride={}", bound_input_memory_->size,
                     bound_input_memory_->fd, bound_input_attr_.w_stride);
        } else {
            bound_input_eligible_ = false;
            LOG_WARN("RKNN bound input unavailable; retaining rknn_inputs_set path: {}", bind_reason);
        }
    }
    if (native_int8 && bound_input_memory_) {
        std::string copy_reason;
        const auto copy_started   = MetricsClock::now();
        const size_t source_bytes = BlobElementCount(input_desc);
        const bool copied         = CopyRknnPackedInt8Input(
            static_cast<const int8_t*>(input.buf), source_bytes,
            static_cast<int8_t*>(bound_input_memory_->virt_addr), bound_input_memory_->size, input_height,
            input_width, input_desc.dims[3], static_cast<int>(bound_input_attr_.w_stride), &copy_reason);
        GetInferencePipelineMetrics().RecordRknnBoundInputCopy(ElapsedNanoseconds(copy_started),
                                                               copied ? source_bytes : 0, copied);
        if (!copied)
            return finish(Status(COSMO_NN_ERR_INVALID_INPUT, copy_reason));
        const auto sync_started = MetricsClock::now();
        const int sync_result   = rknn_mem_sync(context_, bound_input_memory_, RKNN_MEMORY_SYNC_TO_DEVICE);
        GetInferencePipelineMetrics().RecordRknnBoundInputSync(ElapsedNanoseconds(sync_started),
                                                               sync_result == RKNN_SUCC);
        if (sync_result != RKNN_SUCC)
            return finish(RknnError("rknn_mem_sync", sync_result));
        use_bound_input = true;
    }
    int result = RKNN_SUCC;
    if (!use_bound_input) {
        bound_input_eligible_         = false;
        const auto inputs_set_started = MetricsClock::now();
        result                        = rknn_inputs_set(context_, 1, &input);
        GetInferencePipelineMetrics().RecordRknnInputsSet(ElapsedNanoseconds(inputs_set_started), scope);
        if (result != RKNN_SUCC)
            return finish(RknnError("rknn_inputs_set", result));
    }
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
                    decoded ? top_count * sizeof(float) : 0, timing.score_sum_points_rejected);
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
