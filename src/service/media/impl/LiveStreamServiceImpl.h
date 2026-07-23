// LiveStreamService implementation
// Manages stream viewers lifecycle, heartbeat watchdog, and encoder counting.
// Migrated from flow/stream/StreamViewerMng singleton.

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward declaration — full definition in flow/stream/StreamViewer.h (included in .cc)
namespace cosmo {
class StreamViewer;
using StreamViewerPtr = std::shared_ptr<StreamViewer>;
}  // namespace cosmo

#include "service/media/ILiveStreamService.h"

namespace cosmo::service {

class LiveStreamServiceImpl : public ILiveStreamService {
public:
    LiveStreamServiceImpl();
    ~LiveStreamServiceImpl() override;

    void Stop() override;

    cosmo::util::ErrorEnum ViewerCreate(const std::string& channelId, const std::string& algCode,
                                        LiveStream::LiveStreamInfo& streamInfo) override;
    bool ViewerDelete(const std::string& channelId, const std::string& algCode) override;
    cosmo::util::ErrorEnum ViewerHeartBeat(const std::string& channelId, const std::string& algCode) override;

    void SetViewCounts(int viewNum) override;

private:
    struct ViewerStartGate {
        std::condition_variable_any cv;
        cosmo::StreamViewerPtr viewer;
        cosmo::util::ErrorEnum result{cosmo::util::ErrorEnum::NotInit};
        size_t participants{1};
        bool requires_encoder{false};
        bool finished{false};
        bool cancelled{false};
    };

    int ViewerEncoderCountLocked() const;
    void CheckAliveTasks();
    void HeartBeatWatchdog();
    std::vector<cosmo::StreamViewerPtr>::iterator FindViewer(const std::string& channelId,
                                                             const std::string& algCode);

    std::shared_mutex mtx_;
    std::vector<cosmo::StreamViewerPtr> viewers_;
    std::unordered_map<std::string, std::shared_ptr<ViewerStartGate>> starting_viewers_;
    std::atomic<int> view_counts_{8};

    std::mutex stop_mtx_;
    std::shared_mutex lifecycle_mtx_;
    std::atomic<bool> stopping_{false};
    bool stopped_{false};
    std::atomic<bool> is_running_{false};
    std::mutex watchdog_mtx_;
    std::condition_variable watchdog_cv_;
    std::thread watchdog_thread_;
};

}  // namespace cosmo::service
