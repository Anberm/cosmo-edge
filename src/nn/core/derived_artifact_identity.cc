#include "nn/core/derived_artifact_identity.h"

#include <algorithm>
#include <cctype>

#include "nlohmann/json.hpp"
#include "nn/core/sha256.h"

namespace cosmo::nn {
namespace {

    bool IsSha256(const std::string& value) {
        return value.size() == 64U && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                   return std::isdigit(character) != 0 || (character >= static_cast<unsigned char>('a') &&
                                                           character <= static_cast<unsigned char>('f'));
               });
    }

}  // namespace

bool DerivedArtifactIdentity::Validate(std::string& error) const {
    if (schema_version != 1) {
        error = "Unsupported derived artifact identity schema";
        return false;
    }
    if (!IsSha256(source_sha256)) {
        error = "Source SHA-256 must be 64 lowercase hexadecimal characters";
        return false;
    }
    if (!build_sha256.empty() && !IsSha256(build_sha256)) {
        error = "Build SHA-256 must be 64 lowercase hexadecimal characters";
        return false;
    }
    if (backend.empty() || backend_version.empty() || device.empty() || precision.empty()) {
        error = "Backend, backend version, device and precision are required";
        return false;
    }
    for (const auto& option : build_options) {
        if (option.first.empty()) {
            error = "Derived artifact build option keys must not be empty";
            return false;
        }
    }
    error.clear();
    return true;
}

std::string DerivedArtifactIdentity::CanonicalJson() const {
    nlohmann::json identity = {{"backend", backend},
                               {"backend_version", backend_version},
                               {"build_options", build_options},
                               {"device", device},
                               {"precision", precision},
                               {"profile", profile},
                               {"schema_version", schema_version},
                               {"source_sha256", source_sha256}};
    if (!build_sha256.empty() && build_sha256 != source_sha256)
        identity["build_sha256"] = build_sha256;
    return identity.dump();
}

std::string DerivedArtifactIdentity::Key() const {
    const auto canonical = CanonicalJson();
    return Sha256Hex(canonical.data(), canonical.size());
}

}  // namespace cosmo::nn
