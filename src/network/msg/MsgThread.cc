// Message-loop worker thread implementation.

#include "network/msg/MsgThread.h"

#include <unistd.h>

#include <limits>

#include "util/Log.h"

namespace cosmo {

static constexpr int kMsgThreadStop = -99;

bool MsgThread::start() {
    std::lock_guard<std::mutex> stop_lock(stop_mtx_);
    const bool was_accepting = accepting_.exchange(true, std::memory_order_acq_rel);
    is_exit_.store(false, std::memory_order_release);
    if (!Thread::start()) {
        accepting_.store(was_accepting, std::memory_order_release);
        return false;
    }
    return true;
}

void MsgThread::Stop() {
    std::lock_guard<std::mutex> stop_lock(stop_mtx_);
    if (!accepting_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    is_exit_.store(true, std::memory_order_release);
    PutStopMessage();
    Thread::stop();
    is_exit_ = true;
    FinishStop();
}

void MsgThread::DrainAndStop() {
    std::lock_guard<std::mutex> stop_lock(stop_mtx_);
    if (!accepting_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    // The stop message is appended after all accepted work, so the worker
    // processes the existing queue before it exits.
    PutStopMessage();
    Thread::stop();
    is_exit_.store(true, std::memory_order_release);
    FinishStop();
}

void MsgThread::PutStopMessage() {
    std::lock_guard<std::mutex> lock(mtx_);
    msg_list_.emplace_back(kMsgThreadStop, nullptr);
    msg_size_ = msg_list_.size();
    cond_.notify_all();
}

void MsgThread::FinishStop() {
    // Drain remaining messages.
    ClearMsgList();

    // Let subclass release its own resources.
    close();
}

int MsgThread::Put(MsgEnvelope&& msg, bool push_back) {
    bool should_clear = false;
    int result        = -1;
    {
        std::unique_lock<std::mutex> lck(mtx_);

        if (!accepting_.load(std::memory_order_acquire)) {
            should_clear = true;
        } else if (msg_size_ < max_count_) {
            if (push_back) {
                msg_list_.emplace_back(std::move(msg));
            } else {
                msg_list_.emplace_front(std::move(msg));
            }

            msg_size_ = msg_list_.size();
            result    = msg_size_ > static_cast<size_t>(std::numeric_limits<int>::max())
                            ? std::numeric_limits<int>::max()
                            : static_cast<int>(msg_size_);

            if (1 == msg_size_ || is_wait_) {
                cond_.notify_all();
            }
        } else {
            // Queue full — discard after releasing mtx_. ClearMsg is virtual
            // and upload callbacks may re-enter Put().
            should_clear = true;
        }
    }

    if (should_clear) {
        ClearMsg(msg);
    }
    return result;
}

size_t MsgThread::MsgSize() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return msg_size_;
}

bool MsgThread::IsHighLevel() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return msg_size_ > max_count_ / 3;
}

size_t MsgThread::Get(MsgEnvelope& msg) {
    std::unique_lock<std::mutex> lck(mtx_);

    while (msg_list_.empty()) {
        is_wait_ = true;
        cond_.wait(lck);
    }

    is_wait_ = false;
    msg      = std::move(msg_list_.front());

    msg_list_.pop_front();

    msg_size_ = msg_list_.size();

    return msg_size_;
}

void MsgThread::run() {
    is_exit_.store(false, std::memory_order_release);

    while (!is_exit_) {
        try {
            MsgEnvelope msg;

            // Block until a message is available.
            Get(msg);

            // Poison-pill: exit the loop.
            if (kMsgThreadStop == msg.GetMsgId()) {
                break;
            }

            // Dispatch to subclass handler.
            HandleMsg(msg);
        } catch (const std::exception& ex) {
            fprintf(stderr, "HandleMsg error: [%s]\n", ex.what());
            LOG_WARN("HandleMsg error: {}", ex.what());
        }
    }
    is_exit_.store(true, std::memory_order_release);
}

void MsgThread::ClearMsg(MsgEnvelope& /*msg*/) {
    // unique_ptr in MsgEnvelope handles cleanup automatically.
    // Subclasses may override for additional resource release.
}

void MsgThread::ClearMsgList() {
    MsgEnvelopeList pending_messages;
    {
        std::unique_lock<std::mutex> lck(mtx_);
        pending_messages.splice(pending_messages.end(), msg_list_);
        msg_size_ = 0;
    }

    // ClearMsg is a virtual callback and may enqueue/re-enter service code.
    // Never invoke it while holding the queue mutex.
    for (auto& message : pending_messages) {
        ClearMsg(message);
    }
}
}  // namespace cosmo
