#include <array>
#include <filesystem>
#include <fstream>
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

namespace fs = std::filesystem;

namespace {

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

    SECTION("rejects a package containing a symbolic link") {
        const auto source = root / "symlink_source";
        CreateUpgradeLayout(source);
        fs::create_symlink(root / "outside-target", source / "bin" / "unsafe-link", ec);
        REQUIRE(!ec);
        const auto unsigned_archive = staging / "symlink.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.7");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(archive));
    }

    SECTION("rejects a sparse member above the declared per-file limit") {
        const auto source = root / "oversize_source";
        CreateUpgradeLayout(source);
        const auto sparse_file = source / "files" / "oversize.bin";
        {
            std::ofstream stream(sparse_file, std::ios::binary);
            REQUIRE(stream.good());
        }
        fs::resize_file(sparse_file, 500ULL * 1024 * 1024 + 1, ec);
        REQUIRE(!ec);
        const auto unsigned_archive = staging / "oversize.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec(
                    {"tar", "--sparse", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                    output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.6");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(archive));
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
