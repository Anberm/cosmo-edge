#include "bootstrap/ReleaseBootstrap.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#include "bootstrap/ReleaseBootstrapVerifier.h"

namespace cosmo::bootstrap {
namespace {

    constexpr char kPythonPath[] = "/usr/bin/python3";
    constexpr char kBackendPath[] =
        "/appfs/cosmo_wander/cwai_data/.release-bootstrap/scripts/release_bootstrap_backend.py";
    constexpr char kUpdaterPath[] =
        "/appfs/cosmo_wander/cwai_data/.release-bootstrap/scripts/release_updater.py";
    constexpr char kBackendFdPath[]                   = "/proc/self/fd/5";
    constexpr char kInstallArgument[]                 = "--install";
    constexpr char kRecoverArgument[]                 = "--recover";
    constexpr std::size_t kMaximumManifestSize        = 128 * 1024;
    constexpr std::size_t kMaximumPemSize             = 16 * 1024;
    constexpr std::uint64_t kMaximumArchiveSize       = 128ULL * 1024 * 1024 * 1024;
    constexpr std::uint64_t kMaximumTrustedScriptSize = 8ULL * 1024 * 1024;
    constexpr std::uint64_t kMaximumPythonSize        = 512ULL * 1024 * 1024;
    constexpr std::uint32_t kRequestMagic             = 0x43425231;  // CBR1
    constexpr std::uint32_t kApprovalMagic            = 0x43424131;  // CBA1
    constexpr int kArchiveDescriptor                  = 3;
    constexpr int kCommunicationDescriptor            = 4;
    constexpr int kBackendDescriptor                  = 5;
    constexpr int kUpdaterDescriptor                  = 6;
    constexpr int kPythonDescriptor                   = 7;
    constexpr int kFirstTemporaryDescriptor           = 32;

    enum class BackendOperation {
        kInstall,
        kRecover,
    };

    struct RequestHeader {
        std::uint32_t magic;
        std::uint32_t manifest_size;
        std::uint32_t signature_size;
    };

    struct ApprovalHeader {
        std::uint32_t magic;
        std::uint32_t pem_size;
    };

    class OwnedDescriptor {
    public:
        explicit OwnedDescriptor(int descriptor = -1) : descriptor_(descriptor) {}
        ~OwnedDescriptor() {
            Reset();
        }

        OwnedDescriptor(const OwnedDescriptor&)            = delete;
        OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;

        OwnedDescriptor(OwnedDescriptor&& other) noexcept : descriptor_(other.Release()) {}

        OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
            if (this != &other) {
                Reset(other.Release());
            }
            return *this;
        }

        int Get() const {
            return descriptor_;
        }

        explicit operator bool() const {
            return descriptor_ >= 0;
        }

        int Release() {
            const int descriptor = descriptor_;
            descriptor_          = -1;
            return descriptor;
        }

        void Reset(int descriptor = -1) {
            if (descriptor_ >= 0) {
                close(descriptor_);
            }
            descriptor_ = descriptor;
        }

    private:
        int descriptor_;
    };

    int OpenRegularFile(const char* path, bool executable, std::uint64_t maximum_size) {
        if (path == nullptr) {
            return -1;
        }
        OwnedDescriptor descriptor(open(path, O_RDONLY | O_CLOEXEC));
        struct stat information {};
        if (!descriptor || fstat(descriptor.Get(), &information) != 0 || !S_ISREG(information.st_mode) ||
            information.st_size <= 0 || static_cast<std::uint64_t>(information.st_size) > maximum_size ||
            (executable && (information.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)) {
            return -1;
        }
        return descriptor.Release();
    }

    bool WriteAll(int descriptor, const void* input, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(input);
        while (size != 0) {
            const ssize_t count = send(descriptor, bytes, size, MSG_NOSIGNAL);
            if (count > 0) {
                bytes += count;
                size -= static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        return true;
    }

    bool ReadAll(int descriptor, void* output, std::size_t size) {
        auto* bytes = static_cast<std::uint8_t*>(output);
        while (size != 0) {
            const ssize_t count = recv(descriptor, bytes, size, 0);
            if (count > 0) {
                bytes += count;
                size -= static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        return true;
    }

    bool IsRegularArchive(int descriptor) {
        struct stat information {};
        return fstat(descriptor, &information) == 0 && S_ISREG(information.st_mode) &&
               information.st_size > 0 &&
               static_cast<std::uint64_t>(information.st_size) <= kMaximumArchiveSize;
    }

    void CloseUnneededDescriptors() {
#ifdef SYS_close_range
        if (syscall(SYS_close_range, static_cast<unsigned int>(kPythonDescriptor + 1), ~0U, 0U) == 0) {
            return;
        }
#endif
        struct rlimit limit {};
        unsigned long maximum = 65536;
        if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY) {
            maximum = static_cast<unsigned long>(limit.rlim_cur);
        }
        for (unsigned long descriptor = static_cast<unsigned long>(kPythonDescriptor + 1);
             descriptor < maximum; ++descriptor) {
            close(static_cast<int>(descriptor));
        }
    }

    int DuplicateHigh(int descriptor) {
        return descriptor < 0 ? -1 : fcntl(descriptor, F_DUPFD_CLOEXEC, kFirstTemporaryDescriptor);
    }

    bool InstallChildDescriptor(int source, int destination, bool close_on_exec) {
        if (dup2(source, destination) < 0) {
            return false;
        }
        return fcntl(destination, F_SETFD, close_on_exec ? FD_CLOEXEC : 0) == 0;
    }

    pid_t StartBackend(BackendOperation operation, int archive_descriptor, int communication_descriptor,
                       int python_descriptor, int backend_descriptor, int updater_descriptor) {
        OwnedDescriptor archive_copy(
            operation == BackendOperation::kInstall ? DuplicateHigh(archive_descriptor) : -1);
        OwnedDescriptor communication_copy(DuplicateHigh(communication_descriptor));
        OwnedDescriptor python_copy(DuplicateHigh(python_descriptor));
        OwnedDescriptor backend_copy(DuplicateHigh(backend_descriptor));
        OwnedDescriptor updater_copy(DuplicateHigh(updater_descriptor));
        if ((operation == BackendOperation::kInstall && !archive_copy) || !communication_copy ||
            !python_copy || !backend_copy || !updater_copy) {
            return -1;
        }

        const pid_t child = fork();
        if (child != 0) {
            return child;
        }

        for (int descriptor = kArchiveDescriptor; descriptor <= kPythonDescriptor; ++descriptor) {
            close(descriptor);
        }
        const bool descriptors_ready =
            (operation != BackendOperation::kInstall ||
             InstallChildDescriptor(archive_copy.Get(), kArchiveDescriptor, false)) &&
            InstallChildDescriptor(communication_copy.Get(), kCommunicationDescriptor, false) &&
            InstallChildDescriptor(backend_copy.Get(), kBackendDescriptor, false) &&
            InstallChildDescriptor(updater_copy.Get(), kUpdaterDescriptor, false) &&
            InstallChildDescriptor(python_copy.Get(), kPythonDescriptor, true);
        if (!descriptors_ready) {
            _exit(126);
        }
        CloseUnneededDescriptors();

        char* arguments[] = {
            const_cast<char*>(kPythonPath),
            const_cast<char*>("-I"),
            const_cast<char*>("-B"),
            const_cast<char*>(kBackendFdPath),
            const_cast<char*>(operation == BackendOperation::kInstall ? kInstallArgument : kRecoverArgument),
            nullptr};
        char* environment[] = {nullptr};
#ifdef SYS_execveat
        syscall(SYS_execveat, kPythonDescriptor, "", arguments, environment, AT_EMPTY_PATH);
#endif
        fexecve(kPythonDescriptor, arguments, environment);
        _exit(126);
    }

    bool ConfigureProcessSafety() {
        struct sigaction action {};
        action.sa_handler = SIG_IGN;
        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGPIPE, &action, nullptr) != 0) {
            return false;
        }
        struct rlimit no_core {};
        no_core.rlim_cur = 0;
        no_core.rlim_max = 0;
        return setrlimit(RLIMIT_CORE, &no_core) == 0;
    }

    bool IsRootServiceIdentity() {
        return getuid() == 0 && geteuid() == 0 && getgid() == 0 && getegid() == 0;
    }

    bool WaitForSuccessfulBackend(pid_t child) {
        int status   = 0;
        pid_t waited = -1;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    bool OpenStableBackendComponents(OwnedDescriptor& python, OwnedDescriptor& backend,
                                     OwnedDescriptor& updater) {
        python.Reset(OpenRegularFile(kPythonPath, true, kMaximumPythonSize));
        backend.Reset(OpenRegularFile(kBackendPath, false, kMaximumTrustedScriptSize));
        updater.Reset(OpenRegularFile(kUpdaterPath, false, kMaximumTrustedScriptSize));
        return python && backend && updater;
    }

}  // namespace

int BootstrapSignedRelease(const char* archive_path) {
    if (!IsRootServiceIdentity()) {
        std::cerr << "release bootstrap requires the root service identity\n";
        return 1;
    }
    if (!ConfigureProcessSafety()) {
        std::cerr << "release bootstrap cannot establish process safety controls\n";
        return 1;
    }
    if (archive_path == nullptr || archive_path[0] == '\0') {
        std::cerr << "release bootstrap requires an archive path\n";
        return 1;
    }

    OwnedDescriptor archive(open(archive_path, O_RDONLY | O_CLOEXEC));
    if (!archive || !IsRegularArchive(archive.Get())) {
        std::cerr << "release bootstrap rejected archive type or size\n";
        return 1;
    }

    OwnedDescriptor python;
    OwnedDescriptor backend;
    OwnedDescriptor updater;
    if (!OpenStableBackendComponents(python, backend, updater)) {
        std::cerr << "release bootstrap trusted backend is unavailable\n";
        return 1;
    }

    EmbeddedReleaseKey key{};
    std::vector<std::uint8_t> canonical_pem;
    std::string error;
    if (!LoadAndValidateEmbeddedReleaseKey(key, canonical_pem, error) || canonical_pem.empty() ||
        canonical_pem.size() > kMaximumPemSize) {
        std::cerr << "release bootstrap trust-anchor validation failed";
        if (!error.empty()) {
            std::cerr << ": " << error;
        }
        std::cerr << '\n';
        return 1;
    }

    int channel_descriptors[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, channel_descriptors) != 0) {
        std::cerr << "release bootstrap cannot create its verifier channel\n";
        return 1;
    }
    OwnedDescriptor parent_channel(channel_descriptors[0]);
    OwnedDescriptor child_channel(channel_descriptors[1]);
    const pid_t child = StartBackend(BackendOperation::kInstall, archive.Get(), child_channel.Get(),
                                     python.Get(), backend.Get(), updater.Get());
    archive.Reset();
    child_channel.Reset();
    if (child < 0) {
        std::cerr << "release bootstrap cannot start the trusted backend\n";
        return 1;
    }

    RequestHeader request{};
    bool approved                    = ReadAll(parent_channel.Get(), &request, sizeof(request));
    const std::size_t manifest_size  = approved ? ntohl(request.manifest_size) : 0;
    const std::size_t signature_size = approved ? ntohl(request.signature_size) : 0;
    approved = approved && ntohl(request.magic) == kRequestMagic && manifest_size > 0 &&
               manifest_size <= kMaximumManifestSize && signature_size == 64;

    std::vector<std::uint8_t> manifest;
    std::array<std::uint8_t, 64> signature{};
    if (approved) {
        try {
            manifest.resize(manifest_size);
        } catch (const std::bad_alloc&) {
            approved = false;
            error    = "cannot allocate the bounded manifest buffer";
        }
    }
    if (approved) {
        approved = ReadAll(parent_channel.Get(), manifest.data(), manifest.size()) &&
                   ReadAll(parent_channel.Get(), signature.data(), signature.size()) &&
                   VerifyEd25519Manifest(key, manifest.data(), manifest.size(), signature.data(),
                                         signature.size(), error);
    }
    if (approved) {
        const ApprovalHeader response{htonl(kApprovalMagic),
                                      htonl(static_cast<std::uint32_t>(canonical_pem.size()))};
        approved = WriteAll(parent_channel.Get(), &response, sizeof(response)) &&
                   WriteAll(parent_channel.Get(), key.raw, sizeof(key.raw)) &&
                   WriteAll(parent_channel.Get(), key.key_id, sizeof(key.key_id)) &&
                   WriteAll(parent_channel.Get(), key.pem_sha256, sizeof(key.pem_sha256)) &&
                   WriteAll(parent_channel.Get(), canonical_pem.data(), canonical_pem.size());
    }
    parent_channel.Reset();

    const bool backend_succeeded = WaitForSuccessfulBackend(child);
    if (!approved) {
        std::cerr << "release bootstrap embedded-key verification failed";
        if (!error.empty()) {
            std::cerr << ": " << error;
        }
        std::cerr << '\n';
        return 1;
    }
    return backend_succeeded ? 0 : 1;
}

int RecoverFactoryBootstrap() {
    if (!IsRootServiceIdentity()) {
        std::cerr << "release bootstrap recovery requires the root service identity\n";
        return 1;
    }
    if (!ConfigureProcessSafety()) {
        std::cerr << "release bootstrap recovery cannot establish process safety controls\n";
        return 1;
    }

    OwnedDescriptor python;
    OwnedDescriptor backend;
    OwnedDescriptor updater;
    if (!OpenStableBackendComponents(python, backend, updater)) {
        std::cerr << "release bootstrap stable recovery backend is unavailable\n";
        return 1;
    }

    int channel_descriptors[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, channel_descriptors) != 0) {
        std::cerr << "release bootstrap recovery cannot create its private channel\n";
        return 1;
    }
    OwnedDescriptor parent_channel(channel_descriptors[0]);
    OwnedDescriptor child_channel(channel_descriptors[1]);
    const pid_t child = StartBackend(BackendOperation::kRecover, -1, child_channel.Get(), python.Get(),
                                     backend.Get(), updater.Get());
    child_channel.Reset();
    if (child < 0) {
        std::cerr << "release bootstrap recovery cannot start the trusted backend\n";
        return 1;
    }
    const bool backend_succeeded = WaitForSuccessfulBackend(child);
    parent_channel.Reset();
    return backend_succeeded ? 0 : 1;
}

}  // namespace cosmo::bootstrap
