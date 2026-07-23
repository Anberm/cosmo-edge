/// @file IUploadStagingService.h
/// @brief Authenticated, bounded staging for inbound file uploads.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/ErrorCode.h"

namespace cosmo::service {

enum class UploadPurpose {
    kModelComponent = 0,
    kModelArchive,
    kVideo,
    kFaceImport,
    kAudio,
    kAlgorithm,
    kUpgrade,
};

[[nodiscard]] std::string_view UploadPurposeName(UploadPurpose purpose);
[[nodiscard]] bool ParseUploadPurpose(std::string_view value, UploadPurpose& purpose);

struct UploadBeginRequest {
    std::string principal;
    UploadPurpose purpose{UploadPurpose::kModelComponent};
    std::string original_name;
    std::uint64_t total_size{0};
    std::uint32_t total_chunks{0};
    std::string sha256;
    /// Optional R1 client-generated request identifier. This is an in-memory,
    /// principal-scoped alias only and is never used as a filesystem path.
    std::string client_request_id;
};

struct UploadSessionInfo {
    std::string upload_id;
    std::string original_name;
    std::uint64_t total_size{0};
    std::uint64_t received_size{0};
    std::uint64_t max_chunk_size{0};
    std::uint32_t total_chunks{0};
    std::uint32_t next_chunk_index{0};
    std::int64_t expires_at_unix_ms{0};
    bool complete{false};
    /// True only when the corresponding Begin call created this session.
    /// Alias replays always report false, even when no chunks have arrived.
    bool newly_created{false};
};

/// Move-only ownership of a completed staged file. Destroying the lease removes
/// the entire private session directory unless the consumer already moved the
/// file to its permanent location.
class StagedFileLease {
public:
    StagedFileLease() = default;
    ~StagedFileLease();

    StagedFileLease(const StagedFileLease&)            = delete;
    StagedFileLease& operator=(const StagedFileLease&) = delete;
    StagedFileLease(StagedFileLease&& other) noexcept;
    StagedFileLease& operator=(StagedFileLease&& other) noexcept;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] const std::string& Path() const noexcept;
    [[nodiscard]] const std::string& OriginalName() const noexcept;
    [[nodiscard]] std::uint64_t Size() const noexcept;
    [[nodiscard]] const std::string& Sha256() const noexcept;
    /// Revalidate both the pinned inode contents and the current session path.
    [[nodiscard]] bool Revalidate() const noexcept;
    /// Return a caller-owned read-only fd for the pinned, revalidated inode.
    /// The caller must close the returned descriptor. Returns -1 on failure.
    [[nodiscard]] int OpenVerified() const noexcept;

private:
    friend class UploadStagingServiceImpl;
    StagedFileLease(std::string path, int cleanup_root_fd, std::string cleanup_session_name,
                    std::uint64_t cleanup_session_device, std::uint64_t cleanup_session_inode,
                    std::string original_name, std::uint64_t size, std::string sha256,
                    int verified_payload_fd, std::uint64_t verified_payload_device,
                    std::uint64_t verified_payload_inode) noexcept;
    void Reset() noexcept;

    std::string path_;
    int cleanup_root_fd_{-1};
    std::string cleanup_session_name_;
    std::uint64_t cleanup_session_device_{0};
    std::uint64_t cleanup_session_inode_{0};
    std::string original_name_;
    std::uint64_t size_{0};
    std::string sha256_;
    int verified_payload_fd_{-1};
    std::uint64_t verified_payload_device_{0};
    std::uint64_t verified_payload_inode_{0};
};

class IUploadStagingService {
public:
    virtual ~IUploadStagingService() = default;

    virtual util::ErrorEnum Begin(const UploadBeginRequest& request, UploadSessionInfo& info) = 0;

    /// Append one already-staged multipart file. The source is read but remains
    /// owned by the HTTP request and is not removed by this service.
    virtual util::ErrorEnum AppendChunk(const std::string& principal, const std::string& upload_id,
                                        std::uint32_t chunk_index, const std::string& source_path,
                                        UploadSessionInfo& info) = 0;

    virtual util::ErrorEnum Complete(const std::string& principal, const std::string& upload_id,
                                     UploadSessionInfo& info) = 0;

    virtual util::ErrorEnum Consume(const std::string& principal, const std::string& upload_id,
                                    UploadPurpose expected_purpose, StagedFileLease& lease) = 0;

    /// Atomically consume canonical server IDs or principal-scoped R1 client
    /// aliases. No session is consumed if any input is invalid or duplicated.
    virtual util::ErrorEnum ConsumeMany(const std::string& principal,
                                        const std::vector<std::string>& upload_ids,
                                        UploadPurpose expected_purpose,
                                        std::vector<StagedFileLease>& leases) = 0;

    /// R1 compatibility only: accept an issued opaque reference or the exact
    /// previously registered payload path for the same principal and purpose.
    virtual util::ErrorEnum ConsumeLegacyPath(const std::string& principal, const std::string& legacy_path,
                                              UploadPurpose expected_purpose, StagedFileLease& lease) = 0;

    /// Atomically consume R1 opaque references or exact registered paths. No
    /// session is consumed when any input fails ownership, purpose, or state.
    virtual util::ErrorEnum ConsumeLegacyPaths(const std::string& principal,
                                               const std::vector<std::string>& legacy_paths,
                                               UploadPurpose expected_purpose,
                                               std::vector<StagedFileLease>& leases) = 0;

    /// R1 response compatibility. Returns an opaque upload:// reference, never
    /// a server filesystem path. New callers must use the canonical upload ID.
    virtual util::ErrorEnum GetLegacyPath(const std::string& principal, const std::string& upload_id,
                                          UploadPurpose expected_purpose, std::string& path) = 0;

    virtual util::ErrorEnum Cancel(const std::string& principal, const std::string& upload_id) = 0;
    virtual void CleanupExpired()                                                              = 0;
};

}  // namespace cosmo::service
