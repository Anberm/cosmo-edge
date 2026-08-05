#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/rknn/rknn_yolov8_adapter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

namespace cosmo::nn {
namespace {

    bool ParseNchw(const std::vector<int>& shape, int& channels, int& height, int& width) {
        if (shape.size() != 4 || shape[0] != 1 || shape[1] <= 0 || shape[2] <= 0 || shape[3] <= 0)
            return false;
        channels = shape[1];
        height   = shape[2];
        width    = shape[3];
        return true;
    }

    size_t ShapeCount(const std::vector<int>& shape) {
        size_t count = 1;
        for (int dim : shape) {
            if (dim <= 0 || count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
                return 0;
            count *= static_cast<size_t>(dim);
        }
        return count;
    }

    float Sigmoid(float value) {
        if (value >= 0.0f)
            return 1.0f / (1.0f + std::exp(-value));
        const float exponential = std::exp(value);
        return exponential / (1.0f + exponential);
    }

    uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started_at) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - started_at)
                                         .count());
    }

    size_t QuantizedIndex(int8_t value) {
        return static_cast<size_t>(static_cast<int>(value) + 128);
    }

}  // namespace

bool DetectRknnYolov8Layout(const std::vector<std::vector<int>>& shapes,
                            RknnYolov8Layout& layout, std::string& error) {
    layout = {};
    if (shapes.size() != 6) {
        error = "YOLOv8 RKNN adapter requires three box/class head pairs";
        return false;
    }

    int class_count = 0;
    int point_count = 0;
    int previous_height = std::numeric_limits<int>::max();
    for (size_t branch = 0; branch < 3; ++branch) {
        int box_channels = 0, box_height = 0, box_width = 0;
        int cls_channels = 0, cls_height = 0, cls_width = 0;
        if (!ParseNchw(shapes[branch * 2], box_channels, box_height, box_width) ||
            !ParseNchw(shapes[branch * 2 + 1], cls_channels, cls_height, cls_width)) {
            error = "YOLOv8 RKNN heads must be static NCHW tensors with batch 1";
            return false;
        }
        if (box_channels != 64 || box_height != cls_height || box_width != cls_width) {
            error = "YOLOv8 RKNN box/class head dimensions do not match";
            return false;
        }
        if (cls_channels <= 0 || (class_count != 0 && cls_channels != class_count)) {
            error = "YOLOv8 RKNN class counts are inconsistent";
            return false;
        }
        if (box_height >= previous_height) {
            error = "YOLOv8 RKNN heads must be ordered from fine to coarse stride";
            return false;
        }
        if (box_height > std::numeric_limits<int>::max() / box_width ||
            point_count > std::numeric_limits<int>::max() - box_height * box_width) {
            error = "YOLOv8 RKNN point count overflows";
            return false;
        }
        class_count    = cls_channels;
        point_count   += box_height * box_width;
        previous_height = box_height;
    }

    layout.class_count  = class_count;
    layout.point_count  = point_count;
    layout.logical_shape = {1, 4 + class_count, point_count};
    return true;
}

bool ReconstructRknnYolov8(const std::vector<RknnYolov8Head>& heads, int input_height,
                           int input_width, float* output, size_t output_count,
                           std::string& error) {
    std::vector<std::vector<int>> shapes;
    shapes.reserve(heads.size());
    for (const auto& head : heads)
        shapes.push_back(head.shape);

    RknnYolov8Layout layout;
    if (!DetectRknnYolov8Layout(shapes, layout, error))
        return false;
    if (!output || input_height <= 0 || input_width <= 0) {
        error = "YOLOv8 RKNN adapter received an invalid output or input size";
        return false;
    }
    const size_t required = static_cast<size_t>(4 + layout.class_count) *
                            static_cast<size_t>(layout.point_count);
    if (output_count < required) {
        error = "YOLOv8 RKNN logical output buffer is too small";
        return false;
    }

    int point_offset = 0;
    for (size_t branch = 0; branch < 3; ++branch) {
        const auto& box_head = heads[branch * 2];
        const auto& cls_head = heads[branch * 2 + 1];
        int box_channels = 0, height = 0, width = 0;
        int cls_channels = 0, cls_height = 0, cls_width = 0;
        ParseNchw(box_head.shape, box_channels, height, width);
        ParseNchw(cls_head.shape, cls_channels, cls_height, cls_width);
        if (!box_head.data || !cls_head.data || box_head.element_count != ShapeCount(box_head.shape) ||
            cls_head.element_count != ShapeCount(cls_head.shape)) {
            error = "YOLOv8 RKNN output byte count does not match the queried head shape";
            return false;
        }

        const int spatial_count = height * width;
        const float stride_x    = static_cast<float>(input_width) / static_cast<float>(width);
        const float stride_y    = static_cast<float>(input_height) / static_cast<float>(height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int spatial_index = y * width + x;
                float distance[4]{};
                for (int side = 0; side < 4; ++side) {
                    float maximum = -std::numeric_limits<float>::infinity();
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        maximum = std::max(maximum,
                                           box_head.data[channel * spatial_count + spatial_index]);
                    }
                    float denominator = 0.0f;
                    float numerator   = 0.0f;
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        const float value = std::exp(
                            box_head.data[channel * spatial_count + spatial_index] - maximum);
                        denominator += value;
                        numerator += value * static_cast<float>(bin);
                    }
                    if (!(denominator > 0.0f) || !std::isfinite(denominator)) {
                        error = "YOLOv8 RKNN DFL softmax produced an invalid denominator";
                        return false;
                    }
                    distance[side] = numerator / denominator;
                }

                const float left   = (static_cast<float>(x) + 0.5f - distance[0]) * stride_x;
                const float top    = (static_cast<float>(y) + 0.5f - distance[1]) * stride_y;
                const float right  = (static_cast<float>(x) + 0.5f + distance[2]) * stride_x;
                const float bottom = (static_cast<float>(y) + 0.5f + distance[3]) * stride_y;
                const int logical_index = point_offset + spatial_index;
                output[logical_index]                            = (left + right) * 0.5f;
                output[layout.point_count + logical_index]       = (top + bottom) * 0.5f;
                output[2 * layout.point_count + logical_index]   = right - left;
                output[3 * layout.point_count + logical_index]   = bottom - top;
                for (int cls = 0; cls < layout.class_count; ++cls) {
                    output[(4 + cls) * layout.point_count + logical_index] =
                        Sigmoid(cls_head.data[cls * spatial_count + spatial_index]);
                }
            }
        }
        point_offset += spatial_count;
    }
    return true;
}

bool ReconstructRknnYolov8Quantized(const std::vector<RknnYolov8QuantizedHead>& heads,
                                    int input_height, int input_width, float* output,
                                    size_t output_count, std::string& error,
                                    RknnYolov8TransformTiming* timing) {
    std::vector<std::vector<int>> shapes;
    shapes.reserve(heads.size());
    for (const auto& head : heads)
        shapes.push_back(head.shape);

    RknnYolov8Layout layout;
    if (!DetectRknnYolov8Layout(shapes, layout, error))
        return false;
    if (!output || input_height <= 0 || input_width <= 0) {
        error = "YOLOv8 RKNN quantized adapter received an invalid output or input size";
        return false;
    }
    const size_t required = static_cast<size_t>(4 + layout.class_count) *
                            static_cast<size_t>(layout.point_count);
    if (output_count < required) {
        error = "YOLOv8 RKNN logical output buffer is too small";
        return false;
    }
    if (timing)
        *timing = {};

    int point_offset = 0;
    for (size_t branch = 0; branch < 3; ++branch) {
        const auto& box_head = heads[branch * 2];
        const auto& cls_head = heads[branch * 2 + 1];
        int box_channels = 0, height = 0, width = 0;
        int cls_channels = 0, cls_height = 0, cls_width = 0;
        ParseNchw(box_head.shape, box_channels, height, width);
        ParseNchw(cls_head.shape, cls_channels, cls_height, cls_width);
        if (!box_head.data || !cls_head.data ||
            box_head.element_count != ShapeCount(box_head.shape) ||
            cls_head.element_count != ShapeCount(cls_head.shape)) {
            error = "YOLOv8 RKNN quantized output byte count does not match the queried head shape";
            return false;
        }
        if (!std::isfinite(box_head.scale) || !(box_head.scale > 0.0f) ||
            !std::isfinite(cls_head.scale) || !(cls_head.scale > 0.0f) ||
            box_head.zero_point < -128 || box_head.zero_point > 127 ||
            cls_head.zero_point < -128 || cls_head.zero_point > 127) {
            error = "YOLOv8 RKNN quantized output parameters are invalid";
            return false;
        }

        std::array<float, 256> dfl_exp_lut{};
        std::array<float, 256> class_score_lut{};
        for (size_t index = 0; index < dfl_exp_lut.size(); ++index) {
            dfl_exp_lut[index] =
                std::exp(-static_cast<float>(index) * box_head.scale);
            const int quantized = static_cast<int>(index) - 128;
            class_score_lut[index] =
                Sigmoid((static_cast<float>(quantized) - cls_head.zero_point) * cls_head.scale);
        }

        const int spatial_count = height * width;
        const float stride_x    = static_cast<float>(input_width) / static_cast<float>(width);
        const float stride_y    = static_cast<float>(input_height) / static_cast<float>(height);
        const auto dfl_started  = std::chrono::steady_clock::now();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int spatial_index = y * width + x;
                float distance[4]{};
                for (int side = 0; side < 4; ++side) {
                    int maximum = -128;
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        maximum = std::max(
                            maximum,
                            static_cast<int>(box_head.data[channel * spatial_count + spatial_index]));
                    }
                    float denominator = 0.0f;
                    float numerator   = 0.0f;
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        const int quantized =
                            static_cast<int>(box_head.data[channel * spatial_count + spatial_index]);
                        const float value = dfl_exp_lut[static_cast<size_t>(maximum - quantized)];
                        denominator += value;
                        numerator += value * static_cast<float>(bin);
                    }
                    distance[side] = numerator / denominator;
                }

                const float left   = (static_cast<float>(x) + 0.5f - distance[0]) * stride_x;
                const float top    = (static_cast<float>(y) + 0.5f - distance[1]) * stride_y;
                const float right  = (static_cast<float>(x) + 0.5f + distance[2]) * stride_x;
                const float bottom = (static_cast<float>(y) + 0.5f + distance[3]) * stride_y;
                const int logical_index = point_offset + spatial_index;
                output[logical_index]                          = (left + right) * 0.5f;
                output[layout.point_count + logical_index]     = (top + bottom) * 0.5f;
                output[2 * layout.point_count + logical_index] = right - left;
                output[3 * layout.point_count + logical_index] = bottom - top;
            }
        }
        if (timing)
            timing->dfl_nanoseconds += ElapsedNanoseconds(dfl_started);

        const auto class_started = std::chrono::steady_clock::now();
        for (int cls = 0; cls < layout.class_count; ++cls) {
            const auto* source = cls_head.data + cls * spatial_count;
            auto* destination = output + (4 + cls) * layout.point_count + point_offset;
            for (int spatial_index = 0; spatial_index < spatial_count; ++spatial_index)
                destination[spatial_index] = class_score_lut[QuantizedIndex(source[spatial_index])];
        }
        if (timing)
            timing->class_nanoseconds += ElapsedNanoseconds(class_started);
        point_offset += spatial_count;
    }
    return true;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
