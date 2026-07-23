#ifdef COSMO_NN_USE_SOPHON_BACKEND

#include <algorithm>

#include "bmlib_runtime.h"
#include "service/system/impl/AcceleratorMetricsProvider.h"
#include "util/Log.h"

namespace cosmo::service::detail {
namespace {

    class SophonAcceleratorMetricsProvider final : public AcceleratorMetricsProvider {
    public:
        cosmo::MsgGpuInfo QueryUtilization() override {
            cosmo::MsgGpuInfo result;
            bm_handle_t handle{};
            auto status = bm_dev_request(&handle, 0);
            if (status != BM_SUCCESS) {
                LOG_ERRO("bm_dev_request failed:{}", status);
                return result;
            }
            bm_dev_stat statistics{};
            status = bm_get_stat(handle, &statistics);
            if (status != BM_SUCCESS) {
                LOG_ERRO("bm_get_stat failed:{}", status);
                bm_dev_free(handle);
                return result;
            }

            result.gpumemtotal     = std::max(statistics.mem_total, 0);
            result.gpumemusage     = result.gpumemtotal > 0
                                         ? std::clamp(static_cast<double>(std::max(statistics.mem_used, 0)) /
                                                          static_cast<double>(result.gpumemtotal),
                                                      0.0, 1.0)
                                         : 0.0;
            result.gpuusage        = std::clamp(statistics.tpu_util, 0, 100) * 0.01;
            result.gpumemavailable = 0;
            const int heap_count   = std::clamp(statistics.heap_num, 0, 4);
            if (heap_count != statistics.heap_num)
                LOG_WARN("Invalid NPU heap count {}, clamped to {}", statistics.heap_num, heap_count);

            for (int index = 0; index < heap_count; ++index) {
                cosmo::MsgGpuDevUsage device;
                device.gpumemtotal = statistics.heap_stat[index].mem_total;
                device.gpumemavailable =
                    std::min(statistics.heap_stat[index].mem_avail, statistics.heap_stat[index].mem_total);
                result.gpumemavailable += device.gpumemavailable;
                if (device.gpumemtotal > 0) {
                    device.gpumemusage =
                        static_cast<double>(device.gpumemtotal - device.gpumemavailable) / device.gpumemtotal;
                }
                result.gpudevusage.push_back(device);
                LOG_INFO("Heap:{} Total:{}MB AVAIBLE:{}MB Used:{}MB", index,
                         statistics.heap_stat[index].mem_total, statistics.heap_stat[index].mem_avail,
                         statistics.heap_stat[index].mem_used);
            }
            LOG_INFO("[GPU MEM:{}MB AVAIBLE:{}MB MemUsage:{} TpuUsage:{}] stat.heap_num:{}",
                     result.gpumemtotal, result.gpumemavailable, result.gpumemusage, result.gpuusage,
                     heap_count);
            bm_dev_free(handle);
            return result;
        }

        int64_t QueryAvailableMemoryMB() override {
            bm_handle_t handle{};
            auto status = bm_dev_request(&handle, 0);
            if (status != BM_SUCCESS) {
                LOG_ERRO("GetAvailableMemoryMB: bm_dev_request failed:{}", status);
                return -1;
            }
            bm_dev_stat statistics{};
            status = bm_get_stat(handle, &statistics);
            if (status != BM_SUCCESS) {
                LOG_ERRO("GetAvailableMemoryMB: bm_get_stat failed:{}", status);
                bm_dev_free(handle);
                return -1;
            }
            int64_t total_available_mb = 0;
            const int heap_count       = std::clamp(statistics.heap_num, 0, 4);
            for (int index = 0; index < heap_count; ++index)
                total_available_mb += statistics.heap_stat[index].mem_avail;
            bm_dev_free(handle);
            LOG_INFO("[GPU mem check] available:{}MB total:{}MB", total_available_mb, statistics.mem_total);
            return total_available_mb;
        }
    };

}  // namespace

std::unique_ptr<AcceleratorMetricsProvider> CreateAcceleratorMetricsProvider() {
    return std::make_unique<SophonAcceleratorMetricsProvider>();
}

}  // namespace cosmo::service::detail

#endif  // COSMO_NN_USE_SOPHON_BACKEND
