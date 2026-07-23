#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "nn/core/derived_artifact_identity.h"
#include "nn/core/sha256.h"

TEST_CASE("SHA-256 helper matches the standard vector", "[nn][artifact]") {
    const std::string input = "abc";
    CHECK(cosmo::nn::Sha256Hex(input.data(), input.size()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("derived artifact identity is stable and derivative aware", "[nn][artifact]") {
    cosmo::nn::DerivedArtifactIdentity identity;
    identity.source_sha256   = std::string(64, 'a');
    identity.build_sha256    = identity.source_sha256;
    identity.backend         = "example";
    identity.backend_version = "1.2.3";
    identity.device          = "device-family:1";
    identity.precision       = "mixed";
    identity.profile         = "input=1x3x640x640";
    identity.build_options   = {{"optimization", "balanced"}, {"workspace", "1GiB"}};

    std::string error;
    REQUIRE(identity.Validate(error));
    const auto direct = nlohmann::json::parse(identity.CanonicalJson());
    CHECK_FALSE(direct.contains("build_sha256"));
    CHECK(identity.Key().size() == 64U);

    const auto direct_key = identity.Key();
    identity.build_sha256 = std::string(64, 'b');
    REQUIRE(identity.Validate(error));
    const auto derived = nlohmann::json::parse(identity.CanonicalJson());
    CHECK(derived.at("build_sha256") == identity.build_sha256);
    CHECK(identity.Key() != direct_key);
}

TEST_CASE("derived artifact identity rejects incomplete cache inputs", "[nn][artifact]") {
    cosmo::nn::DerivedArtifactIdentity identity;
    identity.source_sha256   = "ABC";
    identity.backend         = "example";
    identity.backend_version = "1";
    identity.device          = "device";
    identity.precision       = "default";

    std::string error;
    CHECK_FALSE(identity.Validate(error));
    CHECK_FALSE(error.empty());
}
