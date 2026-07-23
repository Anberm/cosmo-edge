#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for LinkageServiceImpl — validates CRUD operations,
// strategy workflow parsing, and query/pagination/filtering.
// The service reads config from disk on construction; we use temp dirs for isolation.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/LinkageServiceImpl.h"

using cosmo::service::LinkageServiceImpl;
using cosmo::util::ErrorEnum;

namespace {

std::string MakeValidWorkflow(const std::string& audio_device_id = "speaker-1",
                              const std::string& audio_file_id   = "audio-1") {
    nlohmann::json alarm;
    alarm["actionId"]               = "LA_AlarmData_Code";
    alarm["actionName"]             = "alarm";
    alarm["flowActionId"]           = "alarm-node";
    alarm["preFlowActionId"]        = "-1";
    alarm["configObject"]["params"] = nlohmann::json::array(
        {{{"key", "algs"}, {"value", R"([{"channelId":"channel-1","algorithmId":"algorithm-1"}])"}}});

    nlohmann::json audio;
    audio["actionId"]        = "LA_AudioDevice_Code";
    audio["actionName"]      = "audio";
    audio["flowActionId"]    = "audio-node";
    audio["preFlowActionId"] = "alarm-node";
    audio["configObject"]["params"] =
        nlohmann::json::array({{{"key", "audioDeviceId"}, {"value", audio_device_id}},
                               {{"key", "operation"}, {"value", "1"}},
                               {{"key", "data"}, {"value", audio_file_id}},
                               {{"key", "text"}, {"value", ""}},
                               {{"key", "volume"}, {"value", "50"}},
                               {{"key", "duration"}, {"value", "60"}},
                               {{"key", "times"}, {"value", "1"}},
                               {{"key", "gap"}, {"value", "1"}}});

    return nlohmann::json::array({std::move(alarm), std::move(audio)}).dump();
}

// Helper to create a temp test directory with required structure
struct LinkageTestEnv {
    std::string baseDir;

    LinkageTestEnv() {
        baseDir = std::string("/tmp/cosmo_linkage_") + std::to_string(getpid());
        // NOTE: OverrideRootPathForTest is called AFTER MockServiceRegistry construction
        // to avoid being overwritten by MockServiceRegistry's default path setup.
    }

    void SetupPaths() {
        cosmo::path::OverrideRootPathForTest(baseDir, baseDir);
        std::filesystem::remove_all(baseDir);
        std::filesystem::create_directories(baseDir + "/conf/linkAge");
    }

    ~LinkageTestEnv() {
        cosmo::path::OverrideRootPathForTest("/tmp/cosmo_test", "/tmp/cosmo_test_app");
        std::error_code error;
        std::filesystem::remove_all(baseDir, error);
    }
};

}  // namespace

TEST_CASE("LinkageServiceImpl: CRUD and query operations", "[linkage-service]") {
    LinkageTestEnv env;

    cosmo::test::MockServiceRegistry mocks;
    // Must call SetupPaths AFTER MockServiceRegistry to override its default paths
    env.SetupPaths();

    LinkageServiceImpl sut;

    SECTION("Query returns empty list when no strategies exist") {
        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(results.empty());
        REQUIRE(total == 0);
    }

    SECTION("Delete non-existent ID returns IDNotExist") {
        std::string fakeId = "non-existent-id";
        auto ret           = sut.Delete(fakeId);
        REQUIRE(ret == ErrorEnum::IDNotExist);
    }

    SECTION("Switch non-existent ID returns IDNotExist") {
        std::string fakeId = "non-existent-id";
        auto ret           = sut.Switch(fakeId, true);
        REQUIRE(ret == ErrorEnum::IDNotExist);
    }

    SECTION("Add with invalid workflow JSON fails") {
        std::string id;
        auto ret = sut.Add("test_strategy", "not valid json{{{", id);
        REQUIRE(ret == ErrorEnum::Failed);
    }

    SECTION("Add rejects an empty workflow") {
        std::string id;
        auto ret = sut.Add("test_strategy", "[]", id);
        REQUIRE(ret == ErrorEnum::ParameterException);
    }

    SECTION("Add then Delete succeeds") {
        std::string id;
        auto ret = sut.Add("to_delete", MakeValidWorkflow(), id);
        REQUIRE(ret == ErrorEnum::Success);

        ret = sut.Delete(id);
        REQUIRE(ret == ErrorEnum::Success);

        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 0);
    }

    SECTION("Update non-existent ID returns StrategyNotExist") {
        auto ret = sut.Update("name", "fake-id", MakeValidWorkflow());
        REQUIRE(ret == ErrorEnum::StrategyNotExist);
    }

    SECTION("Add then Update succeeds") {
        std::string id;
        sut.Add("original", MakeValidWorkflow(), id);

        auto ret = sut.Update("updated_name", id, MakeValidWorkflow("speaker-2", "audio-2"));
        REQUIRE(ret == ErrorEnum::Success);

        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 1);
        REQUIRE(results[0].name == "updated_name");
    }

    SECTION("Switch changes strategy status") {
        std::string id;
        sut.Add("switchable", MakeValidWorkflow(), id);

        auto ret = sut.Switch(id, false);
        REQUIRE(ret == ErrorEnum::Success);

        ret = sut.Switch(id, true);
        REQUIRE(ret == ErrorEnum::Success);
    }

    SECTION("Query with name filter") {
        std::string id1, id2;
        sut.Add("alpha_strategy", MakeValidWorkflow(), id1);
        sut.Add("beta_strategy", MakeValidWorkflow(), id2);

        size_t total = 0;
        auto results = sut.Query(1, 10, "alpha", total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].name == "alpha_strategy");
    }

    SECTION("Query with pagination") {
        std::string id;
        sut.Add("s1", MakeValidWorkflow(), id);
        sut.Add("s2", MakeValidWorkflow(), id);
        sut.Add("s3", MakeValidWorkflow(), id);

        size_t total = 0;
        auto page1   = sut.Query(1, 2, "", total);
        REQUIRE(total == 3);
        REQUIRE(page1.size() == 2);

        total      = 0;
        auto page2 = sut.Query(2, 2, "", total);
        REQUIRE(total == 3);
        REQUIRE(page2.size() == 1);
    }

    SECTION("Alarm enqueue does not crash") {
        REQUIRE(sut.Alarm("ch1", "alg1") == true);
    }

    SECTION("IsAudioDeviceInUse returns false when no strategies") {
        REQUIRE(sut.IsAudioDeviceInUse("dev1") == false);
    }

    SECTION("IsAudioFileInUse returns false when no strategies") {
        REQUIRE(sut.IsAudioFileInUse("file1") == false);
    }

    SECTION("Resource-defined action and parameter IDs bind to runtime tasks") {
        std::string id;
        REQUIRE(sut.Add("bound", MakeValidWorkflow("speaker-42", "audio-42"), id) == ErrorEnum::Success);
        REQUIRE(sut.IsAudioDeviceInUse("speaker-42"));
        REQUIRE(sut.IsAudioFileInUse("audio-42"));
    }

    SECTION("Dangling workflow nodes are rejected without persistence") {
        auto workflow                  = nlohmann::json::parse(MakeValidWorkflow());
        workflow[1]["preFlowActionId"] = "missing-parent";
        std::string id;
        REQUIRE(sut.Add("dangling", workflow.dump(), id) == ErrorEnum::ParameterException);
        size_t total = 0;
        REQUIRE(sut.Query(1, 10, "", total).empty());
        REQUIRE(total == 0);
    }

    SECTION("ReadSupportedStorage returns non-negative total") {
        int totalSize = 0;
        std::vector<cosmo::StorageList> storages;
        bool result = sut.ReadSupportedStorage(totalSize, storages);
        REQUIRE(result == true);
        REQUIRE(totalSize >= 0);
    }
}

TEST_CASE("LinkageServiceImpl: persistence failure does not publish a ghost strategy",
          "[linkage-service][consistency]") {
    LinkageTestEnv env;
    cosmo::test::MockServiceRegistry mocks;
    env.SetupPaths();

    const auto config_path = std::filesystem::path(env.baseDir) / "conf/linkAge/linkAgeList.json";
    REQUIRE(std::filesystem::create_directory(config_path));

    LinkageServiceImpl sut;
    std::string id;
    REQUIRE(sut.Add("not-persisted", MakeValidWorkflow(), id) == ErrorEnum::Failed);
    REQUIRE(id.empty());
    for (const auto& entry : std::filesystem::directory_iterator(config_path.parent_path())) {
        REQUIRE_FALSE(entry.is_regular_file());
    }

    size_t total = 0;
    REQUIRE(sut.Query(1, 10, "", total).empty());
    REQUIRE(total == 0);
}

TEST_CASE("LinkageServiceImpl: Stop is idempotent and rejects new alarms", "[linkage-service][lifecycle]") {
    LinkageTestEnv env;
    cosmo::test::MockServiceRegistry mocks;
    env.SetupPaths();

    LinkageServiceImpl sut;
    REQUIRE_NOTHROW(sut.Stop());
    REQUIRE_NOTHROW(sut.Stop());
    REQUIRE_FALSE(sut.Alarm("channel", "algorithm"));
}
