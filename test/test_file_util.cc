#include "catch_amalgamated.hpp"
/*
 * test_file_util.cc — FileUtil unit tests
 *
 * Tests file I/O operations, directory operations, and path utilities.
 * Uses a temporary directory created in /tmp for test isolation.
 */
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "util/FileUtil.h"

using namespace cosmo::util;

namespace {
// Helper: create a unique temp directory for test isolation
std::string CreateTestDir() {
    std::string base = "/tmp/cosmo_test_file_util_XXXXXX";
    char* dir        = mkdtemp(base.data());
    return dir ? std::string(dir) : "";
}

// Helper: remove temp directory recursively
void CleanupTestDir(const std::string& dir) {
    RemovePath(dir);
}
}  // namespace

TEST_CASE("FileUtil: FileExist", "[file-util]") {
    SECTION("Existing file") {
        REQUIRE(FileExist("/etc/hostname"));
    }

    SECTION("Non-existing file") {
        REQUIRE_FALSE(FileExist("/tmp/cosmo_nonexistent_file_xyz_123"));
    }

    SECTION("Empty path") {
        REQUIRE_FALSE(FileExist(""));
    }
}

TEST_CASE("FileUtil: WriteFile and ReadFile", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    std::string file_path = dir + "/test.txt";

    SECTION("Write and read string content") {
        REQUIRE(WriteFile(file_path, "hello world"));
        auto content = ReadFile(file_path);
        REQUIRE(content == "hello world");
    }

    SECTION("Write binary data") {
        std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0xFF};
        REQUIRE(WriteFile(file_path, data.data(), static_cast<int>(data.size())));
        auto read_data = ReadFileBin(file_path);
        REQUIRE(read_data.size() == 4);
        REQUIRE(read_data[0] == 0x00);
        REQUIRE(read_data[3] == 0xFF);
    }

    SECTION("Read non-existent file returns empty") {
        auto content = ReadFile(dir + "/no_such_file.txt");
        REQUIRE(content.empty());
    }

    CleanupTestDir(dir);
}

TEST_CASE("FileUtil: WriteFileAppend", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    std::string file_path = dir + "/append.txt";
    WriteFile(file_path, "first");
    WriteFileAppend(file_path, " second");
    auto content = ReadFile(file_path);
    REQUIRE(content == "first second");

    CleanupTestDir(dir);
}

TEST_CASE("FileUtil: WriteFileAtomically", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    const std::string target   = dir + "/config.json";
    const std::string sentinel = dir + "/sentinel.json";
    REQUIRE(WriteFile(target, "old"));
    REQUIRE(WriteFileAtomically(target, "new"));
    REQUIRE(ReadFile(target) == "new");

    REQUIRE(WriteFile(sentinel, "keep"));
    REQUIRE(std::filesystem::remove(target));
    REQUIRE_NOTHROW(std::filesystem::create_symlink(sentinel, target));
    REQUIRE(WriteFileAtomically(target, "replacement"));
    REQUIRE(ReadFile(target) == "replacement");
    REQUIRE(ReadFile(sentinel) == "keep");
    REQUIRE(std::filesystem::is_regular_file(std::filesystem::symlink_status(target)));

    CleanupTestDir(dir);
}

TEST_CASE("FileUtil: CreateDir", "[file-util]") {
    auto base = CreateTestDir();
    REQUIRE_FALSE(base.empty());

    SECTION("Create nested directory") {
        std::string nested = base + "/sub1/sub2";
        REQUIRE(CreateDir(nested));
        REQUIRE(FileExist(nested));
    }

    CleanupTestDir(base);
}

TEST_CASE("FileUtil: GetFileSize", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    std::string file_path = dir + "/sized.txt";
    WriteFile(file_path, "12345");
    REQUIRE(GetFileSize(file_path) == 5);

    CleanupTestDir(dir);
}

TEST_CASE("FileUtil: Path utilities", "[file-util]") {
    SECTION("GetParentPath") {
        REQUIRE(GetParentPath("/a/b/c.txt") == "/a/b");
    }

    SECTION("GetFileName") {
        REQUIRE(GetFileName("/a/b/c.txt") == "c.txt");
    }

    SECTION("RemoveExtension") {
        REQUIRE(RemoveExtension("/a/b/c.txt") == "/a/b/c");
    }

    SECTION("BaseSimplify removes trailing slash") {
        auto result = BaseSimplify("/a/b/c/");
        REQUIRE_FALSE(result.empty());
    }
}

TEST_CASE("FileUtil: GetAllFileName", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    WriteFile(dir + "/a.txt", "a");
    WriteFile(dir + "/b.json", "b");
    WriteFile(dir + "/c.txt", "c");

    SECTION("No filter returns all files") {
        auto files = GetAllFileName(dir);
        REQUIRE(files.size() == 3);
    }

    SECTION("Filter by extension") {
        auto files = GetAllFileName(dir, ".txt");
        REQUIRE(files.size() == 2);
    }

    CleanupTestDir(dir);
}

TEST_CASE("FileUtil: FileMove", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    std::string src  = dir + "/src.txt";
    std::string dest = dir + "/dest/";
    WriteFile(src, "data");
    CreateDir(dest);

    REQUIRE(FileMove(src, dest));
    REQUIRE_FALSE(FileExist(src));
    REQUIRE(FileExist(dest + "src.txt"));

    CleanupTestDir(dir);
}

TEST_CASE("FileUtil: RemoveFile and RemovePath", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    std::string file_path = dir + "/to_delete.txt";
    WriteFile(file_path, "delete me");

    SECTION("RemoveFile") {
        REQUIRE(RemoveFile(file_path));
        REQUIRE_FALSE(FileExist(file_path));
    }

    SECTION("RemovePath on directory") {
        REQUIRE(RemovePath(dir));
        REQUIRE_FALSE(FileExist(dir));
    }

    // Cleanup in case SECTION didn't remove
    RemovePath(dir);
}

TEST_CASE("FileUtil: FindPrefixedJsonFile", "[file-util]") {
    auto dir = CreateTestDir();
    REQUIRE_FALSE(dir.empty());

    WriteFile(dir + "/config_v1.json", "{}");
    WriteFile(dir + "/29_plate.json", "{}");

    SECTION("Found with prefix") {
        auto result = FindPrefixedJsonFile(dir, "config");
        REQUIRE_FALSE(result.empty());
    }

    SECTION("Not found returns empty") {
        auto result = FindPrefixedJsonFile(dir, "nonexistent");
        REQUIRE(result.empty());
    }

    SECTION("Numeric prefix falls back without leading zeros") {
        auto result = FindPrefixedJsonFile(dir, "029");
        REQUIRE(GetFileName(result) == "29_plate.json");
    }

    CleanupTestDir(dir);
}
