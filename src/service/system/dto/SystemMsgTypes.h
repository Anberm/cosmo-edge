// System info types — MsgMemoryInfo, MsgGpuInfo, MsgDiskInfo, MsgNetInfo, MsgHwInfo.
// Modular DTO header.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct DeviceInfo {
    std::string key;
    std::string name;
    std::string value;
    friend void to_json(nlohmann::json& j, const DeviceInfo& v);
    friend void from_json(const nlohmann::json& j, DeviceInfo& v);
};

struct MsgMemoryInfo {
    int64_t memtotal{-1};
    int64_t memavailable{-1};
    friend void to_json(nlohmann::json& j, const MsgMemoryInfo& v);
    friend void from_json(const nlohmann::json& j, MsgMemoryInfo& v);
};

struct MsgGpuDevUsage {
    double gpuusage{0.0};
    double gpumemusage{0.0};
    int64_t gpumem{0};
    int64_t gpumemtotal{0};
    int64_t gpumemavailable{0};
    friend void to_json(nlohmann::json& j, const MsgGpuDevUsage& v);
    friend void from_json(const nlohmann::json& j, MsgGpuDevUsage& v);
};

struct MsgGpuInfo {
    double gpuusage{0.0};
    bool gpuusageAvailable{true};
    double gpumemusage{0.0};
    int64_t gpumemtotal{0};
    int64_t gpumemavailable{0};
    std::string gpuCapacity;
    std::vector<MsgGpuDevUsage> gpudevusage;
    uint64_t activePreviewPublishers{0};
    uint64_t activePreviewStreams{0};
    uint64_t activeRawPreviewStreams{0};
    uint64_t activeAlgorithmPreviewStreams{0};
    uint64_t previewStreamStarts{0};
    uint64_t previewStreamStops{0};
    uint64_t previewStreamFailures{0};
    uint64_t osdFrames{0};
    double osdMs{0.0};
    uint64_t publishedFrames{0};
    double publishMs{0.0};
    uint64_t firstFrames{0};
    double firstFrameMs{0.0};
    double firstFrameMaxMs{0.0};
    bool videoEncoderAvailable{false};
    std::string videoEncoderBackend;
    std::string videoEncoderImplementation;
    std::string videoEncoderDetail;
    bool videoDecoderAvailable{false};
    std::string videoDecoderBackend;
    std::string videoDecoderImplementation;
    std::string videoDecoderDetail;
    uint64_t rgaFrames{0};
    double rgaMs{0.0};
    uint64_t rgaFailures{0};
    uint64_t mppEncodedFrames{0};
    double mppEncodeMs{0.0};
    uint64_t mppEncodeFailures{0};
    uint64_t mppDecodedFrames{0};
    double mppDecodeMs{0.0};
    uint64_t mppDecodeFailures{0};
    uint64_t mppDecodeFallbacks{0};
    uint64_t mppCopyOutFrames{0};
    double mppCopyOutMs{0.0};
    uint64_t mppCopyOutFailures{0};
    uint64_t mppEarlyDroppedFrames{0};
    uint64_t colorConvertFrames{0};
    double colorConvertMs{0.0};
    uint64_t blobConvertFrames{0};
    double blobConvertMs{0.0};
    uint64_t graphForwardFrames{0};
    double graphForwardMs{0.0};
    uint64_t graphForwardFailures{0};
    uint64_t resultParseFrames{0};
    double resultParseMs{0.0};
    uint64_t resultParseFailures{0};
    uint64_t rknnForwards{0};
    double rknnForwardMs{0.0};
    uint64_t rknnForwardFailures{0};
    uint64_t rknnPrepareCalls{0};
    double rknnPrepareMs{0.0};
    uint64_t rknnInputsSetCalls{0};
    double rknnInputsSetMs{0.0};
    uint64_t rknnRunCalls{0};
    double rknnRunMs{0.0};
    uint64_t rknnOutputsGetCalls{0};
    double rknnOutputsGetMs{0.0};
    uint64_t rknnOutputTransformCalls{0};
    double rknnOutputTransformMs{0.0};
    friend void to_json(nlohmann::json& j, const MsgGpuInfo& v);
    friend void from_json(const nlohmann::json& j, MsgGpuInfo& v);
};

struct MsgDiskInfo {
    int64_t disktotal{-1};
    int64_t diskavailable{-1};
    friend void to_json(nlohmann::json& j, const MsgDiskInfo& v);
    friend void from_json(const nlohmann::json& j, MsgDiskInfo& v);
};

struct MsgNetInfo {
    int64_t networkupperrate{0};
    int64_t networkdownwardrate{0};
    friend void to_json(nlohmann::json& j, const MsgNetInfo& v);
    friend void from_json(const nlohmann::json& j, MsgNetInfo& v);
};
struct MsgHwInfo {
    double cpuusage;
    MsgMemoryInfo memoryinfo;
    MsgGpuInfo gpuinfo;
    MsgDiskInfo diskinfo;
    MsgNetInfo netinfo;
    friend void to_json(nlohmann::json& j, const MsgHwInfo& v);
    friend void from_json(const nlohmann::json& j, MsgHwInfo& v);
};

struct ActionStatus {
    std::string statusCode;
    std::string statusDesc;
    std::string statusDescKey;
    std::string actionId;
    std::string name;

    uint64_t holdCount{0};  // Current queue size (pending packets)

    size_t alarmCount{0};      // Alarm count (alarm nodes only)
    uint64_t insertCount{0};   // Total inserted packets
    uint64_t processCount{0};  // Total processed packets
    uint64_t discardCount{0};  // Total discarded packets

    int64_t periodMs{0};
    uint64_t insertCountPeriod{0};   // Inserted packets in current period
    uint64_t processCountPeriod{0};  // Processed packets in current period
    uint64_t discardCountPeriod{0};  // Discarded packets in current period
    friend void to_json(nlohmann::json& j, const ActionStatus& v);
    friend void from_json(const nlohmann::json& j, ActionStatus& v);
};
using ActionStatusPtr = std::shared_ptr<ActionStatus>;

}  // namespace cosmo
