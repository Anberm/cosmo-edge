#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/stream/RtmpStreamPusher.h"
#include "util/UuidUtil.h"

TEST_CASE("RtmpStreamPusher rejects empty frames concurrently", "[rtmp][concurrency]") {
    const auto output_path =
        std::filesystem::path("/tmp") / ("cosmo-rtmp-pusher-" + cosmo::util::GenerateUUID() + ".flv");

    {
        cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output_path.string(), 640, 480,
                                       25.0F);
        uint8_t byte = 0;
        std::vector<std::thread> producers;
        for (int i = 0; i < 4; ++i) {
            producers.emplace_back([&pusher, &byte]() {
                for (int frame = 0; frame < 5; ++frame) {
                    pusher.PushFrame(nullptr, 1);
                    pusher.PushFrame(&byte, 0);
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }

        REQUIRE_FALSE(pusher.WaitReady(std::chrono::milliseconds(1)));
        REQUIRE(pusher.GetProcInfo().recvFrames == 0);
    }

    std::error_code error;
    std::filesystem::remove(output_path, error);
}
