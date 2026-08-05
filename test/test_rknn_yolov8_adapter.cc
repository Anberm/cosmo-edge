#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <cstdint>
#include <string>
#include <vector>

#include "nn/device/rknn/rknn_yolov8_adapter.h"

TEST_CASE("RKNN YOLOv8 adapter reconstructs the logical tensor", "[nn][rknn][yolov8]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2},
        {1, 3, 2, 2},  {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    RknnYolov8Layout layout;
    std::string error;
    REQUIRE(DetectRknnYolov8Layout(shapes, layout, error));
    REQUIRE(layout.logical_shape == std::vector<int>{1, 7, 21});

    std::vector<std::vector<float>> values;
    std::vector<RknnYolov8Head> heads;
    for (const auto& shape : shapes) {
        const size_t count = static_cast<size_t>(shape[1] * shape[2] * shape[3]);
        values.emplace_back(count, 0.0f);
        heads.push_back({values.back().data(), values.back().size(), shape});
    }

    std::vector<float> output(static_cast<size_t>(7 * 21));
    REQUIRE(ReconstructRknnYolov8(heads, 32, 32, output.data(), output.size(), error));
    CHECK(output[0] == Catch::Approx(4.0f));
    CHECK(output[21] == Catch::Approx(4.0f));
    CHECK(output[42] == Catch::Approx(120.0f));
    CHECK(output[63] == Catch::Approx(120.0f));
    CHECK(output[84] == Catch::Approx(0.5f));
    CHECK(output[105] == Catch::Approx(0.5f));
    CHECK(output[126] == Catch::Approx(0.5f));
    CHECK(output[16] == Catch::Approx(8.0f));
    CHECK(output[20] == Catch::Approx(16.0f));
}

TEST_CASE("RKNN YOLOv8 adapter rejects malformed head order", "[nn][rknn][yolov8]") {
    using namespace cosmo::nn;
    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 4, 4},
        {1, 3, 4, 4},  {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    RknnYolov8Layout layout;
    std::string error;
    CHECK_FALSE(DetectRknnYolov8Layout(shapes, layout, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("RKNN YOLOv8 quantized adapter matches dequantized float heads",
          "[nn][rknn][yolov8][fast-output]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2},
        {1, 3, 2, 2},  {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    std::vector<std::vector<int8_t>> quantized_values;
    std::vector<std::vector<float>> float_values;
    std::vector<RknnYolov8QuantizedHead> quantized_heads;
    std::vector<RknnYolov8Head> float_heads;
    for (size_t head = 0; head < shapes.size(); ++head) {
        const size_t count =
            static_cast<size_t>(shapes[head][1] * shapes[head][2] * shapes[head][3]);
        const int32_t zero_point = head % 2 == 0 ? -61 : 114;
        const float scale       = head % 2 == 0 ? 0.11488f : 0.113557f;
        quantized_values.emplace_back(count);
        float_values.emplace_back(count);
        for (size_t index = 0; index < count; ++index) {
            const auto value = static_cast<int8_t>(static_cast<int>(index % 97) - 48);
            quantized_values.back()[index] = value;
            float_values.back()[index] =
                (static_cast<float>(value) - static_cast<float>(zero_point)) * scale;
        }
        quantized_heads.push_back({quantized_values.back().data(), count, shapes[head], zero_point,
                                   scale});
        float_heads.push_back({float_values.back().data(), count, shapes[head]});
    }

    std::vector<float> expected(static_cast<size_t>(7 * 21));
    std::vector<float> actual(expected.size());
    std::string error;
    REQUIRE(ReconstructRknnYolov8(float_heads, 32, 32, expected.data(), expected.size(), error));
    RknnYolov8TransformTiming timing;
    REQUIRE(ReconstructRknnYolov8Quantized(quantized_heads, 32, 32, actual.data(), actual.size(),
                                           error, &timing));
    for (size_t index = 0; index < expected.size(); ++index)
        CHECK(actual[index] == Catch::Approx(expected[index]).margin(1e-4f));
}

TEST_CASE("RKNN YOLOv8 quantized adapter rejects invalid quantization",
          "[nn][rknn][yolov8][fast-output]") {
    using namespace cosmo::nn;
    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2},
        {1, 3, 2, 2},  {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    std::vector<std::vector<int8_t>> values;
    std::vector<RknnYolov8QuantizedHead> heads;
    for (const auto& shape : shapes) {
        const size_t count = static_cast<size_t>(shape[1] * shape[2] * shape[3]);
        values.emplace_back(count, 0);
        heads.push_back({values.back().data(), count, shape, 0, 0.1f});
    }
    heads[1].scale = 0.0f;
    std::vector<float> output(static_cast<size_t>(7 * 21));
    std::string error;
    CHECK_FALSE(ReconstructRknnYolov8Quantized(heads, 32, 32, output.data(), output.size(), error));
    CHECK_FALSE(error.empty());
}

#endif  // COSMO_NN_USE_RKNN_BACKEND
