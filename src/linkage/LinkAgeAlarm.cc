// LinkAgeAlarm — Link Age Alarm implementation.

#include "linkage/LinkAgeAlarm.h"

#include <nlohmann/json.hpp>

#include "linkage/LinkAgeAudioDevice.h"
#include "util/JsonFieldOpt.h"
#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"

namespace cosmo::linkage {
LinkAgeAlarm::LinkAgeAlarm(LinkAgeParamNode& action) : LinkAgeBase(action) {
    for (const auto& param : action.config_object.params) {
        const auto& key = param.key.ToRefString();
        if (IsAlarmAlgorithmsKey(key)) {
            AnalysisTasks(param.value);
        }
    }
}

LinkAgeAlarm::~LinkAgeAlarm() {
    LOG_INFO("{}", "LinkAgeAlarm Delete");
}

void LinkAgeAlarm::AnalysisTasks(const std::string& value) {
    if (!util::DecodeJson(value, param_.tasks)) {
        LOG_WARN("{}", "DecodeJson failed for LinkAgeAlarm tasks");
    }
    LOG_INFO("{} Support Tasks:{}", GetActionId(), value);
}

bool LinkAgeAlarm::DoAlarm(const std::string& channel_id, const std::string& alg_id) {
    auto it =
        std::find_if(param_.tasks.begin(), param_.tasks.end(), [&](const LinkAgeAlarmTaskUnit& task_unit) {
            return ((task_unit.channel_id == channel_id) && (task_unit.algorithm_id == alg_id));
        });
    if (it != param_.tasks.end()) {
        LOG_INFO("{}/{} FlowId:{} Need Alarm For {}_{}", GetActionId(), GetName(), GetFlowActionId(),
                 channel_id, alg_id);
        return true;
    }
    LOG_INFO("{}/{} FlowId:{} Do Not Need Alarm For {}_{}", GetActionId(), GetName(), GetFlowActionId(),
             channel_id, alg_id);
    return false;
}

}  // namespace cosmo::linkage

// Auto-generated JSON serialization
namespace cosmo::linkage {
void from_json(const nlohmann::json& j, LinkAgeAlarmTaskUnit& v) {
    JSON_OPT_KEY(j, v, "channelId", channel_id);
    JSON_OPT_KEY(j, v, "algorithmId", algorithm_id);
}

void to_json(nlohmann::json& j, const LinkAgeAlarmTaskUnit& v) {
    j["channelId"]   = v.channel_id;
    j["algorithmId"] = v.algorithm_id;
}

}  // namespace cosmo::linkage
