// SystemOperationServiceImpl — System Operation Service Impl implementation.

#include "service/system/impl/SystemOperationServiceImpl.h"

#include <filesystem>

#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/PacketUpgrade.h"
#include "util/ErrorCode.h"
#include "util/Exec.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ThreadRegistry.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

namespace fs = std::filesystem;

void SystemOperationServiceImpl::RebootDevice(const std::string& reason) {
    reboot_mgr_.Reboot(reason);
}

void SystemOperationServiceImpl::ResetDevice(const std::string& reason) {
    auto base_dir = cosmo::path::GetBaseDir();
    reboot_mgr_.Reset(reason, base_dir);
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::ExportLogs(std::string& fileName, std::string& fileUrl) {
    auto timestamp      = cosmo::util::GetMilliseconds();
    auto web_local_path = cosmo::path::GetWebLocalPath(timestamp);
    std::error_code filesystem_error;
    for (fs::directory_iterator entry(web_local_path, filesystem_error), end;
         !filesystem_error && entry != end; entry.increment(filesystem_error)) {
        const auto& path = entry->path();
        if (path.filename().string().find("IedLog") == 0 && path.extension() == ".tar") {
            LOG_INFO("remove {}", path);
            std::error_code remove_error;
            fs::remove(path, remove_error);
        }
    }
    if (filesystem_error) {
        LOG_WARN("Failed to enumerate export directory {}: {}", web_local_path, filesystem_error.message());
        return cosmo::util::ErrorEnum::SysErr;
    }

    fileName               = "IedLog" + std::to_string(timestamp) + ".tar";
    std::string save_path  = (fs::path(web_local_path) / fileName).string();
    const auto log_path    = fs::path(cosmo::path::GetLogPath()).lexically_normal();
    const auto config_path = fs::path(cosmo::path::GetCfgPath()).lexically_normal();
    if (!fs::is_directory(log_path, filesystem_error) || filesystem_error ||
        !fs::is_directory(config_path, filesystem_error) || filesystem_error) {
        LOG_ERRO("{}", "Cannot export invalid log/config directories");
        return cosmo::util::ErrorEnum::SysErr;
    }

    std::string cmd_out;
    const int ret = cosmo::util::Exec(
        {"tar", "-cf", save_path, "-C", log_path.parent_path().string(), log_path.filename().string(), "-C",
         config_path.parent_path().string(), config_path.filename().string()},
        cmd_out);
    if (ret != 0) {
        LOG_ERRO("tar failed ({}): {}", ret, cmd_out);
        return cosmo::util::ErrorEnum::SysErr;
    }

    fileUrl = (fs::path(cosmo::path::GetWebAcessPath(timestamp)) / fileName).string();
    return {};
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::Upgrade(const std::string& filePath) {
    auto result = cosmo::PacketUpgrade(filePath);
    if (result == cosmo::util::ErrorEnum::Success) {
        reboot_mgr_.Reboot("upgrade Reboot");
    }
    return result;
}

void SystemOperationServiceImpl::ShowThreadDebugInfo() {
    cosmo::util::ShowAllThreads();
}

}  // namespace cosmo::service
