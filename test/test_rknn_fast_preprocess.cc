#include "catch_amalgamated.hpp"

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "nn/core/inference_pipeline_metrics.h"
#include "nn/core/shared_resource.h"
#include "nn/device/rknn/rknn_net_node.h"
#include "nn/device/rknn/rknn_preprocess_node.h"
#include "nn/utils/op.h"

namespace {

class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const char* value) : name_(name) {
        if (const char* current = std::getenv(name))
            previous_ = current;
        setenv(name, value, 1);
    }
    ~ScopedEnvironment() {
        if (previous_)
            setenv(name_.c_str(), previous_->c_str(), 1);
        else
            unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

cosmo::nn::BlobDesc PackedImageDesc(int height, int width, cosmo::nn::ImageFormat format,
                                    cosmo::nn::DataType type = cosmo::nn::DATA_TYPE_UINT8) {
    cosmo::nn::BlobDesc desc;
    desc.device_type  = cosmo::nn::DEVICE_NAIVE;
    desc.data_type    = type;
    desc.data_format  = cosmo::nn::DATA_FORMAT_NHWC;
    desc.image_format = format;
    desc.dims         = {1, height, width, 3};
    return desc;
}

}  // namespace

TEST_CASE("RKNN detector fast preprocessing contracts are exact", "[nn][rknn][fast-preprocess]") {
    using namespace cosmo::nn;
    CHECK(IsRknnDetectorResizeContract(640, 640, 1, {114, 114, 114}));
    CHECK_FALSE(IsRknnDetectorResizeContract(640, 640, 0, {114, 114, 114}));
    CHECK_FALSE(IsRknnDetectorResizeContract(224, 224, 1, {114, 114, 114}));
    CHECK(IsRknnNativeNormalizeContract({0.0f, 0.0f, 0.0f}, {}, 0.00392157f,
                                        {1, 640, 640, 3}));
    CHECK_FALSE(IsRknnNativeNormalizeContract({1.0f, 0.0f, 0.0f}, {}, 0.00392157f,
                                              {1, 640, 640, 3}));
    CHECK_FALSE(IsRknnNativeNormalizeContract({0.0f, 0.0f, 0.0f}, {}, 0.00392157f,
                                              {1, 224, 224, 3}));

    const std::array<uint8_t, 6> rgb{0, 127, 255, 255, 1, 128};
    std::array<int8_t, 6> native{};
    MapPackedU8ToNativeInt8(rgb.data(), native.data(), 2, false);
    CHECK((native == std::array<int8_t, 6>{-128, -1, 127, 127, -127, 0}));
    MapPackedU8ToNativeInt8(rgb.data(), native.data(), 2, true);
    CHECK((native == std::array<int8_t, 6>{127, -1, -128, 0, -127, 127}));
}

TEST_CASE("RKNN native input contract requires the model quantization identity",
          "[nn][rknn][fast-preprocess]") {
    using namespace cosmo::nn;
    rknn_tensor_attr attr{};
    attr.n_dims    = 4;
    attr.dims[0]   = 1;
    attr.dims[1]   = 640;
    attr.dims[2]   = 640;
    attr.dims[3]   = 3;
    attr.fmt       = RKNN_TENSOR_NHWC;
    attr.type      = RKNN_TENSOR_INT8;
    attr.qnt_type  = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
    attr.zp        = -128;
    attr.scale     = 0.00392157f;
    const auto desc = PackedImageDesc(640, 640, IMAGE_RGB, DATA_TYPE_INT8);
    CHECK(IsRknnNativeInt8InputCompatible(attr, desc));
    attr.zp = 0;
    CHECK_FALSE(IsRknnNativeInt8InputCompatible(attr, desc));
}

TEST_CASE("RKNN native output capability excludes FP16 and malformed YOLOv8 heads",
          "[nn][rknn][fast-output][fp16]") {
    using namespace cosmo::nn;
    const std::array<std::array<uint32_t, 4>, 6> shapes{{
        {{1, 64, 80, 80}}, {{1, 80, 80, 80}}, {{1, 64, 40, 40}},
        {{1, 80, 40, 40}}, {{1, 64, 20, 20}}, {{1, 80, 20, 20}},
    }};
    std::vector<rknn_tensor_attr> attrs(shapes.size());
    for (size_t index = 0; index < attrs.size(); ++index) {
        auto& attr    = attrs[index];
        attr.index    = static_cast<uint32_t>(index);
        attr.n_dims   = 4;
        attr.fmt      = RKNN_TENSOR_NCHW;
        attr.type     = RKNN_TENSOR_INT8;
        attr.qnt_type = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
        attr.zp       = index % 2 == 0 ? -61 : 114;
        attr.scale    = index % 2 == 0 ? 0.11488f : 0.113557f;
        size_t count  = 1;
        for (size_t dim = 0; dim < shapes[index].size(); ++dim) {
            attr.dims[dim] = shapes[index][dim];
            count *= shapes[index][dim];
        }
        attr.n_elems = static_cast<uint32_t>(count);
        attr.size    = static_cast<uint32_t>(count);
    }

    std::string reason;
    CHECK(IsRknnNativeYolov8OutputCompatible(attrs, &reason));
    CHECK(reason.empty());

    auto fp16       = attrs;
    fp16[0].type    = RKNN_TENSOR_FLOAT16;
    fp16[0].size   *= 2;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(fp16, &reason));
    CHECK(reason.find("FP16") != std::string::npos);

    auto wrong_format    = attrs;
    wrong_format[0].fmt  = RKNN_TENSOR_NHWC;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(wrong_format));
    auto wrong_size      = attrs;
    wrong_size[0].size  += 1;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(wrong_size));
    auto wrong_quantization      = attrs;
    wrong_quantization[0].scale  = 0.0f;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(wrong_quantization));
}

TEST_CASE("RKNN native output switch defaults on and supports explicit rollback",
          "[nn][rknn][fast-output]") {
    using namespace cosmo::nn;
    {
        ScopedEnvironment enabled("COSMO_RKNN_FAST_OUTPUT", "1");
        CHECK(RknnFastOutputEnabled());
    }
    {
        ScopedEnvironment disabled("COSMO_RKNN_FAST_OUTPUT", "0");
        CHECK_FALSE(RknnFastOutputEnabled());
    }
}

TEST_CASE("RKNN classifier-sized normalization keeps the legacy float layout",
          "[nn][rknn][fast-preprocess]") {
    using namespace cosmo::nn;
    Normalize normalize;
    normalize.mean   = {0.0f, 0.0f, 0.0f};
    normalize.scale  = 0.00392157f;
    normalize.is_bgr = false;
    SharedResource resource;
    RknnNormalizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&normalize);
    REQUIRE(bool(node.InferTopShapesWithBottoms({{1, 224, 224, 3}}, {DATA_TYPE_UINT8})));
    CHECK(node.GetTopBlobDataTypes().front() == DATA_TYPE_FLOAT);
    CHECK((node.GetTopBlobShapes().front() == DimsVector{1, 3, 224, 224}));

    auto bottom_desc        = PackedImageDesc(224, 224, IMAGE_BGR);
    bottom_desc.data_format = DATA_FORMAT_NCHW;  // Legacy crop/resize nodes leave this metadata unset.
    auto bottom             = std::make_shared<Blob>(bottom_desc, true);
    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = DATA_TYPE_FLOAT;
    top_desc.dims        = {1, 3, 224, 224};
    auto top             = std::make_shared<Blob>(top_desc, true);
    auto* input          = static_cast<uint8_t*>(bottom->GetHandle().base);
    for (size_t pixel = 0; pixel < static_cast<size_t>(224) * 224; ++pixel) {
        input[pixel * 3]     = 10;
        input[pixel * 3 + 1] = 20;
        input[pixel * 3 + 2] = 30;
    }
    std::vector<std::shared_ptr<Blob>> bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(bottoms, tops)));
    const auto* output = static_cast<const float*>(top->GetHandle().base);
    CHECK(output[0] == Catch::Approx(30.0f * 0.00392157f));
    CHECK(output[224 * 224] == Catch::Approx(20.0f * 0.00392157f));
    CHECK(output[2 * 224 * 224] == Catch::Approx(10.0f * 0.00392157f));
}

TEST_CASE("RKNN RGA preprocessing performs centered RGB letterbox on host buffers",
          "[nn][rknn][rga][fast-preprocess]") {
    using namespace cosmo::nn;
    ScopedEnvironment enable("COSMO_RKNN_FAST_PREPROCESS", "1");
    ScopedEnvironment no_force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "0");

    Resize resize;
    resize.dsize   = {640, 640};
    resize.gravity = 1;
    resize.color   = {114, 114, 114};
    SharedResource resource;
    RknnResizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&resize);
    REQUIRE(bool(node.InferTopShapes()));

    auto bottom = std::make_shared<Blob>(PackedImageDesc(720, 1280, IMAGE_BGR), true);
    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = node.GetTopBlobDataTypes().front();
    top_desc.dims        = node.GetTopBlobShapes().front();
    auto top             = std::make_shared<Blob>(top_desc, true);
    auto* source         = static_cast<uint8_t*>(bottom->GetHandle().base);
    for (size_t pixel = 0; pixel < static_cast<size_t>(720) * 1280; ++pixel) {
        source[pixel * 3]     = 10;
        source[pixel * 3 + 1] = 20;
        source[pixel * 3 + 2] = 30;
    }

    const auto before = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(bottoms, tops)));
    const auto after = GetInferencePipelineMetrics().Snapshot();
    CHECK(after.rknn_rga_fill_calls == before.rknn_rga_fill_calls + 1);
    CHECK(after.rknn_rga_resize_color_calls == before.rknn_rga_resize_color_calls + 1);
    CHECK(after.rknn_rga_failures == before.rknn_rga_failures);
    CHECK(after.rknn_cpu_resize_fallback_calls == before.rknn_cpu_resize_fallback_calls);
    CHECK(top->GetBlobDesc().image_format == IMAGE_RGB);
    CHECK(top->GetBlobDesc().data_format == DATA_FORMAT_NHWC);
    const auto* output = static_cast<const uint8_t*>(top->GetHandle().base);
    CHECK(output[(10 * 640 + 320) * 3] == 114);
    const size_t center = (static_cast<size_t>(320) * 640 + 320) * 3;
    CHECK(output[center] == 30);
    CHECK(output[center + 1] == 20);
    CHECK(output[center + 2] == 10);
}

TEST_CASE("RKNN RGA failure falls back once to CPU while preserving native input mapping",
          "[nn][rknn][rga][fast-preprocess]") {
    using namespace cosmo::nn;
    ScopedEnvironment force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "1");
    Resize resize;
    resize.dsize   = {640, 640};
    resize.gravity = 1;
    resize.color   = {114, 114, 114};
    SharedResource resource;
    RknnResizeNode resize_node;
    resize_node.SetSharedResource(&resource);
    resize_node.LoadParam(&resize);
    REQUIRE(bool(resize_node.InferTopShapes()));

    auto bottom = std::make_shared<Blob>(PackedImageDesc(320, 640, IMAGE_BGR), true);
    BlobDesc resized_desc;
    resized_desc.device_type = DEVICE_NAIVE;
    resized_desc.data_type   = resize_node.GetTopBlobDataTypes().front();
    resized_desc.dims        = resize_node.GetTopBlobShapes().front();
    auto resized             = std::make_shared<Blob>(resized_desc, true);
    auto* source             = static_cast<uint8_t*>(bottom->GetHandle().base);
    std::fill(source, source + static_cast<size_t>(320) * 640 * 3, 128);

    const auto before = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> resize_bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> resize_tops{resized};
    REQUIRE(bool(resize_node.Forward(resize_bottoms, resize_tops)));
    const auto resized_metrics = GetInferencePipelineMetrics().Snapshot();
    CHECK(resized_metrics.rknn_rga_failures == before.rknn_rga_failures + 1);
    CHECK(resized_metrics.rknn_cpu_resize_fallback_calls ==
          before.rknn_cpu_resize_fallback_calls + 1);

    Normalize normalize;
    normalize.mean   = {0.0f, 0.0f, 0.0f};
    normalize.scale  = 0.00392157f;
    normalize.is_bgr = false;
    RknnNormalizeNode normalize_node;
    normalize_node.SetSharedResource(&resource);
    normalize_node.LoadParam(&normalize);
    REQUIRE(bool(normalize_node.InferTopShapesWithBottoms(
        {resized->GetBlobDesc().dims}, {resized->GetBlobDesc().data_type})));
    BlobDesc native_desc;
    native_desc.device_type = DEVICE_NAIVE;
    native_desc.data_type   = normalize_node.GetTopBlobDataTypes().front();
    native_desc.dims        = normalize_node.GetTopBlobShapes().front();
    auto native             = std::make_shared<Blob>(native_desc, true);
    std::vector<std::shared_ptr<Blob>> normalize_bottoms{resized};
    std::vector<std::shared_ptr<Blob>> normalize_tops{native};
    REQUIRE(bool(normalize_node.Forward(normalize_bottoms, normalize_tops)));
    CHECK(native->GetBlobDesc().data_type == DATA_TYPE_INT8);
    CHECK(native->GetBlobDesc().data_format == DATA_FORMAT_NHWC);
    CHECK(static_cast<const int8_t*>(native->GetHandle().base)[(320 * 640 + 320) * 3] == 0);
}

#endif
