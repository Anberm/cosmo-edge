// MsgBaseTypes — Base message types — MsgRecvHead, MsgSendHead, geometry types, enums.

#include "MsgBaseTypes.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgRecvHead& /*h*/) {
    j = nlohmann::json::object();
}

void from_json(const nlohmann::json& /*j*/, MsgRecvHead& /*h*/) {}

void to_json(nlohmann::json& j, const MsgSendHead& h) {
    if (h.msgSendType == MsgSendType::CWAI) {
        j["resCode"] = h.resCode;
        j["resMsg"]  = h.resMsg;
    }
    if (h.msgSendType == MsgSendType::ChinaMobile) {
        j["resultCode"] = h.resultCode;
        j["resultMsg"]  = h.resultMsg;
    }
}

void from_json(const nlohmann::json& j, MsgSendHead& h) {
    if (j.contains("resCode") && !j["resCode"].is_null()) {
        h.msgSendType = MsgSendType::CWAI;
        j.at("resCode").get_to(h.resCode);
        if (j.contains("resMsg") && !j["resMsg"].is_null())
            j.at("resMsg").get_to(h.resMsg);
    }
    if (j.contains("resultCode") && !j["resultCode"].is_null()) {
        h.msgSendType = MsgSendType::ChinaMobile;
        j.at("resultCode").get_to(h.resultCode);
        if (j.contains("resultMsg") && !j["resultMsg"].is_null())
            j.at("resultMsg").get_to(h.resultMsg);
    }
}

void to_json(nlohmann::json& j, const MsgPoint& p) {
    j = nlohmann::json{{"xRatio", p.x}, {"yRatio", p.y}};
}

void from_json(const nlohmann::json& j, MsgPoint& p) {
    if (j.contains("xRatio") && !j["xRatio"].is_null())
        j.at("xRatio").get_to(p.x);
    if (j.contains("yRatio") && !j["yRatio"].is_null())
        j.at("yRatio").get_to(p.y);
}

void to_json(nlohmann::json& j, const MsgRect& r) {
    j = nlohmann::json{{"xRatio", r.x}, {"yRatio", r.y}, {"wRatio", r.width}, {"hRatio", r.height}};
}

void from_json(const nlohmann::json& j, MsgRect& r) {
    if (j.contains("xRatio") && !j["xRatio"].is_null())
        j.at("xRatio").get_to(r.x);
    if (j.contains("yRatio") && !j["yRatio"].is_null())
        j.at("yRatio").get_to(r.y);
    if (j.contains("wRatio") && !j["wRatio"].is_null())
        j.at("wRatio").get_to(r.width);
    if (j.contains("hRatio") && !j["hRatio"].is_null())
        j.at("hRatio").get_to(r.height);
}

void to_json(nlohmann::json& j, const MsgRectReal& r) {
    j = nlohmann::json{{"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}};
}

void from_json(const nlohmann::json& j, MsgRectReal& r) {
    j.at("x").get_to(r.x);
    j.at("y").get_to(r.y);
    j.at("width").get_to(r.width);
    j.at("height").get_to(r.height);
}

bool MsgErrorDetails::Empty() const noexcept {
    return !actualBytes && !limitBytes && !requiredBytes && !availableBytes && !reserveBytes &&
           !actualCount && !limitCount && resource.empty() && purpose.empty();
}

void from_json(const nlohmann::json& j, MsgErrorDetails& v) {
    if (j.contains("actualBytes") && !j["actualBytes"].is_null())
        v.actualBytes = j.at("actualBytes").get<std::uint64_t>();
    if (j.contains("limitBytes") && !j["limitBytes"].is_null())
        v.limitBytes = j.at("limitBytes").get<std::uint64_t>();
    if (j.contains("requiredBytes") && !j["requiredBytes"].is_null())
        v.requiredBytes = j.at("requiredBytes").get<std::uint64_t>();
    if (j.contains("availableBytes") && !j["availableBytes"].is_null())
        v.availableBytes = j.at("availableBytes").get<std::uint64_t>();
    if (j.contains("reserveBytes") && !j["reserveBytes"].is_null())
        v.reserveBytes = j.at("reserveBytes").get<std::uint64_t>();
    if (j.contains("actualCount") && !j["actualCount"].is_null())
        v.actualCount = j.at("actualCount").get<std::uint64_t>();
    if (j.contains("limitCount") && !j["limitCount"].is_null())
        v.limitCount = j.at("limitCount").get<std::uint64_t>();
    if (j.contains("resource") && !j["resource"].is_null())
        j.at("resource").get_to(v.resource);
    if (j.contains("purpose") && !j["purpose"].is_null())
        j.at("purpose").get_to(v.purpose);
}

void to_json(nlohmann::json& j, const MsgErrorDetails& v) {
    j = nlohmann::json::object();
    if (v.actualBytes)
        j["actualBytes"] = *v.actualBytes;
    if (v.limitBytes)
        j["limitBytes"] = *v.limitBytes;
    if (v.requiredBytes)
        j["requiredBytes"] = *v.requiredBytes;
    if (v.availableBytes)
        j["availableBytes"] = *v.availableBytes;
    if (v.reserveBytes)
        j["reserveBytes"] = *v.reserveBytes;
    if (v.actualCount)
        j["actualCount"] = *v.actualCount;
    if (v.limitCount)
        j["limitCount"] = *v.limitCount;
    if (!v.resource.empty())
        j["resource"] = v.resource;
    if (!v.purpose.empty())
        j["purpose"] = v.purpose;
}

void from_json(const nlohmann::json& j, MsgResBase& v) {
    if (j.contains("msgCode") && !j["msgCode"].is_null())
        j.at("msgCode").get_to(v.msgCode);
    if (j.contains("messageKey") && !j["messageKey"].is_null())
        j.at("messageKey").get_to(v.messageKey);
    if (j.contains("msgText") && !j["msgText"].is_null())
        j.at("msgText").get_to(v.msgText);
    if (j.contains("details") && j["details"].is_object())
        j.at("details").get_to(v.details);
    if (j.contains("retryable") && !j["retryable"].is_null())
        j.at("retryable").get_to(v.retryable);
    if (j.contains("recommendedAction") && !j["recommendedAction"].is_null())
        j.at("recommendedAction").get_to(v.recommendedAction);
    if (j.contains("retryAfterSeconds") && !j["retryAfterSeconds"].is_null())
        v.retryAfterSeconds = j.at("retryAfterSeconds").get<std::uint32_t>();
}

void to_json(nlohmann::json& j, const MsgResBase& v) {
    j["msgCode"] = v.msgCode;
    if (!v.messageKey.empty())
        j["messageKey"] = v.messageKey;
    j["msgText"] = v.msgText;
    if (!v.details.Empty())
        j["details"] = v.details;
    if (v.retryable)
        j["retryable"] = true;
    if (!v.recommendedAction.empty())
        j["recommendedAction"] = v.recommendedAction;
    if (v.retryAfterSeconds)
        j["retryAfterSeconds"] = *v.retryAfterSeconds;
}

}  // namespace cosmo
