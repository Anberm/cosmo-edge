// Audio API DTO regression tests.

#include <catch_amalgamated.hpp>
#include <nlohmann/json.hpp>

#include "service/media/dto/AudioDto.h"

TEST_CASE("Audio device add request accepts a valid operation", "[audio][dto]") {
    const auto request = nlohmann::json{
        {"devOperation", 1},
        {"audioDev", {{"name", "speaker"}, {"ip", "192.168.1.10"}, {"ethName", "eth0"}}},
    };

    const auto dto = request.get<cosmo::Audio::MsgModifyAudioDeviceRecv>();

    REQUIRE(static_cast<int>(dto.devOperation) == 1);
    REQUIRE(dto.audioDev.name == "speaker");
    REQUIRE(dto.audioDev.ip == "192.168.1.10");
    REQUIRE(dto.audioDev.ethName == "eth0");
}

TEST_CASE("Audio device test request accepts valid playback operations", "[audio][dto]") {
    auto request = nlohmann::json{
        {"operation", 1},
        {"devSn", "speaker-id"},
        {"data", "audio-id"},
    };

    auto dto = request.get<cosmo::Audio::MsgTestAudioDeviceRecv>();
    REQUIRE(static_cast<int>(dto.operation) == 1);

    request["operation"] = 2;
    request["data"]      = "test announcement";
    dto                  = request.get<cosmo::Audio::MsgTestAudioDeviceRecv>();
    REQUIRE(static_cast<int>(dto.operation) == 2);
}

TEST_CASE("Audio device requests reject invalid operations", "[audio][dto]") {
    const auto modify_request = nlohmann::json{
        {"devOperation", 0},
        {"audioDev", {{"name", "speaker"}, {"ip", "192.168.1.10"}, {"ethName", "eth0"}}},
    };
    const auto test_request = nlohmann::json{
        {"operation", 3},
        {"devSn", "speaker-id"},
        {"data", "audio-id"},
    };

    REQUIRE_THROWS(modify_request.get<cosmo::Audio::MsgModifyAudioDeviceRecv>());
    REQUIRE_THROWS(test_request.get<cosmo::Audio::MsgTestAudioDeviceRecv>());
}
