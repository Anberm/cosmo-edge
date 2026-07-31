#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/guard/ModelLoadPolicy.h"

namespace cosmo::nn {
namespace {

    namespace fs = std::filesystem;

    class ScopedDirectory final {
    public:
        ScopedDirectory() {
            static std::atomic<unsigned int> sequence{0};
            path_ = fs::temp_directory_path() / ("cosmo-model-load-policy-" + std::to_string(getpid()) + "-" +
                                                 std::to_string(sequence.fetch_add(1)));
            fs::create_directories(path_);
        }

        ~ScopedDirectory() {
            std::error_code error;
            fs::remove_all(path_, error);
        }

        [[nodiscard]] fs::path Path(const std::string& name) const {
            return path_ / name;
        }

        [[nodiscard]] const fs::path& Get() const {
            return path_;
        }

    private:
        fs::path path_;
    };

    fs::path WriteModel(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.is_open());
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        return path;
    }

    std::vector<std::uint8_t> ModelBytes(const std::array<std::uint8_t, 4>& magic) {
        return {magic[0], magic[1], magic[2], magic[3], 0x01, 0x02, 0x03, 0x04};
    }

    constexpr std::array<std::uint8_t, 4> kCemc       = {'C', 'E', 'M', 'C'};
    constexpr std::array<std::uint8_t, 4> kCenn       = {'C', 'E', 'N', 'N'};
    constexpr std::array<std::uint8_t, 4> kLegacy     = {0x01, 0x00, 0x01, 0xec};
    constexpr std::array<std::uint8_t, 4> kUnreserved = {0x42, 0x4d, 0x00, 0x01};

    TEST_CASE("Model magic detection compares the raw four bytes", "[nn][model-policy]") {
        REQUIRE(DetectModelMagic(kCemc) == ModelMagic::kCemc);
        REQUIRE(DetectModelMagic(kCenn) == ModelMagic::kCenn);
        REQUIRE(DetectModelMagic(kLegacy) == ModelMagic::kLegacyEncrypted);
        REQUIRE(DetectModelMagic(kUnreserved) == ModelMagic::kUnknown);
        REQUIRE(DetectModelMagic({0xec, 0x01, 0x00, 0x01}) == ModelMagic::kUnknown);
    }

    TEST_CASE("CEMC always routes to Guard regardless of path metadata",
              "[nn][model-policy][model-guard-v2]") {
        ScopedDirectory directory;
        const ModelLoadPolicy policy = ModelLoadPolicy::Production();
        const fs::path target        = WriteModel(directory.Path("any/location/model.nn"), ModelBytes(kCemc));
        fs::permissions(target, fs::perms::all, fs::perm_options::replace);
        const fs::path link = directory.Path("linked-model.nn");
        fs::create_symlink(target, link);

        for (const fs::path& path : {target, link}) {
            for (const ModelLoadIntent intent : {ModelLoadIntent::kCosmoNn, ModelLoadIntent::kRawBmodel}) {
                const ModelLoadDecision decision = policy.Evaluate(path.string(), intent);
                REQUIRE(decision.action == ModelLoadAction::kGuardV2);
                REQUIRE(decision.magic == ModelMagic::kCemc);
                REQUIRE(decision.error == ModelPolicyError::kNone);
                REQUIRE(decision.model_path == fs::absolute(path).lexically_normal().string());
            }
        }
    }

    TEST_CASE("Native model formats follow the selected consumer", "[nn][model-policy]") {
        ScopedDirectory directory;
        const ModelLoadPolicy policy = ModelLoadPolicy::Production();
        const fs::path cenn          = WriteModel(directory.Path("plain-cenn.nn"), ModelBytes(kCenn));
        const fs::path raw           = WriteModel(directory.Path("plain.bmodel"), ModelBytes(kUnreserved));

        REQUIRE(policy.Evaluate(cenn.string(), ModelLoadIntent::kCosmoNn).action ==
                ModelLoadAction::kNativeCenn);
        REQUIRE(policy.Evaluate(cenn.string(), ModelLoadIntent::kRawBmodel).error ==
                ModelPolicyError::kFormatRejected);
        REQUIRE(policy.Evaluate(raw.string(), ModelLoadIntent::kRawBmodel).action ==
                ModelLoadAction::kNativeRawBmodel);
        REQUIRE(policy.Evaluate(raw.string(), ModelLoadIntent::kCosmoNn).error ==
                ModelPolicyError::kFormatRejected);
    }

    TEST_CASE("Legacy encrypted format never falls back to a native loader",
              "[nn][model-policy][model-guard-v2]") {
        ScopedDirectory directory;
        const ModelLoadPolicy policy = ModelLoadPolicy::Production();
        const fs::path legacy        = WriteModel(directory.Path("legacy.nn"), ModelBytes(kLegacy));

        REQUIRE(policy.Evaluate(legacy.string(), ModelLoadIntent::kCosmoNn).error ==
                ModelPolicyError::kFormatRejected);
        REQUIRE(policy.Evaluate(legacy.string(), ModelLoadIntent::kRawBmodel).error ==
                ModelPolicyError::kFormatRejected);
    }

    TEST_CASE("Unreadable model shapes are rejected", "[nn][model-policy]") {
        ScopedDirectory directory;
        const ModelLoadPolicy policy = ModelLoadPolicy::Production();
        const fs::path truncated     = WriteModel(directory.Path("truncated.nn"), {0x42, 0x4d, 0x00});

        REQUIRE(policy.Evaluate(truncated.string(), ModelLoadIntent::kRawBmodel).error ==
                ModelPolicyError::kHeaderReadFailed);
        REQUIRE(policy.Evaluate(directory.Get().string(), ModelLoadIntent::kCosmoNn).error ==
                ModelPolicyError::kPathNotRegularFile);
        REQUIRE(policy.Evaluate(directory.Path("missing.nn").string(), ModelLoadIntent::kCosmoNn).error ==
                ModelPolicyError::kPathNotRegularFile);
    }

}  // namespace
}  // namespace cosmo::nn
