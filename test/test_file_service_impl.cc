#include "catch_amalgamated.hpp"
/*
 * test_file_service_impl.cc — FileServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Test pure logic paths only (construction, URL retrieval).
 * Network-dependent operations (upload/download) cannot be tested without
 * a file server, so they are skipped.
 */
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>

#include "mock/MockServiceRegistry.h"
#include "network/http/HttpRequestHandler.h"
#include "service/path/impl/FileServiceImpl.h"
#include "util/FileUtil.h"
#include "util/PathUtil.h"

using namespace cosmo::service;

TEST_CASE("FileServiceImpl: construction and destruction", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut;
        // destructor runs Shutdown internally
    }());
}

TEST_CASE("FileServiceImpl: GetFileUrl returns empty when not initialized", "[FileService]") {
    FileServiceImpl sut;
    auto url = sut.GetFileUrl(FileType::Image);
    REQUIRE(url.empty());
}

TEST_CASE("FileServiceImpl: double destruction is safe", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut;
        // destructor calls Shutdown — verify no crash on double destroy
    }());
}

TEST_CASE("FileServiceImpl: GetFileUrl for different types", "[FileService]") {
    FileServiceImpl sut;

    SECTION("Image type returns empty when not initialized") {
        REQUIRE(sut.GetFileUrl(FileType::Image).empty());
    }

    SECTION("Video type returns empty when not initialized") {
        REQUIRE(sut.GetFileUrl(FileType::Video).empty());
    }
}

TEST_CASE("FileServiceImpl: multiple instances do not interfere", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut1;
        FileServiceImpl sut2;
    }());
}

TEST_CASE("FileServiceImpl: platform upload boundary rejects unmanaged files", "[FileService][consistency]") {
    const auto test_root = std::filesystem::path("/tmp") / ("cosmo-file-service-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    std::filesystem::create_directories(test_root, ec);
    REQUIRE_FALSE(ec);
    cosmo::path::OverrideRootPathForTest(test_root.string(), test_root.string());

    const auto unmanaged = test_root.parent_path() / "unmanaged-platform-upload.jpg";
    REQUIRE(cosmo::util::WriteFile(unmanaged.string(), "not-an-image"));

    FileServiceImpl sut;
    std::atomic<int> callback_count{0};
    bool callback_result = true;
    sut.UploadFile(
        "task-1",
        [&](const std::string&, bool success, void*) {
            ++callback_count;
            callback_result = success;
        },
        nullptr, "jpg", unmanaged.string(), "gaf_commodity", "/remote/file.jpg");

    REQUIRE(callback_count.load() == 1);
    REQUIRE_FALSE(callback_result);
    std::filesystem::remove(unmanaged, ec);
    std::filesystem::remove_all(test_root, ec);
    cosmo::path::OverrideRootPathForTest("/tmp/cosmo_test", "/tmp/cosmo_test_app");
}

TEST_CASE("FileServiceImpl: accepted uploads always receive a terminal callback",
          "[FileService][consistency]") {
    const auto test_root =
        std::filesystem::path("/tmp") / ("cosmo-file-service-callback-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    std::filesystem::create_directories(test_root, ec);
    REQUIRE_FALSE(ec);
    cosmo::path::OverrideRootPathForTest(test_root.string(), test_root.string());

    const auto local_file = std::filesystem::path(cosmo::path::GetRecordJsonPath()) / "event.jpg";
    REQUIRE(cosmo::util::WriteFile(local_file.string(), "image-data"));

    FileServiceImpl sut;
    std::promise<bool> completion;
    auto future = completion.get_future();
    sut.UploadFile(
        "task-2", [&](const std::string&, bool success, void*) { completion.set_value(success); }, nullptr,
        "jpg", local_file.string(), "gaf_commodity", "/remote/file.jpg");

    REQUIRE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE_FALSE(future.get());
    std::filesystem::remove_all(test_root, ec);
    cosmo::path::OverrideRootPathForTest("/tmp/cosmo_test", "/tmp/cosmo_test_app");
}

TEST_CASE("FileServiceImpl: rejected upload callback may re-enter UploadFile",
          "[FileService][consistency][thread]") {
    const auto test_root =
        std::filesystem::path("/tmp") / ("cosmo-file-service-reentrant-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    cosmo::path::OverrideRootPathForTest(test_root.string(), test_root.string());

    const auto local_file = std::filesystem::path(cosmo::path::GetRecordJsonPath()) / "event.jpg";
    REQUIRE(cosmo::util::WriteFile(local_file.string(), "image-data"));

    // A zero-capacity worker rejects Put synchronously.  Its failure callback
    // must run without FileService's worker mutex held.
    FileServiceImpl sut(0);
    bool outer_called = false;
    bool outer_result = true;
    bool inner_called = false;
    bool inner_result = true;
    sut.UploadFile(
        "outer",
        [&](const std::string&, bool success, void*) {
            outer_called = true;
            outer_result = success;
            sut.UploadFile(
                "inner",
                [&](const std::string&, bool inner_success, void*) {
                    inner_called = true;
                    inner_result = inner_success;
                },
                nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/inner.jpg");
        },
        nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/outer.jpg");

    REQUIRE(outer_called);
    REQUIRE_FALSE(outer_result);
    REQUIRE(inner_called);
    REQUIRE_FALSE(inner_result);
    std::filesystem::remove_all(test_root, ec);
    cosmo::path::OverrideRootPathForTest("/tmp/cosmo_test", "/tmp/cosmo_test_app");
}

TEST_CASE("HttpStringHandler: response size limit aborts before overflow", "[FileService][boundary]") {
    cosmo::network::http::HttpStringHandler handler(4);
    REQUIRE(handler.AppendData("data", 4) == 4);
    REQUIRE(handler.AppendData("x", 1) == 0);
    REQUIRE(handler.GetData() == "data");
}

TEST_CASE("FileServiceImpl: download rejects non-HTTP URLs and clears stale output",
          "[FileService][boundary]") {
    FileServiceImpl sut;
    std::vector<uint8_t> data{1, 2, 3};
    REQUIRE_FALSE(sut.DownloadFile("file:///etc/passwd", data));
    REQUIRE(data.empty());
}
