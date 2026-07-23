#pragma once

#include <string>

#include "service/path/IUploadStagingService.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::detail {

/// Return true only for the file created by the multipart parser for this
/// request. JSON/form metadata alone can never satisfy this check.
[[nodiscard]] bool IsCurrentMultipartFile(const RequestDispatchContext& context,
                                          const std::string& candidate_path);

/// Consume an existing completed upload or securely adopt the file generated
/// by the current multipart request into a one-shot staging session. The latter
/// is the bounded R1 compatibility path for old direct-upload clients.
util::ErrorEnum ClaimHttpUpload(const RequestDispatchContext& context, const std::string& candidate_path,
                                service::UploadPurpose purpose, service::StagedFileLease& lease);

}  // namespace cosmo::detail
