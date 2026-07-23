#include "api/HttpUploadClaim.h"

#include <system_error>

#include "service/detail/ServiceRegistry.h"

namespace cosmo::detail {

bool IsCurrentMultipartFile(const RequestDispatchContext& context, const std::string& candidate_path) {
    return context.transport == RequestTransport::kHttp && !context.principal.empty() &&
           !context.multipart_file_path.empty() && !context.multipart_file_name.empty() &&
           context.multipart_file_size > 0 && candidate_path == context.multipart_file_path;
}

util::ErrorEnum ClaimHttpUpload(const RequestDispatchContext& context, const std::string& candidate_path,
                                service::UploadPurpose purpose, service::StagedFileLease& lease) {
    if (context.transport != RequestTransport::kHttp || context.principal.empty() || candidate_path.empty()) {
        return util::ErrorEnum::InvalidParam;
    }

    auto& staging = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>();
    auto error    = staging.ConsumeLegacyPath(context.principal, candidate_path, purpose, lease);
    if (error == util::ErrorEnum::Success || error != util::ErrorEnum::NoSuchId) {
        return error;
    }

    if (!IsCurrentMultipartFile(context, candidate_path)) {
        return error;
    }

    service::UploadBeginRequest request;
    request.principal     = context.principal;
    request.purpose       = purpose;
    request.original_name = context.multipart_file_name;
    request.total_size    = context.multipart_file_size;
    request.total_chunks  = 1;

    service::UploadSessionInfo info;
    error = staging.Begin(request, info);
    if (error != util::ErrorEnum::Success) {
        return error;
    }
    const auto upload_id = info.upload_id;

    bool cancel_session = true;
    error = staging.AppendChunk(context.principal, upload_id, 0, context.multipart_file_path, info);
    if (error == util::ErrorEnum::Success) {
        error = staging.Complete(context.principal, upload_id, info);
    }
    if (error == util::ErrorEnum::Success) {
        error = staging.Consume(context.principal, upload_id, purpose, lease);
    }
    if (error == util::ErrorEnum::Success) {
        cancel_session = false;
    }
    if (cancel_session) {
        staging.Cancel(context.principal, upload_id);
    }
    return error;
}

}  // namespace cosmo::detail
