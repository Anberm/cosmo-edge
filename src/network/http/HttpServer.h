#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "event2/buffer.h"
#include "event2/event.h"
#include "event2/http.h"
#include "network/http/HttpCommon.h"
#include "network/http/HttpServerThreadPool.h"
#include "network/msg/MsgEnvelope.h"
#include "util/IRequestDispatcher.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"

namespace cosmo::network::http {

class HttpServer {
public:
    HttpServer() = default;
    virtual ~HttpServer();

    HttpServer(const HttpServer&)            = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void SetAppInfo(const std::string& app_key, const std::string& app_secret);

    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    // Initialize the HTTP server
    bool Initialize(const std::string& host_ip, int port, DispatcherFactory factory,
                    HttpServerCallbacks callbacks);

    // Shutdown the HTTP server
    void UnInitialize();

    // Blocking event dispatch loop, call unInitialize() to stop
    void DispatchMsg();

    // Request DispatchMsg() to return without releasing event-thread resources
    // from the calling thread.
    void RequestStop() noexcept;

    // Enqueue an ack result for the event loop to send
    template <typename T>
    void AckResult(HttpResponseCode hcode, HttpRequestToken request_token, T& t, const std::string& req_id) {
        std::string strJsonRes;
        if (!cosmo::util::EncodeJson(t, strJsonRes)) {
            // log failed
        }
        LOG_DEBUG("{}", strJsonRes);
        cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpAck),
                               std::make_unique<HttpAckTask>(static_cast<int>(hcode), request_token,
                                                             std::move(strJsonRes), req_id));

        this->Put(std::move(msg));
    }

    // Enqueue a message
    int Put(cosmo::MsgEnvelope&& msg);

    /// Access callbacks for path resolution (used by MsgHanderThread).
    const HttpServerCallbacks& callbacks() const {
        return callbacks_;
    }

protected:
    // Process response message list
    int HandleRespMsgList();

    // Add response headers
    bool AddHttpHeader(struct evhttp_request* ev_request, const std::string& req_id);
    bool AddHttpOctetHeader(struct evhttp_request* ev_request, const std::string& req_id,
                            const std::string& file_name, long fsize);

    // Common headers: Server, RequestId, AppKey, Nonce, CurTime, CheckSum
    bool AddCommonHeaders(struct evkeyvalq* ev_header, const std::string& req_id);

private:
    enum class LifecycleState {
        kStopped,
        kInitialized,
        kDispatching,
        kStopping,
    };

    struct RequestContext {
        // Non-owning libevent handles. This context is created, read, and
        // destroyed only by the event thread.
        HttpServer* server;
        HttpRequestToken token;
        struct evhttp_request* request;
        struct evhttp_connection* connection;
    };

    struct MultipartSpoolReservation {
        HttpServer* server{nullptr};
        std::string owner;
        std::uint64_t bytes{0};
        bool active{false};

        ~MultipartSpoolReservation();
    };

    // libevent callbacks
    static void ComGenCb(struct evhttp_request* req, void* arg);
    static void RequestCompleteCb(struct evhttp_request* req, void* arg);
    static void ConnectionCloseCb(struct evhttp_connection* connection, void* arg);

    void InsertHttpReqMsg(struct evhttp_request* req, const RequestDispatchContext& context);
    void InsertHttpOctetMsg(struct evhttp_request* req, const RequestDispatchContext& context);
    void InsertHttpMsg(struct evhttp_request* req, InnerMsgId msg_id, const RequestDispatchContext& context);

    std::unique_ptr<HttpReqTask> BuildHttpReqTask(struct evhttp_request* req,
                                                  const RequestDispatchContext& context,
                                                  HttpResponseCode& error_code);
    bool ExtractMultipartBody(HttpReqTask* task, struct evhttp_request* req, const std::string& content_type,
                              HttpResponseCode& error_code);
    bool PrepareRequestContext(struct evhttp_request* req, RequestDispatchContext& context,
                               bool& is_log_request) const;
    bool IsBodySizeAllowed(struct evhttp_request* req) const;
    std::shared_ptr<void> TryReserveMultipartSpool(const RequestDispatchContext& context,
                                                   std::uint64_t bytes);
    void ReleaseMultipartSpool(const std::string& owner, std::uint64_t bytes);

    HttpRequestToken RegisterRequest(struct evhttp_request* req);
    struct evhttp_request* FindRequest(HttpRequestToken token) const;
    void CompleteRequest(HttpRequestToken token, const struct evhttp_request* req);
    void CloseRequest(HttpRequestToken token, struct evhttp_connection* connection);

    void SendImmediateAuthError(struct evhttp_request* req);
    void SendImmediateError(struct evhttp_request* req, HttpResponseCode code);
    void StopAccepting();
    void ShutdownOnEventThread();
    void CleanupEventResources();
    void MarkStopped();

    int DispatchJsonMsg(HttpAckTask* task);
    int DispatchFileMsg(HttpAckTask* task);

    HttpServerThreadPool thread_pool_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_accepting_requests_{false};
    struct event_base* event_base_{nullptr};
    struct evhttp* event_http_{nullptr};
    struct evhttp_bound_socket* bound_socket_{nullptr};

    // Periodic tick event to wake the event loop for response queue processing
    struct event* tick_event_{nullptr};

    // Response message queue
    std::list<cosmo::MsgEnvelope> msg_list_;
    std::mutex msg_mutex_;

    std::unordered_map<HttpRequestToken, std::unique_ptr<RequestContext>> active_requests_;
    HttpRequestToken next_request_token_{1};

    std::mutex multipart_spool_mutex_;
    std::unordered_map<std::string, std::size_t> multipart_spool_requests_by_owner_;
    std::unordered_map<std::string, std::uint64_t> multipart_spool_bytes_by_owner_;
    std::size_t multipart_spool_request_count_{0};
    std::uint64_t multipart_spool_bytes_{0};

    LifecycleState lifecycle_state_{LifecycleState::kStopped};
    std::thread::id event_thread_id_;
    std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_cv_;

    std::string app_key_;
    std::string app_secret_;

    DispatcherFactory dispatcher_factory_;
    std::unique_ptr<cosmo::IRequestDispatcher> request_inspector_;
    HttpServerCallbacks callbacks_;
};

}  // namespace cosmo::network::http
