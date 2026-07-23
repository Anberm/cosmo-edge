// Unit tests for MessageSystemHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageSystemHandler.h"
#include "media/PreviewPipelineMetrics.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockConfigWriteService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockSystemOperationService.h"
#include "mock/MockTimeService.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

MessageSystemHandler MakeHandler(MockServiceRegistry& mocks) {
    return MessageSystemHandler(mocks.configReadSvc, mocks.configWriteSvc, mocks.configNetSvc,
                                mocks.deviceInfoSvc, mocks.systemOpSvc, mocks.timeSvc);
}

}  // namespace

TEST_CASE("SystemHandler: QueryDeviceInfo", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    service::DeviceBasicInfo info;
    info.devSn      = "SN-TEST-001";
    info.devModel   = "COSMO-X";
    info.devVersion = "1.0.0";
    REQUIRE_CALL(mocks.deviceInfoSvc, GetDeviceInfo()).RETURN(info);

    System::MsgQueryDeviceInfoRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryHardwareResource exposes accelerator preview telemetry", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.deviceInfoSvc, GetHardwareResource(_)).RETURN(std::vector<service::HwResourceItem>{});
    MsgGpuInfo gpu;
    gpu.gpuusage = 0.5;
    REQUIRE_CALL(mocks.deviceInfoSvc, GetGpuUtilization()).RETURN(gpu);
    const auto preview = media::GetPreviewPipelineMetrics().Snapshot();

    System::MsgQueryHardwareResourceRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);

    CHECK(ret.resData.accelerator.gpuusage == 0.5);
    CHECK(ret.resData.accelerator.activePreviewStreams == preview.active_preview_streams);
    CHECK(ret.resData.accelerator.activeAlgorithmPreviewStreams == preview.active_algorithm_preview_streams);
}

TEST_CASE("SystemHandler: QueryPictureQuality", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgAlarmParamOverviewInfo quality;
    REQUIRE_CALL(mocks.configReadSvc, GetPictureQuality()).RETURN(quality);

    System::MsgQueryPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: SetPictureQuality", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetPictureQuality(_)).RETURN(cosmo::util::ErrorEnum::Success);

    System::MsgSetPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ResetPictureQuality", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, ResetPictureQuality()).RETURN(cosmo::util::ErrorEnum::Success);

    System::MsgResetPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryDebugMode", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configReadSvc, GetDebugMode()).RETURN(false);

    System::MsgQueryDebugModeRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDebugMode", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetDebugMode(true));

    System::MsgModifyDebugModeRecv data{};
    data.debugModeOpen = true;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryAlarmVideoDuration", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgAlarmParamVideoRecordInfo duration;
    REQUIRE_CALL(mocks.configReadSvc, GetAlarmVideoDuration()).RETURN(duration);

    System::MsgQueryAlarmVideoDurationRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryDevRebootParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgRebootParamInfo reboot;
    REQUIRE_CALL(mocks.configReadSvc, GetRebootParam()).RETURN(reboot);

    System::MsgQueryDevRestartParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDevRestartParam accepts strict HH:MM", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetRebootParam(_))
        .WITH(_1.isTimingRestart)
        .WITH(_1.weekDay == 3)
        .WITH(_1.restartTimeSec == 9 * 3600 + 5 * 60)
        .RETURN(util::ErrorEnum::Success);

    System::MsgModifyDevRestartParamRecv data{};
    data.isTimingRestart = 1;
    data.weekDay         = 3;
    data.restartTime     = std::string("09:05");
    std::error_condition errc;

    (void)handler.Handle(std::move(data), errc);

    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDevRestartParam rejects malformed HH:MM", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    for (const auto* invalid : {"", "9:05", "09:5", "09:05 ", "09-05", "0a:05", "24:00", "23:60"}) {
        INFO("restartTime=" << invalid);
        System::MsgModifyDevRestartParamRecv data{};
        data.restartTime          = std::string(invalid);
        std::error_condition errc = util::ErrorEnum::Success;

        (void)handler.Handle(std::move(data), errc);

        CHECK(errc == util::ErrorEnum::ParameterException);
    }
}

TEST_CASE("SystemHandler: QuerySystemLogo", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::SystemLogoInfo logo;
    REQUIRE_CALL(mocks.configReadSvc, GetSystemLogo()).RETURN(logo);

    System::MsgQuerySystemLogoRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryRunModeParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);

    System::MsgQueryRunModeParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryHttpInterfaceParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::HttpPushParam httpParam;
    REQUIRE_CALL(mocks.configNetSvc, GetHttpInterfaceParam()).RETURN(httpParam);

    System::MsgQueryHttpInterfaceParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryMqttAdapterParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::MqttParam mqttParam;
    REQUIRE_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttParam);

    System::MsgQueryMqttAdapterParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}
