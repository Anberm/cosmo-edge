#include "api/StagedImageInput.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <limits>
#include <utility>

#include "media/EncodedImageInfo.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IUploadStagingService.h"
#include "util/Exception.h"
#include "util/VideoInfo.h"

namespace cosmo::detail {
namespace {

    bool ReadPinnedImage(service::StagedFileLease& lease, std::vector<std::uint8_t>& image) {
        image.clear();
        const auto size  = lease.Size();
        const auto limit = MaxEncodedImageInputBytes();
        if (size == 0) {
            return false;
        }
        if (size > limit) {
            throw util::ImageInputLimitError(size, limit);
        }
        if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return false;
        }

        const int fd = lease.OpenVerified();
        if (fd < 0) {
            return false;
        }
        image.resize(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        while (offset < image.size()) {
            const auto requested = std::min<std::size_t>(image.size() - offset, 64 * 1024);
            const auto read_size = pread(fd, image.data() + offset, requested, static_cast<off_t>(offset));
            if (read_size < 0 && errno == EINTR) {
                continue;
            }
            if (read_size <= 0) {
                close(fd);
                image.clear();
                return false;
            }
            offset += static_cast<std::size_t>(read_size);
        }
        if (close(fd) != 0) {
            image.clear();
            return false;
        }
        media::EncodedImageInfo image_info;
        if (!media::InspectEncodedImage(image, image_info)) {
            image.clear();
            return false;
        }
        if (!media::IsEncodedImageWithinFrameCapability(image_info)) {
            throw util::ImageResolutionLimitError(image_info.pixel_count, media::MaxDecodedImagePixels());
        }
        return true;
    }

}  // namespace

std::uint64_t MaxEncodedImageInputBytes() noexcept {
    return static_cast<std::uint64_t>(media::kVideoFrameMaxSize);
}

util::ErrorEnum ConsumeStagedImages(const RequestDispatchContext& context,
                                    const std::vector<std::string>& upload_ids,
                                    std::vector<std::vector<std::uint8_t>>& images) {
    images.clear();
    if (context.transport != RequestTransport::kHttp || context.principal.empty() || upload_ids.empty()) {
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<service::StagedFileLease> leases;
    auto error = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().ConsumeMany(
        context.principal, upload_ids, service::UploadPurpose::kImage, leases);
    if (error != util::ErrorEnum::Success) {
        return error;
    }

    images.resize(leases.size());
    for (std::size_t index = 0; index < leases.size(); ++index) {
        if (!ReadPinnedImage(leases[index], images[index])) {
            images.clear();
            return util::ErrorEnum::FileAnalysisFailed;
        }
    }
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::detail
