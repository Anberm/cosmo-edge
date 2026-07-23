#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "catch_amalgamated.hpp"
#include "event2/buffer.h"
#include "network/http/MultipartStreamParser.h"
#include "util/UuidUtil.h"

namespace cosmo::network::http {
namespace {

    namespace fs = std::filesystem;

    struct EvbufferDeleter {
        void operator()(evbuffer* buffer) const {
            evbuffer_free(buffer);
        }
    };

    class TempDirectory {
    public:
        TempDirectory() {
            path_ = fs::path("/tmp") / ("cosmo-multipart-" + util::GenerateUUID());
            fs::create_directories(path_);
        }

        ~TempDirectory() {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }

        const fs::path& Path() const {
            return path_;
        }

    private:
        fs::path path_;
    };

    std::string ReadFile(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    MultipartParseResult Parse(const std::string& body, const std::string& boundary,
                               const fs::path& server_path, MultipartLimits limits = {}) {
        std::unique_ptr<evbuffer, EvbufferDeleter> buffer(evbuffer_new());
        REQUIRE(buffer != nullptr);
        REQUIRE(evbuffer_add(buffer.get(), body.data(), body.size()) == 0);
        MultipartStreamParser parser(boundary, limits);
        return parser.ParseToFile(buffer.get(), [&server_path]() { return server_path.string(); });
    }

    std::string FilePart(const std::string& boundary, const std::string& filename, const std::string& content,
                         bool final = true) {
        return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename +
               "\"\r\nContent-Type: application/octet-stream\r\n\r\n" + content + "\r\n--" + boundary +
               (final ? "--\r\n" : "\r\n");
    }

}  // namespace

TEST_CASE("Multipart parser uses a server path and isolates trusted metadata", "[http][multipart]") {
    TempDirectory temp;
    const std::string boundary = "cosmo-boundary";
    const std::string content  = "abc\r\n--cosmo-boundaryX-not-a-boundary\r\ndef";
    const auto server_path     = temp.Path() / "server-generated";
    auto body                  = "--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"uploadId\"\r\n\r\nopaque-id\r\n" +
                FilePart(boundary, "../../client-name.bin", content);

    auto result = Parse(body, boundary, server_path);
    REQUIRE(result.ok);
    CHECK(result.error == MultipartParseError::kNone);
    CHECK(result.fields.at("uploadId") == "opaque-id");
    CHECK(result.fields.at("filePath") == server_path.string());
    CHECK(result.fields.at("fileName") == "client-name.bin");
    CHECK(result.fields.at("contentLength") == std::to_string(content.size()));
    CHECK(result.uploaded_path == server_path.string());
    CHECK(result.uploaded_name == "client-name.bin");
    CHECK(result.uploaded_size == content.size());
    CHECK(ReadFile(server_path) == content);
}

TEST_CASE("Multipart parser only accepts a complete closing delimiter line", "[http][multipart][security]") {
    TempDirectory temp;
    const std::string boundary = "strict-closing-boundary";

    SECTION("boundary-like bytes with a malicious suffix remain file content") {
        const std::string content = "before\r\n--" + boundary + "--evil\r\nafter";
        const auto destination    = temp.Path() / "suffix-content";
        auto result               = Parse(FilePart(boundary, "file.bin", content), boundary, destination);
        REQUIRE(result.ok);
        CHECK(result.uploaded_size == content.size());
        CHECK(ReadFile(destination) == content);
    }

    SECTION("trailing data after a closing delimiter is rejected") {
        const auto destination = temp.Path() / "trailing-data";
        auto body              = FilePart(boundary, "file.bin", "content") + "unexpected";
        auto result            = Parse(body, boundary, destination);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kMalformed);
        CHECK(result.err == "unexpected data after closing multipart boundary");
    }

    SECTION("closing delimiter may be split at the parser read boundary") {
        const std::string header =
            "--" + boundary +
            "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"file.bin\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n";
        REQUIRE(header.size() < 64 * 1024);
        const std::string content(64 * 1024 - header.size() - 1, 'x');
        const auto destination = temp.Path() / "split-closing";
        auto result = Parse(header + content + "\r\n--" + boundary + "--\r\n", boundary, destination);
        REQUIRE(result.ok);
        CHECK(result.uploaded_size == content.size());
        CHECK(ReadFile(destination) == content);
    }

    SECTION("linear whitespace is allowed only before closing CRLF") {
        const auto destination = temp.Path() / "closing-padding";
        auto body              = FilePart(boundary, "file.bin", "content");
        REQUIRE(body.size() >= 2);
        body.insert(body.size() - 2, " \t");
        auto result = Parse(body, boundary, destination);
        REQUIRE(result.ok);
        CHECK(ReadFile(destination) == "content");
    }
}

TEST_CASE("Multipart parser rejects reserved and duplicate fields", "[http][multipart]") {
    TempDirectory temp;
    const std::string boundary = "reserved-boundary";
    const auto server_path     = temp.Path() / "server-generated";

    SECTION("reserved metadata cannot precede the file") {
        auto body = "--" + boundary +
                    "\r\nContent-Disposition: form-data; name=\"filePath\"\r\n\r\n/tmp/evil\r\n" +
                    FilePart(boundary, "safe.bin", "data");
        auto result = Parse(body, boundary, server_path);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kMalformed);
        CHECK(result.err == "reserved multipart field");
        CHECK_FALSE(fs::exists(server_path));
    }

    SECTION("reserved metadata cannot overwrite a parsed file") {
        auto body = FilePart(boundary, "safe.bin", "data", false) +
                    "Content-Disposition: form-data; name=\"contentLength\"\r\n\r\n999999\r\n--" + boundary +
                    "--\r\n";
        auto result = Parse(body, boundary, server_path);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kMalformed);
        CHECK(result.err == "reserved multipart field");
    }

    SECTION("case-insensitive duplicate fields are rejected") {
        auto body = "--" + boundary +
                    "\r\nContent-Disposition: form-data; name=\"purpose\"\r\n\r\nmodel\r\n--" + boundary +
                    "\r\nContent-Disposition: form-data; name=\"Purpose\"\r\n\r\nvideo\r\n" +
                    FilePart(boundary, "safe.bin", "data");
        auto result = Parse(body, boundary, server_path);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kMalformed);
        CHECK(result.err == "duplicate multipart field");
    }
}

TEST_CASE("Multipart parser enforces part field header and file limits", "[http][multipart]") {
    TempDirectory temp;
    const std::string boundary = "limit-boundary";
    MultipartLimits limits;
    limits.max_header_bytes = 256;
    limits.max_field_bytes  = 4;
    limits.max_file_bytes   = 4;
    limits.max_parts        = 2;

    SECTION("field limit") {
        auto body = "--" + boundary +
                    "\r\nContent-Disposition: form-data; name=\"purpose\"\r\n\r\n12345\r\n" +
                    FilePart(boundary, "file.bin", "1234");
        auto result = Parse(body, boundary, temp.Path() / "field", limits);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kPayloadTooLarge);
        CHECK(result.err == "multipart field exceeds limit");
    }

    SECTION("file limit") {
        auto result = Parse(FilePart(boundary, "file.bin", "12345"), boundary, temp.Path() / "file", limits);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kPayloadTooLarge);
        CHECK(result.err == "multipart file exceeds limit or cannot be written");
    }

    SECTION("header limit") {
        const std::string long_name(100, 'a');
        auto header_limits             = limits;
        header_limits.max_header_bytes = 96;
        auto result =
            Parse(FilePart(boundary, long_name, "1"), boundary, temp.Path() / "header", header_limits);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kPayloadTooLarge);
        CHECK(result.err == "multipart headers too large");
    }

    SECTION("part count") {
        auto body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"a\"\r\n\r\n1\r\n--" +
                    boundary + "\r\nContent-Disposition: form-data; name=\"b\"\r\n\r\n2\r\n--" + boundary +
                    "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"f\"\r\n\r\n3\r\n--" +
                    boundary + "--\r\n";
        auto result = Parse(body, boundary, temp.Path() / "parts", limits);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kPayloadTooLarge);
        CHECK(result.err == "too many multipart parts");
    }
}

TEST_CASE("Multipart parser rejects multiple files and unsafe destinations", "[http][multipart]") {
    TempDirectory temp;
    const std::string boundary = "file-boundary";

    SECTION("only one file part is accepted") {
        auto body = FilePart(boundary, "one.bin", "1", false) +
                    "Content-Disposition: form-data; name=\"file2\"; filename=\"two.bin\"\r\n\r\n2\r\n--" +
                    boundary + "--\r\n";
        auto result = Parse(body, boundary, temp.Path() / "one");
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kMalformed);
        CHECK(result.err == "multiple multipart files are not allowed");
    }

    SECTION("an existing destination is never overwritten") {
        const auto destination = temp.Path() / "existing";
        std::ofstream(destination) << "original";
        auto result = Parse(FilePart(boundary, "file.bin", "replacement"), boundary, destination);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kIo);
        CHECK(ReadFile(destination) == "original");
    }

    SECTION("a symlink destination is rejected") {
        const auto target = temp.Path() / "target";
        const auto link   = temp.Path() / "link";
        std::ofstream(target) << "original";
        fs::create_symlink(target, link);
        auto result = Parse(FilePart(boundary, "file.bin", "replacement"), boundary, link);
        CHECK_FALSE(result.ok);
        CHECK(result.error == MultipartParseError::kIo);
        CHECK(ReadFile(target) == "original");
    }
}

}  // namespace cosmo::network::http
