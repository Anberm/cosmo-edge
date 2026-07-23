// Receive error policy for multicast device discovery.
#pragma once

#include <string_view>

namespace cosmo::service::detail {

enum class MulticastReceiveAction {
    kRetry,
    kRestartSocket,
};

MulticastReceiveAction ClassifyMulticastReceiveError(int error_code);

bool IsProductionDiscoveryCommandAllowed(std::string_view command);

bool IsProductionDiscoveryCommandAuthenticationRequired(std::string_view command);

}  // namespace cosmo::service::detail
