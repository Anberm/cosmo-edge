/// @file ArchiveListingValidator.h
/// @brief Strict validation for verbose ZIP and tar archive listings.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cosmo::util {

enum class ArchiveListingFormat {
    kZipVerbose,
    kTarVerbose,
};

struct ArchiveListingLimits {
    size_t max_entries;
    std::uintmax_t max_file_bytes;
    std::uintmax_t max_total_bytes;
};

/// Validate already-captured output from `unzip -Z -l` or
/// `tar -tzvf --quoting-style=escape`. Every output line must match the
/// selected format. Member names are normalized only for common leading
/// `./` and directory trailing-slash forms before duplicate detection.
[[nodiscard]] bool ValidateArchiveListingOutput(std::string_view listing, ArchiveListingFormat format,
                                                const ArchiveListingLimits& limits);

/// Produce and strictly validate a verbose archive listing without extracting
/// the archive.
[[nodiscard]] bool ValidateArchiveListingFile(const std::string& archive_path, ArchiveListingFormat format,
                                              const ArchiveListingLimits& limits);

}  // namespace cosmo::util
