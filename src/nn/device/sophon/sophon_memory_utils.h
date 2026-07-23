#pragma once

#include <array>
#include <cstddef>
#include <limits>

#include "bmlib_runtime.h"

namespace cosmo::nn {

inline bool MakeDeviceMemoryView(const bm_device_mem_t& base, size_t offset, size_t size,
                                 bm_device_mem_t* view) {
    if (view == nullptr || size == 0 || size > std::numeric_limits<unsigned int>::max()) {
        return false;
    }

    const size_t base_size = bm_mem_get_device_size(base);
    if (offset > base_size || size > base_size - offset) {
        return false;
    }

    const auto base_address = bm_mem_get_device_addr(base);
    if (offset > std::numeric_limits<unsigned long long>::max() - base_address) {
        return false;
    }

    *view = bm_mem_from_device(base_address + offset, static_cast<unsigned int>(size));
    return true;
}

inline bool MakeThreePlaneDeviceMemoryView(const bm_device_mem_t& base, size_t frame_offset,
                                           size_t plane_size, std::array<bm_device_mem_t, 3>* views) {
    if (views == nullptr || plane_size > std::numeric_limits<size_t>::max() / 3) {
        return false;
    }
    for (size_t plane = 0; plane < views->size(); ++plane) {
        if (plane > 0 && plane_size > (std::numeric_limits<size_t>::max() - frame_offset) / plane) {
            return false;
        }
        const size_t plane_offset = frame_offset + plane * plane_size;
        if (plane_offset < frame_offset ||
            !MakeDeviceMemoryView(base, plane_offset, plane_size, &views->at(plane))) {
            return false;
        }
    }
    return true;
}

}  // namespace cosmo::nn
