// Concrete IEventNotifier implementation — owns the uWebSocket
// server and event queue routing (migrated from HttpuWebSockets,
// ClientMessageHandler, and TaskClientMsgHandle singletons).

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "service/event/IEventNotifier.h"

// Forward-declare uWebSockets types to keep header lightweight.
namespace uWS {
struct Loop;
template <bool SSL>
struct TemplatedApp;
template <bool SSL, bool isServer>
struct WebSocket;
}  // namespace uWS

struct us_listen_socket_t;

namespace cosmo::service {

class EventNotifierImpl final : public IEventNotifier {
public:
    EventNotifierImpl();
    ~EventNotifierImpl();

    // ---- WebSocket server lifecycle ----
    bool InitializeWebSocket(const std::string& hostIp, int port) override;
    void ShutdownWebSocket() override;

    // ---- IEventNotifier ----
    void WebSocketEventPush(cosmo::CMsgOnEventsReq& eventData) override;
    bool NotifyComplete(cosmo::CMsgOnCompleteReq& req, cosmo::CMsgOnCompleteRsp& rsp) override;
    bool NotifyInfo(cosmo::CMsgOnInfoReq& req, cosmo::CMsgonInfoRsp& rsp) override;
    bool GetVideoPlayUrl(cosmo::CMsgGetVideoPlayReq& req, cosmo::CMsgGetVideoPlayRsp& rsp) override;

    void SetEventPostQue(cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>& que) override;
    void ClearEventPostQue(const cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>& que) override;
    void SetCollectPostQue(cosmo::AsyncQueue<cosmo::CMsgCollectRptReq>& que) override;
    void SetFaceEventPostQue(cosmo::AsyncQueue<cosmo::CMsgFaceEventReq>& que) override;

    void EventPush(cosmo::CMsgOnEventsReq& msg) override;
    void FaceEventPush(cosmo::CMsgFaceEventReq& msg) override;
    void CollectPush(cosmo::CMsgCollectRptReq& msg) override;

private:
    enum class ServerState {
        kStopped,
        kStarting,
        kRunning,
        kStopping,
    };

    // ---- WebSocket internals ----
    bool StartServer(const std::string& ip, int port);
    void StopServer();
    void InitWebSocketServer(const std::function<void(std::string_view)>& onMessage);
    void SendWebSocketMsg(std::string_view url, std::string_view msg);
    void SendWebSocketMsgOnLoop(std::string_view url, std::string_view msg);
    void CloseWebSocketServerOnLoop();
    bool HasWebSocketConnect(std::string_view url);

    // All uWebSockets objects and connection pointers are accessed on loop_'s thread.
    std::unique_ptr<uWS::TemplatedApp<false>> http_app_;
    us_listen_socket_t* http_socket_{nullptr};
    std::map<std::string, std::vector<uWS::WebSocket<false, true>*>, std::less<>> ws_connections_;
    size_t ws_connection_count_{0};

    std::mutex server_mtx_;
    std::condition_variable server_state_cv_;
    uWS::Loop* loop_{nullptr};
    ServerState server_state_{ServerState::kStopped};
    std::mutex shutdown_mtx_;
    std::atomic<bool> has_ws_connection_{false};
    std::thread server_thread_;

    // Internal queue that serialises events to JSON and sends via WebSocket.
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> ws_event_que_;

    // ---- Queue pointers (migrated from TaskClientMsgHandle) ----
    std::mutex post_queue_mtx_;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>* event_que_{nullptr};
    cosmo::AsyncQueue<cosmo::CMsgFaceEventReq>* face_event_que_{nullptr};
    cosmo::AsyncQueue<cosmo::CMsgCollectRptReq>* collect_que_{nullptr};
};

}  // namespace cosmo::service
