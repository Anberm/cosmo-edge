// LinkAgeBase — Link Age Base implementation.

#include "linkage/LinkAgeBase.h"

#include "util/Log.h"

namespace cosmo::linkage {

LinkAgeBase::LinkAgeBase(LinkAgeParamNode& action)
    : action_id_(action.action_id),
      action_name_(action.action_name),
      flow_action_id_(action.flowActionId),
      pre_flow_action_id_(action.preFlowActionId) {}

bool LinkAgeBase::DoAlarm(const std::string& /*channel_id*/, const std::string& /*alg_id*/) {
    return false;
}

bool LinkAgeBase::IsAudioDeviceInUse(const std::string& /*dev_id*/) const {
    return false;
}
bool LinkAgeBase::IsAudioFileInUse(const std::string& /*dev_id*/) const {
    return false;
}
}  // namespace cosmo::linkage
