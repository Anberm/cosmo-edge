#include <atomic>
#include <chrono>
#include <thread>

#include "catch_amalgamated.hpp"
#include "service/event/impl/EventNotifierImpl.h"

using namespace cosmo::service;

TEST_CASE("EventNotifierImpl: construction and destruction", "[EventNotifier]") {
    REQUIRE_NOTHROW([]() { EventNotifierImpl notifier; }());
}

TEST_CASE("EventNotifierImpl: SetEventPostQue and push without processor", "[EventNotifier]") {
    EventNotifierImpl notifier;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_no_proc", 100);
    notifier.SetEventPostQue(eventQue);

    // Push without processor — should not crash
    cosmo::CMsgOnEventsReq req;
    REQUIRE_NOTHROW(notifier.EventPush(req));
}

TEST_CASE("EventNotifierImpl: clearing event queue prevents later delivery", "[EventNotifier]") {
    EventNotifierImpl notifier;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_clear", 100);
    std::atomic<int> processCount{0};
    eventQue.SetProcessor(
        [&processCount](cosmo::CMsgOnEventsReq&&) { processCount.fetch_add(1, std::memory_order_relaxed); });

    notifier.SetEventPostQue(eventQue);
    notifier.ClearEventPostQue(eventQue);
    cosmo::CMsgOnEventsReq req;
    notifier.EventPush(req);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    REQUIRE(processCount.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("EventNotifierImpl: WebSocket and Events", "[EventNotifier]") {
    EventNotifierImpl notifier;

    SECTION("Event queues concurrent push") {
        cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_que", 1000);
        notifier.SetEventPostQue(eventQue);

        std::atomic<int> processCount{0};
        eventQue.SetProcessor([&processCount](cosmo::CMsgOnEventsReq&& req) {
            processCount.fetch_add(1, std::memory_order_relaxed);
        });

        std::thread t1([&]() {
            for (int i = 0; i < 50; ++i) {
                cosmo::CMsgOnEventsReq req;
                notifier.EventPush(req);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < 50; ++i) {
                cosmo::CMsgOnEventsReq req;
                notifier.EventPush(req);
            }
        });

        t1.join();
        t2.join();

        // Wait for queue to process all elements
        for (int i = 0; i < 50 && processCount.load() < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        REQUIRE(processCount.load() == 100);
    }
}

TEST_CASE("EventNotifierImpl: WebSocket shutdown is deferred to server loop", "[EventNotifier]") {
    EventNotifierImpl notifier;

    REQUIRE(notifier.InitializeWebSocket("127.0.0.1", 0));
    REQUIRE_NOTHROW(notifier.ShutdownWebSocket());
}
