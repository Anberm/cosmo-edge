#include <cstdint>
#include <string>

#include "catch_amalgamated.hpp"
#include "util/ArchiveListingValidator.h"

namespace {

constexpr cosmo::util::ArchiveListingLimits kLimits{8, 64, 128};

std::string ZipListing(std::string member, std::uintmax_t size = 4) {
    return "Archive:  /tmp/input.zip\n"
           "Zip file size: 128 bytes, number of entries: 1\n"
           "-rw-r--r--  3.0 unx        " +
           std::to_string(size) + " tx        " + std::to_string(size) + " stor 26-Jul-15 12:00 " + member +
           "\n1 file, " + std::to_string(size) + " bytes uncompressed, " + std::to_string(size) +
           " bytes compressed:  0.0%\n";
}

}  // namespace

TEST_CASE("Archive listing validation is strict and path-aware", "[archive][security]") {
    using cosmo::util::ArchiveListingFormat;
    using cosmo::util::ValidateArchiveListingOutput;

    SECTION("accepts common dot-prefixed tar members after safe normalization") {
        const std::string listing =
            "drwxr-xr-x user/user 0 2026-07-15 12:00 ./\n"
            "drwxr-xr-x user/user 0 2026-07-15 12:00 ./faces/\n"
            "-rw-r--r-- user/user 4 2026-07-15 12:00 ./faces/a.jpg\n";
        REQUIRE(ValidateArchiveListingOutput(listing, ArchiveListingFormat::kTarVerbose, kLimits));
    }

    SECTION("accepts a complete verbose ZIP listing") {
        REQUIRE(ValidateArchiveListingOutput(ZipListing("faces/a.jpg"), ArchiveListingFormat::kZipVerbose,
                                             kLimits));
    }

    SECTION("rejects unknown lines and incomplete ZIP framing") {
        REQUIRE_FALSE(ValidateArchiveListingOutput(
            "warning: ignored input\n-rw-r--r-- user/user 4 2026-07-15 12:00 safe.jpg\n",
            ArchiveListingFormat::kTarVerbose, kLimits));
        REQUIRE_FALSE(
            ValidateArchiveListingOutput("Zip file size: 128 bytes, number of entries: 1\n"
                                         "-rw-r--r-- 3.0 unx 4 tx 4 stor 26-Jul-15 12:00 safe.jpg\n",
                                         ArchiveListingFormat::kZipVerbose, kLimits));
    }

    SECTION("rejects raw controls and newline filename ambiguity") {
        REQUIRE_FALSE(ValidateArchiveListingOutput(ZipListing("faces/a\t.jpg"),
                                                   ArchiveListingFormat::kZipVerbose, kLimits));
        REQUIRE_FALSE(ValidateArchiveListingOutput(ZipListing("faces/first.jpg\ncontinued.jpg"),
                                                   ArchiveListingFormat::kZipVerbose, kLimits));
        std::string with_escape = ZipListing("faces/a.jpg");
        with_escape.insert(with_escape.find("faces/a.jpg"), 1, '\x1b');
        REQUIRE_FALSE(ValidateArchiveListingOutput(with_escape, ArchiveListingFormat::kZipVerbose, kLimits));
    }

    SECTION("rejects traversal, absolute, drive-rooted, and escaped member paths") {
        for (const std::string member : {"../escape", "/absolute", "C:/absolute", "dir\\escape"}) {
            CAPTURE(member);
            REQUIRE_FALSE(
                ValidateArchiveListingOutput(ZipListing(member), ArchiveListingFormat::kZipVerbose, kLimits));
        }
    }

    SECTION("rejects duplicate normalized names before extraction") {
        const std::string tar_listing =
            "-rw-r--r-- user/user 4 2026-07-15 12:00 ./config.json\n"
            "-rw-r--r-- user/user 4 2026-07-15 12:00 config.json\n";
        REQUIRE_FALSE(ValidateArchiveListingOutput(tar_listing, ArchiveListingFormat::kTarVerbose, kLimits));

        const std::string zip_listing =
            "Archive:  /tmp/input.zip\n"
            "Zip file size: 256 bytes, number of entries: 2\n"
            "-rw-r--r-- 3.0 unx 4 tx 4 stor 26-Jul-15 12:00 ./config.json\n"
            "-rw-r--r-- 3.0 unx 4 tx 4 stor 26-Jul-15 12:00 config.json\n"
            "2 files, 8 bytes uncompressed, 8 bytes compressed:  0.0%\n";
        REQUIRE_FALSE(ValidateArchiveListingOutput(zip_listing, ArchiveListingFormat::kZipVerbose, kLimits));
    }

    SECTION("enforces entry, per-file, and aggregate extracted-size quotas") {
        const cosmo::util::ArchiveListingLimits one_entry{1, 8, 8};
        REQUIRE_FALSE(
            ValidateArchiveListingOutput("-rw-r--r-- user/user 1 2026-07-15 12:00 a\n"
                                         "-rw-r--r-- user/user 1 2026-07-15 12:00 b\n",
                                         ArchiveListingFormat::kTarVerbose, one_entry));
        REQUIRE_FALSE(ValidateArchiveListingOutput("-rw-r--r-- user/user 9 2026-07-15 12:00 a\n",
                                                   ArchiveListingFormat::kTarVerbose, one_entry));
        REQUIRE_FALSE(
            ValidateArchiveListingOutput("-rw-r--r-- user/user 5 2026-07-15 12:00 a\n"
                                         "-rw-r--r-- user/user 4 2026-07-15 12:00 b\n",
                                         ArchiveListingFormat::kTarVerbose, {2, 8, 8}));
    }
}
