#pragma once

#include <cstdint>
#include <memory>

#include "service/system/dto/SystemMsgTypes.h"

namespace cosmo::service::detail {

class AcceleratorMetricsProvider {
public:
    virtual ~AcceleratorMetricsProvider() = default;

    virtual cosmo::MsgGpuInfo QueryUtilization() = 0;
    virtual int64_t QueryAvailableMemoryMB()     = 0;
};

// Exactly one backend-specific translation unit provides this factory.
std::unique_ptr<AcceleratorMetricsProvider> CreateAcceleratorMetricsProvider();

}  // namespace cosmo::service::detail
