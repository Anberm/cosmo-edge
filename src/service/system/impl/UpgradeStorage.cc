#include "service/system/impl/UpgradeStorage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {
namespace fs = std::filesystem;
namespace {

    struct EventMediaFile {
        fs::path path;
        std::uint64_t size{0};
    };

    std::uint64_t SaturatingAdd(std::uint64_t lhs, std::uint64_t rhs) {
        const auto max_value = std::numeric_limits<std::uint64_t>::max();
        return rhs > max_value - lhs ? max_value : lhs + rhs;
    }

    util::ErrorEnum CollectEventMediaFiles(const fs::path& event_root,
                                           std::vector<EventMediaFile>& files,
                                           std::uint64_t& total_bytes) {
        files.clear();
        total_bytes = 0;
        std::error_code ec;
        const auto root_status = fs::symlink_status(event_root, ec);
        if (ec == std::errc::no_such_file_or_directory || !fs::exists(root_status)) {
            return util::ErrorEnum::Success;
        }
        if (ec || !fs::is_directory(root_status)) {
            return util::ErrorEnum::SysErr;
        }

        fs::recursive_directory_iterator entry(event_root, fs::directory_options::none, ec), end;
        if (ec) {
            return util::ErrorEnum::SysErr;
        }
        for (; entry != end; entry.increment(ec)) {
            if (ec) {
                return util::ErrorEnum::SysErr;
            }
            const auto file_status = entry->symlink_status(ec);
            if (ec) {
                return util::ErrorEnum::SysErr;
            }
            if (!fs::is_regular_file(file_status) || !IsEventMediaFile(entry->path())) {
                continue;
            }
            const auto size = entry->file_size(ec);
            if (ec) {
                return util::ErrorEnum::SysErr;
            }
            files.push_back({entry->path(), size});
            total_bytes = SaturatingAdd(total_bytes, size);
        }
        return ec ? util::ErrorEnum::SysErr : util::ErrorEnum::Success;
    }

}  // namespace

std::uint64_t RequiredUpgradeSpaceBytes(std::uint64_t package_size_bytes) {
    const auto max_value = std::numeric_limits<std::uint64_t>::max();
    if (package_size_bytes > max_value / 5) {
        return max_value;
    }
    const auto multiplied = package_size_bytes * 5;
    return multiplied / 2 + multiplied % 2;
}

bool IsEventMediaFile(const fs::path& path) {
    static constexpr std::array<const char*, 16> kMediaExtensions{
        ".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif", ".mp4", ".avi",
        ".mov", ".mkv",  ".webm", ".flv", ".ts",  ".m4v", ".h264", ".h265"};
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return std::find(kMediaExtensions.begin(), kMediaExtensions.end(), extension) !=
           kMediaExtensions.end();
}

EventMediaCleanupResult DeleteEventMediaFiles(const fs::path& event_root) {
    EventMediaCleanupResult result;
    std::vector<EventMediaFile> files;
    std::uint64_t total_bytes = 0;
    result.error = CollectEventMediaFiles(event_root, files, total_bytes);
    if (result.error != util::ErrorEnum::Success) {
        return result;
    }

    for (const auto& file : files) {
        std::error_code ec;
        if (fs::remove(file.path, ec)) {
            ++result.deleted_files;
            result.deleted_bytes = SaturatingAdd(result.deleted_bytes, file.size);
        } else if (ec) {
            LOG_WARN("Cannot remove event media {}: {}", file.path.string(), ec.message());
            result.error = util::ErrorEnum::SysErr;
        }
    }
    return result;
}

util::ErrorEnum CheckUpgradeStorage(const fs::path& data_root, const fs::path& event_root,
                                    std::uint64_t package_size_bytes, bool cleanup_event_media,
                                    UpgradeSpaceStatus& status) {
    status = {};
    if (package_size_bytes == 0 || data_root.empty() || event_root.empty() ||
        !cosmo::path::IsWithinRoot(data_root.string(), event_root.string())) {
        return util::ErrorEnum::InvalidParam;
    }

    status.required_bytes = RequiredUpgradeSpaceBytes(package_size_bytes);
    std::error_code ec;
    auto space = fs::space(data_root, ec);
    if (ec) {
        return util::ErrorEnum::SysErr;
    }
    status.available_bytes = space.available;
    status.sufficient = status.available_bytes >= status.required_bytes;
    if (status.sufficient) {
        return util::ErrorEnum::Success;
    }

    if (!cleanup_event_media) {
        std::vector<EventMediaFile> media_files;
        return CollectEventMediaFiles(event_root, media_files, status.event_media_bytes);
    }

    const auto cleanup = DeleteEventMediaFiles(event_root);
    status.event_media_bytes   = cleanup.deleted_bytes;
    status.deleted_media_bytes = cleanup.deleted_bytes;
    status.deleted_media_files = cleanup.deleted_files;
    if (cleanup.error != util::ErrorEnum::Success) {
        return cleanup.error;
    }

    space = fs::space(data_root, ec);
    if (ec) {
        return util::ErrorEnum::SysErr;
    }
    status.available_bytes = space.available;
    status.sufficient = status.available_bytes >= status.required_bytes;
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::service
