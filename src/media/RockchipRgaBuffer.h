#pragma once

#include <rga/im2d.h>
#include <rga/im2d_version.h>

#include <cstddef>
#include <limits>

namespace cosmo::media {

/// One imported RGA buffer handle with deterministic release semantics.
///
/// The wrapper is intentionally backend-wide rather than SoC-specific. It is
/// used for MPP DMA-BUFs, RKNN DMA-BUFs, and the existing host-frame boundary.
class ScopedRgaBufferHandle {
public:
    ScopedRgaBufferHandle() = default;

    ScopedRgaBufferHandle(void* address, size_t bytes) {
        ImportVirtual(address, bytes);
    }

    ~ScopedRgaBufferHandle() {
        Reset();
    }

    ScopedRgaBufferHandle(const ScopedRgaBufferHandle&)            = delete;
    ScopedRgaBufferHandle& operator=(const ScopedRgaBufferHandle&) = delete;

    ScopedRgaBufferHandle(ScopedRgaBufferHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = 0;
    }

    ScopedRgaBufferHandle& operator=(ScopedRgaBufferHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_       = other.handle_;
            other.handle_ = 0;
        }
        return *this;
    }

    bool ImportVirtual(void* address, size_t bytes) {
        if (handle_ != 0 || !address || !ValidSize(bytes)) {
            return false;
        }
        handle_ = importbuffer_virtualaddr(address, static_cast<int>(bytes));
        return handle_ != 0;
    }

    bool ImportFd(int fd, size_t bytes) {
        if (handle_ != 0 || fd < 0 || !ValidSize(bytes)) {
            return false;
        }
        handle_ = importbuffer_fd(fd, static_cast<int>(bytes));
        return handle_ != 0;
    }

    void Reset() {
        if (handle_ != 0) {
            releasebuffer_handle(handle_);
            handle_ = 0;
        }
    }

    [[nodiscard]] rga_buffer_handle_t Get() const {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const {
        return handle_ != 0;
    }

private:
    static bool ValidSize(size_t bytes) {
        return bytes > 0 && bytes <= static_cast<size_t>(std::numeric_limits<int>::max());
    }

    rga_buffer_handle_t handle_{0};
};

inline bool RockchipRgaSucceeded(IM_STATUS status) {
    return status == IM_STATUS_SUCCESS || status == IM_STATUS_NOERROR;
}

inline constexpr bool RockchipRgaHasBt2020ColorSpace() {
#if defined(RGA_CURRENT_API_VERSION) && RGA_CURRENT_API_VERSION >= 0x010a0600
    return true;
#else
    return false;
#endif
}

inline IM_COLOR_SPACE_MODE RockchipRgaBt2020ColorSpace(bool full_range) {
#if defined(RGA_CURRENT_API_VERSION) && RGA_CURRENT_API_VERSION >= 0x010a0600
    return full_range ? IM_YUV_BT2020_FULL_RANGE : IM_YUV_BT2020_LIMIT_RANGE;
#else
    // librga 1.10.1 and earlier do not expose BT.2020 full-CSC modes. Keep
    // compilation and runtime behavior inside the advertised header contract;
    // the caller emits a warning if this colorimetry downgrade is exercised.
    return full_range ? IM_YUV_BT709_FULL_RANGE : IM_YUV_BT709_LIMIT_RANGE;
#endif
}

inline void SetRgaYuvToRgbColorSpace(rga_buffer_t& source, rga_buffer_t& target,
                                     IM_COLOR_SPACE_MODE source_mode = IM_YUV_BT601_LIMIT_RANGE) {
    imsetColorSpace(&source, source_mode);
    // IM_RGB_FULL_RANGE is a newer alias; IM_RGB_FULL is ABI-identical and is
    // present in both supported librga header generations.
    imsetColorSpace(&target, IM_RGB_FULL);
}

}  // namespace cosmo::media
