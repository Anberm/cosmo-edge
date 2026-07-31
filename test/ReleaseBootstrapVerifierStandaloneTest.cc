#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "bootstrap/ReleaseBootstrapVerifier.h"

int main() {
    cosmo::bootstrap::EmbeddedReleaseKey key{};
    std::vector<std::uint8_t> pem;
    std::string error;
    if (!cosmo::bootstrap::LoadAndValidateEmbeddedReleaseKey(key, pem, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    constexpr char expected_pem[] =
        "-----BEGIN PUBLIC KEY-----\n"
        "MCowBQYDK2VwAyEAPUAXw+hDiVqStwqnTRt+vJyYLM8uxJaMwM1V8Sr0Zgw=\n"
        "-----END PUBLIC KEY-----\n";
    if (pem != std::vector<std::uint8_t>(expected_pem, expected_pem + sizeof(expected_pem) - 1)) {
        std::cerr << "canonical PEM mismatch\n";
        return 1;
    }

    const std::array<std::uint8_t, 1> message = {0x72};
    std::array<std::uint8_t, 64> signature    = {
        0x92, 0xa0, 0x09, 0xa9, 0xf0, 0xd4, 0xca, 0xb8, 0x72, 0x0e, 0x82, 0x0b, 0x5f, 0x64, 0x25, 0x40,
        0xa2, 0xb2, 0x7b, 0x54, 0x16, 0x50, 0x3f, 0x8f, 0xb3, 0x76, 0x22, 0x23, 0xeb, 0xdb, 0x69, 0xda,
        0x08, 0x5a, 0xc1, 0xe4, 0x3e, 0x15, 0x99, 0x6e, 0x45, 0x8f, 0x36, 0x13, 0xd0, 0xf1, 0x1d, 0x8c,
        0x38, 0x7b, 0x2e, 0xae, 0xb4, 0x30, 0x2a, 0xee, 0xb0, 0x0d, 0x29, 0x16, 0x12, 0xbb, 0x0c, 0x00};
    if (!cosmo::bootstrap::VerifyEd25519Manifest(key, message.data(), message.size(), signature.data(),
                                                 signature.size(), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    signature[0] ^= 1;
    error.clear();
    if (cosmo::bootstrap::VerifyEd25519Manifest(key, message.data(), message.size(), signature.data(),
                                                signature.size(), error)) {
        std::cerr << "modified signature was accepted\n";
        return 1;
    }
    std::cout << "release bootstrap embedded-key verifier: PASS\n";
    return 0;
}
