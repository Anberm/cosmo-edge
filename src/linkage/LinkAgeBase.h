#pragma once

#include "linkage/LinkAgeBaseCommon.h"

namespace cosmo::linkage {

class LinkAgeBase {
public:
    explicit LinkAgeBase(LinkAgeParamNode& action);
    virtual ~LinkAgeBase() = default;

    std::string GetActionId() const {
        return action_id_;
    }

    std::string GetFlowActionId() const {
        return flow_action_id_;
    }

    std::string GetPreFlowActionId() const {
        return pre_flow_action_id_;
    }

    std::string GetName() const {
        return action_name_;
    }

    virtual bool DoAlarm(const std::string& channel_id, const std::string& alg_id);

    virtual bool IsAudioDeviceInUse(const std::string& dev_id) const;
    virtual bool IsAudioFileInUse(const std::string& dev_id) const;

private:
    std::string action_id_;
    std::string action_name_;
    std::string flow_action_id_;
    std::string pre_flow_action_id_;
};

using LinkAgeBasePtr = std::shared_ptr<LinkAgeBase>;
}  // namespace cosmo::linkage
