#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "service/system/impl/AcceleratorMetricsProvider.h"
#include "util/Log.h"

namespace cosmo::service::detail {
namespace {

    struct MemorySnapshot {
        int64_t total_mb{0};
        int64_t available_mb{0};
    };

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

    double ReadNpuLoad() {
        std::ifstream stream("/sys/class/devfreq/27700000.npu/load");
        std::string value;
        if (!(stream >> value))
            return 0.0;
        const auto delimiter = value.find('@');
        if (delimiter != std::string::npos)
            value.resize(delimiter);
        try {
            return std::clamp(std::stod(value) / 100.0, 0.0, 1.0);
        } catch (const std::exception&) {
            LOG_WARN("Unable to parse RK3576 NPU load: {}", value);
            return 0.0;
        }
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
            result.gpuusage        = ReadNpuLoad();
            result.gpumemtotal     = memory.total_mb;
            result.gpumemavailable = memory.available_mb;
            result.gpumemusage = memory.total_mb > 0
                                     ? static_cast<double>(memory.total_mb - memory.available_mb) /
                                           static_cast<double>(memory.total_mb)
                                     : 0.0;
            result.gpuCapacity = ReadNpuFrequency();

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
