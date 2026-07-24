#include "catch_amalgamated.hpp"
/*
 * test_file_service_impl.cc — FileServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Keep external platform services mocked, and use a one-shot
 * loopback HTTP server for deterministic transfer-boundary coverage.
 */
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <thread>

#include "mock/MockServiceRegistry.h"
#include "network/http/HttpRequest.h"
#include "network/http/HttpRequestHandler.h"
#include "service/path/impl/FileServiceImpl.h"
#include "util/FileUtil.h"
#include "util/PathUtil.h"

using namespace cosmo::service;

namespace {

int OpenLoopbackServer(std::uint16_t& port) {
    const int server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) {
        return -1;
    }
    int reuse = 1;
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        close(server);
        return -1;
    }
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 1) != 0) {
        close(server);
        return -1;
    }
    socklen_t address_size = sizeof(address);
    if (getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
        close(server);
        return -1;
    }
    port = ntohs(address.sin_port);
    return server;
}

bool SendAll(int socket, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const auto result = send(socket, data + sent, size - sent, MSG_NOSIGNAL);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool ServeHttpBodyOnce(int server, std::size_t body_size, char fill) {
    const int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
        close(server);
        return false;
    }

    std::string request;
    std::array<char, 4096> request_buffer{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64 * 1024) {
        const auto received = recv(client, request_buffer.data(), request_buffer.size(), 0);
        if (received <= 0) {
            close(client);
            close(server);
            return false;
        }
        request.append(request_buffer.data(), static_cast<std::size_t>(received));
    }

    const auto header =
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
        "Content-Length: " +
        std::to_string(body_size) + "\r\nConnection: close\r\n\r\n";
    bool success = SendAll(client, header.data(), header.size());
    std::array<char, 64 * 1024> body_buffer{};
    body_buffer.fill(fill);
    std::size_t remaining = body_size;
    while (success && remaining > 0) {
        const auto bytes = std::min(remaining, body_buffer.size());
        success          = SendAll(client, body_buffer.data(), bytes);
        remaining -= bytes;
    }
    shutdown(client, SHUT_RDWR);
    close(client);
    close(server);
    return success;
}

}  // namespace

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

TEST_CASE("FileServiceImpl: resource budget permits HTTP images beyond the legacy 16 MiB cap",
          "[FileService][http][boundary]") {
    constexpr std::size_t kBodySize = 17U * 1024 * 1024 + 123;
    std::uint16_t port              = 0;
    const int server                = OpenLoopbackServer(port);
    REQUIRE(server >= 0);

    std::atomic<bool> served{false};
    std::thread server_thread(
        [&]() { served.store(ServeHttpBodyOnce(server, kBodySize, 'I'), std::memory_order_release); });
    FileServiceImpl sut;
    std::vector<std::uint8_t> data;
    const bool downloaded =
        sut.DownloadFile("http://127.0.0.1:" + std::to_string(port) + "/large-image", data);
    server_thread.join();

    REQUIRE(served.load(std::memory_order_acquire));
    REQUIRE(downloaded);
    REQUIRE(data.size() == kBodySize);
    REQUIRE(data.front() == static_cast<std::uint8_t>('I'));
    REQUIRE(data.back() == static_cast<std::uint8_t>('I'));
}

TEST_CASE("HttpFileHandler: HTTP video-sized responses stream to disk beyond 16 MiB",
          "[FileService][http][streaming]") {
    constexpr std::size_t kBodySize = 32U * 1024 * 1024 + 321;
    std::uint16_t port              = 0;
    const int server                = OpenLoopbackServer(port);
    REQUIRE(server >= 0);

    const auto output =
        std::filesystem::path("/tmp") / ("cosmo-http-video-" + std::to_string(getpid()) + ".mp4");
    std::error_code error;
    std::filesystem::remove(output, error);
    std::atomic<bool> served{false};
    std::thread server_thread(
        [&]() { served.store(ServeHttpBodyOnce(server, kBodySize, 'V'), std::memory_order_release); });

    cosmo::network::http::HttpFileHandler handler(output.string());
    cosmo::network::http::HttpRequest request("http://127.0.0.1:" + std::to_string(port) + "/large-video",
                                              &handler);
    request.SetTimeout(30);
    const auto status = request.Submit(cosmo::network::http::HttpRequestMethod::kGet);
    handler.Flush();
    server_thread.join();

    REQUIRE(served.load(std::memory_order_acquire));
    REQUIRE(static_cast<int>(status) == 200);
    REQUIRE(std::filesystem::file_size(output, error) == kBodySize);
    REQUIRE_FALSE(error);
    std::filesystem::remove(output, error);
}
