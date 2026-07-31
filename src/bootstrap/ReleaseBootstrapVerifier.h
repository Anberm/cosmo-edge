#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::bootstrap {

struct EmbeddedReleaseKey {
    std::uint8_t raw[32];
    std::uint8_t key_id[16];
    std::uint8_t pem_sha256[32];
};

// Reads the three frozen symbols from the linked trust-anchor object, derives
// their expected identities, and emits the canonical Ed25519 public-key PEM.
bool LoadAndValidateEmbeddedReleaseKey(EmbeddedReleaseKey& key, std::vector<std::uint8_t>& canonical_pem,
                                       std::string& error);

bool VerifyEd25519Manifest(const EmbeddedReleaseKey& key, const std::uint8_t* manifest,
                           std::size_t manifest_size, const std::uint8_t* signature,
                           std::size_t signature_size, std::string& error);

}  // namespace cosmo::bootstrap
