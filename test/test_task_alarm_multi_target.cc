// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flow/alarm/TaskAlarm.h"
#include "mock/MockAlarmRecordService.h"
#include "mock/MockCameraService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IEventNotifier.h"

namespace {

class CapturingEventNotifier final : public cosmo::service::IEventNotifier {
public:
    bool InitializeWebSocket(const std::string&, int) override {
        return true;
    }
    void ShutdownWebSocket() override {}
    void WebSocketEventPush(cosmo::CMsgOnEventsReq& eventData) override {
        websocketEvents.push_back(eventData);
    }
    bool NotifyComplete(cosmo::CMsgOnCompleteReq&, cosmo::CMsgOnCompleteRsp&) override {
        return false;
    }
    bool NotifyInfo(cosmo::CMsgOnInfoReq&, cosmo::CMsgonInfoRsp&) override {
        return false;
    }
    bool GetVideoPlayUrl(cosmo::CMsgGetVideoPlayReq&, cosmo::CMsgGetVideoPlayRsp&) override {
        return false;
    }
    void SetEventPostQue(cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>&) override {}
    void ClearEventPostQue(const cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>&) override {}
    void SetCollectPostQue(cosmo::AsyncQueue<cosmo::CMsgCollectRptReq>&) override {}
    void SetFaceEventPostQue(cosmo::AsyncQueue<cosmo::CMsgFaceEventReq>&) override {}
    void EventPush(cosmo::CMsgOnEventsReq& msg) override {
        httpEvents.push_back(msg);
    }
    void FaceEventPush(cosmo::CMsgFaceEventReq&) override {}
    void CollectPush(cosmo::CMsgCollectRptReq&) override {}

    std::vector<cosmo::CMsgOnEventsReq> websocketEvents;
    std::vector<cosmo::CMsgOnEventsReq> httpEvents;
};

class ScopedEventNotifierRegistration {
public:
    explicit ScopedEventNotifierRegistration(cosmo::service::IEventNotifier& notifier) {
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::service::IEventNotifier>(&notifier);
    }
    ~ScopedEventNotifierRegistration() {
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::service::IEventNotifier>(nullptr);
    }
};

cosmo::DataAlarmUnit MakeTaskAlarmUnit(int trackId, std::string trackIdText, cosmo::util::Box box) {
    cosmo::DataAlarmUnit unit;
    unit.flowActionId = "area-flow";
    unit.areaId       = "area-1";
    unit.areaName     = "Area 1";
    unit.trackId      = trackId;
    unit.strTrackId   = trackIdText;
    unit.box          = box;
    unit.boxs.push_back(box);

    cosmo::CMsgOnEventsTarget target;
    target.label      = "no-helmet";
    target.confidence = 0.9F;
    target.trackId    = std::move(trackIdText);
    target.box.x      = box.x;
    target.box.y      = box.y;
    target.box.width  = box.width;
    target.box.height = box.height;
    unit.targets.push_back(std::move(target));
    return unit;
}

cosmo::AlgDataPtr MakeTaskAlarmFrame() {
    auto data                                  = std::make_shared<cosmo::AlgData>();
    data->taskDataAlarm.alarmData              = std::make_shared<cosmo::DataAlarm>();
    data->taskDataAlarm.alarmData->multiAlarms = 1;
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(9, "track-9", {300, 80, 60, 160}));
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(4, "track-4", {100, 60, 80, 180}));
    return data;
}

}  // namespace

TEST_CASE("TaskAlarm emits one event with all same-frame abnormal targets", "[alarm][batch][event]") {
    cosmo::test::MockServiceRegistry mocks;
    CapturingEventNotifier notifier;
    ScopedEventNotifierRegistration notifierRegistration(notifier);

    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel")).RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_)).RETURN(true);

    cosmo::ActionNode action;
    action.actionId     = "alarm-action";
    action.actionName   = "Alarm";
    action.flowActionId = "alarm-flow";
    cosmo::TaskAlarm alarm("channel", "task", action);

    cosmo::MsgDynamicKeyValue targetCount;
    targetCount.key   = "param.targetAlarmCount";
    targetCount.value = "1";
    targetCount.keys  = {"param", "targetAlarmCount"};
    std::vector<cosmo::MsgDynamicKeyValue> params{targetCount};
    alarm.SetParam("channel", "task", params);

    alarm.HandFrame(MakeTaskAlarmFrame());

    REQUIRE(alarm.GetAlarmRealCnt() == 1);
    REQUIRE(notifier.httpEvents.size() == 1);
    REQUIRE(notifier.websocketEvents.size() == 1);
    REQUIRE(notifier.httpEvents[0].targets.size() == 2);
    CHECK(notifier.httpEvents[0].recordId == "track-4");
    CHECK(notifier.httpEvents[0].targets[0].trackId == "track-4");
    CHECK(notifier.httpEvents[0].targets[1].trackId == "track-9");

    // Both target counters must be updated by the first batch. With a per-target
    // limit of one, the next frame must not leak a second event for either target.
    alarm.HandFrame(MakeTaskAlarmFrame());
    CHECK(alarm.GetAlarmRealCnt() == 1);
    CHECK(notifier.httpEvents.size() == 1);
    CHECK(notifier.websocketEvents.size() == 1);
}
