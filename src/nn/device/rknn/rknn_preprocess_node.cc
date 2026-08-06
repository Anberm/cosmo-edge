#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include "nn/device/rknn/rknn_preprocess_node.h"

#include <rga/im2d.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>

#include "nn/core/inference_pipeline_metrics.h"
#include "nn/device/rknn/rknn_net_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/op.h"
#include "util/Log.h"

namespace cosmo::nn {
namespace {

using MetricsClock = std::chrono::steady_clock;

constexpr int kDetectorInputSize = 640;
constexpr float kNormalizeScale  = 0.00392157f;

uint64_t ElapsedNanoseconds(MetricsClock::time_point started_at) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     MetricsClock::now() - started_at)
                                     .count());
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

bool RgaSucceeded(IM_STATUS status) {
    return status == IM_STATUS_SUCCESS || status == IM_STATUS_NOERROR;
}

class ScopedRgaHandle {
public:
    ScopedRgaHandle() = default;

    ScopedRgaHandle(void* data, size_t size) {
        ImportVirtual(data, size);
    }

    ~ScopedRgaHandle() {
        if (handle_ != 0)
            releasebuffer_handle(handle_);
    }

    [[nodiscard]] rga_buffer_handle_t Get() const { return handle_; }

    void ImportVirtual(void* data, size_t size) {
        if (handle_ != 0 || !data || size == 0 ||
            size > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return;
        }
        handle_ = importbuffer_virtualaddr(data, static_cast<int>(size));
    }

private:
    rga_buffer_handle_t handle_{0};
};

void LogRgaFallbackOnce(IM_STATUS status) {
    static std::atomic_flag logged = ATOMIC_FLAG_INIT;
    if (!logged.test_and_set(std::memory_order_relaxed)) {
        LOG_WARN("RKNN detector RGA preprocessing failed with status {} ({}); using CPU fallback",
                 status, imStrError_t(status));
    }
}

void LogRgaBoundInputFallbackOnce(const std::string& reason) {
    static std::atomic_flag logged = ATOMIC_FLAG_INIT;
    if (!logged.test_and_set(std::memory_order_relaxed)) {
        LOG_WARN("RKNN RGA bound input unavailable; retaining the host preprocessing path: {}", reason);
    }
}

void BilinearResizePacked(const uint8_t* source, int source_width, int source_height,
                          int channels, uint8_t* destination, int destination_width,
                          int destination_height, bool swap_red_blue) {
    const float x_ratio = static_cast<float>(source_width) / destination_width;
    const float y_ratio = static_cast<float>(source_height) / destination_height;
    for (int destination_y = 0; destination_y < destination_height; ++destination_y) {
        const float source_y = (destination_y + 0.5f) * y_ratio - 0.5f;
        int y0               = static_cast<int>(source_y);
        const float y_part   = source_y - y0;
        y0                   = std::max(0, std::min(y0, source_height - 1));
        const int y1         = std::min(y0 + 1, source_height - 1);
        for (int destination_x = 0; destination_x < destination_width; ++destination_x) {
            const float source_x = (destination_x + 0.5f) * x_ratio - 0.5f;
            int x0               = static_cast<int>(source_x);
            const float x_part   = source_x - x0;
            x0                   = std::max(0, std::min(x0, source_width - 1));
            const int x1         = std::min(x0 + 1, source_width - 1);
            for (int channel = 0; channel < channels; ++channel) {
                const int source_channel = swap_red_blue && channel != 1 ? 2 - channel : channel;
                const float v00 = source[(y0 * source_width + x0) * channels + source_channel];
                const float v01 = source[(y0 * source_width + x1) * channels + source_channel];
                const float v10 = source[(y1 * source_width + x0) * channels + source_channel];
                const float v11 = source[(y1 * source_width + x1) * channels + source_channel];
                const float value = v00 * (1 - x_part) * (1 - y_part) +
                                    v01 * x_part * (1 - y_part) +
                                    v10 * (1 - x_part) * y_part + v11 * x_part * y_part;
                destination[(destination_y * destination_width + destination_x) * channels +
                            channel] = static_cast<uint8_t>(
                    std::min(255.0f, std::max(0.0f, value + 0.5f)));
            }
        }
    }
}

size_t PackedByteCount(int width, int height) {
    if (width <= 0 || height <= 0)
        return 0;
    const auto pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels > std::numeric_limits<size_t>::max() / 3)
        return 0;
    return pixels * 3;
}

}  // namespace

bool RknnFastPreprocessEnabled() {
    return EnvironmentFlag("COSMO_RKNN_FAST_PREPROCESS", true);
}

bool RknnForceRgaFailure() {
    return EnvironmentFlag("COSMO_RKNN_RGA_FORCE_FAIL", false);
}

bool IsRknnDetectorResizeContract(int out_height, int out_width, int gravity,
                                  const std::vector<int>& padding_color) {
    return out_height == kDetectorInputSize && out_width == kDetectorInputSize && gravity == 1 &&
           padding_color.size() >= 3 && padding_color[0] == 114 && padding_color[1] == 114 &&
           padding_color[2] == 114;
}

bool IsRknnNativeNormalizeContract(const std::vector<float>& mean,
                                   const std::vector<float>& std_dev, float scale,
                                   const DimsVector& input_dims) {
    if (input_dims.size() != 4 || input_dims[0] <= 0 || input_dims[1] != kDetectorInputSize ||
        input_dims[2] != kDetectorInputSize || input_dims[3] != 3 || mean.size() < 3 ||
        !std_dev.empty() || std::fabs(scale - kNormalizeScale) > 1e-8f) {
        return false;
    }
    return std::all_of(mean.begin(), mean.begin() + 3,
                       [](float value) { return std::fabs(value) <= 1e-8f; });
}

void MapPackedU8ToNativeInt8(const uint8_t* source, int8_t* destination, size_t pixels,
                             bool swap_red_blue) {
    if (!source || !destination)
        return;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto source_offset      = pixel * 3;
        const auto destination_offset = pixel * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            const size_t source_channel = swap_red_blue && channel != 1 ? 2 - channel : channel;
            destination[destination_offset + channel] = static_cast<int8_t>(
                static_cast<int>(source[source_offset + source_channel]) - 128);
        }
    }
}

RknnResizeNode::RknnResizeNode() : Node() {
    node_type     = NodeType::NODE_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_RESIZE).append("_0");
    one_blob_only = true;
}

RknnResizeNode::~RknnResizeNode() {
    ReleaseRgaBoundTarget();
}

void RknnResizeNode::LoadParam(Op* op) {
    const auto* resize = dynamic_cast<Resize*>(op);
    if (!resize)
        return;
    if (resize->dsize.size() >= 2) {
        out_height_ = resize->dsize[0];
        out_width_  = resize->dsize[1];
    }
    gravity_          = resize->gravity;
    padding_color_    = resize->color;
    detector_contract_ =
        IsRknnDetectorResizeContract(out_height_, out_width_, gravity_, padding_color_);
}

DeviceType RknnResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status RknnResizeNode::InferTopShapes() {
    if (out_height_ <= 0 || out_width_ <= 0)
        return Status(COSMO_NN_ERR_INVALID_CFG, "RKNN resize output dimensions must be positive");
    shared_resource->net_input_w = out_width_;
    shared_resource->net_input_h = out_height_;
    top_blob_shapes     = {{1, out_height_, out_width_, 3}};
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return COSMO_NN_OK;
}

size_t RknnResizeNode::GetBottomCount() {
    return 1;
}

size_t RknnResizeNode::GetTopCount() {
    return 1;
}

void RknnResizeNode::ReleaseRgaBoundTarget() {
    if (rga_bound_target_handle_ != 0) {
        releasebuffer_handle(static_cast<rga_buffer_handle_t>(rga_bound_target_handle_));
        rga_bound_target_handle_     = 0;
        rga_bound_target_generation_ = 0;
    }
}

bool RknnResizeNode::AcquireRgaBoundTarget(uint32_t& handle) {
    handle = 0;
    if (!detector_contract_ || rga_bound_target_unavailable_ || !RknnRgaBoundInputEnabled() ||
        !shared_resource || !shared_resource->rknn_bound_input_provider) {
        return false;
    }
    std::string reason;
    auto* provider = shared_resource->rknn_bound_input_provider;
    if (!provider->EnsureRgaBoundInput(out_height_, out_width_, reason)) {
        rga_bound_target_unavailable_ = true;
        LogRgaBoundInputFallbackOnce(reason);
        return false;
    }
    const auto& target = shared_resource->rknn_bound_input_target;
    if (target.owner != provider || !target.Matches(out_height_, out_width_) ||
        target.bytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
        rga_bound_target_unavailable_ = true;
        LogRgaBoundInputFallbackOnce("provider returned an incompatible target");
        return false;
    }
    if (rga_bound_target_handle_ != 0 && rga_bound_target_generation_ == target.generation) {
        handle = rga_bound_target_handle_;
        return true;
    }
    ReleaseRgaBoundTarget();
    const auto import_started = MetricsClock::now();
    const auto imported       = importbuffer_fd(target.fd, static_cast<int>(target.bytes));
    GetInferencePipelineMetrics().RecordRknnRgaBoundInputImport(ElapsedNanoseconds(import_started),
                                                                imported != 0);
    if (imported == 0) {
        rga_bound_target_unavailable_ = true;
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        LogRgaBoundInputFallbackOnce("RGA could not import the RKNN DMA-BUF fd");
        return false;
    }
    rga_bound_target_handle_     = static_cast<uint32_t>(imported);
    rga_bound_target_generation_ = target.generation;
    handle                       = rga_bound_target_handle_;
    return true;
}

bool RknnResizeNode::ResizeWithRga(const Blob& bottom, Blob& top, bool allow_bound_target) {
    if (!detector_contract_ || RknnForceRgaFailure()) {
        if (detector_contract_ && RknnForceRgaFailure())
            GetInferencePipelineMetrics().RecordRknnRgaFailure();
        return false;
    }

    auto& mutable_bottom = const_cast<Blob&>(bottom);
    auto bottom_desc     = mutable_bottom.GetBlobDesc();
    auto bottom_handle   = mutable_bottom.GetHandle();
    auto top_handle      = top.GetHandle();
    if (!bottom_handle.base || !top_handle.base || bottom_desc.dims.size() != 4 ||
        bottom_desc.dims[0] != 1 || bottom_desc.dims[3] != 3 ||
        (bottom_desc.image_format != IMAGE_BGR && bottom_desc.image_format != IMAGE_RGB)) {
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        return false;
    }

    const int source_height = bottom_desc.dims[1];
    const int source_width  = bottom_desc.dims[2];
    const auto source_size  = PackedByteCount(source_width, source_height);
    const auto target_size  = PackedByteCount(out_width_, out_height_);
    ScopedRgaHandle source_handle(bottom_handle.base, source_size);
    ScopedRgaHandle host_target_handle;
    uint32_t target_handle  = 0;
    const bool bound_target = allow_bound_target && AcquireRgaBoundTarget(target_handle);
    if (!bound_target) {
        host_target_handle.ImportVirtual(top_handle.base, target_size);
        target_handle = host_target_handle.Get();
    }
    if (source_handle.Get() == 0 || target_handle == 0) {
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        LogRgaFallbackOnce(IM_STATUS_OUT_OF_MEMORY);
        return false;
    }

    const int source_format =
        bottom_desc.image_format == IMAGE_RGB ? RK_FORMAT_RGB_888 : RK_FORMAT_BGR_888;
    auto source = wrapbuffer_handle_t(source_handle.Get(), source_width, source_height,
                                      source_width, source_height, source_format);
    int target_width_stride = out_width_;
    if (bound_target)
        target_width_stride = shared_resource->rknn_bound_input_target.width_stride;
    auto target = wrapbuffer_handle_t(static_cast<rga_buffer_handle_t>(target_handle), out_width_,
                                      out_height_, target_width_stride, out_height_, RK_FORMAT_RGB_888);

    const auto fill_started = MetricsClock::now();
    const im_rect full_target{0, 0, out_width_, out_height_};
    const int fill_color = (padding_color_[0] << 16) | (padding_color_[1] << 8) |
                           padding_color_[2];
    auto status = imfill_t(target, full_target, fill_color, 1);
    GetInferencePipelineMetrics().RecordRknnRgaFill(ElapsedNanoseconds(fill_started));
    if (!RgaSucceeded(status)) {
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        LogRgaFallbackOnce(status);
        return false;
    }

    const float scale = std::min(static_cast<float>(out_width_) / source_width,
                                 static_cast<float>(out_height_) / source_height);
    const int resized_width  = static_cast<int>(source_width * scale);
    const int resized_height = static_cast<int>(source_height * scale);
    const int offset_x       = (out_width_ - resized_width) / 2;
    const int offset_y       = (out_height_ - resized_height) / 2;
    const im_rect source_rect{0, 0, source_width, source_height};
    const im_rect target_rect{offset_x, offset_y, resized_width, resized_height};
    const im_rect empty_rect{};
    const rga_buffer_t empty_buffer{};
    const auto resize_started = MetricsClock::now();
    status = improcess(source, target, empty_buffer, source_rect, target_rect, empty_rect, IM_SYNC);
    GetInferencePipelineMetrics().RecordRknnRgaResizeColor(
        ElapsedNanoseconds(resize_started));
    if (!RgaSucceeded(status)) {
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        LogRgaFallbackOnce(status);
        return false;
    }
    if (bound_target) {
        auto& bound_input = shared_resource->rknn_bound_input_target;
        if (bound_input.owner == shared_resource->rknn_bound_input_provider &&
            bound_input.generation == rga_bound_target_generation_) {
            bound_input.frame_ready = true;
        }
    }
    return true;
}

void RknnResizeNode::ResizeWithCpu(const Blob& bottom, Blob& top, bool output_rgb) const {
    auto& mutable_bottom = const_cast<Blob&>(bottom);
    auto bottom_desc     = mutable_bottom.GetBlobDesc();
    const int source_height = bottom_desc.dims[1];
    const int source_width  = bottom_desc.dims[2];
    const int channels      = bottom_desc.dims[3];
    const auto* source      = static_cast<const uint8_t*>(mutable_bottom.GetHandle().base);
    auto* destination       = static_cast<uint8_t*>(top.GetHandle().base);
    const bool swap_red_blue = output_rgb && bottom_desc.image_format != IMAGE_RGB;

    if (gravity_ == 0) {
        BilinearResizePacked(source, source_width, source_height, channels, destination,
                             out_width_, out_height_, swap_red_blue);
        return;
    }

    const float scale = std::min(static_cast<float>(out_width_) / source_width,
                                 static_cast<float>(out_height_) / source_height);
    const int resized_width  = static_cast<int>(source_width * scale);
    const int resized_height = static_cast<int>(source_height * scale);
    const uint8_t padding = static_cast<uint8_t>(padding_color_.empty() ? 114 : padding_color_[0]);
    std::memset(destination, padding, PackedByteCount(out_width_, out_height_));
    std::vector<uint8_t> resized(PackedByteCount(resized_width, resized_height));
    BilinearResizePacked(source, source_width, source_height, channels, resized.data(),
                         resized_width, resized_height, swap_red_blue);
    int offset_x = 0;
    int offset_y = 0;
    if (gravity_ == 1) {
        offset_x = (out_width_ - resized_width) / 2;
        offset_y = (out_height_ - resized_height) / 2;
    }
    for (int row = 0; row < resized_height; ++row) {
        std::memcpy(destination + ((offset_y + row) * out_width_ + offset_x) * channels,
                    resized.data() + row * resized_width * channels,
                    static_cast<size_t>(resized_width) * channels);
    }
}

Status RknnResizeNode::ResizeSingle(const std::shared_ptr<Blob>& bottom, const std::shared_ptr<Blob>& top,
                                    bool allow_bound_target) {
    if (!bottom || !top || !bottom->GetHandle().base || !top->GetHandle().base)
        return Status(COSMO_NN_ERR_NULL_PARAM, "RKNN resize input or output is null");
    const auto bottom_desc = bottom->GetBlobDesc();
    if (bottom_desc.data_type != DATA_TYPE_UINT8 || bottom_desc.data_format != DATA_FORMAT_NHWC ||
        bottom_desc.dims.size() != 4 || bottom_desc.dims[0] != 1 || bottom_desc.dims[1] <= 0 ||
        bottom_desc.dims[2] <= 0 || bottom_desc.dims[3] != 3) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "RKNN detector resize requires packed batch-1 uint8 input");
    }

    bool rga_success = false;
    if (detector_contract_)
        rga_success = ResizeWithRga(*bottom, *top, allow_bound_target);
    if (!rga_success) {
        const auto cpu_started = MetricsClock::now();
        try {
            ResizeWithCpu(*bottom, *top, detector_contract_);
        } catch (const std::bad_alloc&) {
            return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "RKNN CPU resize fallback allocation failed");
        }
        if (detector_contract_)
            GetInferencePipelineMetrics().RecordRknnCpuResizeFallback(
                ElapsedNanoseconds(cpu_started));
    }

    auto top_desc         = top->GetBlobDesc();
    top_desc.data_format  = DATA_FORMAT_NHWC;
    top_desc.image_format = detector_contract_ ? IMAGE_RGB : bottom_desc.image_format;
    top->SetBlobDesc(top_desc);
    return COSMO_NN_OK;
}

Status RknnResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                               std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    if (top_blobs.size() != 1 || !top_blobs[0])
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN resize requires exactly one output");
    const int batch = static_cast<int>(bottom_blobs.size());
    if (batch <= 0 || batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN resize batch size is invalid");

    auto top_desc    = top_blobs[0]->GetBlobDesc();
    top_desc.dims[0] = batch;
    top_blobs[0]->SetBlobDesc(top_desc);
    const size_t slice_size = PackedByteCount(out_width_, out_height_);
    if (shared_resource &&
        shared_resource->rknn_bound_input_target.owner == shared_resource->rknn_bound_input_provider) {
        shared_resource->rknn_bound_input_target.frame_ready = false;
    }
    for (int index = 0; index < batch; ++index) {
        BlobDesc slice_desc = top_desc;
        slice_desc.dims[0]  = 1;
        BlobHandle slice_handle;
        slice_handle.base = static_cast<uint8_t*>(top_blobs[0]->GetHandle().base) +
                            static_cast<size_t>(index) * slice_size;
        auto slice = std::make_shared<Blob>(slice_desc, slice_handle);
        RETURN_ON_FAIL(ResizeSingle(bottom_blobs[index], slice, batch == 1));
        top_desc.image_format = slice->GetBlobDesc().image_format;
        top_desc.data_format  = slice->GetBlobDesc().data_format;
    }
    top_blobs[0]->SetBlobDesc(top_desc);
    timer.Stop();
    return COSMO_NN_OK;
}

RknnNormalizeNode::RknnNormalizeNode() : Node() {
    node_type     = NodeType::NODE_NORMALIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_NORMALIZE).append("_0");
    one_blob_only = true;
}

void RknnNormalizeNode::LoadParam(Op* op) {
    const auto* normalize = dynamic_cast<Normalize*>(op);
    if (!normalize)
        return;
    mean_          = normalize->mean;
    std_dev_       = normalize->std;
    uniform_scale_ = normalize->scale;
    is_bgr_        = normalize->is_bgr;
    if (std_dev_.empty()) {
        scale_.assign(mean_.size(), uniform_scale_);
    } else {
        scale_.resize(std_dev_.size());
        std::transform(std_dev_.begin(), std_dev_.end(), scale_.begin(),
                       [](float value) { return 1.0f / value; });
    }
}

DeviceType RknnNormalizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

bool RknnNormalizeNode::NeedBottomShapesInfered() {
    return true;
}

Status RknnNormalizeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                                    std::vector<DataType> types) {
    if (dims.size() != 1 || types.size() != 1 || dims[0].size() != 4)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN normalize input shape is invalid");
    detector_sized_ = dims[0][1] == kDetectorInputSize && dims[0][2] == kDetectorInputSize &&
                      dims[0][3] == 3;
    native_contract_ = types[0] == DATA_TYPE_UINT8 &&
                       IsRknnNativeNormalizeContract(mean_, std_dev_, uniform_scale_, dims[0]);
    if (native_contract_) {
        top_blob_shapes     = {dims[0]};
        top_blob_data_types = {DataType::DATA_TYPE_INT8};
    } else {
        top_blob_shapes     = {{dims[0][0], dims[0][3], dims[0][1], dims[0][2]}};
        top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    }
    return COSMO_NN_OK;
}

size_t RknnNormalizeNode::GetBottomCount() {
    return 1;
}

size_t RknnNormalizeNode::GetTopCount() {
    return 1;
}

bool RknnNormalizeNode::NeedSwapRedBlue(ImageFormat format) const {
    if (format == IMAGE_BGR || format == IMAGE_BGRA)
        return !is_bgr_;
    if (format == IMAGE_RGB || format == IMAGE_RGBA)
        return is_bgr_;
    return false;
}

bool RknnNormalizeNode::CanBypassBoundInput(const Blob& bottom) const {
    if (!native_contract_ || !shared_resource || !shared_resource->rknn_bound_input_provider) {
        return false;
    }
    const auto& target = shared_resource->rknn_bound_input_target;
    const auto desc    = const_cast<Blob&>(bottom).GetBlobDesc();
    return target.frame_ready && target.owner == shared_resource->rknn_bound_input_provider &&
           desc.data_type == DATA_TYPE_UINT8 && desc.data_format == DATA_FORMAT_NHWC &&
           desc.image_format == IMAGE_RGB && desc.dims.size() == 4 && desc.dims[0] == 1 &&
           target.Matches(desc.dims[1], desc.dims[2]) && !NeedSwapRedBlue(desc.image_format);
}

Status RknnNormalizeNode::ForwardNative(const Blob& bottom, Blob& top) {
    auto& mutable_bottom = const_cast<Blob&>(bottom);
    const auto bottom_desc = mutable_bottom.GetBlobDesc();
    auto top_desc          = top.GetBlobDesc();
    if (bottom_desc.data_type != DATA_TYPE_UINT8 || bottom_desc.data_format != DATA_FORMAT_NHWC ||
        top_desc.data_type != DATA_TYPE_INT8 || top_desc.data_format != DATA_FORMAT_NHWC) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN native preprocessing blob contract mismatch");
    }
    const size_t pixels = static_cast<size_t>(bottom_desc.dims[0]) * bottom_desc.dims[1] *
                          bottom_desc.dims[2];
    const auto started = MetricsClock::now();
    MapPackedU8ToNativeInt8(static_cast<const uint8_t*>(mutable_bottom.GetHandle().base),
                            static_cast<int8_t*>(top.GetHandle().base), pixels,
                            NeedSwapRedBlue(bottom_desc.image_format));
    GetInferencePipelineMetrics().RecordRknnNativeInputMap(ElapsedNanoseconds(started));
    GetInferencePipelineMetrics().RecordRknnPreprocessFastHit();
    top_desc.image_format = is_bgr_ ? IMAGE_BGR : IMAGE_RGB;
    top.SetBlobDesc(top_desc);
    return COSMO_NN_OK;
}

Status RknnNormalizeNode::ForwardFloat(const Blob& bottom, Blob& top) {
    auto& mutable_bottom = const_cast<Blob&>(bottom);
    const auto bottom_desc = mutable_bottom.GetBlobDesc();
    const auto top_desc    = top.GetBlobDesc();
    if (bottom_desc.data_type != DATA_TYPE_UINT8 || top_desc.data_type != DATA_TYPE_FLOAT ||
        top_desc.data_format != DATA_FORMAT_NCHW || mean_.size() < 3 || scale_.size() < 3) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN float normalize blob contract mismatch");
    }
    const int batch    = bottom_desc.dims[0];
    const int height   = bottom_desc.dims[1];
    const int width    = bottom_desc.dims[2];
    const int channels = bottom_desc.dims[3];
    const int plane    = height * width;
    const bool swap_red_blue = NeedSwapRedBlue(bottom_desc.image_format);
    const auto* source = static_cast<const uint8_t*>(mutable_bottom.GetHandle().base);
    auto* destination  = static_cast<float*>(top.GetHandle().base);
    for (int batch_index = 0; batch_index < batch; ++batch_index) {
        const auto* batch_source = source + batch_index * plane * channels;
        auto* batch_destination  = destination + batch_index * plane * 3;
        for (int pixel = 0; pixel < plane; ++pixel) {
            const int source_offset = pixel * channels;
            const int first_channel = swap_red_blue ? 2 : 0;
            const int third_channel = swap_red_blue ? 0 : 2;
            batch_destination[pixel] =
                (static_cast<float>(batch_source[source_offset + first_channel]) - mean_[0]) *
                scale_[0];
            batch_destination[plane + pixel] =
                (static_cast<float>(batch_source[source_offset + 1]) - mean_[1]) * scale_[1];
            batch_destination[2 * plane + pixel] =
                (static_cast<float>(batch_source[source_offset + third_channel]) - mean_[2]) *
                scale_[2];
        }
    }
    return COSMO_NN_OK;
}

Status RknnNormalizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    if (bottom_blobs.size() != 1 || top_blobs.size() != 1 || !bottom_blobs[0] ||
        !top_blobs[0] || !bottom_blobs[0]->GetHandle().base || !top_blobs[0]->GetHandle().base) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "RKNN normalize requires exactly one valid input and output");
    }
    const auto bottom_desc = bottom_blobs[0]->GetBlobDesc();
    if (bottom_desc.dims.size() != 4 || bottom_desc.dims[0] <= 0 ||
        bottom_desc.dims[0] > max_batch) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN normalize batch size is invalid");
    }
    SetCurrentBatch(top_blobs[0], bottom_desc.dims[0]);
    auto runtime_top_desc        = top_blobs[0]->GetBlobDesc();
    runtime_top_desc.data_format = native_contract_ ? DATA_FORMAT_NHWC : DATA_FORMAT_NCHW;
    top_blobs[0]->SetBlobDesc(runtime_top_desc);
    Status status;
    if (native_contract_) {
        if (CanBypassBoundInput(*bottom_blobs[0])) {
            runtime_top_desc.image_format = is_bgr_ ? IMAGE_BGR : IMAGE_RGB;
            top_blobs[0]->SetBlobDesc(runtime_top_desc);
            GetInferencePipelineMetrics().RecordRknnPreprocessFastHit();
            GetInferencePipelineMetrics().RecordRknnRgaBoundInputNormalizeBypass();
            status = COSMO_NN_OK;
        } else {
            status = ForwardNative(*bottom_blobs[0], *top_blobs[0]);
        }
    } else {
        const auto fallback_started = MetricsClock::now();
        status = ForwardFloat(*bottom_blobs[0], *top_blobs[0]);
        if (detector_sized_)
            GetInferencePipelineMetrics().RecordRknnCpuNormalizeFallback(
                ElapsedNanoseconds(fallback_started));
    }
    timer.Stop();
    return status;
}

}  // namespace cosmo::nn

#endif
