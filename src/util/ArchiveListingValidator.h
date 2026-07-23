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

struct ArchiveListingInspection {
    size_t entry_count{0};
    std::uintmax_t largest_file_bytes{0};
    std::uintmax_t total_bytes{0};
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

/// Strictly inspect paths, entry types, duplicate names, and entry count while
/// reporting the declared uncompressed resource requirement. No arbitrary byte
/// ceiling is imposed; callers compare the result with a runtime resource budget.
[[nodiscard]] bool InspectArchiveListingOutput(std::string_view listing, ArchiveListingFormat format,
                                               size_t max_entries, ArchiveListingInspection& inspection);
[[nodiscard]] bool InspectArchiveListingFile(const std::string& archive_path, ArchiveListingFormat format,
                                             size_t max_entries, ArchiveListingInspection& inspection);

}  // namespace cosmo::util
