#include "catch_amalgamated.hpp"
/*
 * test_ai_detector_fps.cc - AiDetectorFps.h free-function unit tests.
 *
 * Exercises the fps-aware placement math directly over std::vector<AiDetectorChannel>, without
 * constructing an AiDetector (whose base AlgActionBase is heavy / device-bound).
 */
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "flow/detect/AiDetectorFps.h"

using Catch::Approx;
using namespace cosmo;

namespace {
AiDetectorChannel makeChannel(const std::string& ch,
                              const std::vector<std::pair<std::string, float>>& tasks) {
    AiDetectorChannel channel;
    channel.channel = ch;
    for (const auto& kv : tasks) {
        channel.tasks.push_back({kv.first, kv.second});
    }
    return channel;
}
}  // namespace

TEST_CASE("AiDetectorFps NormalizeRequestedFps", "[AiDetectorFps]") {
    using ai_detector_fps::NormalizeRequestedFps;
    REQUIRE(NormalizeRequestedFps(12.0f) == Approx(12.0f));
    REQUIRE(NormalizeRequestedFps(0.0f) == Approx(0.0f));
    REQUIRE(NormalizeRequestedFps(-1.0f) == Approx(0.0f));
    REQUIRE(NormalizeRequestedFps(std::nan("")) == Approx(0.0f));
    REQUIRE(NormalizeRequestedFps(std::numeric_limits<float>::infinity()) == Approx(0.0f));
}

TEST_CASE("AiDetectorFps EffectiveMaxReuseCount", "[AiDetectorFps]") {
    using ai_detector_fps::EffectiveMaxReuseCount;
    constexpr size_t kHardMax = 3;
    constexpr float kBudget   = 36.0f;

    REQUIRE(EffectiveMaxReuseCount(3.0f, kBudget, kHardMax) == 3);
    REQUIRE(EffectiveMaxReuseCount(5.0f, kBudget, kHardMax) == 3);
    REQUIRE(EffectiveMaxReuseCount(12.0f, kBudget, kHardMax) == 3);
    REQUIRE(EffectiveMaxReuseCount(24.0f, kBudget, kHardMax) == 1);
    REQUIRE(EffectiveMaxReuseCount(-1.0f, kBudget, kHardMax) == 3);
}

TEST_CASE("AiDetectorFps ParseReuseProfile", "[AiDetectorFps]") {
    using ai_detector_fps::ParseReuseProfile;

    const auto profile = ParseReuseProfile(" 12:2, 5=3 ; 24:1, bad, 18:0 ");
    REQUIRE(profile.size() == 3);
    REQUIRE(profile[0].max_fps == Approx(5.0f));
    REQUIRE(profile[0].reuse_count == 3);
    REQUIRE(profile[1].max_fps == Approx(12.0f));
    REQUIRE(profile[1].reuse_count == 2);
    REQUIRE(profile[2].max_fps == Approx(24.0f));
    REQUIRE(profile[2].reuse_count == 1);
}

TEST_CASE("AiDetectorFps EffectiveMaxReuseCount profile override", "[AiDetectorFps]") {
    using ai_detector_fps::EffectiveMaxReuseCount;
    using ai_detector_fps::ParseReuseProfile;
    constexpr size_t kHardMax = 6;
    constexpr float kBudget   = 36.0f;

    const auto profile = ParseReuseProfile("5:6,12:2,24:1");
    REQUIRE(EffectiveMaxReuseCount(3.0f, kBudget, kHardMax, profile) == 6);
    REQUIRE(EffectiveMaxReuseCount(5.0f, kBudget, kHardMax, profile) == 6);
    REQUIRE(EffectiveMaxReuseCount(10.0f, kBudget, kHardMax, profile) == 2);
    REQUIRE(EffectiveMaxReuseCount(12.0f, kBudget, kHardMax, profile) == 2);
    REQUIRE(EffectiveMaxReuseCount(24.0f, kBudget, kHardMax, profile) == 1);

    const auto empty_profile = ParseReuseProfile("invalid");
    REQUIRE(EffectiveMaxReuseCount(12.0f, kBudget, kHardMax, empty_profile) == 3);
}

TEST_CASE("AiDetectorFps DefaultReuseProfile follows tested fps gradient", "[AiDetectorFps]") {
    using ai_detector_fps::DefaultReuseProfile;
    using ai_detector_fps::EffectiveMaxReuseCount;
    constexpr size_t kHardMax = 3;
    constexpr float kBudget   = 36.0f;

    const auto profile = DefaultReuseProfile();
    REQUIRE(EffectiveMaxReuseCount(3.0f, kBudget, kHardMax, profile) == 3);
    REQUIRE(EffectiveMaxReuseCount(5.0f, kBudget, kHardMax, profile) == 3);
    REQUIRE(EffectiveMaxReuseCount(8.0f, kBudget, kHardMax, profile) == 3);
    REQUIRE(EffectiveMaxReuseCount(10.0f, kBudget, kHardMax, profile) == 2);
    REQUIRE(EffectiveMaxReuseCount(12.0f, kBudget, kHardMax, profile) == 2);
    REQUIRE(EffectiveMaxReuseCount(16.0f, kBudget, kHardMax, profile) == 1);
    REQUIRE(EffectiveMaxReuseCount(24.0f, kBudget, kHardMax, profile) == 1);
}

TEST_CASE("AiDetectorFps RKNN profile isolates channels by default", "[AiDetectorFps][rknn]") {
    using ai_detector_fps::EffectiveMaxReuseCount;
    const auto profile = ai_detector_fps::RknnDefaultReuseProfile();

    REQUIRE(EffectiveMaxReuseCount(3.0f, 36.0f, 3, profile) == 1);
    REQUIRE(EffectiveMaxReuseCount(7.0f, 36.0f, 3, profile) == 1);
    REQUIRE(EffectiveMaxReuseCount(24.0f, 36.0f, 3, profile) == 1);
}

TEST_CASE("AiDetectorFps drain batch never exceeds model capacity", "[AiDetectorFps][batch]") {
    using ai_detector_fps::EffectiveDetectorDrainBatch;

    REQUIRE(EffectiveDetectorDrainBatch(4, 1) == 1);
    REQUIRE(EffectiveDetectorDrainBatch(4, 2) == 2);
    REQUIRE(EffectiveDetectorDrainBatch(4, 8) == 4);
    REQUIRE(EffectiveDetectorDrainBatch(0, 4) == 1);
    REQUIRE(EffectiveDetectorDrainBatch(4, 0) == 1);
}

TEST_CASE("AiDetectorFps ChannelAssignedFps takes max not sum", "[AiDetectorFps]") {
    using ai_detector_fps::ChannelAssignedFps;
    REQUIRE(ChannelAssignedFps(makeChannel("ch", {})) == Approx(0.0f));
    REQUIRE(ChannelAssignedFps(makeChannel("ch", {{"t1", 3.0f}, {"t2", 8.0f}})) == Approx(8.0f));
}

TEST_CASE("AiDetectorFps AssignedFps sums per-channel max", "[AiDetectorFps]") {
    using ai_detector_fps::AssignedFps;
    std::vector<AiDetectorChannel> inst = {
        makeChannel("ch1", {{"t1", 12.0f}, {"t2", 5.0f}}),  // channel max 12
        makeChannel("ch2", {{"t3", 24.0f}}),                // channel max 24
    };
    REQUIRE(AssignedFps(inst) == Approx(36.0f));
    REQUIRE(AssignedFps({}) == Approx(0.0f));
}

TEST_CASE("AiDetectorFps DeltaFpsForTask", "[AiDetectorFps]") {
    using ai_detector_fps::DeltaFpsForTask;
    std::vector<AiDetectorChannel> inst = {makeChannel("ch1", {{"t1", 12.0f}})};
    REQUIRE(DeltaFpsForTask(inst, "ch_new", 12.0f) == Approx(12.0f));  // new channel → requested
    REQUIRE(DeltaFpsForTask(inst, "ch1", 5.0f) == Approx(0.0f));       // lower fps, no rise
    REQUIRE(DeltaFpsForTask(inst, "ch1", 24.0f) == Approx(12.0f));     // higher fps, rises by diff
}

TEST_CASE("AiDetectorFps PeakFpsAfterTask", "[AiDetectorFps]") {
    using ai_detector_fps::PeakFpsAfterTask;
    std::vector<AiDetectorChannel> inst = {makeChannel("ch1", {{"t1", 24.0f}}),
                                           makeChannel("ch2", {{"t2", 5.0f}})};
    REQUIRE(PeakFpsAfterTask(inst, "ch3", 12.0f) == Approx(24.0f));
    REQUIRE(PeakFpsAfterTask(inst, "ch2", 30.0f) == Approx(30.0f));
    REQUIRE(PeakFpsAfterTask({}, "ch1", 5.0f) == Approx(5.0f));
}

TEST_CASE("AiDetectorFps CanAccept placement matrix", "[AiDetectorFps]") {
    using ai_detector_fps::CanAccept;
    constexpr size_t kMaxReuse = 3;
    constexpr float kBudget    = 36.0f;

    SECTION("empty instance accepts a new channel") {
        REQUIRE(CanAccept({}, "ch1", 12.0f, kMaxReuse, kBudget));
    }

    SECTION("12fps builds up to one instance across 3 channels (12+12+12=36)") {
        REQUIRE(CanAccept({}, "ch1", 12.0f, kMaxReuse, kBudget));
        std::vector<AiDetectorChannel> one = {makeChannel("ch1", {{"t1", 12.0f}})};
        REQUIRE(CanAccept(one, "ch2", 12.0f, kMaxReuse, kBudget));  // 12 + 12 = 24
        std::vector<AiDetectorChannel> two = {one[0], makeChannel("ch2", {{"t2", 12.0f}})};
        REQUIRE(CanAccept(two, "ch3", 12.0f, kMaxReuse, kBudget));  // 24 + 12 = 36
    }

    SECTION("12fps 4th channel must split (3+1): channel cap and budget both reject") {
        std::vector<AiDetectorChannel> three = {makeChannel("ch1", {{"t1", 12.0f}}),
                                                makeChannel("ch2", {{"t2", 12.0f}}),
                                                makeChannel("ch3", {{"t3", 12.0f}})};
        REQUIRE_FALSE(CanAccept(three, "ch4", 12.0f, kMaxReuse, kBudget));
    }

    SECTION("5fps uses the tested hard cap of 3, not the old cap of 6") {
        std::vector<AiDetectorChannel> three = {makeChannel("ch1", {{"t1", 5.0f}}),
                                                makeChannel("ch2", {{"t2", 5.0f}}),
                                                makeChannel("ch3", {{"t3", 5.0f}})};
        REQUIRE_FALSE(CanAccept(three, "ch4", 5.0f, kMaxReuse, kBudget));
    }

    SECTION("24fps x2 must split (budget forces 1+1)") {
        std::vector<AiDetectorChannel> one = {makeChannel("ch1", {{"t1", 24.0f}})};
        REQUIRE_FALSE(CanAccept(one, "ch2", 24.0f, kMaxReuse, kBudget));  // 24 + 24 = 48 > 36
    }

    SECTION("mixed high-fps placement uses the instance peak fps cap") {
        std::vector<AiDetectorChannel> one = {makeChannel("ch1", {{"t1", 24.0f}})};
        REQUIRE_FALSE(CanAccept(one, "ch2", 12.0f, kMaxReuse, kBudget));
    }

    SECTION("channel cap rejects even when fps budget remains") {
        std::vector<AiDetectorChannel> two = {makeChannel("ch1", {{"t1", 3.0f}}),
                                              makeChannel("ch2", {{"t2", 3.0f}})};
        REQUIRE_FALSE(CanAccept(two, "ch3", 3.0f, 2, kBudget));  // 6 fps, but cap=2
    }

    SECTION("unknown fps falls back to compatibility reuse count, not exclusive placement") {
        std::vector<AiDetectorChannel> two = {makeChannel("ch1", {{"t1", 0.0f}}),
                                              makeChannel("ch2", {{"t2", 0.0f}})};
        REQUIRE(CanAccept(two, "ch3", -1.0f, kMaxReuse, kBudget));
        std::vector<AiDetectorChannel> three = {two[0], two[1], makeChannel("ch3", {{"t3", 0.0f}})};
        REQUIRE_FALSE(CanAccept(three, "ch4", -1.0f, kMaxReuse, kBudget));
    }

    SECTION("same-channel lower-fps task adds no load") {
        std::vector<AiDetectorChannel> ch = {makeChannel("ch1", {{"t1", 24.0f}})};
        REQUIRE(CanAccept(ch, "ch1", 5.0f, kMaxReuse, kBudget));  // max stays 24, delta 0
    }

    SECTION("default profile splits 12fps at two channels and 16fps at one channel") {
        const auto profile = ai_detector_fps::DefaultReuseProfile();
        std::vector<AiDetectorChannel> two = {makeChannel("ch1", {{"t1", 12.0f}}),
                                              makeChannel("ch2", {{"t2", 12.0f}})};
        REQUIRE_FALSE(CanAccept(two, "ch3", 12.0f, kMaxReuse, kBudget, profile));

        std::vector<AiDetectorChannel> one = {makeChannel("ch1", {{"t1", 16.0f}})};
        REQUIRE_FALSE(CanAccept(one, "ch2", 16.0f, kMaxReuse, kBudget, profile));
    }

    SECTION("same-channel higher-fps task raising max beyond budget is rejected by the gate") {
        std::vector<AiDetectorChannel> ch = {makeChannel("ch1", {{"t1", 12.0f}})};
        // new 30fps task raises channel max: delta = 30-12 = 18; 12 + 18 = 30 > 20 budget.
        // GetInst rule 1 bypasses the gate for existing channels; this documents why.
        REQUIRE_FALSE(CanAccept(ch, "ch1", 30.0f, kMaxReuse, 20.0f));
    }
}
