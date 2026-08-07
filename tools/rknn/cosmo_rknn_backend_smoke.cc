#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "infer/BmodelTool.h"
#include "nn/core/blob.h"
#include "nn/device/rknn/rknn_net_node.h"
#include "util/Log.h"

namespace {

template <typename T>
std::vector<T> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("cannot open " + path.string());
    const auto bytes = stream.tellg();
    if (bytes <= 0 || bytes % static_cast<std::streamoff>(sizeof(T)) != 0)
        throw std::runtime_error("invalid byte count for " + path.string());
    std::vector<T> result(static_cast<size_t>(bytes) / sizeof(T));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(result.data()), bytes))
        throw std::runtime_error("short read from " + path.string());
    return result;
}

void WriteFile(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot write " + path.string());
}

size_t ElementCount(const cosmo::nn::DimsVector& shape) {
    size_t count = 1;
    for (int dim : shape) {
        if (dim <= 0 || count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
            throw std::runtime_error("invalid tensor shape");
        count *= static_cast<size_t>(dim);
    }
    return count;
}

std::string ShapeString(const std::vector<int>& shape) {
    std::string result;
    for (size_t index = 0; index < shape.size(); ++index) {
        if (index != 0)
            result += "x";
        result += std::to_string(shape[index]);
    }
    return result;
}

class LogGuard {
public:
    LogGuard() { cosmo::log::LogInit("cosmo-rknn-backend-smoke", "/tmp", "p3"); }
    ~LogGuard() { cosmo::log::LogShutDown(); }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 7 || argc > 8) {
        std::cerr << "Usage: " << argv[0]
                  << " <model.rknn> <input-f32-nchw.bin> <height> <width> <output.bin> <iterations> "
                     "[warmup=1]\n";
        return 2;
    }

    try {
        const auto model = ReadFile<unsigned char>(argv[1]);
        const auto input = ReadFile<float>(argv[2]);
        const int height = std::stoi(argv[3]);
        const int width = std::stoi(argv[4]);
        const int iterations = std::stoi(argv[6]);
        const int warmup = argc == 8 ? std::stoi(argv[7]) : 1;
        if (height <= 0 || width <= 0 || iterations <= 0 || warmup < 0 ||
            input.size() != static_cast<size_t>(3) * height * width) {
            throw std::runtime_error("invalid dimensions, iteration count, or input size");
        }

        LogGuard log_guard;
        const auto model_info = cosmo::BmodelTool::GetBmodelInfo(argv[1]);
        if (!model_info.valid || model_info.networks.size() != 1)
            throw std::runtime_error("RKNN model metadata query failed: " + model_info.error_msg);
        const auto& network = model_info.networks[0];
        const std::vector<int> expected_input{1, 3, height, width};
        if (network.inputs.size() != 1 || network.inputs[0].shape != expected_input ||
            network.outputs.size() != 1) {
            throw std::runtime_error("RKNN model metadata does not match the smoke contract");
        }

        cosmo::nn::RknnNetNode node;
        node.SetNetworkInputNames({"images"});
        node.SetNetworkOutputNames({"output0"});
        auto status = node.LoadWeight(reinterpret_cast<const char*>(model.data()), model.size());
        if (!bool(status))
            throw std::runtime_error(status.description());
        status = node.InferTopShapes();
        if (!bool(status))
            throw std::runtime_error(status.description());

        const auto top_shapes = node.GetTopBlobShapes();
        if (top_shapes.size() != 1 || network.outputs[0].shape != top_shapes[0])
            throw std::runtime_error("smoke runner requires one logical output");

        cosmo::nn::BlobDesc bottom_desc;
        bottom_desc.device_type = cosmo::nn::DEVICE_NAIVE;
        bottom_desc.data_type = cosmo::nn::DATA_TYPE_FLOAT;
        bottom_desc.data_format = cosmo::nn::DATA_FORMAT_NCHW;
        bottom_desc.dims = {1, 3, height, width};
        cosmo::nn::BlobHandle bottom_handle;
        bottom_handle.base = const_cast<float*>(input.data());
        auto bottom = std::make_shared<cosmo::nn::Blob>(bottom_desc, bottom_handle);

        cosmo::nn::BlobDesc top_desc;
        top_desc.device_type = cosmo::nn::DEVICE_NAIVE;
        top_desc.data_type = cosmo::nn::DATA_TYPE_FLOAT;
        top_desc.data_format = cosmo::nn::DATA_FORMAT_NCHW;
        top_desc.dims = top_shapes[0];
        auto top = std::make_shared<cosmo::nn::Blob>(top_desc, true);
        if (!top->GetHandle().base)
            throw std::runtime_error("failed to allocate logical output");

        std::vector<std::shared_ptr<cosmo::nn::Blob>> bottoms{bottom};
        std::vector<std::shared_ptr<cosmo::nn::Blob>> tops{top};
        for (int index = 0; index < warmup; ++index) {
            status = node.Forward(bottoms, tops);
            if (!bool(status))
                throw std::runtime_error(status.description());
        }

        std::vector<double> elapsed_ms;
        elapsed_ms.reserve(iterations);
        for (int index = 0; index < iterations; ++index) {
            const auto start = std::chrono::steady_clock::now();
            status = node.Forward(bottoms, tops);
            const auto stop = std::chrono::steady_clock::now();
            if (!bool(status))
                throw std::runtime_error(status.description());
            elapsed_ms.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        }

        const size_t output_count = ElementCount(top_shapes[0]);
        WriteFile(argv[5], top->GetHandle().base, output_count * sizeof(float));
        const double sum = std::accumulate(elapsed_ms.begin(), elapsed_ms.end(), 0.0);
        const auto minmax = std::minmax_element(elapsed_ms.begin(), elapsed_ms.end());
        std::cout << std::fixed << std::setprecision(4)
                  << "metadata_status=PASS\n"
                  << "metadata_input_shape=" << ShapeString(network.inputs[0].shape) << "\n"
                  << "metadata_output_shape=" << ShapeString(network.outputs[0].shape) << "\n"
                  << "logical_shape=";
        for (size_t index = 0; index < top_shapes[0].size(); ++index)
            std::cout << (index == 0 ? "" : "x") << top_shapes[0][index];
        std::cout << "\niterations=" << iterations
                  << "\nlatency_mean_ms=" << sum / elapsed_ms.size()
                  << "\nlatency_min_ms=" << *minmax.first
                  << "\nlatency_max_ms=" << *minmax.second
                  << "\nbackend_smoke_status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "backend_smoke_status=FAIL error=" << error.what() << '\n';
        return 1;
    }
}
