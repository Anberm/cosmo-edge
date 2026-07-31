#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "catch_amalgamated.hpp"
#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/PacketUpgrade.h"
#include "service/system/impl/SystemOperationServiceImpl.h"
#include "util/Exec.h"
#include "util/PathUtil.h"
#include "util/ResourceBudget.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kSourceRuntimeEnvironment = "COSMO_SOURCE_RUNTIME";

class ScopedSourceRuntimeEnvironment {
public:
    ScopedSourceRuntimeEnvironment() {
        const char* value = std::getenv(kSourceRuntimeEnvironment);
        original_value_   = value == nullptr ? std::nullopt : std::optional<std::string>(value);
        unsetenv(kSourceRuntimeEnvironment);
    }

    ~ScopedSourceRuntimeEnvironment() {
        if (original_value_) {
            setenv(kSourceRuntimeEnvironment, original_value_->c_str(), 1);
        } else {
            unsetenv(kSourceRuntimeEnvironment);
        }
    }

    ScopedSourceRuntimeEnvironment(const ScopedSourceRuntimeEnvironment&)            = delete;
    ScopedSourceRuntimeEnvironment& operator=(const ScopedSourceRuntimeEnvironment&) = delete;

private:
    std::optional<std::string> original_value_;
};

void CreateUpgradeLayout(const fs::path& root) {
    static constexpr std::array required_dirs{"bin", "files", "font", "lib", "scripts", "web"};
    for (const char* directory : required_dirs) {
        fs::create_directories(root / directory);
    }
    std::ofstream(root / "scripts" / "install.sh") << "#!/bin/sh\nexit 0\n";
}

fs::path AddUpgradeChecksumToName(const fs::path& archive, const fs::path& destination_dir,
                                  std::string_view version = "9.9.9") {
    std::string output;
    if (cosmo::util::Exec({"md5sum", archive.string()}, output) != 0) {
        return {};
    }
    std::istringstream stream(output);
    std::string md5;
    if (!(stream >> md5) || md5.size() != 32) {
        return {};
    }
    const auto destination = destination_dir / ("cosmo-V" + std::string(version) + "-" + md5 + ".tar.gz");
    std::error_code ec;
    fs::rename(archive, destination, ec);
    return ec ? fs::path{} : destination;
}

}  // namespace

TEST_CASE("SystemOperationServiceImpl: System operations", "[system][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::SystemOperationServiceImpl sysOpSvc;

    SECTION("ExportLogs creates tar file successfully") {
        std::string testRoot = "/tmp/cosmo sysop;test";
        std::error_code cleanup_error;
        fs::remove_all(testRoot, cleanup_error);
        cosmo::path::OverrideRootPathForTest(testRoot, testRoot);

        // Create directories that cosmo::path:: will return
        auto webDir = cosmo::path::GetWebLocalPath();
        auto logDir = cosmo::path::GetLogPath();
        auto cfgDir = cosmo::path::GetCfgPath();

        // Create some dummy old logs to be cleaned up
        std::ofstream(webDir + "/IedLog_old.tar").put('a');
        std::ofstream(logDir + "/dummy.log").put('b');
        std::ofstream(cfgDir + "/dummy.cfg").put('c');

        std::string fileName;
        std::string fileUrl;
        auto result = sysOpSvc.ExportLogs(fileName, fileUrl);

        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(fileName.find("IedLog") == 0);
        REQUIRE(fileName.find(".tar") != std::string::npos);
        const auto archive = fs::path(testRoot) / fs::path(fileUrl).relative_path();
        REQUIRE(fs::is_regular_file(archive));
        std::string listing;
        REQUIRE(cosmo::util::Exec({"tar", "-tf", archive.string()}, listing) == 0);
        REQUIRE(listing.find("dummy.log") != std::string::npos);
        REQUIRE(listing.find("dummy.cfg") != std::string::npos);

        // Cleanup
        fs::remove_all(testRoot);
    }
}

TEST_CASE("SystemOperationServiceImpl: SOURCE runtime disables software upgrade",
          "[system][upgrade][source]") {
    cosmo::test::MockServiceRegistry mocks;
    ScopedSourceRuntimeEnvironment environment;
    cosmo::service::SystemOperationServiceImpl sysOpSvc;
    const auto missing_archive = fs::temp_directory_path() / "cosmo-source-upgrade-must-not-be-read.tar.gz";

    SECTION("rejects before inspecting an archive when SOURCE is active") {
        REQUIRE(setenv(kSourceRuntimeEnvironment, "1", 1) == 0);
        REQUIRE(sysOpSvc.Upgrade(missing_archive.string()) == cosmo::util::ErrorEnum::OperationNotSupport);
    }

    SECTION("preserves the ordinary upgrade path when SOURCE is inactive") {
        REQUIRE(sysOpSvc.Upgrade(missing_archive.string()) ==
                cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
    }
}

TEST_CASE("PacketUpgrade accepts cosmo tar.gz package names", "[system][upgrade]") {
    std::string md5sum;

    auto result = cosmo::UpgradeFileNameCheck("cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.tar.gz", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::Success);
    REQUIRE(md5sum == "52d08574819464a735d4b0a90f26c924");

    result = cosmo::UpgradeFileNameCheck("cosmo-v1.1.0-52D08574819464A735D4B0A90F26C924.tar.gz", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::Success);
    REQUIRE(md5sum == "52d08574819464a735d4b0a90f26c924");

    result = cosmo::UpgradeFileNameCheck("cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.mpkt", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
}

TEST_CASE("PacketUpgrade rejects empty filename", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade rejects random filename", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("random.txt", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade rejects missing md5", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("cosmo-V1.0.0.tar.gz", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade accepts only canonical signed release names", "[system][upgrade][signed]") {
    REQUIRE(cosmo::SignedReleaseFileNameCheck("cosmo-release-factory-v23.1.tar.gz") ==
            cosmo::util::ErrorEnum::Success);
    REQUIRE(cosmo::SignedReleaseFileNameCheck("cosmo-release-a.tar.gz") == cosmo::util::ErrorEnum::Success);
    REQUIRE(cosmo::SignedReleaseFileNameCheck("cosmo-release-" + std::string(64, 'a') + ".tar.gz") ==
            cosmo::util::ErrorEnum::Success);
    REQUIRE(cosmo::SignedReleaseFileNameCheck("cosmo-release-.tar.gz") != cosmo::util::ErrorEnum::Success);
    REQUIRE(cosmo::SignedReleaseFileNameCheck("cosmo-release-Factory.tar.gz") !=
            cosmo::util::ErrorEnum::Success);
    REQUIRE(cosmo::SignedReleaseFileNameCheck("cosmo-release-../escape.tar.gz") !=
            cosmo::util::ErrorEnum::Success);
    REQUIRE(cosmo::SignedReleaseFileNameCheck(
                "cosmo-release-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.tar.gz") !=
            cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade validates archive boundaries before extraction", "[system][upgrade][archive]") {
    const auto root      = fs::temp_directory_path() / "cosmo_packet_upgrade_validation_test";
    const auto data_root = root / "data";
    const auto app_root  = root / "app";
    const auto staging   = root / "staging";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(staging);
    cosmo::path::OverrideRootPathForTest(data_root.string(), app_root.string());

    SECTION("accepts a valid package with the expected layout") {
        const auto source = root / "valid_source";
        CreateUpgradeLayout(source);
        const auto unsigned_archive = staging / "valid.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging);
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
        const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
        REQUIRE(fs::is_regular_file(upgrade_root / archive.filename()));
        REQUIRE(fs::is_directory(upgrade_root / "bin"));
        REQUIRE(fs::is_regular_file(upgrade_root / "scripts" / "install.sh"));
    }

    SECTION("stages a signed release archive without legacy extraction") {
        const auto archive = staging / "cosmo-release-factory-v23.1.tar.gz";
        constexpr std::array<unsigned char, 20> opaque_gzip{
            0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        {
            std::ofstream stream(archive, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(opaque_gzip.data()), opaque_gzip.size());
            REQUIRE(stream.good());
        }

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
        const auto upgrade_root   = fs::path(cosmo::path::GetUpgradePath());
        const auto staged_archive = upgrade_root / archive.filename();
        REQUIRE(fs::is_regular_file(staged_archive));
        REQUIRE_FALSE(fs::exists(archive));

        std::array<unsigned char, opaque_gzip.size()> staged_bytes{};
        std::ifstream stream(staged_archive, std::ios::binary);
        stream.read(reinterpret_cast<char*>(staged_bytes.data()), staged_bytes.size());
        REQUIRE(stream.gcount() == static_cast<std::streamsize>(staged_bytes.size()));
        REQUIRE(stream.peek() == std::char_traits<char>::eof());
        REQUIRE(staged_bytes == opaque_gzip);
    }

    SECTION("rejects traversal without clearing the previous upgrade directory") {
        const auto source = root / "traversal_source";
        fs::create_directories(source);
        std::ofstream(source / "payload") << "unsafe";
        const auto unsigned_archive = staging / "traversal.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "--transform=s|^|../|", "-C",
                                   source.string(), "payload"},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.8");
        REQUIRE_FALSE(archive.empty());
        const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
        std::ofstream(upgrade_root / "sentinel") << "keep";

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(upgrade_root / "sentinel"));
        REQUIRE(fs::is_regular_file(archive));
        REQUIRE_FALSE(fs::exists(data_root / "payload"));
    }

    SECTION("rejects a package containing an unsafe symlink") {
        const auto source = root / "symlink_unsafe_source";
        CreateUpgradeLayout(source);
        // Target traverses above the archive root: must still be rejected even
        // though safe same-directory symlinks are now allowed.
        fs::create_symlink("../../etc/passwd", source / "bin" / "unsafe-link", ec);
        REQUIRE(!ec);
        const auto unsigned_archive = staging / "symlink_unsafe.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.7");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(archive));
    }

    SECTION("accepts a package containing safe internal symlinks") {
        const auto source = root / "symlink_safe_source";
        CreateUpgradeLayout(source);
        // Shared-library versioning chain: libfoo.so -> libfoo.so.1 -> the real
        // file, all relative and confined to lib/. Must be accepted.
        std::ofstream(source / "lib" / "libfoo.so.1.0.0") << "payload";
        fs::create_symlink("libfoo.so.1.0.0", source / "lib" / "libfoo.so.1", ec);
        REQUIRE(!ec);
        fs::create_symlink("libfoo.so.1", source / "lib" / "libfoo.so", ec);
        REQUIRE(!ec);
        const auto unsigned_archive = staging / "symlink_safe.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.4");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
        const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
        REQUIRE(fs::is_symlink(upgrade_root / "lib" / "libfoo.so"));
        REQUIRE(fs::is_regular_file(upgrade_root / "lib" / "libfoo.so.1.0.0"));
    }

    SECTION("accepts a sparse member above the former fixed per-file ceiling when storage permits") {
        constexpr std::uintmax_t kFormerCeiling = 500ULL * 1024 * 1024;
        const auto budget = cosmo::util::InspectStorageResourceBudget(cosmo::path::GetUpgradePath());
        REQUIRE(budget.valid);
        if (budget.usable_bytes <= kFormerCeiling + 1024 * 1024) {
            WARN("Insufficient disposable storage budget to exercise the former 500 MiB boundary");
        } else {
            const auto source = root / "large_source";
            CreateUpgradeLayout(source);
            const auto sparse_file = source / "files" / "large-model.bin";
            {
                std::ofstream stream(sparse_file, std::ios::binary);
                REQUIRE(stream.good());
            }
            fs::resize_file(sparse_file, kFormerCeiling + 1, ec);
            REQUIRE(!ec);
            const auto unsigned_archive = staging / "large.tar.gz";
            std::string output;
            REQUIRE(cosmo::util::Exec(
                        {"tar", "--sparse", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                        output) == 0);
            const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.6");
            REQUIRE_FALSE(archive.empty());

            REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
            const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
            REQUIRE(fs::file_size(upgrade_root / "files" / "large-model.bin") == kFormerCeiling + 1);
        }
    }

    SECTION("rejects an archive already placed inside the destructive output directory") {
        const auto source = root / "inside_source";
        CreateUpgradeLayout(source);
        const auto upgrade_root     = fs::path(cosmo::path::GetUpgradePath());
        const auto unsigned_archive = upgrade_root / "inside.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, upgrade_root, "9.9.5");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(archive));
    }

    fs::remove_all(root, ec);
}

TEST_CASE("SystemOperationServiceImpl: ShowThreadDebugInfo does not crash", "[system][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::SystemOperationServiceImpl sysOpSvc;
    REQUIRE_NOTHROW(sysOpSvc.ShowThreadDebugInfo());
}
