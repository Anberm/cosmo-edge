#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <string_view>

#include "catch_amalgamated.hpp"
#include "flow/face/PersonImport.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockPersonDaoService.h"
#include "mock/MockServiceRegistry.h"
#include "util/Exception.h"
#include "util/PathUtil.h"

namespace {

class RootPathOverrideGuard {
public:
    RootPathOverrideGuard(const std::string& data_path, const std::string& app_data_path) {
        cosmo::path::OverrideRootPathForTest(data_path, app_data_path);
    }

    ~RootPathOverrideGuard() {
        cosmo::path::OverrideRootPathForTest("/data/cwaiuserdata", "/appfs/cosmo_wander/cwai_data");
    }

    RootPathOverrideGuard(const RootPathOverrideGuard&)            = delete;
    RootPathOverrideGuard& operator=(const RootPathOverrideGuard&) = delete;
};

void WriteLe16(std::ostream& stream, std::uint16_t value) {
    stream.put(static_cast<char>(value & 0xffU));
    stream.put(static_cast<char>((value >> 8U) & 0xffU));
}

void WriteLe32(std::ostream& stream, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        stream.put(static_cast<char>((value >> shift) & 0xffU));
    }
}

bool WriteEmptyStoredZip(const std::filesystem::path& archive_path, std::string_view member) {
    if (member.empty() || member.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    std::ofstream stream(archive_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    const auto member_size = static_cast<std::uint16_t>(member.size());

    WriteLe32(stream, 0x04034b50U);
    WriteLe16(stream, 20);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0x21);
    WriteLe32(stream, 0);
    WriteLe32(stream, 0);
    WriteLe32(stream, 0);
    WriteLe16(stream, member_size);
    WriteLe16(stream, 0);
    stream.write(member.data(), static_cast<std::streamsize>(member.size()));

    const auto central_offset = static_cast<std::uint32_t>(static_cast<std::streamoff>(stream.tellp()));
    WriteLe32(stream, 0x02014b50U);
    WriteLe16(stream, 0x0314);
    WriteLe16(stream, 20);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0x21);
    WriteLe32(stream, 0);
    WriteLe32(stream, 0);
    WriteLe32(stream, 0);
    WriteLe16(stream, member_size);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe32(stream, 0100644U << 16U);
    WriteLe32(stream, 0);
    stream.write(member.data(), static_cast<std::streamsize>(member.size()));

    const auto central_size =
        static_cast<std::uint32_t>(static_cast<std::streamoff>(stream.tellp())) - central_offset;
    WriteLe32(stream, 0x06054b50U);
    WriteLe16(stream, 0);
    WriteLe16(stream, 0);
    WriteLe16(stream, 1);
    WriteLe16(stream, 1);
    WriteLe32(stream, central_size);
    WriteLe32(stream, central_offset);
    WriteLe16(stream, 0);
    return stream.good();
}

}  // namespace

TEST_CASE("PersonImport rejects and preserves unmanaged input paths", "[face][security]") {
    namespace fs = std::filesystem;

    const auto suffix      = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root    = fs::path("/tmp") / ("cosmo_person_import_root_" + suffix);
    const fs::path outside = fs::path("/tmp") / ("cosmo_person_import_outside_" + suffix + ".zip");
    fs::create_directories(root);
    std::ofstream(outside) << "not an owned upload";
    const RootPathOverrideGuard root_override(root.string(), root.string());
    const fs::path upload_link = fs::path(cosmo::path::GetUploadPath()) / "outside.zip";
    fs::create_symlink(outside, upload_link);

    {
        cosmo::PersonImport importer;
        REQUIRE_THROWS_AS(importer.ImportFile(outside.string(), "face-lib"), cosmo::util::ErrorMessage);
        REQUIRE_THROWS_AS(importer.ImportFile(upload_link.string(), "face-lib"), cosmo::util::ErrorMessage);

        const fs::path traversal_archive = fs::path(cosmo::path::GetUploadPath()) / "traversal.zip";
        const fs::path escaped_path = root.parent_path() / ("cosmo_person_import_escape_" + suffix + ".jpg");
        REQUIRE(WriteEmptyStoredZip(traversal_archive, "../" + escaped_path.filename().string()));
        REQUIRE_THROWS_AS(importer.ImportFile(traversal_archive.string(), "face-lib"),
                          cosmo::util::ErrorMessage);
        REQUIRE_FALSE(fs::exists(traversal_archive));
        REQUIRE_FALSE(fs::exists(escaped_path));
    }
    REQUIRE(fs::exists(outside));

    fs::remove(outside);
    fs::remove_all(root);
}

TEST_CASE("PersonImport Stop waits for an active import and rejects restart", "[face][lifecycle]") {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    const auto suffix   = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root = fs::path("/tmp") / ("cosmo_person_import_stop_" + suffix);
    fs::create_directories(root);

    {
        cosmo::test::MockServiceRegistry mocks;
        const RootPathOverrideGuard root_override(root.string(), root.string());
        const auto archive = fs::path(cosmo::path::GetUploadPath()) / "faces.zip";
        REQUIRE(WriteEmptyStoredZip(archive, "ignored.txt"));

        auto face_lib = std::make_shared<cosmo::FaceLib>();
        REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("face-lib")).RETURN(face_lib);

        auto import_started        = std::make_shared<std::promise<void>>();
        auto import_started_future = import_started->get_future();
        auto release_import        = std::make_shared<std::promise<void>>();
        auto release_import_future = release_import->get_future().share();
        REQUIRE_CALL(mocks.personDaoSvc, Begin()).SIDE_EFFECT({
            import_started->set_value();
            release_import_future.wait();
        });
        REQUIRE_CALL(mocks.personDaoSvc, Commit());
        REQUIRE_CALL(mocks.faceLibSvc, ReleaseFaceModels());

        cosmo::PersonImport importer;
        REQUIRE(importer.ImportFile(archive.string(), "face-lib"));

        const bool worker_started = import_started_future.wait_for(5s) == std::future_status::ready;
        auto stop_future          = std::async(std::launch::async, [&importer]() { importer.Stop(); });
        const bool stop_waited    = stop_future.wait_for(100ms) == std::future_status::timeout;
        release_import->set_value();
        const bool stop_completed = stop_future.wait_for(5s) == std::future_status::ready;
        if (stop_completed) {
            stop_future.get();
        }

        CHECK(worker_started);
        CHECK(stop_waited);
        REQUIRE(stop_completed);
        CHECK_FALSE(importer.ImportFile(archive.string(), "face-lib"));
        importer.Stop();
    }

    fs::remove_all(root);
}
