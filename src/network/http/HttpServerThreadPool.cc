// HttpServerThreadPool — Http Server Thread Pool implementation.

#include "network/http/HttpServerThreadPool.h"

#include <array>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "network/http/HttpCommon.h"
#include "network/http/HttpServerThread.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace cosmo::network::http {
namespace {

    constexpr std::size_t kPriority0WorkerIndex = 0;
    constexpr std::size_t kPriority1WorkerIndex = 1;
    constexpr std::size_t kNormalWorkerBegin    = 2;
    constexpr std::size_t kMinimumThreadCount   = kNormalWorkerBegin + 1;

    constexpr std::array<const char*, 2> kPriority0Interfaces = {"dologin", "resetsystem"};
    constexpr std::array<const char*, 2> kPriority1Interfaces = {"threaddebuginfo", "querydeviceinfo"};

}  // namespace

HttpServerThreadPool::HttpServerThreadPool() = default;

HttpServerThreadPool::~HttpServerThreadPool() {
    Uninitialize();
}

bool HttpServerThreadPool::Initialize(int thread_num, HttpServer* server, DispatcherFactory factory) {
    if (!msg_handler_threads_.empty()) {
        LOG_ERRO("{}", "HttpServerThreadPool is already initialized");
        return false;
    }
    if (thread_num < static_cast<int>(kMinimumThreadCount)) {
        LOG_ERRO("HttpServerThreadPool requires at least {} handler threads, got {}", kMinimumThreadCount,
                 thread_num);
        return false;
    }
    if (server == nullptr) {
        LOG_ERRO("{}", "HttpServerThreadPool requires a valid HTTP server");
        return false;
    }
    if (!factory) {
        LOG_ERRO("{}", "HttpServerThreadPool dispatcher factory is not configured");
        return false;
    }

    is_accepting_.store(false, std::memory_order_release);
    const auto handler_count = static_cast<std::size_t>(thread_num);
    std::vector<std::unique_ptr<MsgHanderThread>> candidate_threads;
    std::size_t handler_index = 0;

    try {
        candidate_threads.reserve(handler_count);
        for (; handler_index < handler_count; ++handler_index) {
            auto dispatcher = factory();
            if (!dispatcher) {
                LOG_ERRO("HttpServerThreadPool dispatcher factory returned null for handler {}",
                         handler_index);
                return false;
            }

            auto name = std::string("MsgHanderThread_") + std::to_string(handler_index);
            candidate_threads.emplace_back(
                std::make_unique<MsgHanderThread>(name, server, std::move(dispatcher)));
        }

        for (handler_index = 0; handler_index < handler_count; ++handler_index) {
            if (!candidate_threads[handler_index]->start()) {
                LOG_ERRO("HttpServerThreadPool failed to start handler thread {}", handler_index);
                return false;
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERRO("HttpServerThreadPool initialization failed near handler {}: {}", handler_index, ex.what());
        return false;
    } catch (...) {
        LOG_ERRO("HttpServerThreadPool initialization failed near handler {} with an unknown exception",
                 handler_index);
        return false;
    }

    msg_handler_threads_.swap(candidate_threads);
    is_accepting_.store(true, std::memory_order_release);
    return true;
}

void HttpServerThreadPool::Uninitialize() {
    is_accepting_.store(false, std::memory_order_release);
    for (auto& handler_thread : msg_handler_threads_) {
        handler_thread->DrainAndStop();
    }
    msg_handler_threads_.clear();
}

std::optional<std::size_t> HttpServerThreadPool::MsgInPrioIndex(const cosmo::MsgEnvelope& msg) const {
    const auto* task = static_cast<const HttpReqTask*>(msg.GetData());
    if (task == nullptr) {
        return std::nullopt;
    }

    const auto interface = cosmo::util::ToLower(task->interface);
    for (const auto* priority_interface : kPriority0Interfaces) {
        if (interface.find(priority_interface) != std::string::npos) {
            return kPriority0WorkerIndex;
        }
    }
    for (const auto* priority_interface : kPriority1Interfaces) {
        if (interface.find(priority_interface) != std::string::npos) {
            return kPriority1WorkerIndex;
        }
    }
    return std::nullopt;
}

int HttpServerThreadPool::PutMsg(cosmo::MsgEnvelope&& msg) {
    if (!is_accepting_.load(std::memory_order_acquire) || msg_handler_threads_.size() < kMinimumThreadCount) {
        return -1;
    }

    std::size_t handler_index = kNormalWorkerBegin;
    std::size_t min_msg_count = std::numeric_limits<std::size_t>::max();
    if (const auto priority_index = MsgInPrioIndex(msg)) {
        handler_index = *priority_index;
        min_msg_count = msg_handler_threads_[handler_index]->MsgCount();
    } else {
        for (std::size_t index = kNormalWorkerBegin; index < msg_handler_threads_.size(); ++index) {
            const auto msg_count = msg_handler_threads_[index]->MsgCount();
            if (min_msg_count > msg_count) {
                min_msg_count = msg_count;
                handler_index = index;
            }

            if (min_msg_count == 0) {
                break;
            }
        }
    }

    LOG_INFO("PutMsg To Http Pool {}, This Pool Have {} Tasks in Queue", handler_index, min_msg_count);
    return msg_handler_threads_[handler_index]->Put(std::move(msg));
}

}  // namespace cosmo::network::http
