#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "service/system/impl/AcceleratorMetricsProvider.h"

namespace cosmo::service::detail {
namespace {

    struct MemorySnapshot {
        int64_t total_mb{0};
        int64_t available_mb{0};
    };

    struct NpuLoadSnapshot {
        double aggregate{0.0};
        std::vector<double> cores;
    };

    std::optional<unsigned int> ParseUnsigned(const std::ssub_match& match) {
        const auto text = match.str();
        unsigned int value{0};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    std::optional<NpuLoadSnapshot> ParseNpuLoad(const std::string& text) {
        static const std::regex core_pattern(R"(Core\s*([0-9]+)\s*:\s*([0-9]+)\s*%)");
        std::map<unsigned int, double> cores_by_id;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), core_pattern);
             it != std::sregex_iterator(); ++it) {
            const auto core_id = ParseUnsigned((*it)[1]);
            const auto percent = ParseUnsigned((*it)[2]);
            if (!core_id || !percent || *percent > 100)
                return std::nullopt;
            cores_by_id[*core_id] = static_cast<double>(*percent) / 100.0;
        }
        if (cores_by_id.empty())
            return std::nullopt;

        NpuLoadSnapshot result;
        result.cores.reserve(cores_by_id.size());
        for (const auto& [core_id, load] : cores_by_id) {
            (void)core_id;
            result.cores.push_back(load);
            result.aggregate = std::max(result.aggregate, load);
        }
        return result;
    }

    std::optional<NpuLoadSnapshot> ReadNpuLoadFile(const std::string& path) {
        std::ifstream stream(path);
        if (!stream)
            return std::nullopt;
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return ParseNpuLoad(text);
    }

    std::optional<NpuLoadSnapshot> ReadNpuLoad() {
        if (const char* configured_path = std::getenv("COSMO_RKNPU_LOAD_PATH");
            configured_path && *configured_path) {
            return ReadNpuLoadFile(configured_path);
        }

        static const std::array<const char*, 2> load_paths{
            "/run/cosmo-edge/metrics/rknpu-load",
            "/sys/kernel/debug/rknpu/load",
        };
        for (const auto* path : load_paths) {
            if (auto load = ReadNpuLoadFile(path))
                return load;
        }
        return std::nullopt;
    }

    MemorySnapshot ReadSharedMemory() {
        MemorySnapshot result;
        std::ifstream stream("/proc/meminfo");
        std::string key;
        uint64_t value_kib = 0;
        std::string unit;
        while (stream >> key >> value_kib >> unit) {
            const auto value_mb = value_kib / 1024;
            if (value_mb > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                continue;
            if (key == "MemTotal:")
                result.total_mb = static_cast<int64_t>(value_mb);
            else if (key == "MemAvailable:")
                result.available_mb = static_cast<int64_t>(value_mb);
        }
        result.available_mb = std::clamp<int64_t>(result.available_mb, 0, result.total_mb);
        return result;
    }

    std::string ReadNpuFrequency() {
        std::ifstream stream("/sys/class/devfreq/27700000.npu/cur_freq");
        uint64_t hz = 0;
        if (!(stream >> hz))
            return "RK3576 shared-memory NPU";
        std::ostringstream text;
        text << (hz / 1000000) << " MHz";
        return text.str();
    }

    class RknnAcceleratorMetricsProvider final : public AcceleratorMetricsProvider {
    public:
        cosmo::MsgGpuInfo QueryUtilization() override {
            const auto memory = ReadSharedMemory();
            cosmo::MsgGpuInfo result;
            result.utilizationMetric = "busy-time-load";
            result.memoryDomain      = "shared-system";
            if (const auto load = ReadNpuLoad()) {
                // The dashboard uses the busiest core as the device health
                // signal while preserving all per-core values in telemetry.
                result.gpuusage          = load->aggregate;
                result.gpuusageAvailable = true;
                result.coreUtilizations  = load->cores;
            } else {
                result.gpuusage          = 0.0;
                result.gpuusageAvailable = false;
            }
            result.gpumemtotal     = memory.total_mb;
            result.gpumemavailable = memory.available_mb;
            result.gpumemusage     = memory.total_mb > 0
                                         ? static_cast<double>(memory.total_mb - memory.available_mb) /
                                           static_cast<double>(memory.total_mb)
                                         : 0.0;
            result.gpuCapacity     = ReadNpuFrequency();

            cosmo::MsgGpuDevUsage shared;
            shared.gpuusage        = result.gpuusage;
            shared.gpumemtotal     = result.gpumemtotal;
            shared.gpumemavailable = result.gpumemavailable;
            shared.gpumemusage     = result.gpumemusage;
            result.gpudevusage.push_back(shared);
            return result;
        }

        int64_t QueryAvailableMemoryMB() override {
            return ReadSharedMemory().available_mb;
        }
    };

}  // namespace

std::unique_ptr<AcceleratorMetricsProvider> CreateAcceleratorMetricsProvider() {
    return std::make_unique<RknnAcceleratorMetricsProvider>();
}

}  // namespace cosmo::service::detail

#endif  // COSMO_NN_USE_RKNN_BACKEND
