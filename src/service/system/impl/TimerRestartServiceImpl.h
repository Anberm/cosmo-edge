#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "service/system/ITimerRestartService.h"
#include "util/PeriodicTimer.h"

namespace cosmo::service {

class TimerRestartServiceImpl : public ITimerRestartService {
public:
    TimerRestartServiceImpl() = default;
    ~TimerRestartServiceImpl() override;

    void Start() override;
    void Stop() override;

private:
    std::mutex lifecycle_mtx_;
    std::unique_ptr<cosmo::PeriodicTimer> timer_;
    cosmo::TaskId restart_task_id_{cosmo::kInvalidTaskId};
    bool stop_requested_{false};
    void CheckRestart();
    void DateSecRestart(int64_t today_sec);
};
}  // namespace cosmo::service
