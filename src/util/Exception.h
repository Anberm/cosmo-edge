// Public exception classes for Cosmo.

#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <system_error>
#include <utility>

#include "util/ErrorCode.h"

namespace cosmo::util {

class Exception : public std::exception {
public:
    explicit Exception(const std::string& msg) noexcept;

    [[nodiscard]] const char* what() const noexcept override;

protected:
    Exception() noexcept = default;
    void SetMessage(std::string&& msg) noexcept;

private:
    std::string msg_;
};

class ErrorMessage : public Exception {
public:
    explicit ErrorMessage(std::error_condition errc) noexcept;
    ErrorMessage(std::error_condition errc, const char* msg) noexcept;

    [[nodiscard]] std::error_condition GetValue() const noexcept;

private:
    std::error_condition errc_;
};

/// Actionable storage admission failure. It remains an ErrorMessage so existing
/// callers continue to observe ErrorEnum::ResourceLimit.
class ResourceLimitError : public ErrorMessage {
public:
    ResourceLimitError(const char* message, std::string resource, std::string purpose,
                       std::uint64_t required_bytes, std::uint64_t available_bytes,
                       std::uint64_t reserve_bytes) noexcept
        : ErrorMessage(ErrorEnum::ResourceLimit, message),
          resource_(std::move(resource)),
          purpose_(std::move(purpose)),
          required_bytes_(required_bytes),
          available_bytes_(available_bytes),
          reserve_bytes_(reserve_bytes) {}

    [[nodiscard]] const std::string& Resource() const noexcept {
        return resource_;
    }
    [[nodiscard]] const std::string& Purpose() const noexcept {
        return purpose_;
    }
    [[nodiscard]] std::uint64_t RequiredBytes() const noexcept {
        return required_bytes_;
    }
    [[nodiscard]] std::uint64_t AvailableBytes() const noexcept {
        return available_bytes_;
    }
    [[nodiscard]] std::uint64_t ReserveBytes() const noexcept {
        return reserve_bytes_;
    }

private:
    std::string resource_;
    std::string purpose_;
    std::uint64_t required_bytes_{0};
    std::uint64_t available_bytes_{0};
    std::uint64_t reserve_bytes_{0};
};

/// An encoded image is larger than the media pipeline can safely decode.
/// This is a media capability boundary, not a product upload quota.
class ImageInputLimitError : public ErrorMessage {
public:
    ImageInputLimitError(std::uint64_t actual_bytes, std::uint64_t limit_bytes) noexcept
        : ErrorMessage(ErrorEnum::ImageContentSizeInvalid,
                       "Encoded image exceeds the device decoding input limit"),
          actual_bytes_(actual_bytes),
          limit_bytes_(limit_bytes) {}

    [[nodiscard]] std::uint64_t ActualBytes() const noexcept {
        return actual_bytes_;
    }
    [[nodiscard]] std::uint64_t LimitBytes() const noexcept {
        return limit_bytes_;
    }

private:
    std::uint64_t actual_bytes_{0};
    std::uint64_t limit_bytes_{0};
};

/// The compressed file is small enough to transfer, but its decoded dimensions
/// exceed the largest frame supported by the media pipeline.
class ImageResolutionLimitError : public ErrorMessage {
public:
    ImageResolutionLimitError(std::uint64_t actual_pixels, std::uint64_t limit_pixels) noexcept
        : ErrorMessage(ErrorEnum::ImageContentSizeInvalid,
                       "Image resolution exceeds the device processing capability"),
          actual_pixels_(actual_pixels),
          limit_pixels_(limit_pixels) {}

    [[nodiscard]] std::uint64_t ActualPixels() const noexcept {
        return actual_pixels_;
    }
    [[nodiscard]] std::uint64_t LimitPixels() const noexcept {
        return limit_pixels_;
    }

private:
    std::uint64_t actual_pixels_{0};
    std::uint64_t limit_pixels_{0};
};

}  // namespace cosmo::util
