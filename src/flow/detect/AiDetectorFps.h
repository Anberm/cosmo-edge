// AiDetectorFps.h — fps-aware instance placement helpers for AiDetector.
//
// Header-only and free of hardware / inference dependencies, so the pure placement math can be
// unit tested by constructing std::vector<AiDetectorChannel> directly, without instantiating an
// AiDetector (whose base AlgActionBase is heavy and device-bound).

#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace cosmo {

// One task bound to a channel, carrying the task's requested fps for instance placement.
struct AiDetectorTaskBinding {
    std::string task;
    float fps{0.0f};
};

struct AiDetectorChannel {
    std::string channel;
    std::vector<AiDetectorTaskBinding> tasks;
};

// Default per-instance fps budget when no per-algCode override is configured.
inline constexpr float kDefaultInstanceFpsBudget = 36.0f;

// Hard cap used by fps-aware placement. Stress tests show 3 is a better global ceiling than 6.
inline constexpr size_t kMaxReuseHardLimit = 3;

// Safe compatibility cap when the orchestration did not expose fps to placement.
inline constexpr size_t kUnknownFpsReuseCount = 3;

namespace ai_detector_fps {

    inline constexpr size_t kMaxDetectorDrainBatch = 4;

    struct ReuseRule {
        float max_fps{0.0f};
        size_t reuse_count{0};
    };

    using ReuseProfile = std::vector<ReuseRule>;

    // Tested placement gradient: <=8fps reuse 3, <=12fps reuse 2, higher fps reuse 1.
    inline ReuseProfile DefaultReuseProfile() {
        return {{8.0f, 3}, {12.0f, 2}, {100.0f, 1}};
    }

    // RKNN CV graphs use a conservative default across Rockchip targets until
    // a model/candidate-specific capacity result proves that sharing is safe.
    // Environment overrides remain available for qualified combinations.
    inline ReuseProfile RknnDefaultReuseProfile() {
        return {{100.0f, 1}};
    }

    inline size_t EffectiveDetectorDrainBatch(size_t configured_batch, size_t model_max_batch) {
        const size_t safe_configured = std::max<size_t>(1, configured_batch);
        const size_t safe_model_max  = std::max<size_t>(1, model_max_batch);
        return std::min(safe_configured, safe_model_max);
    }

    inline bool HasConfiguredFps(float fps) {
        return fps > 0.0f && std::isfinite(fps);
    }

    inline std::string TrimCopy(const std::string& value) {
        const auto begin = value.find_first_not_of(" \t\n\r\f\v");
        if (begin == std::string::npos) {
            return {};
        }
        const auto end = value.find_last_not_of(" \t\n\r\f\v");
        return value.substr(begin, end - begin + 1);
    }

    inline bool ParsePositiveFloat(const std::string& value, float& parsed) {
        const auto trimmed = TrimCopy(value);
        if (trimmed.empty()) {
            return false;
        }

        char* end          = nullptr;
        errno              = 0;
        const float result = std::strtof(trimmed.c_str(), &end);
        if (end == trimmed.c_str() || *end != '\0' || errno == ERANGE || !HasConfiguredFps(result)) {
            return false;
        }
        parsed = result;
        return true;
    }

    inline bool ParsePositiveSize(const std::string& value, size_t& parsed) {
        const auto trimmed = TrimCopy(value);
        if (trimmed.empty() || trimmed.front() == '-') {
            return false;
        }

        char* end                       = nullptr;
        errno                           = 0;
        const unsigned long long result = std::strtoull(trimmed.c_str(), &end, 10);
        if (end == trimmed.c_str() || *end != '\0' || errno == ERANGE || result == 0 ||
            result > std::numeric_limits<size_t>::max()) {
            return false;
        }
        parsed = static_cast<size_t>(result);
        return true;
    }

    // Parses "5:3,12:2,24:1" or "5=3;12=2;24=1" into ascending fps thresholds.
    inline ReuseProfile ParseReuseProfile(const std::string& raw_profile) {
        ReuseProfile profile;
        size_t start = 0;
        while (start <= raw_profile.size()) {
            size_t end = raw_profile.find_first_of(",;", start);
            if (end == std::string::npos) {
                end = raw_profile.size();
            }

            const auto token = TrimCopy(raw_profile.substr(start, end - start));
            if (!token.empty()) {
                size_t split = token.find(':');
                if (split == std::string::npos) {
                    split = token.find('=');
                }

                if (split != std::string::npos) {
                    float max_fps      = 0.0f;
                    size_t reuse_count = 0;
                    if (ParsePositiveFloat(token.substr(0, split), max_fps) &&
                        ParsePositiveSize(token.substr(split + 1), reuse_count)) {
                        profile.push_back({max_fps, reuse_count});
                    }
                }
            }

            if (end == raw_profile.size()) {
                break;
            }
            start = end + 1;
        }

        std::sort(profile.begin(), profile.end(),
                  [](const ReuseRule& lhs, const ReuseRule& rhs) { return lhs.max_fps < rhs.max_fps; });
        return profile;
    }

    // Keep unconfigured fps as 0. Placement handles it with the compatibility channel cap instead
    // of pretending it is 25fps, which over-splits low-fps deployments when initFps is not populated.
    inline float NormalizeRequestedFps(float fps) {
        return HasConfiguredFps(fps) ? fps : 0.0f;
    }

    inline size_t ClampReuseCount(size_t value, size_t hard_max_reuse_count) {
        const size_t capped_hard_max = std::max<size_t>(1, hard_max_reuse_count);
        return std::clamp(value, static_cast<size_t>(1), capped_hard_max);
    }

    inline size_t EffectiveMaxReuseCount(float requested_fps, float instance_fps_budget,
                                         size_t hard_max_reuse_count) {
        if (!HasConfiguredFps(requested_fps) || instance_fps_budget <= 0.0f ||
            !std::isfinite(instance_fps_budget)) {
            return ClampReuseCount(kUnknownFpsReuseCount, hard_max_reuse_count);
        }

        const auto by_fps = static_cast<size_t>(std::floor(instance_fps_budget / requested_fps));
        return ClampReuseCount(by_fps, hard_max_reuse_count);
    }

    inline size_t EffectiveMaxReuseCount(float requested_fps, float instance_fps_budget,
                                         size_t hard_max_reuse_count, const ReuseProfile& reuse_profile) {
        if (HasConfiguredFps(requested_fps)) {
            for (const auto& rule : reuse_profile) {
                if (requested_fps <= rule.max_fps) {
                    return ClampReuseCount(rule.reuse_count, hard_max_reuse_count);
                }
            }
        }
        return EffectiveMaxReuseCount(requested_fps, instance_fps_budget, hard_max_reuse_count);
    }

    // Max task fps within a single channel. Multiple tasks on one channel share one inference per
    // frame, so a channel's contribution is the max (not the sum) of its tasks' fps.
    inline float ChannelAssignedFps(const AiDetectorChannel& channel) {
        float max_fps = 0.0f;
        for (const auto& binding : channel.tasks) {
            if (binding.fps > max_fps) {
                max_fps = binding.fps;
            }
        }
        return max_fps;
    }

    // Total fps load on an instance = sum of each channel's max-task-fps.
    inline float AssignedFps(const std::vector<AiDetectorChannel>& channels) {
        float total = 0.0f;
        for (const auto& channel : channels) {
            total += ChannelAssignedFps(channel);
        }
        return total;
    }

    inline float PeakFpsAfterTask(const std::vector<AiDetectorChannel>& channels,
                                  const std::string& channel_id, float requested_fps) {
        float peak = requested_fps;
        for (const auto& channel : channels) {
            float channel_fps = ChannelAssignedFps(channel);
            if (channel.channel == channel_id) {
                channel_fps = std::max(channel_fps, requested_fps);
            }
            peak = std::max(peak, channel_fps);
        }
        return peak;
    }

    // Incremental fps delta if a task with requested_fps is added to channel_id:
    //   existing channel: delta = max(current, requested) - current
    //   new channel:      delta = requested (caller-normalized)
    inline float DeltaFpsForTask(const std::vector<AiDetectorChannel>& channels,
                                 const std::string& channel_id, float requested_fps) {
        for (const auto& channel : channels) {
            if (channel.channel == channel_id) {
                const float current = ChannelAssignedFps(channel);
                return std::max(current, requested_fps) - current;
            }
        }
        return requested_fps;
    }

    // Placement gate: a new task fits iff the channel hard cap and the fps budget both hold.
    inline bool CanAccept(const std::vector<AiDetectorChannel>& channels, const std::string& channel_id,
                          float requested_fps, size_t hard_max_reuse_count, float instance_fps_budget,
                          const ReuseProfile& reuse_profile) {
        const float normalized = NormalizeRequestedFps(requested_fps);
        const float delta      = DeltaFpsForTask(channels, channel_id, normalized);
        const bool channel_exists =
            std::any_of(channels.begin(), channels.end(),
                        [&](const AiDetectorChannel& ch) { return ch.channel == channel_id; });
        const size_t channel_count_after_add = channel_exists ? channels.size() : channels.size() + 1;
        const float placement_fps            = PeakFpsAfterTask(channels, channel_id, normalized);
        const size_t effective_max_reuse =
            EffectiveMaxReuseCount(placement_fps, instance_fps_budget, hard_max_reuse_count, reuse_profile);

        if (channel_count_after_add > effective_max_reuse) {
            return false;
        }

        if (!HasConfiguredFps(requested_fps)) {
            return true;
        }

        return AssignedFps(channels) + delta <= instance_fps_budget;
    }

    inline bool CanAccept(const std::vector<AiDetectorChannel>& channels, const std::string& channel_id,
                          float requested_fps, size_t hard_max_reuse_count, float instance_fps_budget) {
        return CanAccept(channels, channel_id, requested_fps, hard_max_reuse_count, instance_fps_budget,
                         ReuseProfile{});
    }

}  // namespace ai_detector_fps

}  // namespace cosmo
