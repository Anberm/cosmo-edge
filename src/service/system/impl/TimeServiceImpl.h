#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "service/system/ITimeService.h"

namespace cosmo::service {

class TimeServiceImpl : public ITimeService {
public:
    TimeServiceImpl();
    ~TimeServiceImpl() override;

    TimeServiceImpl(const TimeServiceImpl&)            = delete;
    TimeServiceImpl& operator=(const TimeServiceImpl&) = delete;

    // ---- ITimeService ----
    TimeStatus GetTimeStatus(std::vector<TimeZoneItem>& zones) override;
    cosmo::util::ErrorEnum SyncNtp(const NtpConfig& config, int timeZoneId) override;
    cosmo::util::ErrorEnum SetTime(int64_t timestamp, int timeZoneId) override;

private:
    struct TimeZonePersist;
    struct NtpPersist;
    struct TimeZoneCityList;
    struct NtpOperation;

    void LoadConfig();
    static bool IsValidNtpConfig(const NtpPersist& config);
    bool BuildTimeZoneConfig(int cityId, TimeZonePersist& config, std::string& environment) const;
    static bool ApplyTimeZoneEnvironment(const std::string& environment);
    bool SaveNtpConfig(const NtpPersist& config) const;
    bool SaveTimeZoneConfig(const TimeZonePersist& config) const;
    bool PersistConfig(const NtpPersist& ntpConfig, const TimeZonePersist& timeZoneConfig,
                       const NtpPersist& oldNtpConfig) const;
    void RestoreConfig(const NtpPersist& ntpConfig, const TimeZonePersist& timeZoneConfig) const;

    // NTP calibration — connects to server, returns 100ns-unit timestamp (0 on failure)
    static int64_t NtpCalibrate(const std::string& host, int port,
                                const std::shared_ptr<NtpOperation>& operation);

    // NTP periodic sync timer
    bool StartNtpTimer(int intervalMin, const std::string& host, int port);
    void StopNtpTimer();

    mutable std::mutex mutex_;

    // NTP sync thread
    std::thread ntp_thread_;
    std::mutex ntp_mutex_;
    std::shared_ptr<NtpOperation> ntp_operation_;

    std::unique_ptr<TimeZonePersist> tz_persist_;
    std::unique_ptr<NtpPersist> ntp_persist_;
    std::unique_ptr<TimeZoneCityList> city_list_;

    std::string tz_config_path_;
    std::string ntp_config_path_;
};

}  // namespace cosmo::service
