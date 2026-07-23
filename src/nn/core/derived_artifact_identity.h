#pragma once

#include <map>
#include <string>

namespace cosmo::nn {

// Stable identity for a backend-specific artifact derived from a source model.
// Backends own artifact creation; this contract owns cache compatibility.
struct DerivedArtifactIdentity {
    int schema_version = 1;
    std::string source_sha256;
    std::string build_sha256;
    std::string backend;
    std::string backend_version;
    std::string device;
    std::string precision;
    std::string profile;
    std::map<std::string, std::string> build_options;

    bool Validate(std::string& error) const;
    std::string CanonicalJson() const;
    std::string Key() const;
};

}  // namespace cosmo::nn
