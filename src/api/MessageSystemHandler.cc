// MessageSystemHandler — System message handler.

#include "api/MessageSystemHandler.h"

#include <utility>

#include "api/HttpUploadClaim.h"
#include "media/PreviewPipelineMetrics.h"
#include "media/VideoDecoder.h"
#include "media/VideoEncoder.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IUploadStagingService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/ISystemOperationService.h"
#include "service/system/ITimeService.h"
#include "util/CipherUtil.h"
#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {

namespace {
    // NTP parameter limits
    constexpr int kNtpIntervalMin = 1;
    constexpr int kNtpIntervalMax = 1440;
    constexpr int kNtpEnableMin   = 0;
    constexpr int kNtpEnableMax   = 2;
    constexpr int kTimeZoneIdMin  = 1;
    constexpr int kTimeZoneIdMax  = 95;

    // Reboot/Reset messages
    constexpr const char* kRebootMsg = "Rebooting, please do not power off";
    constexpr const char* kResetMsg  = "Resetting, please do not power off";
    constexpr int kRebootWaitSec     = 40;
}  // namespace

MessageSystemHandler::MessageSystemHandler(service::IConfigReadService& config_read,
                                           service::IConfigWriteService& config_write,
                                           service::IConfigNetworkService& config_network,
                                           service::IDeviceInfoService& device_info,
                                           service::ISystemOperationService& system_op,
                                           service::ITimeService& time_service)
    : config_read_(config_read),
      config_write_(config_write),
      config_network_(config_network),
      device_info_(device_info),
      system_op_(system_op),
      time_service_(time_service) {}

// Device information
System::MsgQueryDeviceInfoSend MessageSystemHandler::Handle(System::MsgQueryDeviceInfoRecv&& /*data*/,
                                                            std::error_condition& /*errc*/) {
    System::MsgQueryDeviceInfoSend retData{};
    auto info = device_info_.GetDeviceInfo();
    retData.resData.devInfoList.push_back({"deviceType", "设备型号", info.devModel});
    retData.resData.devInfoList.push_back({"hardwareVersion", "固件版本", info.devVersion});
    retData.resData.devInfoList.push_back({"softwareVersion", "软件版本", info.softwareVersion});
    retData.resData.devInfoList.push_back({"deviceSn", "设备SN", info.devSn});
    retData.resData.devInfoList.push_back({"devRunTimeSec", "运行时间", std::to_string(info.appRuntime)});
    return retData;
}

// Hardware resources
System::MsgQueryHardwareResourceSend MessageSystemHandler::Handle(
    System::MsgQueryHardwareResourceRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryHardwareResourceSend retData{};
    double customScore = 0.0;
    auto items         = device_info_.GetHardwareResource(customScore);
    for (auto& item : items) {
        System::MsgQueryHardwareResourceSend::Item it{};
        it.key          = item.key;
        it.name         = item.name;
        it.usedPercent  = item.usedPercent;
        it.usedSize     = item.usedSize;
        it.unusedSize   = item.unusedSize;
        it.available    = item.available;
        it.memoryDomain = item.memoryDomain;
        retData.resData.itemList.push_back(std::move(it));
    }
    retData.resData.customScore               = COSMO_FORMAT("{:.4f}", customScore);
    retData.resData.accelerator               = device_info_.GetGpuUtilization();
    const auto preview                        = media::GetPreviewPipelineMetrics().Snapshot();
    auto& accelerator                         = retData.resData.accelerator;
    accelerator.activePreviewPublishers       = preview.active_publishers;
    accelerator.activePreviewStreams          = preview.active_preview_streams;
    accelerator.activeRawPreviewStreams       = preview.active_raw_preview_streams;
    accelerator.activeAlgorithmPreviewStreams = preview.active_algorithm_preview_streams;
    accelerator.previewStreamStarts           = preview.preview_stream_starts;
    accelerator.previewStreamStops            = preview.preview_stream_stops;
    accelerator.previewStreamFailures         = preview.preview_stream_failures;
    accelerator.osdFrames                     = preview.osd_frames;
    accelerator.osdMs                         = preview.osd_nanoseconds / 1000000.0;
    accelerator.publishedFrames               = preview.published_frames;
    accelerator.publishMs                     = preview.publish_nanoseconds / 1000000.0;
    accelerator.firstFrames                   = preview.first_frames;
    accelerator.firstFrameMs                  = preview.first_frame_nanoseconds / 1000000.0;
    accelerator.firstFrameMaxMs               = preview.first_frame_max_nanoseconds / 1000000.0;
    const auto encoder_capability             = media::VideoEncoder::Probe(media::VideoCodecType::kH264);
    accelerator.videoEncoderAvailable         = encoder_capability.available;
    accelerator.videoEncoderBackend           = encoder_capability.backend;
    accelerator.videoEncoderImplementation    = encoder_capability.implementation;
    accelerator.videoEncoderDetail            = encoder_capability.detail;
    const auto decoder_capability             = media::VideoDecoder::Probe(media::VideoCodecType::kH264);
    accelerator.videoDecoderAvailable         = decoder_capability.available;
    accelerator.videoDecoderBackend           = decoder_capability.backend;
    accelerator.videoDecoderImplementation    = decoder_capability.implementation;
    accelerator.videoDecoderDetail            = decoder_capability.detail;
    accelerator.rgaFrames                     = preview.rga_frames;
    accelerator.rgaMs                         = preview.rga_nanoseconds / 1000000.0;
    accelerator.rgaFailures                   = preview.rga_failures;
    accelerator.mppEncodedFrames              = preview.mpp_encoded_frames;
    accelerator.mppEncodeMs                   = preview.mpp_encode_nanoseconds / 1000000.0;
    accelerator.mppEncodeFailures             = preview.mpp_encode_failures;
    accelerator.mppDecodedFrames              = preview.mpp_decoded_frames;
    accelerator.mppDecodeMs                   = preview.mpp_decode_nanoseconds / 1000000.0;
    accelerator.mppDecodeFailures             = preview.mpp_decode_failures;
    accelerator.mppDecodeFallbacks             = preview.mpp_decode_fallbacks;
    accelerator.mppCopyOutFrames              = preview.mpp_copy_out_frames;
    accelerator.mppCopyOutMs                  = preview.mpp_copy_out_nanoseconds / 1000000.0;
    accelerator.mppCopyOutFailures            = preview.mpp_copy_out_failures;
    accelerator.mppEarlyDroppedFrames         = preview.mpp_early_dropped_frames;
    const auto inference                      = nn::GetInferencePipelineMetrics().Snapshot();
    accelerator.colorConvertFrames            = inference.color_convert_frames;
    accelerator.colorConvertMs                = inference.color_convert_nanoseconds / 1000000.0;
    accelerator.blobConvertFrames             = inference.blob_convert_frames;
    accelerator.blobConvertMs                 = inference.blob_convert_nanoseconds / 1000000.0;
    accelerator.graphForwardFrames            = inference.graph_forward_frames;
    accelerator.graphForwardMs                = inference.graph_forward_nanoseconds / 1000000.0;
    accelerator.graphForwardFailures          = inference.graph_forward_failures;
    accelerator.resultParseFrames             = inference.result_parse_frames;
    accelerator.resultParseMs                 = inference.result_parse_nanoseconds / 1000000.0;
    accelerator.resultParseFailures           = inference.result_parse_failures;
    accelerator.rknnForwards                  = inference.rknn_forwards;
    accelerator.rknnForwardMs                 = inference.rknn_forward_nanoseconds / 1000000.0;
    accelerator.rknnForwardFailures           = inference.rknn_forward_failures;
    accelerator.rknnPrepareCalls              = inference.rknn_prepare_calls;
    accelerator.rknnPrepareMs                 = inference.rknn_prepare_nanoseconds / 1000000.0;
    accelerator.rknnInputsSetCalls            = inference.rknn_inputs_set_calls;
    accelerator.rknnInputsSetMs               = inference.rknn_inputs_set_nanoseconds / 1000000.0;
    accelerator.rknnRunCalls                  = inference.rknn_run_calls;
    accelerator.rknnRunMs                     = inference.rknn_run_nanoseconds / 1000000.0;
    accelerator.rknnOutputsGetCalls           = inference.rknn_outputs_get_calls;
    accelerator.rknnOutputsGetMs              = inference.rknn_outputs_get_nanoseconds / 1000000.0;
    accelerator.rknnOutputTransformCalls      = inference.rknn_output_transform_calls;
    accelerator.rknnOutputTransformMs         =
        inference.rknn_output_transform_nanoseconds / 1000000.0;
    accelerator.rknnMutexWaitCalls            = inference.rknn_mutex_wait_calls;
    accelerator.rknnMutexWaitMs               = inference.rknn_mutex_wait_nanoseconds / 1000000.0;
    accelerator.rknnDetectorForwards          = inference.rknn_detector_forwards;
    accelerator.rknnDetectorForwardMs         =
        inference.rknn_detector_forward_nanoseconds / 1000000.0;
    accelerator.rknnDetectorForwardFailures   = inference.rknn_detector_forward_failures;
    accelerator.rknnDetectorPrepareCalls      = inference.rknn_detector_prepare_calls;
    accelerator.rknnDetectorPrepareMs         =
        inference.rknn_detector_prepare_nanoseconds / 1000000.0;
    accelerator.rknnDetectorInputsSetCalls    = inference.rknn_detector_inputs_set_calls;
    accelerator.rknnDetectorInputsSetMs       =
        inference.rknn_detector_inputs_set_nanoseconds / 1000000.0;
    accelerator.rknnDetectorRunCalls          = inference.rknn_detector_run_calls;
    accelerator.rknnDetectorRunMs             =
        inference.rknn_detector_run_nanoseconds / 1000000.0;
    accelerator.rknnDetectorOutputsGetCalls   = inference.rknn_detector_outputs_get_calls;
    accelerator.rknnDetectorOutputsGetMs      =
        inference.rknn_detector_outputs_get_nanoseconds / 1000000.0;
    accelerator.rknnDetectorOutputTransformCalls =
        inference.rknn_detector_output_transform_calls;
    accelerator.rknnDetectorOutputTransformMs =
        inference.rknn_detector_output_transform_nanoseconds / 1000000.0;
    accelerator.rknnDetectorMutexWaitCalls    = inference.rknn_detector_mutex_wait_calls;
    accelerator.rknnDetectorMutexWaitMs       =
        inference.rknn_detector_mutex_wait_nanoseconds / 1000000.0;
    accelerator.rknnPreprocessFastHits        = inference.rknn_preprocess_fast_hits;
    accelerator.rknnRgaFillCalls              = inference.rknn_rga_fill_calls;
    accelerator.rknnRgaFillMs                 = inference.rknn_rga_fill_nanoseconds / 1000000.0;
    accelerator.rknnRgaResizeColorCalls       = inference.rknn_rga_resize_color_calls;
    accelerator.rknnRgaResizeColorMs          =
        inference.rknn_rga_resize_color_nanoseconds / 1000000.0;
    accelerator.rknnRgaFailures               = inference.rknn_rga_failures;
    accelerator.rknnCpuResizeFallbackCalls    = inference.rknn_cpu_resize_fallback_calls;
    accelerator.rknnCpuResizeFallbackMs       =
        inference.rknn_cpu_resize_fallback_nanoseconds / 1000000.0;
    accelerator.rknnCpuNormalizeFallbackCalls = inference.rknn_cpu_normalize_fallback_calls;
    accelerator.rknnCpuNormalizeFallbackMs    =
        inference.rknn_cpu_normalize_fallback_nanoseconds / 1000000.0;
    accelerator.rknnNativeInputMapCalls       = inference.rknn_native_input_map_calls;
    accelerator.rknnNativeInputMapMs          =
        inference.rknn_native_input_map_nanoseconds / 1000000.0;
    accelerator.rknnNativeInt8Inputs          = inference.rknn_native_int8_inputs;
    accelerator.rknnFloatInputs               = inference.rknn_float_inputs;
    accelerator.rknnInputCompatibilityFallbacks = inference.rknn_input_compatibility_fallbacks;
    accelerator.yolov8PostprocessCalls        = inference.yolov8_postprocess_calls;
    accelerator.yolov8PostprocessMs           =
        inference.yolov8_postprocess_nanoseconds / 1000000.0;
    accelerator.yolov8NmsCalls                = inference.yolov8_nms_calls;
    accelerator.yolov8NmsMs                   = inference.yolov8_nms_nanoseconds / 1000000.0;
    return retData;
}

// Time query
System::MsgQueryTimeSend MessageSystemHandler::Handle(System::MsgQueryTimeRecv&& /*data*/,
                                                      std::error_condition& /*errc*/) {
    System::MsgQueryTimeSend retData{};
    std::vector<service::TimeZoneItem> zones;
    auto ts = time_service_.GetTimeStatus(zones);

    retData.resData.timeStatus.timestamp     = ts.timestamp;
    retData.resData.timeStatus.timeString    = ts.timeString;
    retData.resData.timeStatus.timeZoneValue = ts.timeZoneValue;
    retData.resData.timeStatus.timeZoneId    = ts.timeZoneId;
    retData.resData.timeStatus.ntp.enable    = ts.ntp.enable;
    retData.resData.timeStatus.ntp.server    = ts.ntp.server;
    retData.resData.timeStatus.ntp.interval  = ts.ntp.interval;
    retData.resData.timeStatus.ntp.port      = ts.ntp.port;

    for (auto& z : zones) {
        System::MsgQueryTimeSend::ZoneInfo zf{};
        zf.name  = z.name;
        zf.value = z.value;
        zf.id    = z.id;
        retData.resData.zoneInfoList.push_back(std::move(zf));
    }
    return retData;
}

// NTP sync
System::MsgNTPDateSend MessageSystemHandler::Handle(System::MsgNTPDateRecv&& data,
                                                    std::error_condition& errc) {
    System::MsgNTPDateSend retData{};
    if (data.interval < kNtpIntervalMin || data.interval > kNtpIntervalMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (data.ntpEnable < kNtpEnableMin || data.ntpEnable > kNtpEnableMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (data.timeZoneId < kTimeZoneIdMin || data.timeZoneId > kTimeZoneIdMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if ((1 == data.ntpEnable || 2 == data.ntpEnable) && data.ntpServer.empty()) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    if (data.ntpPort <= 0 || data.ntpPort > 65535) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    service::NtpConfig config{data.ntpEnable, data.ntpServer, data.ntpPort, data.interval};
    errc = time_service_.SyncNtp(config, data.timeZoneId);
    return retData;
}

// Time setting
System::MsgModifyTimeSend MessageSystemHandler::Handle(System::MsgModifyTimeRecv&& data,
                                                       std::error_condition& errc) {
    System::MsgModifyTimeSend retData{};
    if (data.timeZoneId < kTimeZoneIdMin || data.timeZoneId > kTimeZoneIdMax) {
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    errc = time_service_.SetTime(data.timestamp, data.timeZoneId);
    return retData;
}

// Photo quality reset
System::MsgResetPictureQualitySend MessageSystemHandler::Handle(System::MsgResetPictureQualityRecv&& /*data*/,
                                                                std::error_condition& errc) {
    System::MsgResetPictureQualitySend retData{};
    errc = config_write_.ResetPictureQuality();
    return retData;
}

// Photo quality setting
System::MsgSetPictureQualitySend MessageSystemHandler::Handle(System::MsgSetPictureQualityRecv&& data,
                                                              std::error_condition& errc) {
    System::MsgSetPictureQualitySend retData{};
    CfgAlarmParamOverviewInfo info;
    info.picQuality         = data.fullPictureQuality;
    info.areaOverview       = data.areaOverlay;
    info.targetBoxOverview  = data.targetBoxOverlay;
    info.targetSizeOverview = data.targetSizeOverlay;
    info.alarmTypeOverview  = data.alarmNameOverlay;
    errc                    = config_write_.SetPictureQuality(info);
    return retData;
}

// Photo quality query
System::MsgQueryPictureQualitySend MessageSystemHandler::Handle(System::MsgQueryPictureQualityRecv&& /*data*/,
                                                                std::error_condition& /*errc*/) {
    System::MsgQueryPictureQualitySend retData{};
    auto info                          = config_read_.GetPictureQuality();
    retData.resData.fullPictureQuality = info.picQuality;
    retData.resData.areaOverlay        = info.areaOverview;
    retData.resData.targetBoxOverlay   = info.targetBoxOverview;
    retData.resData.targetSizeOverlay  = info.targetSizeOverview;
    retData.resData.alarmNameOverlay   = info.alarmTypeOverview;
    return retData;
}

// Alarm recording time reset
System::MsgResetAlarmVideoDurationSend MessageSystemHandler::Handle(
    System::MsgResetAlarmVideoDurationRecv&& /*data*/, std::error_condition& errc) {
    System::MsgResetAlarmVideoDurationSend retData{};
    errc = config_write_.ResetAlarmVideoDuration();
    return retData;
}

// Alarm recording time setting
System::MsgSetAlarmVideoDurationSend MessageSystemHandler::Handle(System::MsgSetAlarmVideoDurationRecv&& data,
                                                                  std::error_condition& errc) {
    System::MsgSetAlarmVideoDurationSend retData{};
    CfgAlarmParamVideoRecordInfo info;
    info.bopen         = data.enable;
    info.preDuration   = data.videoPreTime;
    info.aftreDuration = data.videoPostTime;
    errc               = config_write_.SetAlarmVideoDuration(info);
    return retData;
}

// Alarm recording time query
System::MsgQueryAlarmVideoDurationSend MessageSystemHandler::Handle(
    System::MsgQueryAlarmVideoDurationRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryAlarmVideoDurationSend retData{};
    auto info                     = config_read_.GetAlarmVideoDuration();
    retData.resData.enable        = info.bopen;
    retData.resData.videoPreTime  = info.preDuration;
    retData.resData.videoPostTime = info.aftreDuration;
    return retData;
}

// Scheduled reboot reset
System::MsgResetDevRestartParamSend MessageSystemHandler::Handle(
    System::MsgResetDevRestartParamRecv&& /*data*/, std::error_condition& errc) {
    System::MsgResetDevRestartParamSend retData{};
    errc = config_write_.ResetRebootParam();
    return retData;
}

// Scheduled reboot setting
System::MsgModifyDevRestartParamSend MessageSystemHandler::Handle(System::MsgModifyDevRestartParamRecv&& data,
                                                                  std::error_condition& errc) {
    System::MsgModifyDevRestartParamSend retData{};
    CfgRebootParamInfo info;
    info.isTimingRestart     = data.isTimingRestart;
    info.weekDay             = data.weekDay;
    const auto& restart_time = data.restartTime.ToRefString();

    if (restart_time.empty()) {
        LOG_WARN("{}", "[TimerRestart] Invalid empty restartTime");
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }

    if (restart_time.size() != 5) {
        LOG_WARN("[TimerRestart] Analysis invalid restartTime:{}", restart_time);
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    try {
        info.restartTimeSec = util::HMSTime(restart_time).ToInt();
    } catch (const std::exception&) {
        LOG_WARN("[TimerRestart] Analysis invalid restartTime:{}", restart_time);
        errc = util::ErrorEnum::ParameterException;
        return retData;
    }
    errc = config_write_.SetRebootParam(info);
    return retData;
}

// Scheduled reboot query
System::MsgQueryDevRestartParamSend MessageSystemHandler::Handle(
    System::MsgQueryDevRestartParamRecv&& /*data*/, std::error_condition& /*errc*/) {
    System::MsgQueryDevRestartParamSend retData{};
    auto info                       = config_read_.GetRebootParam();
    retData.resData.isTimingRestart = info.isTimingRestart;
    retData.resData.weekDay         = info.weekDay;
    retData.resData.restartTime =
        COSMO_FORMAT("{:02}:{:02}", info.restartTimeSec / 3600, (info.restartTimeSec % 3600) / 60);
    return retData;
}

// Reboot/Reset
System::MsgResetSystemSend MessageSystemHandler::Handle(System::MsgResetSystemRecv&& data,
                                                        std::error_condition& errc) {
    System::MsgResetSystemSend retData{};
    switch (data.resetOperation) {
        case 0: {
            retData.resData.locationUrl = "/";
            retData.resData.waitSeconds = kRebootWaitSec;
            retData.resultMsg           = kRebootMsg;
            system_op_.RebootDevice("Interface Reboot");
        } break;
        case 1: {
            retData.resData.locationUrl = "/";
            retData.resData.waitSeconds = kRebootWaitSec;
            retData.resultMsg           = kResetMsg;
            system_op_.ResetDevice("Interface Reset");
        } break;
        default: {
            errc = util::ErrorEnum::ParameterException;
        } break;
    }
    return retData;
}

// Config and log export
System::MsgExportFileSend MessageSystemHandler::Handle(System::MsgExportFileRecv&& data,
                                                       std::error_condition& errc) {
    System::MsgExportFileSend retData{};
    if (data.exportType != 1) {
        throw util::ErrorMessage(util::ErrorEnum::OperationNotSupport);
    }
    errc = system_op_.ExportLogs(retData.resData.fileName, retData.resData.fileUrl);
    return retData;
}

// Upgrade
System::MsgUpgradeSend MessageSystemHandler::Handle(System::MsgUpgradeRecv&& data,
                                                    std::error_condition& errc) {
    System::MsgUpgradeSend retData{};
    if (data.filePath.empty() || !util::FileExist(data.filePath) || util::GetFileSize(data.filePath) == 0) {
        LOG_WARN("upgrade upload file invalid, fileName={}, filePath={}, contentLength={}", data.fileName,
                 data.filePath, data.contentLength);
        errc = util::ErrorEnum::UpLoadDataEmpty;
        return retData;
    }
    errc = system_op_.Upgrade(data.filePath);
    return retData;
}

System::MsgUpgradeSend MessageSystemHandler::Handle(System::MsgUpgradeRecv&& data,
                                                    const RequestDispatchContext& context,
                                                    std::error_condition& errc) {
    System::MsgUpgradeSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty()) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    service::StagedFileLease lease;
    if (!data.uploadId.empty()) {
        errc = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().Consume(
            context.principal, data.uploadId, service::UploadPurpose::kUpgrade, lease);
    } else {
        errc = detail::ClaimHttpUpload(context, data.filePath, service::UploadPurpose::kUpgrade, lease);
    }
    if (errc) {
        return result;
    }
    if (!lease.Revalidate()) {
        errc = util::ErrorEnum::FileAnalysisFailed;
        return result;
    }
    data.filePath = lease.Path();
    return Handle(std::move(data), errc);
}

}  // namespace cosmo
