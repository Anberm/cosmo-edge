// Device discovery service implementation — multicast-based device search.
// Migrated from tools/SearchMulticast into the service layer.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "service/network/DeviceDiscoveryTypes.h"
#include "service/network/IDeviceDiscoveryService.h"
#include "util/AsyncQueue.h"

namespace cosmo::service {

class DeviceDiscoveryServiceImpl : public IDeviceDiscoveryService {
public:
    DeviceDiscoveryServiceImpl(const DeviceDiscoveryServiceImpl&)            = delete;
    DeviceDiscoveryServiceImpl& operator=(const DeviceDiscoveryServiceImpl&) = delete;

    explicit DeviceDiscoveryServiceImpl(const std::string& multicast_ip = "239.255.0.0",
                                        int multicast_port              = 46000);
    ~DeviceDiscoveryServiceImpl() override;

    void Start() override;
    void Stop() override;

private:
    enum class ReceiveLoopExit {
        kStopped,
        kRestartSocket,
    };

    // Multicast initialization (returns 0 on success).
    int InitMulticast();
    bool JoinMulticastGroup();
    bool AddMulticastMembership(uint32_t group_address, uint32_t interface_address);
    void CloseSocket();
    void CloseSocketLocked();

    // Background loops.
    void InitLoop();             // Async init with retry.
    ReceiveLoopExit RecvLoop();  // Receive multicast messages.

    // Message sending.
    void SendMessage(std::string&& msg, const std::string& peer_ip);

    // Message handlers.
    void HandleProbe(DiscoveryProbeRecv&& data);

    std::atomic<bool> inited_{false};
    std::atomic<bool> stop_{false};
    std::string multicast_ip_;
    int multicast_port_;
    int socket_fd_{-1};
    uint32_t multicast_group_address_{0};
    uint32_t multicast_interface_address_{0};
    bool multicast_joined_{false};
    std::mutex thread_mtx_;
    std::mutex socket_mtx_;
    std::thread init_thread_;
    AsyncQueue<DiscoveryProbeRecv> probe_queue_;
};

}  // namespace cosmo::service
