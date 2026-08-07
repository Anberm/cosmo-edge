#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "network/msg/MsgEnvelope.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::network::http {
class MsgHanderThread;
class HttpServer;

class HttpServerThreadPool {
public:
    HttpServerThreadPool();
    virtual ~HttpServerThreadPool();

    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    // Initialize at least three workers. HttpServer serializes lifecycle calls with PutMsg().
    bool Initialize(int thread_num, HttpServer* server, DispatcherFactory factory);

    // Shutdown thread pool
    void Uninitialize();

    // Dispatch message to appropriate thread
    int PutMsg(cosmo::MsgEnvelope&& msg);

private:
    std::optional<std::size_t> MsgInPrioIndex(const cosmo::MsgEnvelope& msg) const;

    std::vector<std::unique_ptr<MsgHanderThread>> msg_handler_threads_;
    std::atomic<bool> is_accepting_{false};
};

}  // namespace cosmo::network::http
