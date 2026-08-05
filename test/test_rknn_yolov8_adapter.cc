#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_RKNN_BACKEND

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

#endif  // COSMO_NN_USE_RKNN_BACKEND
