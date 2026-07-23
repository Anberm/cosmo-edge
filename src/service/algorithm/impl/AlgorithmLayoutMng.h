// AIBox platform layout operations — stateless file operations extracted from
// AlgorithmPacketMng

#pragma once

#include <string>
#include <vector>

#include "util/ErrorCode.h"
#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service::detail {

/// Stateless layout file operations for the AIBox platform.
/// All methods operate directly on the filesystem without shared state.
class AlgorithmLayoutMng {
public:
    static cosmo::util::ErrorEnum LayoutSave(const algorithm::LayoutSaveReq& req);
    static cosmo::util::ErrorEnum GetLayoutDetail(const std::string& id, const std::string& filePath,
                                                  algorithm::LayoutDetailResult& outResult);
    static cosmo::util::ErrorEnum GetLayoutList(const std::string& supplier, int usage,
                                                const std::string& filePath,
                                                algorithm::LayoutListResult& outResult);
    static cosmo::util::ErrorEnum LayoutExportSingle(const std::string& code, const std::string& name,
                                                     const std::string& category, const std::string& supplier,
                                                     const std::string& versionId,
                                                     algorithm::LayoutExportResult& outResult);
    static cosmo::util::ErrorEnum LayoutExportAll(const std::string& algorithmName,
                                                  const std::string& supplier,
                                                  const std::string& algorithmUsage,
                                                  const std::string& algorithmCategory,
                                                  const std::vector<std::string>& algorithmIds,
                                                  algorithm::LayoutExportResult& outResult);
    static cosmo::util::ErrorEnum GetAtomicActionList(int actionUsage, const std::string& filePath,
                                                      algorithm::AtomicActionListResult& outResult);

private:
    static bool ResolveLayoutDirectory(const std::string& requested_path, bool allow_template,
                                       std::string& resolved_path);
    static bool ResolveManagedRegularFile(const std::string& root, const std::string& candidate,
                                          bool must_exist, std::string& resolved_path);
    static bool ResolveActionsFile(const std::string& requested_path, std::string& resolved_path);
};

}  // namespace cosmo::service::detail
