#pragma once

#include <atomic>
#include <thread>

#include "network/http/HttpCommon.h"
#include "network/msg/MsgThread.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::network::http {
class HttpServer;

// HTTP request handler thread
class MsgHanderThread : public cosmo::MsgThread {
public:
    explicit MsgHanderThread(const std::string& name, HttpServer* server,
                             std::unique_ptr<cosmo::IRequestDispatcher> dispatcher, size_t maxCount = 8);
    virtual ~MsgHanderThread();

    size_t MsgCount() {
        return ((is_thread_busy_.load(std::memory_order_relaxed) ? 1 : 0) + MsgSize());
    };

protected:
    void HandleMsg(cosmo::MsgEnvelope& msg) override;
    void ClearMsg(cosmo::MsgEnvelope& msg) override;

    void ProcessHttpReqTask(HttpReqTask& task);
    void ProcessHttpOctetReqTask(HttpReqTask& task);

    void DelTmpPath(const HttpReqTask& task);

private:
    void SendHttpAck(HttpResponseCode code, HttpRequestToken request_token, std::string response,
                     std::string request_id);

    std::atomic<bool> is_thread_busy_{false};
    std::unique_ptr<cosmo::IRequestDispatcher> handler_;
    HttpServer* server_{nullptr};
};

}  // namespace cosmo::network::http
