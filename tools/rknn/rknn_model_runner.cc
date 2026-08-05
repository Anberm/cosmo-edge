#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "rknn_api.h"

namespace {

class ContextGuard {
public:
    ~ContextGuard() {
        if (context_ != 0) {
            rknn_destroy(context_);
        }
    }
    rknn_context* Out() { return &context_; }
    rknn_context Get() const { return context_; }

private:
    rknn_context context_{0};
};

template <typename T>
std::vector<T> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open: " + path.string());
    }
    const auto bytes = stream.tellg();
    if (bytes <= 0 || bytes % static_cast<std::streamoff>(sizeof(T)) != 0) {
        throw std::runtime_error("invalid byte count: " + path.string());
    }
    std::vector<T> data(static_cast<std::size_t>(bytes) / sizeof(T));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(data.data()), bytes);
    if (!stream) {
        throw std::runtime_error("short read: " + path.string());
    }
    return data;
}

void WriteFile(const std::filesystem::path& path, const void* data, std::size_t bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot create: " + path.string());
    }
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    if (!stream) {
        throw std::runtime_error("short write: " + path.string());
    }
}

void Check(int result, const std::string& action) {
    if (result != RKNN_SUCC) {
        throw std::runtime_error(action + " failed with RKNN code " + std::to_string(result));
    }
}

rknn_core_mask ParseCoreMask(const std::string& value) {
    if (value == "auto")
        return RKNN_NPU_CORE_AUTO;
    if (value == "0")
        return RKNN_NPU_CORE_0;
    if (value == "1")
        return RKNN_NPU_CORE_1;
    if (value == "01")
        return RKNN_NPU_CORE_0_1;
    throw std::runtime_error("core mask must be one of: auto, 0, 1, 01");
}

std::size_t ElementCount(const rknn_tensor_attr& attr) {
    if (attr.n_elems != 0) {
        return attr.n_elems;
    }
    return std::accumulate(attr.dims, attr.dims + attr.n_dims, std::size_t{1},
                           [](std::size_t product, std::uint32_t value) { return product * value; });
}

std::vector<float> NchwToNhwc(const std::vector<float>& source, const rknn_tensor_attr& attr) {
    if (attr.n_dims != 4) {
        throw std::runtime_error("validation runner requires a four-dimensional image input");
    }
    std::size_t batch = attr.dims[0];
    std::size_t channels = 0;
    std::size_t height = 0;
    std::size_t width = 0;
    if (attr.fmt == RKNN_TENSOR_NCHW) {
        channels = attr.dims[1];
        height = attr.dims[2];
        width = attr.dims[3];
    } else if (attr.fmt == RKNN_TENSOR_NHWC) {
        height = attr.dims[1];
        width = attr.dims[2];
        channels = attr.dims[3];
    } else {
        throw std::runtime_error("validation runner supports only NCHW/NHWC native inputs");
    }
    std::vector<float> result(source.size());
    for (std::size_t n = 0; n < batch; ++n) {
        for (std::size_t c = 0; c < channels; ++c) {
            for (std::size_t h = 0; h < height; ++h) {
                for (std::size_t w = 0; w < width; ++w) {
                    const auto source_index = ((n * channels + c) * height + h) * width + w;
                    const auto target_index = ((n * height + h) * width + w) * channels + c;
                    result[target_index] = source[source_index];
                }
            }
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 7) {
        std::cerr << "Usage: " << argv[0]
                  << " <model.rknn> <input-f32-nchw.bin> <output-dir> [iterations=1] [warmup=1] [auto|0|1|01]\n";
        return 2;
    }

    try {
        const auto model = ReadFile<std::uint8_t>(argv[1]);
        const auto input_values = ReadFile<float>(argv[2]);
        const std::filesystem::path output_dir(argv[3]);
        const int iterations = argc >= 5 ? std::stoi(argv[4]) : 1;
        const int warmup = argc >= 6 ? std::stoi(argv[5]) : 1;
        const auto core_mask = ParseCoreMask(argc >= 7 ? argv[6] : "auto");
        if (iterations <= 0 || warmup < 0) {
            throw std::runtime_error("iterations must be positive and warmup must be non-negative");
        }
        std::filesystem::create_directories(output_dir);

        ContextGuard context;
        if (model.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("model exceeds RKNN API size limit");
        }
        Check(rknn_init(context.Out(), const_cast<std::uint8_t*>(model.data()),
                        static_cast<std::uint32_t>(model.size()), 0, nullptr),
              "rknn_init");
        Check(rknn_set_core_mask(context.Get(), core_mask), "rknn_set_core_mask");

        rknn_sdk_version version{};
        Check(rknn_query(context.Get(), RKNN_QUERY_SDK_VERSION, &version, sizeof(version)),
              "RKNN_QUERY_SDK_VERSION");
        rknn_input_output_num counts{};
        Check(rknn_query(context.Get(), RKNN_QUERY_IN_OUT_NUM, &counts, sizeof(counts)),
              "RKNN_QUERY_IN_OUT_NUM");
        if (counts.n_input != 1) {
            throw std::runtime_error("validation runner currently requires exactly one input");
        }
        rknn_tensor_attr input_attr{};
        input_attr.index = 0;
        Check(rknn_query(context.Get(), RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr)),
              "RKNN_QUERY_INPUT_ATTR");
        if (input_values.size() != ElementCount(input_attr)) {
            throw std::runtime_error("input float count " + std::to_string(input_values.size()) +
                                     " does not match model element count " +
                                     std::to_string(ElementCount(input_attr)));
        }
        const auto input_nhwc = NchwToNhwc(input_values, input_attr);

        rknn_input input{};
        input.index = 0;
        input.buf = const_cast<float*>(input_nhwc.data());
        input.size = static_cast<std::uint32_t>(input_nhwc.size() * sizeof(float));
        input.pass_through = 0;
        input.type = RKNN_TENSOR_FLOAT32;
        // RKNN Runtime 2.3.2 only accepts NHWC source layout on its input
        // conversion path. CosmoEdge supplies NCHW, so make the boundary copy
        // explicit rather than relying on a silently rejected NCHW request.
        input.fmt = RKNN_TENSOR_NHWC;
        Check(rknn_inputs_set(context.Get(), 1, &input), "rknn_inputs_set");

        for (int index = 0; index < warmup; ++index) {
            Check(rknn_run(context.Get(), nullptr), "rknn_run warmup");
        }

        std::vector<double> elapsed_ms;
        elapsed_ms.reserve(iterations);
        for (int index = 0; index < iterations; ++index) {
            const auto start = std::chrono::steady_clock::now();
            Check(rknn_run(context.Get(), nullptr), "rknn_run");
            const auto stop = std::chrono::steady_clock::now();
            elapsed_ms.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        }

        std::vector<rknn_output> outputs(counts.n_output);
        for (std::uint32_t index = 0; index < counts.n_output; ++index) {
            outputs[index].index = index;
            outputs[index].want_float = 1;
            outputs[index].is_prealloc = 0;
        }
        Check(rknn_outputs_get(context.Get(), counts.n_output, outputs.data(), nullptr), "rknn_outputs_get");
        for (std::uint32_t index = 0; index < counts.n_output; ++index) {
            const auto path = output_dir / ("output-" + std::to_string(index) + ".f32.bin");
            WriteFile(path, outputs[index].buf, outputs[index].size);
            std::cout << "output_" << index << "_path=" << path << '\n';
            std::cout << "output_" << index << "_bytes=" << outputs[index].size << '\n';
        }
        Check(rknn_outputs_release(context.Get(), counts.n_output, outputs.data()), "rknn_outputs_release");

        const auto sum = std::accumulate(elapsed_ms.begin(), elapsed_ms.end(), 0.0);
        const auto minmax = std::minmax_element(elapsed_ms.begin(), elapsed_ms.end());
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "api_version=" << version.api_version << '\n';
        std::cout << "driver_version=" << version.drv_version << '\n';
        std::cout << "input_native_type=" << get_type_string(input_attr.type) << '\n';
        std::cout << "input_native_format=" << get_format_string(input_attr.fmt) << '\n';
        std::cout << "input_source_format=NHWC\n";
        std::cout << "iterations=" << iterations << '\n';
        std::cout << "latency_mean_ms=" << sum / elapsed_ms.size() << '\n';
        std::cout << "latency_min_ms=" << *minmax.first << '\n';
        std::cout << "latency_max_ms=" << *minmax.second << '\n';
        std::cout << "runner_status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runner_status=FAIL error=" << error.what() << '\n';
        return 1;
    }
}
