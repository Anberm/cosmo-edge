// CipherUtil — Append 16-byte MAC after encrypted content

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include "util/CipherUtil.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>

#include "cryptopp/aes.h"
#include "cryptopp/base64.h"
#include "cryptopp/cryptlib.h"
#include "cryptopp/filters.h"
#include "cryptopp/gcm.h"
#include "cryptopp/hex.h"
#include "cryptopp/hmac.h"
#include "cryptopp/md5.h"
#include "cryptopp/modes.h"
#include "cryptopp/sha.h"
#include "util/FileUtil.h"

namespace cosmo::util {

// SAFETY NOTICE:
// Multiple reinterpret_cast<const uint8_t*> and reinterpret_cast<uint8_t*> are used
// in this file to cast std::string::c_str() (const char*) or basic char arrays
// to CryptoPP's expected byte* format.
// This is safe as it's a standard idiom for Crypto++ byte-level operations.

namespace crypto = CryptoPP;

namespace {

    constexpr std::size_t kMaxBase64DecodedBytes = 64ULL * 1024 * 1024;

    bool IsBase64Alphabet(unsigned char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9') || value == '+' || value == '/';
    }

    bool DecodeBase64Strict(const std::string& input, std::vector<std::uint8_t>& output) {
        output.clear();
        if (input.empty()) {
            return true;
        }
        if (input.size() % 4 != 0) {
            return false;
        }

        std::size_t padding = 0;
        if (input.back() == '=') {
            ++padding;
        }
        if (input.size() >= 2 && input[input.size() - 2] == '=') {
            ++padding;
        }
        for (std::size_t index = 0; index < input.size() - padding; ++index) {
            if (!IsBase64Alphabet(static_cast<unsigned char>(input[index]))) {
                return false;
            }
        }
        for (std::size_t index = input.size() - padding; index < input.size(); ++index) {
            if (input[index] != '=') {
                return false;
            }
        }

        if (input.size() / 4 > (std::numeric_limits<std::size_t>::max() - 2) / 3) {
            return false;
        }
        const auto expected_size = input.size() / 4 * 3 - padding;
        if (expected_size > kMaxBase64DecodedBytes) {
            return false;
        }

        try {
            crypto::Base64Decoder decoder;
            decoder.PutMessageEnd(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
            if (decoder.MaxRetrievable() != expected_size) {
                return false;
            }
            output.resize(expected_size);
            if (expected_size != 0 && decoder.Get(output.data(), output.size()) != output.size()) {
                output.clear();
                return false;
            }
        } catch (const crypto::Exception&) {
            output.clear();
            return false;
        }
        return true;
    }

}  // namespace

std::string EncAesGcmNoPadding(const std::string& enc_str, const std::string& key, const std::string& iv) {
    using aes_gcm_cipher = crypto::GCM<crypto::AES>;

    char mac[16] = {0};

    std::string out_str;

    out_str.resize(enc_str.size() + 16);

    aes_gcm_cipher::Encryption enc;

    enc.SetKeyWithIV(reinterpret_cast<const uint8_t*>(key.c_str()), key.size(),
                     reinterpret_cast<const uint8_t*>(iv.c_str()));

    enc.EncryptAndAuthenticate(reinterpret_cast<uint8_t*>(&out_str[0]), reinterpret_cast<uint8_t*>(mac),
                               sizeof(mac), reinterpret_cast<const uint8_t*>(iv.c_str()),
                               static_cast<int>(iv.size()), nullptr, 0,
                               reinterpret_cast<const uint8_t*>(enc_str.c_str()), enc_str.size());

    memcpy(&out_str[enc_str.size()], mac, sizeof(mac));

    return out_str;
}

std::string DecAesGcmNoPadding(const std::string& dec_str, const std::string& key, const std::string& iv) {
    using aes_gcm_cipher = crypto::GCM<crypto::AES>;

    std::string out_str;

    if (dec_str.size() <= 16) {
        return out_str;
    }

    size_t out_len = dec_str.size() - 16;

    out_str.resize(out_len);

    aes_gcm_cipher::Decryption dec;

    dec.SetKeyWithIV(reinterpret_cast<const uint8_t*>(key.c_str()), key.size(),
                     reinterpret_cast<const uint8_t*>(iv.c_str()));

    const bool verified = dec.DecryptAndVerify(
        reinterpret_cast<uint8_t*>(&out_str[0]), reinterpret_cast<const uint8_t*>(&dec_str[out_len]), 16,
        reinterpret_cast<const uint8_t*>(iv.c_str()), static_cast<int>(iv.size()), nullptr, 0,
        reinterpret_cast<const uint8_t*>(dec_str.c_str()), out_len);
    if (!verified) {
        std::fill(out_str.begin(), out_str.end(), '\0');
        out_str.clear();
    }

    return out_str;
}

std::string EncMd5(const std::string& enc_str, bool is_upper) {
    crypto::Weak::MD5 md5;

    uint8_t digest[16] = {0};

    md5.CalculateDigest(digest, reinterpret_cast<const uint8_t*>(enc_str.c_str()), enc_str.size());

    std::ostringstream osstr;
    if (is_upper)
        osstr << std::hex << std::uppercase << std::setfill('0');
    else
        osstr << std::hex << std::setfill('0');

    for (int i = 0; static_cast<size_t>(i) < sizeof(digest); ++i) {
        osstr << std::setw(2) << int(digest[i]);
    }

    return osstr.str();
}

std::string EncFileMd5(const std::string& file_path, bool is_upper) {
    std::string read_str = cosmo::util::ReadFile(file_path);

    return EncMd5(read_str, is_upper);
}

std::string EncBase64(const std::string& enc_str) {
    crypto::Base64Encoder encoder(nullptr, false);
    std::string out_str;

    encoder.PutMessageEnd(reinterpret_cast<const uint8_t*>(enc_str.c_str()), enc_str.size());

    size_t size = encoder.MaxRetrievable();

    out_str.resize(size);

    if (size != 0) {
        encoder.Get(reinterpret_cast<uint8_t*>(&out_str[0]), size);
    }

    return out_str;
}

std::string EncBase64Ex(const uint8_t* data, size_t size) {
    crypto::Base64Encoder encoder(nullptr, false);
    std::string out_str;

    if (data == nullptr && size != 0) {
        return {};
    }
    encoder.PutMessageEnd(data, size);

    size_t sz = encoder.MaxRetrievable();

    out_str.resize(sz);

    if (sz != 0) {
        encoder.Get(reinterpret_cast<uint8_t*>(&out_str[0]), sz);
    }

    return out_str;
}

std::string DecBase64(const std::string& dec_str) {
    std::vector<std::uint8_t> decoded;
    if (!DecodeBase64Strict(dec_str, decoded)) {
        return {};
    }
    return {decoded.begin(), decoded.end()};
}

std::vector<uint8_t> DecBase64Vec(const std::string& dec_str) {
    std::vector<std::uint8_t> decoded;
    if (!DecodeBase64Strict(dec_str, decoded)) {
        return {};
    }
    return decoded;
}

std::string Sha1(const std::string& input) {
    CryptoPP::SHA1 sha1;

    std::string digest;
    std::string output;

    // NOLINT: CryptoPP pipeline takes ownership
    CryptoPP::StringSource(input, true, new CryptoPP::HashFilter(sha1, new CryptoPP::StringSink(digest)));
    // NOLINT: CryptoPP pipeline takes ownership
    CryptoPP::StringSource(digest, true, new CryptoPP::HexEncoder(new CryptoPP::StringSink(output)));

    return output;
}

std::string GenerateSignature(const std::string& access_key, const std::string& timestamp,
                              const std::string& noise, const std::string& secret_key) {
    std::string message = access_key + "," + timestamp + "," + noise;

    CryptoPP::HMAC<CryptoPP::SHA256> hmac(reinterpret_cast<const CryptoPP::byte*>(secret_key.c_str()),
                                          secret_key.size());

    std::string digest;
    // NOLINT: CryptoPP pipeline takes ownership
    CryptoPP::StringSource ss1(
        message, true,
        new CryptoPP::HashFilter(hmac, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(digest),
                                                                   false  // No line break
                                                                   )));

    return digest;
}

std::string DecAesEcbPadding(const std::string& dec_str, const std::string& key, const std::string& /*iv*/) {
    CryptoPP::ECB_Mode<CryptoPP::Rijndael>::Decryption decryption;
    decryption.SetKey(reinterpret_cast<const uint8_t*>(key.c_str()), key.size());

    std::string recovered;

    // NOLINT: CryptoPP pipeline takes ownership
    CryptoPP::StringSource ss(
        dec_str, true,
        new CryptoPP::StreamTransformationFilter(decryption, new CryptoPP::StringSink(recovered),
                                                 CryptoPP::StreamTransformationFilter::PKCS_PADDING));

    return recovered;
}
}  // namespace cosmo::util
