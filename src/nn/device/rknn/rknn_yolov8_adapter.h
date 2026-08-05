#pragma once

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <cstddef>
#include <string>
#include <vector>

namespace cosmo::nn {

struct RknnYolov8Head {
    const float* data{nullptr};
    size_t element_count{0};
    std::vector<int> shape;
};

struct RknnYolov8Layout {
    int class_count{0};
    int point_count{0};
    std::vector<int> logical_shape;
};

bool DetectRknnYolov8Layout(const std::vector<std::vector<int>>& shapes,
                            RknnYolov8Layout& layout, std::string& error);

bool ReconstructRknnYolov8(const std::vector<RknnYolov8Head>& heads, int input_height,
                           int input_width, float* output, size_t output_count,
                           std::string& error);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
