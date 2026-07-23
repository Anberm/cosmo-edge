// Receive error policy for multicast device discovery.

#include "service/network/impl/DeviceDiscoveryReceivePolicy.h"

#include <cerrno>

namespace cosmo::service::detail {

MulticastReceiveAction ClassifyMulticastReceiveError(int error_code) {
    if (error_code == EAGAIN || error_code == EINTR) {
        return MulticastReceiveAction::kRetry;
    }
#if EWOULDBLOCK != EAGAIN
    if (error_code == EWOULDBLOCK) {
        return MulticastReceiveAction::kRetry;
    }
#endif
    return MulticastReceiveAction::kRestartSocket;
}

bool IsProductionDiscoveryCommandAllowed(std::string_view command) {
    return command == "probe";
}

bool IsProductionDiscoveryCommandAuthenticationRequired(std::string_view command) {
    return command == "modifyNetCard" || command == "writeHWInfo" || command == "modifyAuthCode" ||
           command == "queryAuthMessage";
}

}  // namespace cosmo::service::detail
