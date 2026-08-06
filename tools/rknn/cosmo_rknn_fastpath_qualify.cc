#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nn/core/blob.h"
#include "nn/core/shared_resource.h"
#include "nn/device/cpu/cpu_normalize_node.h"
#include "nn/device/cpu/cpu_resize_node.h"
#include "nn/device/rknn/rknn_net_node.h"
#include "nn/device/rknn/rknn_preprocess_node.h"
#include "nn/node/yolov8_decode_node.h"
#include "nn/utils/op.h"
#include "stb/stb_image.h"
#include "util/Log.h"

namespace {

using Clock = std::chrono::steady_clock;
using cosmo::nn::Blob;
using cosmo::nn::BlobDesc;
using cosmo::nn::BlobHandle;
using cosmo::nn::DataFormat;
using cosmo::nn::DataType;
using cosmo::nn::DimsVector;
using cosmo::nn::ImageFormat;
using cosmo::nn::Node;

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
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot write " + path.string());
}

std::vector<std::filesystem::path> ReadInputList(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("cannot open input list " + path.string());
    std::vector<std::filesystem::path> result;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            result.emplace_back(line);
    }
    if (result.empty())
        throw std::runtime_error("input list is empty");
    return result;
}

std::vector<uint8_t> ReadBgrFrame(const std::filesystem::path& path, int expected_height,
                                  int expected_width) {
    if (path.extension() == ".bin")
        return ReadFile<uint8_t>(path);
    const auto encoded = ReadFile<uint8_t>(path);
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> rgb(
        stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height,
                              &channels, 3),
        stbi_image_free);
    if (!rgb)
        throw std::runtime_error("cannot decode image " + path.string());
    if (height != expected_height || width != expected_width)
        throw std::runtime_error("decoded image dimensions do not match the qualification contract");
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    std::vector<uint8_t> bgr(bytes);
    for (size_t offset = 0; offset < bytes; offset += 3) {
        bgr[offset]     = rgb.get()[offset + 2];
        bgr[offset + 1] = rgb.get()[offset + 1];
        bgr[offset + 2] = rgb.get()[offset];
    }
    return bgr;
}

size_t ElementCount(const DimsVector& shape) {
    size_t count = 1;
    for (int dim : shape) {
        if (dim <= 0 || count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
            throw std::runtime_error("invalid tensor shape");
        count *= static_cast<size_t>(dim);
    }
    return count;
}

BlobDesc MakeDesc(DataType type, DataFormat format, ImageFormat image_format,
                  DimsVector dims) {
    BlobDesc desc;
    desc.device_type  = cosmo::nn::DEVICE_NAIVE;
    desc.data_type    = type;
    desc.data_format  = format;
    desc.image_format = image_format;
    desc.dims         = std::move(dims);
    return desc;
}

std::shared_ptr<Blob> AllocateBlob(const BlobDesc& desc) {
    auto blob = std::make_shared<Blob>(desc, true);
    if (!blob->GetHandle().base)
        throw std::runtime_error("failed to allocate blob");
    return blob;
}

void CheckStatus(cosmo::nn::Status status, const char* operation) {
    if (!status)
        throw std::runtime_error(std::string(operation) + ": " + status.description());
}

double ElapsedMilliseconds(Clock::time_point started_at) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started_at).count();
}

struct StageTimes {
    double resize_ms{0};
    double normalize_ms{0};
    double network_ms{0};
    double total_ms{0};
};

struct DirectOutputStageTimes {
    double network_ms{0};
    double postprocess_ms{0};
    double total_ms{0};
};

class ScopedEnvironmentFlag {
public:
    ScopedEnvironmentFlag(const char* name, const char* value) : name_(name) {
        if (const char* current = std::getenv(name_)) {
            had_value_ = true;
            value_     = current;
        }
        if (setenv(name_, value, 1) != 0)
            throw std::runtime_error(std::string("cannot set environment flag ") + name_);
    }

    ~ScopedEnvironmentFlag() {
        if (had_value_)
            setenv(name_, value_.c_str(), 1);
        else
            unsetenv(name_);
    }

private:
    const char* name_;
    bool had_value_{false};
    std::string value_;
};

struct PathBuffers {
    std::unique_ptr<Node> resize;
    std::unique_ptr<Node> normalize;
    std::shared_ptr<Blob> resized;
    std::shared_ptr<Blob> normalized;
};

class QualificationRunner {
public:
    QualificationRunner(const std::vector<unsigned char>& model, int source_height, int source_width)
        : source_height_(source_height), source_width_(source_width) {
        ConfigurePreprocessing();
        network_.SetNetworkInputNames({"images"});
        network_.SetNetworkOutputNames({"output0"});
        CheckStatus(network_.LoadWeight(reinterpret_cast<const char*>(model.data()), model.size()),
                    "load RKNN model");
        CheckStatus(network_.InferTopShapes(), "infer RKNN output shapes");
        const auto output_shapes = network_.GetTopBlobShapes();
        const auto output_types  = network_.GetTopBlobDataTypes();
        if (output_shapes.size() != 1 || output_types.size() != 1 ||
            output_types.front() != cosmo::nn::DATA_TYPE_FLOAT) {
            throw std::runtime_error("qualification runner requires one logical float output");
        }
        output_ = AllocateBlob(MakeDesc(output_types.front(), cosmo::nn::DATA_FORMAT_NCHW,
                                        cosmo::nn::IMAGE_UNKNOWN, output_shapes.front()));
        output_bytes_ = ElementCount(output_shapes.front()) * sizeof(float);
    }

    StageTimes RunLegacy(const std::vector<uint8_t>& source,
                         const std::filesystem::path& output_path) {
        return Run(source, legacy_, output_path);
    }

    StageTimes RunFast(const std::vector<uint8_t>& source,
                       const std::filesystem::path& output_path) {
        return Run(source, fast_, output_path);
    }

    void DumpLastResizedInputs(const std::filesystem::path& output_dir) const {
        constexpr size_t kPackedBytes = static_cast<size_t>(640) * 640 * 3;
        WriteFile(output_dir / "legacy-resized.bgr.u8.bin", legacy_.resized->GetHandle().base,
                  kPackedBytes);
        WriteFile(output_dir / "fast-resized.rgb.u8.bin", fast_.resized->GetHandle().base,
                  kPackedBytes);
    }

    const std::shared_ptr<Blob>& FastNormalized() const {
        return fast_.normalized;
    }

private:
    void ConfigurePreprocessing() {
        cosmo::nn::Resize resize;
        resize.dsize   = {640, 640};
        resize.gravity = 1;
        resize.color   = {114, 114, 114};
        cosmo::nn::Normalize normalize;
        normalize.mean   = {0.0f, 0.0f, 0.0f};
        normalize.scale  = 0.00392157f;
        normalize.is_bgr = false;

        legacy_.resize    = std::make_unique<cosmo::nn::CpuResizeNode>();
        legacy_.normalize = std::make_unique<cosmo::nn::CpuNormalizeNode>();
        ConfigurePath(legacy_, legacy_resource_, resize, normalize);

        fast_.resize    = std::make_unique<cosmo::nn::RknnResizeNode>();
        fast_.normalize = std::make_unique<cosmo::nn::RknnNormalizeNode>();
        ConfigurePath(fast_, fast_resource_, resize, normalize);
    }

    static void ConfigurePath(PathBuffers& path, cosmo::nn::SharedResource& resource,
                              cosmo::nn::Resize& resize, cosmo::nn::Normalize& normalize) {
        path.resize->SetSharedResource(&resource);
        path.resize->LoadParam(&resize);
        CheckStatus(path.resize->InferTopShapes(), "infer resize output shape");
        path.resized = AllocateBlob(
            MakeDesc(path.resize->GetTopBlobDataTypes().front(), cosmo::nn::DATA_FORMAT_NHWC,
                     cosmo::nn::IMAGE_UNKNOWN, path.resize->GetTopBlobShapes().front()));

        path.normalize->SetSharedResource(&resource);
        path.normalize->LoadParam(&normalize);
        CheckStatus(path.normalize->InferTopShapesWithBottoms(
                        {path.resized->GetBlobDesc().dims},
                        {path.resized->GetBlobDesc().data_type}),
                    "infer normalize output shape");
        const auto normalized_type = path.normalize->GetTopBlobDataTypes().front();
        const auto normalized_format = normalized_type == cosmo::nn::DATA_TYPE_INT8
                                           ? cosmo::nn::DATA_FORMAT_NHWC
                                           : cosmo::nn::DATA_FORMAT_NCHW;
        path.normalized = AllocateBlob(
            MakeDesc(normalized_type, normalized_format, cosmo::nn::IMAGE_RGB,
                     path.normalize->GetTopBlobShapes().front()));
    }

    StageTimes Run(const std::vector<uint8_t>& source, PathBuffers& path,
                   const std::filesystem::path& output_path) {
        const size_t expected_bytes =
            static_cast<size_t>(source_height_) * static_cast<size_t>(source_width_) * 3;
        if (source.size() != expected_bytes)
            throw std::runtime_error("source frame byte count does not match dimensions");

        BlobHandle source_handle;
        source_handle.base = const_cast<uint8_t*>(source.data());
        auto source_blob = std::make_shared<Blob>(
            MakeDesc(cosmo::nn::DATA_TYPE_UINT8, cosmo::nn::DATA_FORMAT_NHWC,
                     cosmo::nn::IMAGE_BGR, {1, source_height_, source_width_, 3}),
            source_handle);

        StageTimes times;
        const auto total_started = Clock::now();
        std::vector<std::shared_ptr<Blob>> resize_bottoms{source_blob};
        std::vector<std::shared_ptr<Blob>> resize_tops{path.resized};
        auto stage_started = Clock::now();
        CheckStatus(path.resize->Forward(resize_bottoms, resize_tops), "resize frame");
        times.resize_ms = ElapsedMilliseconds(stage_started);

        std::vector<std::shared_ptr<Blob>> normalize_bottoms{path.resized};
        std::vector<std::shared_ptr<Blob>> normalize_tops{path.normalized};
        stage_started = Clock::now();
        CheckStatus(path.normalize->Forward(normalize_bottoms, normalize_tops), "normalize frame");
        times.normalize_ms = ElapsedMilliseconds(stage_started);

        std::vector<std::shared_ptr<Blob>> network_bottoms{path.normalized};
        std::vector<std::shared_ptr<Blob>> network_tops{output_};
        stage_started = Clock::now();
        CheckStatus(network_.Forward(network_bottoms, network_tops), "run RKNN detector");
        times.network_ms = ElapsedMilliseconds(stage_started);
        times.total_ms   = ElapsedMilliseconds(total_started);
        WriteFile(output_path, output_->GetHandle().base, output_bytes_);
        return times;
    }

    int source_height_;
    int source_width_;
    size_t output_bytes_{0};
    cosmo::nn::SharedResource legacy_resource_;
    cosmo::nn::SharedResource fast_resource_;
    PathBuffers legacy_;
    PathBuffers fast_;
    cosmo::nn::RknnNetNode network_;
    std::shared_ptr<Blob> output_;
};

class DirectOutputQualificationRunner {
public:
    explicit DirectOutputQualificationRunner(const std::vector<unsigned char>& model) {
        network_.SetSharedResource(&resource_);
        network_.SetNetworkInputNames({"images"});
        network_.SetNetworkOutputNames({"output0"});
        CheckStatus(network_.LoadWeight(reinterpret_cast<const char*>(model.data()), model.size()),
                    "load direct-output RKNN model");
        CheckStatus(network_.InferTopShapes(), "infer direct-output RKNN shapes");
        const auto network_shapes = network_.GetTopBlobShapes();
        const auto network_types  = network_.GetTopBlobDataTypes();
        if (network_shapes.size() != 1 || network_types.size() != 1 ||
            network_types.front() != cosmo::nn::DATA_TYPE_FLOAT) {
            throw std::runtime_error("direct-output qualification requires one logical float output");
        }
        logical_output_ = AllocateBlob(MakeDesc(network_types.front(), cosmo::nn::DATA_FORMAT_NCHW,
                                                cosmo::nn::IMAGE_UNKNOWN, network_shapes.front()));

        cosmo::nn::YoloPost post;
        post.nms_threshold      = 0.7f;
        post.nms_detection_conf = 0.25f;
        post.top_k              = 1000;
        post.input_width        = 640;
        post.input_height       = 640;
        decode_.SetSharedResource(&resource_);
        decode_.SetMaxBatch(1);
        decode_.LoadParam(&post);
        CheckStatus(decode_.InferTopShapes(), "infer direct-output YOLOv8 decode shape");
        detection_output_ =
            AllocateBlob(MakeDesc(decode_.GetTopBlobDataTypes().front(), cosmo::nn::DATA_FORMAT_NCHW,
                                  cosmo::nn::IMAGE_UNKNOWN, decode_.GetTopBlobShapes().front()));
        detection_bytes_ = ElementCount(decode_.GetTopBlobShapes().front()) * sizeof(float);
    }

    DirectOutputStageTimes Run(const std::shared_ptr<Blob>& normalized, bool direct,
                               const std::filesystem::path& output_path) {
        ScopedEnvironmentFlag direct_flag("COSMO_RKNN_DIRECT_CANDIDATES", direct ? "1" : "0");
        DirectOutputStageTimes times;
        const auto total_started = Clock::now();
        std::vector<std::shared_ptr<Blob>> network_bottoms{normalized};
        std::vector<std::shared_ptr<Blob>> network_tops{logical_output_};
        auto stage_started = Clock::now();
        CheckStatus(network_.Forward(network_bottoms, network_tops), "run direct-output RKNN detector");
        times.network_ms = ElapsedMilliseconds(stage_started);

        std::vector<std::shared_ptr<Blob>> decode_bottoms{logical_output_};
        std::vector<std::shared_ptr<Blob>> decode_tops{detection_output_};
        stage_started = Clock::now();
        CheckStatus(decode_.Forward(decode_bottoms, decode_tops), "decode direct-output detections");
        times.postprocess_ms = ElapsedMilliseconds(stage_started);
        times.total_ms       = ElapsedMilliseconds(total_started);
        WriteFile(output_path, detection_output_->GetHandle().base, detection_bytes_);
        return times;
    }

private:
    cosmo::nn::SharedResource resource_;
    cosmo::nn::RknnNetNode network_;
    cosmo::nn::YoloV8DecodeNode decode_;
    std::shared_ptr<Blob> logical_output_;
    std::shared_ptr<Blob> detection_output_;
    size_t detection_bytes_{0};
};

class LogGuard {
public:
    LogGuard() { cosmo::log::LogInit("cosmo-rknn-fastpath-qualify", "/tmp", "qualify"); }
    ~LogGuard() { cosmo::log::LogShutDown(); }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) {
        std::cerr << "Usage: " << argv[0]
                  << " <model.rknn> <bgr-input-list.txt> <height> <width> <output-dir> "
                     "[--direct-output-parity]\n";
        return 2;
    }

    try {
        const auto model       = ReadFile<unsigned char>(argv[1]);
        const auto input_paths = ReadInputList(argv[2]);
        const int height       = std::stoi(argv[3]);
        const int width        = std::stoi(argv[4]);
        const std::filesystem::path output_dir(argv[5]);
        const bool qualify_direct_output = argc == 7 && std::string(argv[6]) == "--direct-output-parity";
        if (argc == 7 && !qualify_direct_output)
            throw std::runtime_error("unknown qualification option");
        if (height <= 0 || width <= 0)
            throw std::runtime_error("source dimensions must be positive");
        std::filesystem::create_directories(output_dir / "legacy");
        std::filesystem::create_directories(output_dir / "fast");
        if (qualify_direct_output) {
            std::filesystem::create_directories(output_dir / "output-legacy");
            std::filesystem::create_directories(output_dir / "output-direct");
        }

        LogGuard log_guard;
        QualificationRunner runner(model, height, width);
        std::unique_ptr<DirectOutputQualificationRunner> direct_output_runner;
        if (qualify_direct_output)
            direct_output_runner = std::make_unique<DirectOutputQualificationRunner>(model);
        std::ofstream timings(output_dir / "timings.tsv");
        if (!timings)
            throw std::runtime_error("cannot write timing report");
        timings << "sample\tmode\tresize_ms\tnormalize_ms\tnetwork_ms\ttotal_ms\n";
        timings << std::fixed << std::setprecision(4);
        std::ofstream direct_output_timings;
        if (qualify_direct_output) {
            direct_output_timings.open(output_dir / "direct-output-timings.tsv");
            if (!direct_output_timings)
                throw std::runtime_error("cannot write direct-output timing report");
            direct_output_timings << "sample\tmode\tnetwork_ms\tpostprocess_ms\ttotal_ms\n";
            direct_output_timings << std::fixed << std::setprecision(4);
        }

        for (size_t index = 0; index < input_paths.size(); ++index) {
            const auto source = ReadBgrFrame(input_paths[index], height, width);
            const auto sample = "sample-" + [&] {
                std::ostringstream stream;
                stream << std::setw(4) << std::setfill('0') << index;
                return stream.str();
            }();
            const auto legacy = runner.RunLegacy(source, output_dir / "legacy" / (sample + ".f32.bin"));
            const auto fast   = runner.RunFast(source, output_dir / "fast" / (sample + ".f32.bin"));
            const auto write_times = [&](const char* mode, const StageTimes& value) {
                timings << sample << '\t' << mode << '\t' << value.resize_ms << '\t'
                        << value.normalize_ms << '\t' << value.network_ms << '\t'
                        << value.total_ms << '\n';
            };
            write_times("legacy", legacy);
            write_times("fast", fast);
            if (direct_output_runner) {
                const auto legacy_output = direct_output_runner->Run(
                    runner.FastNormalized(), false, output_dir / "output-legacy" / (sample + ".f32.bin"));
                const auto direct_output = direct_output_runner->Run(
                    runner.FastNormalized(), true, output_dir / "output-direct" / (sample + ".f32.bin"));
                const auto write_direct_times = [&](const char* mode, const DirectOutputStageTimes& value) {
                    direct_output_timings << sample << '\t' << mode << '\t' << value.network_ms << '\t'
                                          << value.postprocess_ms << '\t' << value.total_ms << '\n';
                };
                write_direct_times("legacy-output", legacy_output);
                write_direct_times("direct-output", direct_output);
            }
            if (index == 0)
                runner.DumpLastResizedInputs(output_dir / "preprocessed-sample-0000");
            std::cout << "qualified=" << (index + 1) << '/' << input_paths.size() << '\n';
        }
        std::cout << "fastpath_qualification_status=PASS samples=" << input_paths.size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fastpath_qualification_status=FAIL error=" << error.what() << '\n';
        return 1;
    }
}
