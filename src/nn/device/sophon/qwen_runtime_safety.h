#pragma once

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"

namespace cosmo::nn::qwen_runtime_safety {

inline std::runtime_error RuntimeError(const char* operation, const std::string& detail = {}) {
    std::string message = "Qwen Sophon runtime operation failed: ";
    message += operation;
    if (!detail.empty()) {
        message += " (";
        message += detail;
        message += ')';
    }
    return std::runtime_error(message);
}

inline void CheckStatus(bm_status_t status, const char* operation) {
    if (status != BM_SUCCESS) {
        throw RuntimeError(operation, "status=" + std::to_string(static_cast<int>(status)));
    }
}

inline size_t CheckedMultiply(size_t left, size_t right, const char* operation) {
    if (left != 0 && right > std::numeric_limits<size_t>::max() / left) {
        throw RuntimeError(operation, "size overflow");
    }
    return left * right;
}

inline unsigned int CheckedTransferSize(size_t size, const char* operation) {
    if (size > std::numeric_limits<unsigned int>::max()) {
        throw RuntimeError(operation, "transfer exceeds SDK limit");
    }
    return static_cast<unsigned int>(size);
}

inline void CheckDeviceRange(const bm_device_mem_t& memory, size_t offset, size_t size,
                             const char* operation) {
    const size_t capacity = bm_mem_get_device_size(memory);
    if (offset > capacity || size > capacity - offset) {
        throw RuntimeError(operation, "device memory range exceeds allocation");
    }
}

inline bm_device_mem_t DeviceView(const bm_device_mem_t& memory, size_t offset, size_t size,
                                  const char* operation) {
    CheckDeviceRange(memory, offset, size, operation);
    const auto address = bm_mem_get_device_addr(memory);
    if (offset > std::numeric_limits<unsigned long long>::max() - address) {
        throw RuntimeError(operation, "device address overflow");
    }
    return bm_mem_from_device(address + offset, CheckedTransferSize(size, operation));
}

inline void ClearDevice(bm_handle_t handle, bm_device_mem_t& memory, const char* operation) {
    if (handle == nullptr || bm_mem_get_device_size(memory) == 0) {
        throw RuntimeError(operation, "invalid handle or allocation");
    }
    int value = 0;
    CheckStatus(bm_memset_device_ext(handle, &value, 1, memory), operation);
}

inline void CopyHostToDevice(bm_handle_t handle, bm_device_mem_t destination, const void* source, size_t size,
                             const char* operation) {
    if (handle == nullptr || (source == nullptr && size != 0)) {
        throw RuntimeError(operation, "invalid handle or source");
    }
    CheckDeviceRange(destination, 0, size, operation);
    if (size == 0) {
        return;
    }
    CheckStatus(bm_memcpy_s2d_partial(handle, destination, const_cast<void*>(source),
                                      CheckedTransferSize(size, operation)),
                operation);
}

inline void CopyDeviceToHost(bm_handle_t handle, void* destination, bm_device_mem_t source, size_t size,
                             const char* operation) {
    if (handle == nullptr || (destination == nullptr && size != 0)) {
        throw RuntimeError(operation, "invalid handle or destination");
    }
    CheckDeviceRange(source, 0, size, operation);
    if (size == 0) {
        return;
    }
    CheckStatus(bm_memcpy_d2s_partial(handle, destination, source, CheckedTransferSize(size, operation)),
                operation);
}

inline void CopyDeviceToDevice(bm_handle_t handle, bm_device_mem_t destination, size_t destination_offset,
                               bm_device_mem_t source, size_t source_offset, size_t size,
                               const char* operation) {
    if (handle == nullptr) {
        throw RuntimeError(operation, "invalid handle");
    }
    CheckDeviceRange(destination, destination_offset, size, operation);
    CheckDeviceRange(source, source_offset, size, operation);
    if (size == 0) {
        return;
    }
    CheckStatus(bm_memcpy_d2d_byte(handle, destination, destination_offset, source, source_offset, size),
                operation);
}

inline void FreeDeviceNoThrow(bm_handle_t handle, bm_device_mem_t memory) noexcept {
    if (handle == nullptr || bm_mem_get_device_size(memory) == 0) {
        return;
    }
    try {
        bm_free_device(handle, memory);
    } catch (...) {
        std::fputs("[Qwen] bm_free_device threw during cleanup\n", stderr);
    }
}

inline void DestroyRuntimeNoThrow(void** runtime) noexcept {
    if (runtime == nullptr || *runtime == nullptr) {
        return;
    }
    try {
        bmrt_destroy(*runtime);
    } catch (...) {
        std::fputs("[Qwen] bmrt_destroy threw during cleanup\n", stderr);
    }
    *runtime = nullptr;
}

inline void FreeHandleNoThrow(bm_handle_t* handle) noexcept {
    if (handle == nullptr || *handle == nullptr) {
        return;
    }
    try {
        bm_dev_free(*handle);
    } catch (...) {
        std::fputs("[Qwen] bm_dev_free threw during cleanup\n", stderr);
    }
    *handle = nullptr;
}

}  // namespace cosmo::nn::qwen_runtime_safety
