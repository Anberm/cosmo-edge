// PacketUpgrade — Packet Upgrade implementation.

#include "service/system/impl/PacketUpgrade.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include "cryptopp/cryptlib.h"
#include "cryptopp/files.h"
#include "cryptopp/hex.h"
#include "cryptopp/md5.h"
#include "util/ErrorCode.h"
#include "util/Exec.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"

namespace cosmo {

namespace {

    constexpr size_t kMaxUpgradeArchiveEntries         = 20000;
    constexpr std::uintmax_t kMaxUpgradeArchiveBytes   = 500ULL * 1024 * 1024;
    constexpr std::uintmax_t kMaxUpgradeFileBytes      = 500ULL * 1024 * 1024;
    constexpr std::uintmax_t kMaxUpgradeExtractedBytes = 2ULL * 1024 * 1024 * 1024;
    constexpr std::uintmax_t kMaxUpgradeTreeBytes      = kMaxUpgradeArchiveBytes + kMaxUpgradeExtractedBytes;

    bool IsGzipFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        std::array<unsigned char, 2> header{};
        return stream.read(reinterpret_cast<char*>(header.data()), header.size()) && header[0] == 0x1f &&
               header[1] == 0x8b;
    }

    bool NormalizeUpgradeArchiveMember(std::string member, bool is_directory, std::string& normalized) {
        normalized.clear();
        if (member.empty() || member.size() > 512 || member.front() == '/' ||
            member.find('\\') != std::string::npos) {
            return false;
        }

        while (member.size() >= 2 && member.compare(0, 2, "./") == 0) {
            member.erase(0, 2);
        }
        while (!member.empty() && member.back() == '/') {
            member.pop_back();
        }
        if (member.empty() || member == ".") {
            if (!is_directory) {
                return false;
            }
            normalized = ".";
            return true;
        }

        std::istringstream stream(member);
        std::string component;
        while (std::getline(stream, component, '/')) {
            if (!cosmo::path::IsSafePathComponent(component)) {
                return false;
            }
        }
        normalized = std::move(member);
        return true;
    }

    bool ValidateUpgradeArchiveListing(const std::string& archive_path) {
        std::string listing;
        if (util::Exec({"tar", "-tzvf", archive_path, "--quoting-style=escape"}, listing) != 0) {
            LOG_ERRO("cannot inspect upgrade archive: {}", archive_path);
            return false;
        }

        size_t entry_count        = 0;
        std::uintmax_t total_size = 0;
        std::unordered_set<std::string> members;
        std::istringstream stream(listing);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) {
                continue;
            }
            if (line.size() < 10 || std::string("-dlcbpsh").find(line.front()) == std::string::npos) {
                LOG_ERRO("unrecognized upgrade archive listing output: {}", line);
                return false;
            }

            const bool is_directory = line.front() == 'd';
            if (line.front() != '-' && !is_directory) {
                LOG_ERRO("{}", "upgrade archive contains a non-regular entry");
                return false;
            }

            std::istringstream line_stream(line);
            std::string permissions;
            std::string owner;
            std::string date;
            std::string time;
            std::string member;
            std::uintmax_t entry_size = 0;
            if (!(line_stream >> permissions >> owner >> entry_size >> date >> time)) {
                return false;
            }
            std::getline(line_stream >> std::ws, member);

            std::string normalized;
            if (++entry_count > kMaxUpgradeArchiveEntries || entry_size > kMaxUpgradeFileBytes ||
                entry_size > kMaxUpgradeExtractedBytes - total_size ||
                !NormalizeUpgradeArchiveMember(member, is_directory, normalized) ||
                !members.insert(normalized).second) {
                LOG_ERRO("upgrade archive contains unsafe entry: {}", member);
                return false;
            }
            total_size += entry_size;
        }
        return entry_count > 0;
    }

    std::string Md5SumFile(const std::string& filePath) {
        try {
            CryptoPP::Weak::MD5 hash;
            CryptoPP::byte digest[CryptoPP::Weak::MD5::DIGESTSIZE];

            // NOLINT: CryptoPP pipeline takes ownership
            CryptoPP::FileSource file(
                filePath.c_str(), true,
                new CryptoPP::HashFilter(hash, new CryptoPP::ArraySink(digest, sizeof(digest))));

            std::string hex_digest;
            CryptoPP::HexEncoder encoder;
            // NOLINT: CryptoPP pipeline takes ownership
            encoder.Attach(new CryptoPP::StringSink(hex_digest));
            encoder.Put(digest, sizeof(digest));
            encoder.MessageEnd();

            return hex_digest;
        } catch (const CryptoPP::Exception& e) {
            std::cerr << "CryptoPP exception: " << e.what() << std::endl;
            return "";
        } catch (const std::exception& e) {
            std::cerr << "Standard exception: " << e.what() << std::endl;
            return "";
        }
    }

    bool HasExpectedPackageLayout(const fs::path& packageRoot) {
        static constexpr std::array requiredDirs{"bin", "files", "font", "lib", "scripts", "web"};
        for (const char* dir : requiredDirs) {
            std::error_code ec;
            if (!fs::is_directory(packageRoot / dir, ec) || ec) {
                LOG_ERRO("upgrade package missing required directory: {}", (packageRoot / dir).string());
                return false;
            }
        }
        return true;
    }

    bool FindExpectedPackageLayout(const fs::path& extractRoot, fs::path& packageRoot) {
        if (HasExpectedPackageLayout(extractRoot)) {
            packageRoot = extractRoot;
            return true;
        }

        std::error_code ec;
        fs::path candidate;
        int dir_count = 0;
        for (const auto& entry : fs::directory_iterator(extractRoot, ec)) {
            if (ec) {
                return false;
            }
            if (entry.is_directory(ec)) {
                candidate = entry.path();
                ++dir_count;
            }
            if (ec) {
                return false;
            }
        }

        if (dir_count == 1 && HasExpectedPackageLayout(candidate)) {
            packageRoot = candidate;
            return true;
        }

        return false;
    }

}  // namespace

// Filename format: cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz
util::ErrorEnum UpgradeFileNameCheck(std::string file_name, std::string& md5sum) {
    std::smatch matchResult;
    // Match: cosmo-V1.0.0-<32-char-md5>.tar.gz
    static const std::regex regExp(R"(^cosmo-[Vv]([0-9]+\.[0-9]+\.[0-9]+)-([0-9a-fA-F]{32})\.tar\.gz$)");
    if (!regex_match(file_name, matchResult, regExp)) {
        LOG_ERRO("upgrade file name not match: {}", file_name);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }

    std::string file_version = matchResult[1].str();
    md5sum                   = util::ToLower(matchResult[2].str());

    LOG_INFO("Upgrade package: version={}, md5={}", file_version, md5sum);
    return util::ErrorEnum::Success;
}

util::ErrorEnum PacketUpgrade(const fs::path& filePath) {
    std::error_code ec;
    const auto absolute_file = fs::absolute(filePath, ec);
    if (ec || absolute_file.filename().empty() ||
        !cosmo::path::IsSafePathComponent(absolute_file.filename().string(), 255)) {
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    std::string file_name = absolute_file.filename().string();
    std::string file_name_md5sum;
    auto ret = UpgradeFileNameCheck(file_name, file_name_md5sum);
    if (ret != util::ErrorEnum::Success) {
        return ret;
    }

    std::string resolved_file;
    if (!cosmo::path::ResolveExistingPathWithinRoot(
            absolute_file.parent_path().string(), absolute_file.string(),
            cosmo::path::PathEntryType::kRegularFile, resolved_file)) {
        LOG_ERRO("upgrade package is not a safe regular file: {}", filePath.string());
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }

    const auto archive_size = fs::file_size(resolved_file, ec);
    if (ec || archive_size == 0 || archive_size > kMaxUpgradeArchiveBytes || !IsGzipFile(resolved_file)) {
        LOG_ERRO("upgrade package has invalid size or format: {}", resolved_file);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }

    const auto md5Str = Md5SumFile(resolved_file);
    if (md5Str.empty() || (util::ToLower(md5Str) != file_name_md5sum)) {
        LOG_ERRO("upgrade package md5 error, expected={}, actual={}", file_name_md5sum, md5Str);
        return util::ErrorEnum::UpgradeFileNotMatch;
    }
    if (!ValidateUpgradeArchiveListing(resolved_file)) {
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }

    fs::path upgradeFileDir(cosmo::path::GetUpgradePath());
    std::string resolved_upgrade_dir;
    if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetBaseDir(), upgradeFileDir.string(),
                                                    cosmo::path::PathEntryType::kDirectory,
                                                    resolved_upgrade_dir) ||
        cosmo::path::IsWithinRoot(resolved_upgrade_dir, resolved_file)) {
        LOG_ERRO("unsafe upgrade directory or source path: {}", upgradeFileDir.string());
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    upgradeFileDir = resolved_upgrade_dir;

    remove_all(upgradeFileDir, ec);
    if (ec || !create_directory(upgradeFileDir, ec) || ec) {
        LOG_ERRO("cannot create directory {}", upgradeFileDir);
        return util::ErrorEnum::FileOpenFailed;
    }
    fs::permissions(upgradeFileDir, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) {
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::FileOpenFailed;
    }

    std::string upgrade_file_path;
    if (!cosmo::path::ResolvePathWithinRoot(upgradeFileDir.string(), (upgradeFileDir / file_name).string(),
                                            upgrade_file_path)) {
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    const fs::path upgrade_file_name(upgrade_file_path);
    rename(resolved_file, upgrade_file_name, ec);
    LOG_INFO("{} -> {} result: {}", resolved_file, upgrade_file_name.c_str(), ec.message());
    if (ec) {
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::FileMoveFailed;
    }

    // Extract tar.gz
    std::string output;
    const int exit_code =
        util::Exec({"tar", "--extract", "--gzip", "--file", upgrade_file_name.string(), "--directory",
                    upgradeFileDir.string(), "--no-same-owner", "--no-same-permissions"},
                   output);
    if (exit_code != 0) {
        LOG_ERRO("extract upgrade package failed: {}", output);
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    LOG_INFO("upgrade package extracted to {}", upgradeFileDir.c_str());

    if (!cosmo::path::ValidateDirectoryTreeWithinRoot(upgradeFileDir.string(), kMaxUpgradeArchiveEntries + 1,
                                                      kMaxUpgradeFileBytes, kMaxUpgradeTreeBytes)) {
        LOG_ERRO("{}", "upgrade package extracted an unsafe directory tree");
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }

    fs::path packageRoot;
    if (!FindExpectedPackageLayout(upgradeFileDir, packageRoot)) {
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    LOG_INFO("upgrade package layout root: {}", packageRoot.string());

    // Caller (SystemOperationServiceImpl::Upgrade) is responsible for
    // triggering a reboot after this function returns successfully.
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
