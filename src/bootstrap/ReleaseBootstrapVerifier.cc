#include "bootstrap/ReleaseBootstrapVerifier.h"

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

extern "C" {
extern const std::uint8_t cosmo_release_public_key_raw_v1[32];
extern const std::uint8_t cosmo_release_public_key_id_v1[16];
extern const std::uint8_t cosmo_release_public_key_pem_sha256_v1[32];
}

namespace cosmo::bootstrap {
namespace {

    constexpr std::array<std::uint8_t, 23> kReleaseKeyIdDomain = {'c', 'o', 's', 'm', 'o', '-', 'r', 'e',
                                                                  'l', 'e', 'a', 's', 'e', '-', 'k', 'e',
                                                                  'y', '-', 'i', 'd', '-', 'v', '1'};

    using BioPtr        = std::unique_ptr<BIO, decltype(&BIO_free)>;
    using KeyPtr        = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using KeyContextPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

    bool IsAllZero(const std::uint8_t* data, std::size_t size) {
        std::uint8_t aggregate = 0;
        for (std::size_t index = 0; index < size; ++index) {
            aggregate = static_cast<std::uint8_t>(aggregate | data[index]);
        }
        return aggregate == 0;
    }

    bool BuildCanonicalPem(const std::uint8_t* raw, std::vector<std::uint8_t>& pem, std::string& error) {
        KeyPtr key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, raw, 32), &EVP_PKEY_free);
        if (!key) {
            error = "cannot construct the embedded Ed25519 public key";
            return false;
        }
        BioPtr output(BIO_new(BIO_s_mem()), &BIO_free);
        if (!output || PEM_write_bio_PUBKEY(output.get(), key.get()) != 1) {
            error = "cannot encode the embedded Ed25519 public key";
            return false;
        }
        const char* bytes = nullptr;
        const long length = BIO_get_mem_data(output.get(), &bytes);
        if (length <= 0 || length > 16 * 1024 || bytes == nullptr) {
            error = "embedded public-key PEM encoding has an invalid size";
            return false;
        }
        pem.assign(bytes, bytes + length);
        return true;
    }

}  // namespace

bool LoadAndValidateEmbeddedReleaseKey(EmbeddedReleaseKey& key, std::vector<std::uint8_t>& canonical_pem,
                                       std::string& error) {
    std::copy_n(cosmo_release_public_key_raw_v1, sizeof(key.raw), key.raw);
    std::copy_n(cosmo_release_public_key_id_v1, sizeof(key.key_id), key.key_id);
    std::copy_n(cosmo_release_public_key_pem_sha256_v1, sizeof(key.pem_sha256), key.pem_sha256);
    if (IsAllZero(key.raw, sizeof(key.raw)) || IsAllZero(key.key_id, sizeof(key.key_id)) ||
        IsAllZero(key.pem_sha256, sizeof(key.pem_sha256))) {
        error = "embedded release trust anchor contains a zero identity";
        return false;
    }

    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> key_id_digest{};
    SHA256_CTX key_id_context;
    if (SHA256_Init(&key_id_context) != 1 ||
        SHA256_Update(&key_id_context, kReleaseKeyIdDomain.data(), kReleaseKeyIdDomain.size()) != 1) {
        error = "cannot derive the embedded release key ID";
        return false;
    }
    const std::array<std::uint8_t, 2> version = {0, 1};
    if (SHA256_Update(&key_id_context, version.data(), version.size()) != 1 ||
        SHA256_Update(&key_id_context, key.raw, sizeof(key.raw)) != 1 ||
        SHA256_Final(key_id_digest.data(), &key_id_context) != 1) {
        error = "cannot derive the embedded release key ID";
        return false;
    }
    if (CRYPTO_memcmp(key.key_id, key_id_digest.data(), sizeof(key.key_id)) != 0) {
        error = "embedded release key ID does not match the raw public key";
        return false;
    }

    if (!BuildCanonicalPem(key.raw, canonical_pem, error)) {
        return false;
    }
    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> pem_digest{};
    if (SHA256(canonical_pem.data(), canonical_pem.size(), pem_digest.data()) == nullptr ||
        CRYPTO_memcmp(key.pem_sha256, pem_digest.data(), sizeof(key.pem_sha256)) != 0) {
        error = "embedded release PEM digest does not match the raw public key";
        return false;
    }
    return true;
}

bool VerifyEd25519Manifest(const EmbeddedReleaseKey& key, const std::uint8_t* manifest,
                           std::size_t manifest_size, const std::uint8_t* signature,
                           std::size_t signature_size, std::string& error) {
    if (manifest == nullptr || manifest_size == 0 || manifest_size > 128 * 1024 || signature == nullptr ||
        signature_size != 64) {
        error = "bootstrap manifest or signature size is invalid";
        return false;
    }
    KeyPtr public_key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key.raw, sizeof(key.raw)),
                      &EVP_PKEY_free);
    KeyContextPtr context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!public_key || !context ||
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, public_key.get()) != 1) {
        error = "cannot initialize embedded-key signature verification";
        return false;
    }
    if (EVP_DigestVerify(context.get(), signature, signature_size, manifest, manifest_size) != 1) {
        error = "release manifest signature does not match the embedded key";
        return false;
    }
    return true;
}

}  // namespace cosmo::bootstrap
