// AlgorithmLayoutSave.cc — Layout save logic for AlgorithmLayoutMng.
// Split from AlgorithmLayoutMng.cc to reduce file size (DEBT-007).

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "service/algorithm/impl/AlgorithmLayoutMng.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service::detail {
namespace alg = algorithm;

namespace {

    bool ResolveAlgorithmRoot(std::string& root) {
        const auto configured = cosmo::path::GetAlgorithmPath();
        return cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetResourcePath(), configured,
                                                          cosmo::path::PathEntryType::kDirectory, root);
    }

    bool AllocateExportDirectory(const std::string& prefix, std::string& directory) {
        directory.clear();
        const auto temporary_root = cosmo::path::GetTemporaryDirPath();
        std::string resolved_root;
        if (!cosmo::path::ResolveExistingPathWithinRoot(
                temporary_root, temporary_root, cosmo::path::PathEntryType::kDirectory, resolved_root)) {
            return false;
        }

        const auto name      = prefix + cosmo::util::GenerateUUID();
        const auto candidate = (std::filesystem::path(resolved_root) / name).string();
        std::error_code error;
        if (!std::filesystem::create_directory(candidate, error) || error) {
            return false;
        }
        std::filesystem::permissions(candidate, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, error);
        if (error || !cosmo::path::ResolveExistingPathWithinRoot(
                         resolved_root, candidate, cosmo::path::PathEntryType::kDirectory, directory)) {
            std::error_code cleanup_error;
            std::filesystem::remove(candidate, cleanup_error);
            directory.clear();
            return false;
        }
        return true;
    }

    bool ResolveNewExportFile(const std::string& name, std::string& path) {
        path.clear();
        if (!cosmo::path::IsSafePathComponent(name)) {
            return false;
        }
        const auto root = cosmo::path::GetTemporaryDirPath();
        if (!cosmo::path::ResolvePathWithinRoot(root, (std::filesystem::path(root) / name).string(), path)) {
            return false;
        }
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        return error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
    }

    void RemoveManagedExportDirectory(const std::string& directory) {
        std::string resolved;
        if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetTemporaryDirPath(), directory,
                                                        cosmo::path::PathEntryType::kDirectory, resolved)) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(resolved, error);
    }

    void RemoveManagedExportFile(const std::string& file) {
        std::string resolved;
        if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetTemporaryDirPath(), file,
                                                        cosmo::path::PathEntryType::kRegularFile, resolved)) {
            return;
        }
        std::error_code error;
        std::filesystem::remove(resolved, error);
    }

    bool MakePrivateExportFile(const std::string& file) {
        std::error_code error;
        std::filesystem::permissions(file,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, error);
        return !error;
    }

}  // namespace

cosmo::util::ErrorEnum AlgorithmLayoutMng::LayoutExportSingle(
    const std::string& code, const std::string& name, const std::string& /*category*/,
    const std::string& /*supplier*/, const std::string& /*versionId*/, alg::LayoutExportResult& outResult) {
    if (!cosmo::path::IsSafePathComponent(code, 128)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::string algorithmDir;
    if (!ResolveAlgorithmRoot(algorithmDir)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::string jsonFilePath = cosmo::util::FindPrefixedJsonFile(algorithmDir, code);
    std::string resolvedJsonFilePath;
    if (jsonFilePath.empty() ||
        !cosmo::path::ResolveExistingPathWithinRoot(
            algorithmDir, jsonFilePath, cosmo::path::PathEntryType::kRegularFile, resolvedJsonFilePath))
        return cosmo::util::ErrorEnum::ParameterException;
    jsonFilePath              = resolvedJsonFilePath;
    std::string algorithmName = name;
    if (algorithmName.empty()) {
        nlohmann::json nameDoc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(jsonFilePath, nameDoc) ==
                cosmo::util::ErrorEnum::Success &&
            nameDoc.contains("algorithmName") && nameDoc["algorithmName"].is_string())
            algorithmName = nameDoc["algorithmName"].get<std::string>();
    }
    std::string cleanAlgorithmName;
    for (char c : algorithmName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            cleanAlgorithmName += '_';
        else
            cleanAlgorithmName += c;
    }
    if (cleanAlgorithmName.empty())
        cleanAlgorithmName = "algorithm";
    if (!cosmo::path::IsSafePathComponent(cleanAlgorithmName, 120)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::string tempDir;
    if (!AllocateExportDirectory("algorithm_export_", tempDir)) {
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::error_code ec;

    std::string destJsonFile =
        (std::filesystem::path(tempDir) / (code + "_" + cleanAlgorithmName + ".json")).string();
    std::filesystem::copy_file(jsonFilePath, destJsonFile, std::filesystem::copy_options::none, ec);
    if (ec) {
        RemoveManagedExportDirectory(tempDir);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::string archiveFilePath;
    if (!ResolveNewExportFile("algorithm_" + cosmo::util::GenerateUUID() + ".tar.gz", archiveFilePath)) {
        RemoveManagedExportDirectory(tempDir);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::string jsonFileName = code + "_" + cleanAlgorithmName + ".json";
    std::string outStr;
    if (cosmo::util::Exec({"tar", "-czf", archiveFilePath, "-C", tempDir, jsonFileName}, outStr) != 0) {
        RemoveManagedExportDirectory(tempDir);
        RemoveManagedExportFile(archiveFilePath);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    RemoveManagedExportDirectory(tempDir);
    std::string resolvedArchive;
    if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetTemporaryDirPath(), archiveFilePath,
                                                    cosmo::path::PathEntryType::kRegularFile,
                                                    resolvedArchive) ||
        !MakePrivateExportFile(resolvedArchive)) {
        RemoveManagedExportFile(archiveFilePath);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    outResult.filePath = resolvedArchive;
    outResult.fileName = code + "_" + cleanAlgorithmName + ".tar.gz";
    return cosmo::util::ErrorEnum::Success;
}

namespace {

    // Read a JSON field that may be string or int, return as string (DEBT-C08 B3)
    std::string ReadJsonFieldAsString(const nlohmann::json& doc, const std::string& key) {
        if (!doc.contains(key))
            return "";
        const auto& val = doc[key];
        if (val.is_string())
            return val.get<std::string>();
        if (val.is_number_integer())
            return std::to_string(val.get<int>());
        return "";
    }

    // Check if a document matches all export filter criteria
    bool MatchesExportFilter(const nlohmann::json& doc, const std::unordered_set<std::string>& idSet,
                             bool filterByIds, const std::string& algorithmName, const std::string& supplier,
                             const std::string& algorithmUsage, const std::string& algorithmCategory) {
        if (!doc.contains("algorithmCode"))
            return false;

        if (filterByIds) {
            std::string docCode = ReadJsonFieldAsString(doc, "algorithmCode");
            if (idSet.find(docCode) == idSet.end())
                return false;
        }
        if (!algorithmName.empty()) {
            std::string docName = ReadJsonFieldAsString(doc, "algorithmName");
            if (docName.find(algorithmName) == std::string::npos)
                return false;
        }
        if (!supplier.empty()) {
            if (ReadJsonFieldAsString(doc, "supplier") != supplier)
                return false;
        }
        if (!algorithmUsage.empty()) {
            if (ReadJsonFieldAsString(doc, "algorithmUsage") != algorithmUsage)
                return false;
        }
        if (!algorithmCategory.empty()) {
            if (ReadJsonFieldAsString(doc, "algorithmCategory") != algorithmCategory)
                return false;
        }
        return true;
    }

}  // namespace

cosmo::util::ErrorEnum AlgorithmLayoutMng::LayoutExportAll(const std::string& algorithmName,
                                                           const std::string& supplier,
                                                           const std::string& algorithmUsage,
                                                           const std::string& algorithmCategory,
                                                           const std::vector<std::string>& algorithmIds,
                                                           alg::LayoutExportResult& outResult) {
    if (algorithmIds.size() > 10000 ||
        std::any_of(algorithmIds.begin(), algorithmIds.end(),
                    [](const std::string& id) { return !cosmo::path::IsSafePathComponent(id, 128); })) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::string algorithmDir;
    if (!ResolveAlgorithmRoot(algorithmDir)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    DIR* dir = opendir(algorithmDir.c_str());
    if (!dir) {
        LOG_ERRO("LayoutExportAll: cannot open algorithm directory {}", algorithmDir);
        return cosmo::util::ErrorEnum::ParameterException;
    }

    std::unordered_set<std::string> idSet(algorithmIds.begin(), algorithmIds.end());
    bool filterByIds  = !idSet.empty();
    bool hasAnyFilter = filterByIds || !algorithmName.empty() || !supplier.empty() ||
                        !algorithmUsage.empty() || !algorithmCategory.empty();

    // Create temp directory for export
    std::string tempDir;
    std::error_code ec;
    if (!AllocateExportDirectory("algorithm_export_all_", tempDir)) {
        closedir(dir);
        LOG_ERRO("{}", "LayoutExportAll: cannot create private temp directory");
        return cosmo::util::ErrorEnum::ParameterException;
    }

    int fileCount = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename[0] == '.' || filename.size() < 5 || filename.substr(filename.size() - 5) != ".json")
            continue;
        if (filename == "actions.json" || filename == "atomicModels.json" || filename == "list.json" ||
            filename == "algorithms.json")
            continue;

        if (!cosmo::path::IsSafePathComponent(filename)) {
            continue;
        }
        const auto candidate = (std::filesystem::path(algorithmDir) / filename).string();
        std::string jsonFilePath;
        if (!cosmo::path::ResolveExistingPathWithinRoot(
                algorithmDir, candidate, cosmo::path::PathEntryType::kRegularFile, jsonFilePath)) {
            continue;
        }

        if (hasAnyFilter) {
            nlohmann::json doc;
            if (cosmo::util::JsonFileUtil::ReadJsonFile(jsonFilePath, doc) != cosmo::util::ErrorEnum::Success)
                continue;
            if (!MatchesExportFilter(doc, idSet, filterByIds, algorithmName, supplier, algorithmUsage,
                                     algorithmCategory))
                continue;
        }

        std::string destFile = (std::filesystem::path(tempDir) / filename).string();
        ec.clear();
        std::filesystem::copy_file(jsonFilePath, destFile, std::filesystem::copy_options::none, ec);
        if (!ec)
            fileCount++;
    }
    closedir(dir);

    if (fileCount == 0) {
        RemoveManagedExportDirectory(tempDir);
        LOG_WARN("{}", "LayoutExportAll: no algorithm files found to export");
        return cosmo::util::ErrorEnum::ParameterException;
    }

    // Package as tar.gz
    const std::string archive_name = "algorithms_export_" + cosmo::util::GenerateUUID() + ".tar.gz";
    std::string archiveFilePath;
    if (!ResolveNewExportFile(archive_name, archiveFilePath)) {
        RemoveManagedExportDirectory(tempDir);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    std::string outStr;
    if (cosmo::util::Exec({"tar", "-czf", archiveFilePath, "-C", tempDir, "."}, outStr) != 0) {
        LOG_ERRO("LayoutExportAll: tar failed: {}", outStr);
        RemoveManagedExportDirectory(tempDir);
        RemoveManagedExportFile(archiveFilePath);
        return cosmo::util::ErrorEnum::ParameterException;
    }

    RemoveManagedExportDirectory(tempDir);
    std::string resolvedArchive;
    if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetTemporaryDirPath(), archiveFilePath,
                                                    cosmo::path::PathEntryType::kRegularFile,
                                                    resolvedArchive) ||
        !MakePrivateExportFile(resolvedArchive)) {
        RemoveManagedExportFile(archiveFilePath);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    outResult.filePath = resolvedArchive;
    outResult.fileName = archive_name;
    LOG_INFO("LayoutExportAll: exported {} algorithm(s) to {}", fileCount, archiveFilePath);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmLayoutMng::GetAtomicActionList(int actionUsage, const std::string& filePath,
                                                               alg::AtomicActionListResult& outResult) {
    std::string actionsFilePath;
    if (!ResolveActionsFile(filePath, actionsFilePath)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    nlohmann::json doc;
    if (cosmo::util::JsonFileUtil::ReadJsonArray(actionsFilePath, doc) != cosmo::util::ErrorEnum::Success) {
        return cosmo::util::ErrorEnum::Success;
    }
    for (const auto& action : doc) {
        if (!action.is_object())
            continue;
        int itemUsage = 0;
        if (action.contains("actionUsage")) {
            const auto& usageVal = action["actionUsage"];
            if (usageVal.is_number_integer())
                itemUsage = usageVal.get<int>();
            else if (usageVal.is_string()) {
                try {
                    itemUsage = std::stoi(usageVal.get<std::string>());
                } catch (const std::exception& e) {
                    LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
                    itemUsage = 0;
                }
            }
        }
        int itemType = 0;
        if (action.contains("actionType")) {
            const auto& typeVal = action["actionType"];
            if (typeVal.is_number_integer())
                itemType = typeVal.get<int>();
            else if (typeVal.is_string()) {
                try {
                    itemType = std::stoi(typeVal.get<std::string>());
                } catch (const std::exception& e) {
                    LOG_ERRO("{} caught exception: {}", __FUNCTION__, e.what());
                    itemType = 0;
                }
            }
        }
        if (actionUsage > 0 && itemUsage > 0 && itemUsage != actionUsage)
            continue;
        alg::AtomicAction item;
        item.actionUsage = itemUsage;
        item.actionType  = itemType;
        if (action.contains("id") && action["id"].is_string())
            item.id = action["id"].get<std::string>();
        if (action.contains("name") && action["name"].is_string())
            item.name = action["name"].get<std::string>();
        if (action.contains("actionName") && action["actionName"].is_string())
            item.actionName = action["actionName"].get<std::string>();
        else
            item.actionName = item.name;
        if (item.name.empty())
            item.name = item.actionName;
        if (action.contains("inputParamConfig") && action["inputParamConfig"].is_string())
            item.inputParamConfig = action["inputParamConfig"].get<std::string>();
        outResult.list.push_back(item);
    }
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service::detail
