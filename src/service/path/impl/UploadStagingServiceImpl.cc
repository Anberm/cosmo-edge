#include "service/path/impl/UploadStagingServiceImpl.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "cryptopp/sha.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace fs = std::filesystem;

namespace cosmo::service {
namespace {

    constexpr mode_t kPrivateFileMode   = 0600;
    constexpr mode_t kCompletedFileMode = 0400;

    bool CloseChecked(int fd) {
        if (fd < 0) {
            return true;
        }
        return close(fd) == 0 || errno == EINTR;
    }

    class ScopedFd {
    public:
        explicit ScopedFd(int fd = -1) noexcept : fd_(fd) {}

        ~ScopedFd() {
            CloseChecked(fd_);
        }

        ScopedFd(const ScopedFd&)            = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        ScopedFd(ScopedFd&& other) noexcept : fd_(other.Release()) {}

        ScopedFd& operator=(ScopedFd&& other) noexcept {
            if (this != &other) {
                CloseChecked(fd_);
                fd_ = other.Release();
            }
            return *this;
        }

        [[nodiscard]] int Get() const noexcept {
            return fd_;
        }

        [[nodiscard]] int Release() noexcept {
            const int fd = fd_;
            fd_          = -1;
            return fd;
        }

    private:
        int fd_{-1};
    };

    bool ComputeSha256Pinned(int fd, std::string& digest) {
        digest.clear();
        if (fd < 0) {
            return false;
        }
        CryptoPP::SHA256 sha;
        std::array<char, 64 * 1024> buffer{};
        off_t offset = 0;
        while (true) {
            const auto count = pread(fd, buffer.data(), buffer.size(), offset);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0) {
                return false;
            }
            if (count == 0) {
                break;
            }
            sha.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()),
                       static_cast<std::size_t>(count));
            offset += count;
        }
        std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest_bytes{};
        sha.Final(digest_bytes.data());
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (auto byte : digest_bytes) {
            output << std::setw(2) << static_cast<unsigned int>(byte);
        }
        digest = output.str();
        return true;
    }

    bool SameIdentity(const struct stat& status, std::uint64_t device, std::uint64_t inode) {
        return static_cast<std::uint64_t>(status.st_dev) == device &&
               static_cast<std::uint64_t>(status.st_ino) == inode;
    }

    bool IsAsciiAlphaNumeric(unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    bool IsUuidComponent(const std::string& value) {
        if (value.size() != 36) {
            return false;
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (value[i] != '-') {
                    return false;
                }
            } else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f') ||
                         (value[i] >= 'A' && value[i] <= 'F'))) {
                return false;
            }
        }
        return true;
    }

    bool RemoveTreeContentsAt(int directory_fd) {
        const int iterator_fd = fcntl(directory_fd, F_DUPFD_CLOEXEC, 0);
        if (iterator_fd < 0) {
            return false;
        }
        DIR* directory = fdopendir(iterator_fd);
        if (directory == nullptr) {
            CloseChecked(iterator_fd);
            return false;
        }

        bool success = true;
        errno        = 0;
        while (auto* entry = readdir(directory)) {
            const std::string name(entry->d_name);
            if (name == "." || name == "..") {
                continue;
            }

            struct stat status {};
            if (fstatat(directory_fd, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
                success = false;
                continue;
            }
            if (S_ISDIR(status.st_mode)) {
                const int child_fd =
                    openat(directory_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                if (child_fd < 0) {
                    success = false;
                    continue;
                }
                const bool child_success = RemoveTreeContentsAt(child_fd);
                CloseChecked(child_fd);
                if (!child_success || unlinkat(directory_fd, name.c_str(), AT_REMOVEDIR) != 0) {
                    success = false;
                }
            } else if (unlinkat(directory_fd, name.c_str(), 0) != 0) {
                success = false;
            }
            errno = 0;
        }
        if (errno != 0) {
            success = false;
        }
        if (closedir(directory) != 0) {
            success = false;
        }
        return success;
    }

    bool RemoveSessionAt(int root_fd, const std::string& session_name, std::uint64_t expected_device,
                         std::uint64_t expected_inode) {
        if (root_fd < 0 || !IsUuidComponent(session_name)) {
            return false;
        }
        const int session_fd =
            openat(root_fd, session_name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (session_fd < 0) {
            return errno == ENOENT;
        }
        struct stat status {};
        const bool identity_matches = fstat(session_fd, &status) == 0 && S_ISDIR(status.st_mode) &&
                                      SameIdentity(status, expected_device, expected_inode);
        bool success = identity_matches && RemoveTreeContentsAt(session_fd);
        CloseChecked(session_fd);
        if (success && unlinkat(root_fd, session_name.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
            success = false;
        }
        return success;
    }

    std::uint64_t ReservationLimit(const UploadStagingConfig& config, int root_fd) {
        struct statvfs status {};
        if (root_fd < 0 || fstatvfs(root_fd, &status) != 0) {
            return 0;
        }
        if (status.f_frsize == 0) {
            return 0;
        }
        const auto max_value = std::numeric_limits<std::uint64_t>::max();
        const auto available = status.f_bavail > max_value / status.f_frsize
                                   ? max_value
                                   : static_cast<std::uint64_t>(status.f_bavail) * status.f_frsize;
        return std::min(config.max_reserved_bytes, available / 5);
    }

}  // namespace

std::string_view UploadPurposeName(UploadPurpose purpose) {
    switch (purpose) {
        case UploadPurpose::kModelComponent:
            return "model-component";
        case UploadPurpose::kModelArchive:
            return "model-archive";
        case UploadPurpose::kVideo:
            return "video";
        case UploadPurpose::kFaceImport:
            return "face-import";
        case UploadPurpose::kAudio:
            return "audio";
        case UploadPurpose::kAlgorithm:
            return "algorithm";
        case UploadPurpose::kUpgrade:
            return "upgrade";
    }
    return {};
}

bool ParseUploadPurpose(std::string_view value, UploadPurpose& purpose) {
    for (auto candidate : {UploadPurpose::kModelComponent, UploadPurpose::kModelArchive,
                           UploadPurpose::kVideo, UploadPurpose::kFaceImport, UploadPurpose::kAudio,
                           UploadPurpose::kAlgorithm, UploadPurpose::kUpgrade}) {
        if (value == UploadPurposeName(candidate)) {
            purpose = candidate;
            return true;
        }
    }
    return false;
}

StagedFileLease::StagedFileLease(std::string path, int cleanup_root_fd, std::string cleanup_session_name,
                                 std::uint64_t cleanup_session_device, std::uint64_t cleanup_session_inode,
                                 std::string original_name, std::uint64_t size, std::string sha256,
                                 int verified_payload_fd, std::uint64_t verified_payload_device,
                                 std::uint64_t verified_payload_inode) noexcept
    : path_(std::move(path)),
      cleanup_root_fd_(cleanup_root_fd),
      cleanup_session_name_(std::move(cleanup_session_name)),
      cleanup_session_device_(cleanup_session_device),
      cleanup_session_inode_(cleanup_session_inode),
      original_name_(std::move(original_name)),
      size_(size),
      sha256_(std::move(sha256)),
      verified_payload_fd_(verified_payload_fd),
      verified_payload_device_(verified_payload_device),
      verified_payload_inode_(verified_payload_inode) {}

StagedFileLease::~StagedFileLease() {
    Reset();
}

StagedFileLease::StagedFileLease(StagedFileLease&& other) noexcept
    : path_(std::move(other.path_)),
      cleanup_root_fd_(other.cleanup_root_fd_),
      cleanup_session_name_(std::move(other.cleanup_session_name_)),
      cleanup_session_device_(other.cleanup_session_device_),
      cleanup_session_inode_(other.cleanup_session_inode_),
      original_name_(std::move(other.original_name_)),
      size_(other.size_),
      sha256_(std::move(other.sha256_)),
      verified_payload_fd_(other.verified_payload_fd_),
      verified_payload_device_(other.verified_payload_device_),
      verified_payload_inode_(other.verified_payload_inode_) {
    other.path_.clear();
    other.cleanup_root_fd_ = -1;
    other.cleanup_session_name_.clear();
    other.cleanup_session_device_ = 0;
    other.cleanup_session_inode_  = 0;
    other.original_name_.clear();
    other.size_ = 0;
    other.sha256_.clear();
    other.verified_payload_fd_     = -1;
    other.verified_payload_device_ = 0;
    other.verified_payload_inode_  = 0;
}

StagedFileLease& StagedFileLease::operator=(StagedFileLease&& other) noexcept {
    if (this != &other) {
        Reset();
        path_                    = std::move(other.path_);
        cleanup_root_fd_         = other.cleanup_root_fd_;
        cleanup_session_name_    = std::move(other.cleanup_session_name_);
        cleanup_session_device_  = other.cleanup_session_device_;
        cleanup_session_inode_   = other.cleanup_session_inode_;
        original_name_           = std::move(other.original_name_);
        size_                    = other.size_;
        sha256_                  = std::move(other.sha256_);
        verified_payload_fd_     = other.verified_payload_fd_;
        verified_payload_device_ = other.verified_payload_device_;
        verified_payload_inode_  = other.verified_payload_inode_;
        other.path_.clear();
        other.cleanup_root_fd_ = -1;
        other.cleanup_session_name_.clear();
        other.cleanup_session_device_ = 0;
        other.cleanup_session_inode_  = 0;
        other.original_name_.clear();
        other.size_ = 0;
        other.sha256_.clear();
        other.verified_payload_fd_     = -1;
        other.verified_payload_device_ = 0;
        other.verified_payload_inode_  = 0;
    }
    return *this;
}

bool StagedFileLease::Valid() const noexcept {
    return !path_.empty() && cleanup_root_fd_ >= 0 && !cleanup_session_name_.empty() &&
           verified_payload_fd_ >= 0;
}

const std::string& StagedFileLease::Path() const noexcept {
    return path_;
}

const std::string& StagedFileLease::OriginalName() const noexcept {
    return original_name_;
}

std::uint64_t StagedFileLease::Size() const noexcept {
    return size_;
}

const std::string& StagedFileLease::Sha256() const noexcept {
    return sha256_;
}

bool StagedFileLease::Revalidate() const noexcept {
    try {
        if (!Valid() || original_name_.empty() || sha256_.empty()) {
            return false;
        }
        struct stat pinned_status {};
        std::string pinned_digest;
        if (fstat(verified_payload_fd_, &pinned_status) != 0 || !S_ISREG(pinned_status.st_mode) ||
            pinned_status.st_nlink != 1 || pinned_status.st_uid != geteuid() ||
            (pinned_status.st_mode & 0077) != 0 || pinned_status.st_size < 0 ||
            static_cast<std::uint64_t>(pinned_status.st_size) != size_ ||
            !SameIdentity(pinned_status, verified_payload_device_, verified_payload_inode_) ||
            !ComputeSha256Pinned(verified_payload_fd_, pinned_digest) || pinned_digest != sha256_) {
            return false;
        }

        const int session_fd = openat(cleanup_root_fd_, cleanup_session_name_.c_str(),
                                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (session_fd < 0) {
            return false;
        }
        struct stat session_status {};
        if (fstat(session_fd, &session_status) != 0 || !S_ISDIR(session_status.st_mode) ||
            session_status.st_uid != geteuid() || (session_status.st_mode & 0077) != 0 ||
            !SameIdentity(session_status, cleanup_session_device_, cleanup_session_inode_)) {
            CloseChecked(session_fd);
            return false;
        }
        const int path_fd = openat(session_fd, original_name_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        CloseChecked(session_fd);
        if (path_fd < 0) {
            return false;
        }
        struct stat path_status {};
        const bool valid = fstat(path_fd, &path_status) == 0 && S_ISREG(path_status.st_mode) &&
                           path_status.st_nlink == 1 && path_status.st_uid == geteuid() &&
                           (path_status.st_mode & 0077) == 0 && path_status.st_size >= 0 &&
                           static_cast<std::uint64_t>(path_status.st_size) == size_ &&
                           SameIdentity(path_status, verified_payload_device_, verified_payload_inode_);
        CloseChecked(path_fd);
        if (!valid) {
            return false;
        }

        const int textual_path_fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (textual_path_fd < 0) {
            return false;
        }
        struct stat textual_path_status {};
        const bool textual_path_valid =
            fstat(textual_path_fd, &textual_path_status) == 0 && S_ISREG(textual_path_status.st_mode) &&
            textual_path_status.st_nlink == 1 && textual_path_status.st_uid == geteuid() &&
            (textual_path_status.st_mode & 0077) == 0 && textual_path_status.st_size >= 0 &&
            static_cast<std::uint64_t>(textual_path_status.st_size) == size_ &&
            SameIdentity(textual_path_status, verified_payload_device_, verified_payload_inode_);
        CloseChecked(textual_path_fd);
        return textual_path_valid;
    } catch (...) {
        return false;
    }
}

int StagedFileLease::OpenVerified() const noexcept {
    if (!Revalidate()) {
        return -1;
    }
    const int session_fd = openat(cleanup_root_fd_, cleanup_session_name_.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (session_fd < 0) {
        return -1;
    }
    const int fd = openat(session_fd, original_name_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CloseChecked(session_fd);
    struct stat status {};
    std::string digest;
    if (fd < 0 || fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        status.st_uid != geteuid() || (status.st_mode & 0077) != 0 || status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) != size_ ||
        !SameIdentity(status, verified_payload_device_, verified_payload_inode_) ||
        !ComputeSha256Pinned(fd, digest) || digest != sha256_ || lseek(fd, 0, SEEK_SET) < 0) {
        CloseChecked(fd);
        return -1;
    }
    return fd;
}

void StagedFileLease::Reset() noexcept {
    CloseChecked(verified_payload_fd_);
    verified_payload_fd_ = -1;
    if (cleanup_root_fd_ >= 0) {
        if (!RemoveSessionAt(cleanup_root_fd_, cleanup_session_name_, cleanup_session_device_,
                             cleanup_session_inode_)) {
            LOG_WARN("Failed to safely clean consumed upload session {}", cleanup_session_name_);
        }
        CloseChecked(cleanup_root_fd_);
    }
    path_.clear();
    cleanup_root_fd_ = -1;
    cleanup_session_name_.clear();
    cleanup_session_device_ = 0;
    cleanup_session_inode_  = 0;
    original_name_.clear();
    size_ = 0;
    sha256_.clear();
    verified_payload_device_ = 0;
    verified_payload_inode_  = 0;
}

UploadStagingServiceImpl::UploadStagingServiceImpl()
    : UploadStagingServiceImpl(UploadStagingConfig{
          (fs::path(cosmo::path::GetUploadPath()) / "sessions").string(),
      }) {}

UploadStagingServiceImpl::UploadStagingServiceImpl(UploadStagingConfig config, NowFunction now)
    : config_(std::move(config)), now_(std::move(now)) {
    if (config_.root_path.empty() || config_.root_path == "/" || config_.max_total_size == 0 ||
        config_.max_chunk_size == 0 || config_.max_chunks == 0 || config_.max_sessions_per_principal == 0 ||
        config_.max_sessions == 0 || config_.session_ttl <= std::chrono::milliseconds::zero() ||
        config_.max_session_lifetime < config_.session_ttl ||
        config_.cleanup_interval < std::chrono::milliseconds::zero() || !now_) {
        throw std::invalid_argument("invalid upload staging configuration");
    }
    if (config_.cleanup_interval > config_.session_ttl) {
        config_.cleanup_interval = config_.session_ttl;
    }

    std::error_code ec;
    fs::create_directories(config_.root_path, ec);
    if (ec) {
        throw std::runtime_error("cannot create upload staging root: " + ec.message());
    }
    const auto absolute_root = fs::absolute(config_.root_path, ec).lexically_normal();
    if (ec || absolute_root == absolute_root.root_path()) {
        throw std::runtime_error("cannot normalize upload staging root");
    }
    config_.root_path = absolute_root.string();
    root_fd_          = open(config_.root_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat root_stat {};
    struct stat root_path_stat {};
    if (root_fd_ < 0 || fstat(root_fd_, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) ||
        root_stat.st_uid != geteuid() || lstat(config_.root_path.c_str(), &root_path_stat) != 0 ||
        !S_ISDIR(root_path_stat.st_mode) || S_ISLNK(root_path_stat.st_mode) ||
        root_path_stat.st_dev != root_stat.st_dev || root_path_stat.st_ino != root_stat.st_ino ||
        fchmod(root_fd_, 0700) != 0) {
        CloseChecked(root_fd_);
        root_fd_ = -1;
        throw std::runtime_error("cannot securely open upload staging root");
    }
    root_device_ = static_cast<std::uint64_t>(root_stat.st_dev);
    root_inode_  = static_cast<std::uint64_t>(root_stat.st_ino);
    CleanupOrphans();
    try {
        StartCleanupThread();
    } catch (...) {
        CloseChecked(root_fd_);
        root_fd_ = -1;
        throw;
    }
}

UploadStagingServiceImpl::~UploadStagingServiceImpl() {
    StopCleanupThread();
    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions.reserve(sessions_.size() + consuming_sessions_.size());
        const auto retire_sessions = [&sessions](const SessionMap& session_map) {
            for (const auto& [id, session] : session_map) {
                (void)id;
                std::lock_guard<std::mutex> session_lock(session->mutex);
                session->state = SessionState::kRetired;
                sessions.push_back(session);
            }
        };
        retire_sessions(sessions_);
        retire_sessions(consuming_sessions_);
        sessions_.clear();
        consuming_sessions_.clear();
        client_request_aliases_.clear();
        principal_session_counts_.clear();
        active_session_count_ = 0;
        reserved_bytes_       = 0;
    }
    RemoveSessions(sessions);
    CloseChecked(root_fd_);
    root_fd_ = -1;
}

util::ErrorEnum UploadStagingServiceImpl::Begin(const UploadBeginRequest& request, UploadSessionInfo& info) {
    info = {};
    CleanupExpired();

    std::string original_name;
    if (request.principal.empty() || request.principal.size() > 256 || !IsPurposeValid(request.purpose) ||
        !NormalizeOriginalName(request.original_name, original_name) || request.total_size == 0 ||
        request.total_size > config_.max_total_size || request.total_chunks == 0 ||
        request.total_chunks > config_.max_chunks || request.total_chunks > request.total_size ||
        !IsSha256Valid(request.sha256) || !IsClientRequestIdValid(request.client_request_id)) {
        return request.total_size > config_.max_total_size ? util::ErrorEnum::FileSizeBig
                                                           : util::ErrorEnum::InvalidParam;
    }
    if (!ValidateRootPath()) {
        return util::ErrorEnum::FileOpenFailed;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    if (!request.client_request_id.empty()) {
        // Canonical IDs and client aliases share the public identifier surface.
        // Keep their namespaces disjoint so an alias can never redirect an
        // operation intended for an existing canonical session.
        if (sessions_.find(request.client_request_id) != sessions_.end() ||
            consuming_sessions_.find(request.client_request_id) != consuming_sessions_.end()) {
            return util::ErrorEnum::InvalidParam;
        }
        const auto alias_key = std::make_pair(request.principal, request.client_request_id);
        const auto alias_it  = client_request_aliases_.find(alias_key);
        if (alias_it != client_request_aliases_.end()) {
            const auto session_it = sessions_.find(alias_it->second);
            if (session_it == sessions_.end()) {
                return util::ErrorEnum::NoSuchId;
            }
            const auto& session = session_it->second;
            std::lock_guard<std::mutex> session_lock(session->mutex);
            if (!ImmutableMetadataMatches(*session, request, original_name)) {
                return util::ErrorEnum::InvalidParam;
            }
            info = MakeInfo(*session);
            return util::ErrorEnum::Success;
        }
    }
    const auto count_it        = principal_session_counts_.find(request.principal);
    const auto principal_count = count_it == principal_session_counts_.end() ? 0 : count_it->second;
    if (active_session_count_ >= config_.max_sessions ||
        principal_count >= config_.max_sessions_per_principal) {
        return util::ErrorEnum::ResourceLimit;
    }
    const auto minimum_chunks = (request.total_size - 1) / config_.max_chunk_size + 1;
    if (request.total_chunks < minimum_chunks) {
        return util::ErrorEnum::InvalidParam;
    }
    const auto reservation_limit = ReservationLimit(config_, root_fd_);
    if (request.total_size > reservation_limit || reserved_bytes_ > reservation_limit - request.total_size) {
        return util::ErrorEnum::ResourceLimit;
    }

    std::string upload_id;
    int session_fd = -1;
    struct stat session_stat {};
    for (int attempt = 0; attempt < 10; ++attempt) {
        upload_id = cosmo::util::GenerateUUID();
        const bool collides_with_alias =
            std::any_of(client_request_aliases_.begin(), client_request_aliases_.end(),
                        [&upload_id](const auto& entry) { return entry.first.second == upload_id; });
        if (sessions_.find(upload_id) != sessions_.end() ||
            consuming_sessions_.find(upload_id) != consuming_sessions_.end() ||
            upload_id == request.client_request_id || collides_with_alias) {
            continue;
        }
        if (mkdirat(root_fd_, upload_id.c_str(), 0700) != 0) {
            if (errno == EEXIST) {
                upload_id.clear();
                continue;
            }
            LOG_WARN("Cannot create upload session directory: {}", std::strerror(errno));
            return util::ErrorEnum::FileOpenFailed;
        }
        session_fd = openat(root_fd_, upload_id.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (session_fd >= 0 && fstat(session_fd, &session_stat) == 0 && S_ISDIR(session_stat.st_mode)) {
            break;
        }
        CloseChecked(session_fd);
        session_fd = -1;
        unlinkat(root_fd_, upload_id.c_str(), AT_REMOVEDIR);
        upload_id.clear();
    }
    if (upload_id.empty() || session_fd < 0) {
        return util::ErrorEnum::FileOpenFailed;
    }

    const fs::path session_dir  = fs::path(config_.root_path) / upload_id;
    const fs::path payload_path = session_dir / original_name;
    int fd = openat(session_fd, original_name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    kPrivateFileMode);
    struct stat payload_stat {};
    const bool payload_valid = fd >= 0 && fstat(fd, &payload_stat) == 0 && S_ISREG(payload_stat.st_mode) &&
                               payload_stat.st_nlink == 1;
    const bool payload_closed = fd < 0 || CloseChecked(fd);
    if (!payload_valid || !payload_closed) {
        if (fd >= 0) {
            unlinkat(session_fd, original_name.c_str(), 0);
        }
        CloseChecked(session_fd);
        unlinkat(root_fd_, upload_id.c_str(), AT_REMOVEDIR);
        return util::ErrorEnum::FileOpenFailed;
    }
    CloseChecked(session_fd);

    auto session               = std::make_shared<Session>();
    session->principal         = request.principal;
    session->purpose           = request.purpose;
    session->upload_id         = upload_id;
    session->client_request_id = request.client_request_id;
    session->original_name     = std::move(original_name);
    session->session_dir       = session_dir.string();
    session->payload_path      = payload_path.string();
    session->sha256            = request.sha256;
    session->total_size        = request.total_size;
    session->total_chunks      = request.total_chunks;
    session->session_device    = static_cast<std::uint64_t>(session_stat.st_dev);
    session->session_inode     = static_cast<std::uint64_t>(session_stat.st_ino);
    session->payload_device    = static_cast<std::uint64_t>(payload_stat.st_dev);
    session->payload_inode     = static_cast<std::uint64_t>(payload_stat.st_ino);
    session->chunks.resize(request.total_chunks);
    const auto created_at        = now_();
    session->expires_at          = created_at + config_.session_ttl;
    session->absolute_expires_at = created_at + config_.max_session_lifetime;
    const auto system_created_at = std::chrono::system_clock::now();
    session->expires_at_unix_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      (system_created_at + config_.session_ttl).time_since_epoch())
                                      .count();
    session->absolute_expires_at_unix_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            (system_created_at + config_.max_session_lifetime).time_since_epoch())
            .count();

    sessions_.emplace(upload_id, session);
    if (!request.client_request_id.empty()) {
        client_request_aliases_.emplace(std::make_pair(request.principal, request.client_request_id),
                                        upload_id);
    }
    ++principal_session_counts_[request.principal];
    ++active_session_count_;
    reserved_bytes_ += request.total_size;
    info               = MakeInfo(*session);
    info.newly_created = true;
    return util::ErrorEnum::Success;
}

util::ErrorEnum UploadStagingServiceImpl::AppendChunk(const std::string& principal,
                                                      const std::string& upload_id, std::uint32_t chunk_index,
                                                      const std::string& source_path,
                                                      UploadSessionInfo& info) {
    const std::string request_principal = principal;
    const std::string identifier        = upload_id;
    const std::string chunk_path        = source_path;
    info                                = {};
    CleanupExpired();
    auto session = FindSession(request_principal, identifier);
    if (!session) {
        return util::ErrorEnum::NoSuchId;
    }

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->principal != request_principal) {
        return util::ErrorEnum::AuthFailed;
    }
    if (session->state == SessionState::kRetired) {
        return util::ErrorEnum::NoSuchId;
    }
    if (chunk_path.empty() || chunk_index >= session->total_chunks) {
        return util::ErrorEnum::InvalidParam;
    }
    if (chunk_index < session->next_chunk_index) {
        std::uint64_t source_size = 0;
        std::string source_sha256;
        if (!InspectRegularFile(chunk_path, config_.max_chunk_size, source_size, source_sha256)) {
            return util::ErrorEnum::FileOpenFailed;
        }
        const auto& record = session->chunks[chunk_index];
        if (record.size == source_size && !record.sha256.empty() && record.sha256 == source_sha256) {
            int payload_fd = -1;
            struct stat payload_status {};
            const bool payload_valid =
                OpenValidatedPayload(*session, O_RDONLY, payload_fd) &&
                fstat(payload_fd, &payload_status) == 0 && payload_status.st_size >= 0 &&
                static_cast<std::uint64_t>(payload_status.st_size) == session->received_size;
            CloseChecked(payload_fd);
            if (!payload_valid) {
                return util::ErrorEnum::FileAnalysisFailed;
            }
            if (session->state == SessionState::kComplete) {
                const auto validation_result = ValidateCompletedPayload(*session, false);
                if (validation_result != util::ErrorEnum::Success) {
                    return validation_result;
                }
            }
            info = MakeInfo(*session);
            return util::ErrorEnum::Success;
        }
        return util::ErrorEnum::InvalidParam;
    }
    if (session->state != SessionState::kOpen || chunk_index != session->next_chunk_index) {
        return util::ErrorEnum::InvalidParam;
    }

    const auto remaining        = session->total_size - session->received_size;
    const auto remaining_chunks = session->total_chunks - chunk_index - 1;
    std::uint64_t copied        = 0;
    std::string chunk_sha256;
    const auto append_limit = std::min(config_.max_chunk_size, remaining - remaining_chunks);
    int payload_fd          = -1;
    if (!OpenValidatedPayload(*session, O_WRONLY, payload_fd)) {
        return util::ErrorEnum::FileOpenFailed;
    }
    auto result =
        AppendRegularFile(chunk_path, payload_fd, session->received_size, append_limit, copied, chunk_sha256);
    CloseChecked(payload_fd);
    if (result != util::ErrorEnum::Success) {
        return result;
    }
    if (copied == 0 || copied > remaining) {
        return util::ErrorEnum::FileSizeSmall;
    }
    session->chunks[chunk_index] = ChunkRecord{copied, std::move(chunk_sha256)};
    session->received_size += copied;
    ++session->next_chunk_index;
    RefreshIdleExpiry(*session);
    info = MakeInfo(*session);
    return util::ErrorEnum::Success;
}

util::ErrorEnum UploadStagingServiceImpl::Complete(const std::string& principal, const std::string& upload_id,
                                                   UploadSessionInfo& info) {
    const std::string request_principal = principal;
    const std::string identifier        = upload_id;
    info                                = {};
    CleanupExpired();
    auto session = FindSession(request_principal, identifier);
    if (!session) {
        return util::ErrorEnum::NoSuchId;
    }

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->principal != request_principal) {
        return util::ErrorEnum::AuthFailed;
    }
    if (session->state == SessionState::kRetired) {
        return util::ErrorEnum::NoSuchId;
    }
    if (session->state == SessionState::kOpen && (session->next_chunk_index != session->total_chunks ||
                                                  session->received_size != session->total_size)) {
        return util::ErrorEnum::InvalidParam;
    }
    const auto validation_result = ValidateCompletedPayload(*session, session->state == SessionState::kOpen);
    if (validation_result != util::ErrorEnum::Success) {
        return validation_result;
    }
    session->state = SessionState::kComplete;
    info           = MakeInfo(*session);
    return util::ErrorEnum::Success;
}

util::ErrorEnum UploadStagingServiceImpl::Consume(const std::string& principal, const std::string& upload_id,
                                                  UploadPurpose expected_purpose, StagedFileLease& lease) {
    std::vector<StagedFileLease> leases;
    const auto result = ConsumeMany(principal, {upload_id}, expected_purpose, leases);
    if (result == util::ErrorEnum::Success) {
        lease = std::move(leases.front());
    }
    return result;
}

util::ErrorEnum UploadStagingServiceImpl::ConsumeMany(const std::string& principal,
                                                      const std::vector<std::string>& upload_ids,
                                                      UploadPurpose expected_purpose,
                                                      std::vector<StagedFileLease>& leases) {
    return ConsumeBatch(principal, upload_ids, expected_purpose, false, leases);
}

util::ErrorEnum UploadStagingServiceImpl::ConsumeLegacyPath(const std::string& principal,
                                                            const std::string& legacy_path,
                                                            UploadPurpose expected_purpose,
                                                            StagedFileLease& lease) {
    std::vector<StagedFileLease> leases;
    const auto result = ConsumeLegacyPaths(principal, {legacy_path}, expected_purpose, leases);
    if (result == util::ErrorEnum::Success) {
        lease = std::move(leases.front());
    }
    return result;
}

util::ErrorEnum UploadStagingServiceImpl::ConsumeLegacyPaths(const std::string& principal,
                                                             const std::vector<std::string>& legacy_paths,
                                                             UploadPurpose expected_purpose,
                                                             std::vector<StagedFileLease>& leases) {
    return ConsumeBatch(principal, legacy_paths, expected_purpose, true, leases);
}

util::ErrorEnum UploadStagingServiceImpl::GetLegacyPath(const std::string& principal,
                                                        const std::string& upload_id,
                                                        UploadPurpose expected_purpose, std::string& path) {
    const std::string request_principal = principal;
    const std::string identifier        = upload_id;
    path.clear();
    CleanupExpired();
    auto session = FindSession(request_principal, identifier);
    if (!session) {
        return util::ErrorEnum::NoSuchId;
    }
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->principal != request_principal) {
        return util::ErrorEnum::AuthFailed;
    }
    if (session->purpose != expected_purpose || session->state != SessionState::kComplete) {
        return util::ErrorEnum::InvalidParam;
    }
    const auto validation_result = ValidateCompletedPayload(*session, false);
    if (validation_result != util::ErrorEnum::Success) {
        return validation_result;
    }
    path = MakeOpaqueReference(*session);
    return util::ErrorEnum::Success;
}

util::ErrorEnum UploadStagingServiceImpl::Cancel(const std::string& principal, const std::string& upload_id) {
    CleanupExpired();
    std::shared_ptr<Session> removed_session;
    {
        std::lock_guard<std::mutex> sessions_lock(sessions_mutex_);
        auto session = ResolveSessionLocked(principal, upload_id);
        if (!session) {
            return util::ErrorEnum::Success;
        }
        std::lock_guard<std::mutex> session_lock(session->mutex);
        if (session->principal != principal) {
            return util::ErrorEnum::AuthFailed;
        }
        session->state  = SessionState::kRetired;
        removed_session = session;
        RemoveSessionLocked(session);
    }
    RemoveSessions({removed_session});
    return util::ErrorEnum::Success;
}

void UploadStagingServiceImpl::CleanupExpired() {
    std::vector<std::shared_ptr<Session>> expired_sessions;
    const auto now = now_();
    {
        std::lock_guard<std::mutex> sessions_lock(sessions_mutex_);
        expired_sessions.reserve(sessions_.size());
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            auto session = it->second;
            // Append/complete can hold the per-session mutex while copying,
            // hashing, or syncing a large payload. Cleanup must never retain
            // the global registry mutex while waiting for that I/O. A busy
            // session is retried by the next explicit or background cleanup.
            std::unique_lock<std::mutex> session_lock(session->mutex, std::try_to_lock);
            if (!session_lock.owns_lock()) {
                ++it;
                continue;
            }
            if (session->expires_at > now) {
                ++it;
                continue;
            }
            session->state = SessionState::kRetired;
            expired_sessions.push_back(session);
            ReleaseSessionAccountingLocked(session);
            it = sessions_.erase(it);
        }
    }
    RemoveSessions(expired_sessions);
}

bool UploadStagingServiceImpl::IsPurposeValid(UploadPurpose purpose) {
    switch (purpose) {
        case UploadPurpose::kModelComponent:
        case UploadPurpose::kModelArchive:
        case UploadPurpose::kVideo:
        case UploadPurpose::kFaceImport:
        case UploadPurpose::kAudio:
        case UploadPurpose::kAlgorithm:
        case UploadPurpose::kUpgrade:
            return true;
    }
    return false;
}

bool UploadStagingServiceImpl::IsSha256Valid(const std::string& sha256) {
    if (sha256.empty()) {
        return true;
    }
    return sha256.size() == 64 && std::all_of(sha256.begin(), sha256.end(), [](unsigned char c) {
               return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
           });
}

bool UploadStagingServiceImpl::IsClientRequestIdValid(const std::string& client_request_id) {
    if (client_request_id.empty()) {
        return true;
    }
    return client_request_id.size() <= 128 &&
           std::all_of(client_request_id.begin(), client_request_id.end(), [](unsigned char c) {
               return IsAsciiAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == ':';
           });
}

bool UploadStagingServiceImpl::NormalizeOriginalName(const std::string& input, std::string& output) {
    output.clear();
    if (input.empty() || input.size() > 1024 || input.find('\0') != std::string::npos) {
        return false;
    }
    auto separator = input.find_last_of("/\\");
    output         = input.substr(separator == std::string::npos ? 0 : separator + 1);
    if (output.empty() || output == "." || output == ".." || output.size() > 255) {
        return false;
    }
    return std::none_of(output.begin(), output.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool UploadStagingServiceImpl::IsUuidLike(const std::string& value) {
    return IsUuidComponent(value);
}

std::string UploadStagingServiceImpl::MakeOpaqueReference(const Session& session) {
    std::ostringstream encoded_name;
    encoded_name << std::uppercase << std::hex << std::setfill('0');
    for (unsigned char c : session.original_name) {
        if (IsAsciiAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded_name << static_cast<char>(c);
        } else {
            encoded_name << '%' << std::setw(2) << static_cast<unsigned int>(c);
        }
    }
    return "upload://" + session.upload_id + "/" + encoded_name.str();
}

bool UploadStagingServiceImpl::ComputeSha256(int fd, std::string& digest) {
    return ComputeSha256Pinned(fd, digest);
}

bool UploadStagingServiceImpl::InspectRegularFile(const std::string& path, std::uint64_t max_size,
                                                  std::uint64_t& size, std::string& sha256) {
    size = 0;
    sha256.clear();
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    struct stat before {};
    struct stat after {};
    bool valid = fstat(fd, &before) == 0 && S_ISREG(before.st_mode) && before.st_nlink == 1 &&
                 before.st_size > 0 && static_cast<std::uint64_t>(before.st_size) <= max_size &&
                 ComputeSha256(fd, sha256) && fstat(fd, &after) == 0 &&
                 SameIdentity(before, static_cast<std::uint64_t>(after.st_dev),
                              static_cast<std::uint64_t>(after.st_ino)) &&
                 before.st_size == after.st_size;
    if (valid) {
        size = static_cast<std::uint64_t>(before.st_size);
    }
    return CloseChecked(fd) && valid;
}

util::ErrorEnum UploadStagingServiceImpl::AppendRegularFile(
    const std::string& source_path, int destination_fd, std::uint64_t expected_destination_size,
    std::uint64_t max_bytes, std::uint64_t& copied_bytes, std::string& sha256) {
    copied_bytes = 0;
    sha256.clear();
    int source_fd = open(source_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source_fd < 0) {
        return util::ErrorEnum::FileOpenFailed;
    }
    struct stat source_status {};
    if (fstat(source_fd, &source_status) != 0 || !S_ISREG(source_status.st_mode) ||
        source_status.st_nlink != 1 || source_status.st_size < 0) {
        CloseChecked(source_fd);
        return util::ErrorEnum::FileOpenFailed;
    }
    const auto source_size = static_cast<std::uint64_t>(source_status.st_size);
    if (source_size == 0) {
        CloseChecked(source_fd);
        return util::ErrorEnum::FileSizeSmall;
    }
    if (source_size > max_bytes) {
        CloseChecked(source_fd);
        return util::ErrorEnum::FileSizeBig;
    }

    if (destination_fd < 0) {
        CloseChecked(source_fd);
        return util::ErrorEnum::FileOpenFailed;
    }
    struct stat destination_status {};
    if (fstat(destination_fd, &destination_status) != 0 || !S_ISREG(destination_status.st_mode) ||
        destination_status.st_size < 0 ||
        static_cast<std::uint64_t>(destination_status.st_size) != expected_destination_size ||
        (source_status.st_dev == destination_status.st_dev &&
         source_status.st_ino == destination_status.st_ino)) {
        CloseChecked(source_fd);
        return util::ErrorEnum::FileOpenFailed;
    }
    const auto original_size = destination_status.st_size;
    if (lseek(destination_fd, original_size, SEEK_SET) < 0) {
        CloseChecked(source_fd);
        return util::ErrorEnum::FileOpenFailed;
    }

    util::ErrorEnum result = util::ErrorEnum::Success;
    CryptoPP::SHA256 sha;
    std::array<char, 64 * 1024> buffer{};
    while (copied_bytes < source_size) {
        const auto to_read =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), source_size - copied_bytes));
        ssize_t read_size = read(source_fd, buffer.data(), to_read);
        if (read_size < 0 && errno == EINTR) {
            continue;
        }
        if (read_size <= 0) {
            result = util::ErrorEnum::FileOpenFailed;
            break;
        }
        ssize_t written = 0;
        while (written < read_size) {
            auto write_size =
                write(destination_fd, buffer.data() + written, static_cast<std::size_t>(read_size - written));
            if (write_size < 0 && errno == EINTR) {
                continue;
            }
            if (write_size <= 0) {
                result = util::ErrorEnum::FileOpenFailed;
                break;
            }
            written += write_size;
        }
        if (result != util::ErrorEnum::Success) {
            break;
        }
        sha.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()),
                   static_cast<std::size_t>(read_size));
        copied_bytes += static_cast<std::uint64_t>(read_size);
    }
    struct stat source_after {};
    if (result == util::ErrorEnum::Success &&
        (fstat(source_fd, &source_after) != 0 || source_after.st_size != source_status.st_size ||
         !SameIdentity(source_after, static_cast<std::uint64_t>(source_status.st_dev),
                       static_cast<std::uint64_t>(source_status.st_ino)))) {
        result = util::ErrorEnum::FileOpenFailed;
    }
    if (result == util::ErrorEnum::Success && fsync(destination_fd) != 0) {
        result = util::ErrorEnum::FileOpenFailed;
    }
    if (result != util::ErrorEnum::Success) {
        if (ftruncate(destination_fd, original_size) != 0) {
            LOG_WARN("{}", "Failed to roll back partial upload append");
        }
        copied_bytes = 0;
    } else {
        std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest_bytes{};
        sha.Final(digest_bytes.data());
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (auto byte : digest_bytes) {
            output << std::setw(2) << static_cast<unsigned int>(byte);
        }
        sha256 = output.str();
    }
    CloseChecked(source_fd);
    return result;
}

std::shared_ptr<UploadStagingServiceImpl::Session> UploadStagingServiceImpl::FindSession(
    const std::string& principal, const std::string& upload_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return ResolveSessionLocked(principal, upload_id);
}

std::shared_ptr<UploadStagingServiceImpl::Session> UploadStagingServiceImpl::ResolveSessionLocked(
    const std::string& principal, const std::string& upload_id) const {
    // Prefer the server-issued canonical namespace. Begin() prevents new
    // aliases from colliding with canonical IDs, while this ordering keeps
    // canonical sessions reachable if legacy in-memory state is ambiguous.
    const auto session_it = sessions_.find(upload_id);
    if (session_it != sessions_.end()) {
        return session_it->second;
    }
    const auto alias_it = client_request_aliases_.find(std::make_pair(principal, upload_id));
    if (alias_it != client_request_aliases_.end()) {
        const auto aliased_session_it = sessions_.find(alias_it->second);
        return aliased_session_it == sessions_.end() ? nullptr : aliased_session_it->second;
    }
    return nullptr;
}

UploadSessionInfo UploadStagingServiceImpl::MakeInfo(const Session& session) const {
    UploadSessionInfo info;
    info.upload_id          = session.upload_id;
    info.original_name      = session.original_name;
    info.total_size         = session.total_size;
    info.received_size      = session.received_size;
    info.max_chunk_size     = config_.max_chunk_size;
    info.total_chunks       = session.total_chunks;
    info.next_chunk_index   = session.next_chunk_index;
    info.expires_at_unix_ms = session.expires_at_unix_ms;
    info.complete           = session.state == SessionState::kComplete;
    return info;
}

bool UploadStagingServiceImpl::ImmutableMetadataMatches(const Session& session,
                                                        const UploadBeginRequest& request,
                                                        const std::string& normalized_name) const {
    return session.principal == request.principal && session.purpose == request.purpose &&
           session.client_request_id == request.client_request_id &&
           session.original_name == normalized_name && session.total_size == request.total_size &&
           session.total_chunks == request.total_chunks && session.sha256 == request.sha256;
}

void UploadStagingServiceImpl::RefreshIdleExpiry(Session& session) const {
    session.expires_at = std::min(now_() + config_.session_ttl, session.absolute_expires_at);
    const auto refreshed_unix_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::system_clock::now() + config_.session_ttl).time_since_epoch())
            .count();
    session.expires_at_unix_ms = std::min(refreshed_unix_ms, session.absolute_expires_at_unix_ms);
}

bool UploadStagingServiceImpl::ValidateRootPath() const {
    struct stat path_status {};
    return root_fd_ >= 0 && lstat(config_.root_path.c_str(), &path_status) == 0 &&
           S_ISDIR(path_status.st_mode) && !S_ISLNK(path_status.st_mode) && path_status.st_uid == geteuid() &&
           (path_status.st_mode & 0077) == 0 && SameIdentity(path_status, root_device_, root_inode_);
}

bool UploadStagingServiceImpl::OpenValidatedPayload(const Session& session, int flags,
                                                    int& payload_fd) const {
    payload_fd = -1;
    if (!ValidateRootPath() || !IsUuidLike(session.upload_id) ||
        session.session_dir != (fs::path(config_.root_path) / session.upload_id).string() ||
        session.payload_path !=
            (fs::path(config_.root_path) / session.upload_id / session.original_name).string()) {
        return false;
    }

    const int session_fd =
        openat(root_fd_, session.upload_id.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (session_fd < 0) {
        return false;
    }
    struct stat session_status {};
    if (fstat(session_fd, &session_status) != 0 || !S_ISDIR(session_status.st_mode) ||
        session_status.st_uid != geteuid() || (session_status.st_mode & 0077) != 0 ||
        !SameIdentity(session_status, session.session_device, session.session_inode)) {
        CloseChecked(session_fd);
        return false;
    }

    payload_fd = openat(session_fd, session.original_name.c_str(), flags | O_CLOEXEC | O_NOFOLLOW);
    struct stat payload_status {};
    const bool valid = payload_fd >= 0 && fstat(payload_fd, &payload_status) == 0 &&
                       S_ISREG(payload_status.st_mode) && payload_status.st_nlink == 1 &&
                       payload_status.st_uid == geteuid() && (payload_status.st_mode & 0077) == 0 &&
                       SameIdentity(payload_status, session.payload_device, session.payload_inode);
    CloseChecked(session_fd);
    if (!valid) {
        CloseChecked(payload_fd);
        payload_fd = -1;
    }
    return valid;
}

util::ErrorEnum UploadStagingServiceImpl::ValidateCompletedPayload(Session& session, bool establish_digest,
                                                                   int* verified_fd) const {
    if (verified_fd != nullptr) {
        *verified_fd = -1;
    }
    int payload_fd = -1;
    if (!OpenValidatedPayload(session, O_RDONLY, payload_fd)) {
        return util::ErrorEnum::FileNotExist;
    }
    ScopedFd scoped_payload_fd(payload_fd);

    struct stat before {};
    struct stat after {};
    std::string digest;
    const bool valid = fstat(payload_fd, &before) == 0 && before.st_size >= 0 &&
                       static_cast<std::uint64_t>(before.st_size) == session.total_size &&
                       ComputeSha256(payload_fd, digest) && fstat(payload_fd, &after) == 0 &&
                       SameIdentity(after, session.payload_device, session.payload_inode) &&
                       before.st_size == after.st_size;
    if (!valid) {
        return util::ErrorEnum::FileAnalysisFailed;
    }
    if ((!session.sha256.empty() && digest != session.sha256) ||
        (!session.verified_sha256.empty() && digest != session.verified_sha256) ||
        (!establish_digest && session.verified_sha256.empty())) {
        return util::ErrorEnum::FileAnalysisFailed;
    }
    if (establish_digest) {
        session.verified_sha256 = digest;
    }
    if (fchmod(payload_fd, kCompletedFileMode) != 0 || fsync(payload_fd) != 0) {
        return util::ErrorEnum::FileOpenFailed;
    }
    if (verified_fd != nullptr) {
        *verified_fd = scoped_payload_fd.Release();
        return util::ErrorEnum::Success;
    }
    return CloseChecked(scoped_payload_fd.Release()) ? util::ErrorEnum::Success
                                                     : util::ErrorEnum::FileOpenFailed;
}

util::ErrorEnum UploadStagingServiceImpl::ConsumeBatch(const std::string& principal,
                                                       const std::vector<std::string>& identifiers,
                                                       UploadPurpose expected_purpose, bool legacy_references,
                                                       std::vector<StagedFileLease>& leases) {
    CleanupExpired();
    if (principal.empty() || identifiers.empty() || !IsPurposeValid(expected_purpose)) {
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::shared_ptr<Session>> selected_sessions;
    std::vector<std::unique_lock<std::mutex>> session_locks;
    std::vector<StagedFileLease> new_leases;
    std::vector<ScopedFd> verified_payload_fds;
    std::vector<ScopedFd> cleanup_root_fds;
    std::unique_lock<std::mutex> sessions_lock(sessions_mutex_, std::defer_lock);
    bool sessions_hidden = false;

    const auto restore_sessions = [&]() {
        if (!sessions_lock.owns_lock()) {
            sessions_lock.lock();
        }
        for (const auto& session : selected_sessions) {
            session->state          = SessionState::kComplete;
            const auto consuming_it = consuming_sessions_.find(session->upload_id);
            if (consuming_it != consuming_sessions_.end()) {
                sessions_.insert(consuming_sessions_.extract(consuming_it));
            }
        }
        sessions_hidden = false;
    };

    try {
        selected_sessions.reserve(identifiers.size());
        session_locks.reserve(identifiers.size());
        new_leases.reserve(identifiers.size());
        verified_payload_fds.reserve(identifiers.size());
        cleanup_root_fds.reserve(identifiers.size());

        std::set<std::string> exact_identifiers;
        std::set<std::string> canonical_ids;
        sessions_lock.lock();
        for (const auto& identifier : identifiers) {
            if (identifier.empty() || !exact_identifiers.emplace(identifier).second) {
                return util::ErrorEnum::InvalidParam;
            }

            std::shared_ptr<Session> session;
            if (legacy_references) {
                const auto match = std::find_if(sessions_.begin(), sessions_.end(), [&](const auto& entry) {
                    return entry.second->payload_path == identifier ||
                           MakeOpaqueReference(*entry.second) == identifier;
                });
                if (match != sessions_.end()) {
                    session = match->second;
                }
            } else {
                session = ResolveSessionLocked(principal, identifier);
            }
            if (!session) {
                return util::ErrorEnum::NoSuchId;
            }
            if (!canonical_ids.emplace(session->upload_id).second) {
                return util::ErrorEnum::InvalidParam;
            }

            session_locks.emplace_back(session->mutex);
            if (session->principal != principal) {
                return util::ErrorEnum::AuthFailed;
            }
            if (session->purpose != expected_purpose || session->state != SessionState::kComplete) {
                return util::ErrorEnum::InvalidParam;
            }
            selected_sessions.push_back(std::move(session));
        }

        // Allocate and copy every lease metadata string before hiding sessions.
        // These leases remain unarmed (fd == -1), so exceptional cleanup cannot
        // remove payload directories that still belong to a rolled-back batch.
        for (const auto& session : selected_sessions) {
            new_leases.push_back(StagedFileLease(
                session->payload_path, -1, session->upload_id, session->session_device,
                session->session_inode, session->original_name, session->total_size, session->verified_sha256,
                -1, session->payload_device, session->payload_inode));
        }

        sessions_hidden = true;
        for (const auto& session : selected_sessions) {
            const auto active_it = sessions_.find(session->upload_id);
            if (active_it == sessions_.end()) {
                restore_sessions();
                return util::ErrorEnum::NoSuchId;
            }
            session->state = SessionState::kConsuming;
            consuming_sessions_.insert(sessions_.extract(active_it));
        }
        sessions_lock.unlock();

        for (const auto& session : selected_sessions) {
            int verified_payload_fd      = -1;
            const auto validation_result = ValidateCompletedPayload(*session, false, &verified_payload_fd);
            if (validation_result != util::ErrorEnum::Success) {
                restore_sessions();
                return validation_result;
            }
            verified_payload_fds.emplace_back(verified_payload_fd);
        }
        if (!ValidateRootPath()) {
            restore_sessions();
            return util::ErrorEnum::FileNotExist;
        }

        for (std::size_t i = 0; i < selected_sessions.size(); ++i) {
            const int cleanup_root_fd = fcntl(root_fd_, F_DUPFD_CLOEXEC, 0);
            if (cleanup_root_fd < 0) {
                restore_sessions();
                return util::ErrorEnum::FileOpenFailed;
            }
            cleanup_root_fds.emplace_back(cleanup_root_fd);
        }

        sessions_lock.lock();
        for (std::size_t i = 0; i < selected_sessions.size(); ++i) {
            const auto& session = selected_sessions[i];
            ReleaseSessionAccountingLocked(session);
            session->state = SessionState::kRetired;
            consuming_sessions_.erase(session->upload_id);
            new_leases[i].cleanup_root_fd_     = cleanup_root_fds[i].Release();
            new_leases[i].verified_payload_fd_ = verified_payload_fds[i].Release();
        }
        sessions_hidden = false;
        leases.swap(new_leases);
        return util::ErrorEnum::Success;
    } catch (const std::bad_alloc&) {
        if (sessions_hidden) {
            restore_sessions();
        }
        return util::ErrorEnum::ResourceLimit;
    } catch (...) {
        if (sessions_hidden) {
            restore_sessions();
        }
        return util::ErrorEnum::FileOpenFailed;
    }
}

void UploadStagingServiceImpl::ReleaseSessionAccountingLocked(const std::shared_ptr<Session>& session) {
    if (!session->client_request_id.empty()) {
        client_request_aliases_.erase(std::make_pair(session->principal, session->client_request_id));
    }
    auto count_it = principal_session_counts_.find(session->principal);
    if (count_it != principal_session_counts_.end()) {
        if (count_it->second <= 1) {
            principal_session_counts_.erase(count_it);
        } else {
            --count_it->second;
        }
    }
    if (active_session_count_ > 0) {
        --active_session_count_;
    }
    reserved_bytes_ = reserved_bytes_ >= session->total_size ? reserved_bytes_ - session->total_size : 0;
}

void UploadStagingServiceImpl::RemoveSessionLocked(const std::shared_ptr<Session>& session) {
    session->state = SessionState::kRetired;
    sessions_.erase(session->upload_id);
    ReleaseSessionAccountingLocked(session);
}

void UploadStagingServiceImpl::CleanupOrphans() {
    const int iterator_fd = fcntl(root_fd_, F_DUPFD_CLOEXEC, 0);
    if (iterator_fd < 0) {
        LOG_WARN("{}", "Failed to inspect orphan upload sessions");
        return;
    }
    DIR* directory = fdopendir(iterator_fd);
    if (directory == nullptr) {
        CloseChecked(iterator_fd);
        LOG_WARN("{}", "Failed to inspect orphan upload sessions");
        return;
    }
    errno = 0;
    while (auto* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (!IsUuidLike(name)) {
            continue;
        }
        const int session_fd =
            openat(root_fd_, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        struct stat status {};
        if (session_fd < 0 || fstat(session_fd, &status) != 0 || !S_ISDIR(status.st_mode)) {
            CloseChecked(session_fd);
            continue;
        }
        CloseChecked(session_fd);
        if (!RemoveSessionAt(root_fd_, name, static_cast<std::uint64_t>(status.st_dev),
                             static_cast<std::uint64_t>(status.st_ino))) {
            LOG_WARN("Failed to safely remove orphan upload session {}", name);
        }
        errno = 0;
    }
    closedir(directory);
}

void UploadStagingServiceImpl::RemoveSessions(const std::vector<std::shared_ptr<Session>>& sessions) const {
    for (const auto& session : sessions) {
        if (session != nullptr &&
            !RemoveSessionAt(root_fd_, session->upload_id, session->session_device, session->session_inode)) {
            LOG_WARN("Failed to safely remove upload session {}", session->upload_id);
        }
    }
}

void UploadStagingServiceImpl::StartCleanupThread() {
    if (config_.cleanup_interval == std::chrono::milliseconds::zero()) {
        return;
    }
    cleanup_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(cleanup_mutex_);
        while (!stopping_) {
            if (cleanup_condition_.wait_for(lock, config_.cleanup_interval, [this]() { return stopping_; })) {
                break;
            }
            lock.unlock();
            CleanupExpired();
            lock.lock();
        }
    });
}

void UploadStagingServiceImpl::StopCleanupThread() {
    {
        std::lock_guard<std::mutex> lock(cleanup_mutex_);
        stopping_ = true;
    }
    cleanup_condition_.notify_all();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

}  // namespace cosmo::service
