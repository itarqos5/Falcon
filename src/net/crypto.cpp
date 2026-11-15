#include "falcon/net/crypto.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif
#else
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>
#endif

namespace falcon::net {

namespace {

#ifdef _WIN32

std::string statusText(long status) {
    char text[32];
    std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(status));
    return text;
}

// ---- DER ----------------------------------------------------------------------------------------------------------------------------------------------------

struct Tlv {
    int tag = 0;
    const std::uint8_t* data = nullptr;
    std::size_t len = 0;
};

// The next element (tag, length, contents) between `p` and `end`; `p` moves past it.
bool nextTlv(const std::uint8_t*& p, const std::uint8_t* end, Tlv& out) {
    if (end - p < 2) return false;
    out.tag = *p++;
    std::size_t len = *p++;
    if ((len & 0x80) != 0) {
        const std::size_t count = len & 0x7F;
        if (count == 0 || count > 4 || static_cast<std::size_t>(end - p) < count) return false;
        len = 0;
        for (std::size_t i = 0; i < count; ++i) len = (len << 8) | *p++;
    }
    if (static_cast<std::size_t>(end - p) < len) return false;
    out.data = p;
    out.len = len;
    p += len;
    return true;
}

// An INTEGER's bytes without the zero that keeps it positive.
Bytes integerBytes(const Tlv& t) {
    const std::uint8_t* p = t.data;
    std::size_t n = t.len;
    while (n > 1 && *p == 0) {
        ++p;
        --n;
    }
    return Bytes(p, p + n);
}

Bytes leftPad(Bytes b, std::size_t size) {
    if (b.size() >= size) return b;
    b.insert(b.begin(), size - b.size(), 0);
    return b;
}

// RSAPublicKey { n, e } out of an X.509 SubjectPublicKeyInfo.
bool parsePublicKey(const Bytes& der, Bytes& n, Bytes& e) {
    const std::uint8_t* p = der.data();
    const std::uint8_t* end = p + der.size();
    Tlv spki, algorithm, bits;
    if (!nextTlv(p, end, spki) || spki.tag != 0x30) return false;
    const std::uint8_t* q = spki.data;
    const std::uint8_t* qEnd = q + spki.len;
    if (!nextTlv(q, qEnd, algorithm) || algorithm.tag != 0x30) return false;
    if (!nextTlv(q, qEnd, bits) || bits.tag != 0x03 || bits.len < 2) return false;
    const std::uint8_t* k = bits.data + 1;  // (the first byte counts the unused bits)
    const std::uint8_t* kEnd = bits.data + bits.len;
    Tlv key, modulus, exponent;
    if (!nextTlv(k, kEnd, key) || key.tag != 0x30) return false;
    const std::uint8_t* m = key.data;
    const std::uint8_t* mEnd = m + key.len;
    if (!nextTlv(m, mEnd, modulus) || modulus.tag != 0x02 || !nextTlv(m, mEnd, exponent) || exponent.tag != 0x02) return false;
    n = integerBytes(modulus);
    e = integerBytes(exponent);
    return !n.empty() && !e.empty();
}

// The modulus, the public exponent and the two primes of an RSA private key (PKCS#8, or PKCS#1 itself).
bool parsePrivateKey(const Bytes& der, Bytes& n, Bytes& e, Bytes& p, Bytes& q) {
    const std::uint8_t* cursor = der.data();
    const std::uint8_t* end = cursor + der.size();
    Tlv outer;
    if (!nextTlv(cursor, end, outer) || outer.tag != 0x30) return false;
    const std::uint8_t* c = outer.data;
    const std::uint8_t* cEnd = c + outer.len;
    Tlv version, next;
    if (!nextTlv(c, cEnd, version) || version.tag != 0x02) return false;
    const std::uint8_t* save = c;
    if (!nextTlv(c, cEnd, next)) return false;
    if (next.tag == 0x30) {  // PKCS#8: the algorithm, then an octet string that holds the PKCS#1 key
        Tlv octets;
        if (!nextTlv(c, cEnd, octets) || octets.tag != 0x04) return false;
        const std::uint8_t* k = octets.data;
        const std::uint8_t* kEnd = k + octets.len;
        Tlv key;
        if (!nextTlv(k, kEnd, key) || key.tag != 0x30) return false;
        c = key.data;
        cEnd = c + key.len;
        if (!nextTlv(c, cEnd, version) || version.tag != 0x02) return false;
    } else {
        c = save;  // PKCS#1: the modulus is next
    }
    Tlv fields[7];  // n, e, d, p, q, dp, dq (the rest is not needed)
    for (Tlv& f : fields) {
        if (!nextTlv(c, cEnd, f) || f.tag != 0x02) return false;
    }
    n = integerBytes(fields[0]);
    e = integerBytes(fields[1]);
    p = integerBytes(fields[3]);
    q = integerBytes(fields[4]);
    return !n.empty() && !e.empty() && !p.empty() && !q.empty();
}

// ---- CNG ----------------------------------------------------------------------------------------------------------------------------------------------------

Bytes hashWith(const wchar_t* algorithm, const Bytes& data) {
    Bytes digest;
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0))) return digest;
    ULONG size = 0, got = 0;
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof size, &got, 0);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (size != 0 && NT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        digest.resize(size);
        const bool ok = NT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0)) && NT_SUCCESS(BCryptFinishHash(hash, digest.data(), size, 0));
        if (!ok) digest.clear();
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return digest;
}

// A key blob of the system's own kind: the header, then the numbers as big-endian bytes.
Bytes rsaBlob(bool isPrivate, const Bytes& n, const Bytes& e, const Bytes& p, const Bytes& q) {
    const std::size_t half = n.size() / 2;
    BCRYPT_RSAKEY_BLOB header{};
    header.Magic = isPrivate ? BCRYPT_RSAPRIVATE_MAGIC : BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = static_cast<ULONG>(n.size() * 8);
    header.cbPublicExp = static_cast<ULONG>(e.size());
    header.cbModulus = static_cast<ULONG>(n.size());
    header.cbPrime1 = isPrivate ? static_cast<ULONG>(half) : 0;
    header.cbPrime2 = isPrivate ? static_cast<ULONG>(half) : 0;
    Bytes blob(reinterpret_cast<const std::uint8_t*>(&header), reinterpret_cast<const std::uint8_t*>(&header) + sizeof header);
    blob.insert(blob.end(), e.begin(), e.end());
    blob.insert(blob.end(), n.begin(), n.end());
    if (isPrivate) {
        const Bytes p1 = leftPad(p, half), q1 = leftPad(q, half);
        blob.insert(blob.end(), p1.begin(), p1.end());
        blob.insert(blob.end(), q1.begin(), q1.end());
    }
    return blob;
}

#else

// ---- OpenSSL ------------------------------------------------------------------------------------------------------------------------------------------------

// OpenSSL reads the DER keys itself and does the arithmetic of RSA; only what it answers with has to be turned into text.
std::string sslError() {
    const unsigned long code = ERR_get_error();
    if (code == 0) return "no details";
    char text[256];
    ERR_error_string_n(code, text, sizeof text);
    return text;
}

struct KeyFree {
    void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};
struct ContextFree {
    void operator()(EVP_PKEY_CTX* context) const { EVP_PKEY_CTX_free(context); }
};
struct DigestFree {
    void operator()(EVP_MD_CTX* context) const { EVP_MD_CTX_free(context); }
};

Bytes hashWith(const EVP_MD* algorithm, const Bytes& data) {
    Bytes digest(EVP_MAX_MD_SIZE);
    unsigned int size = 0;
    if (EVP_Digest(data.data(), data.size(), digest.data(), &size, algorithm, nullptr) != 1) return {};
    digest.resize(size);
    return digest;
}

#endif

}  // namespace

// ---- the public functions -----------------------------------------------------------------------------------------------------------------------------------

bool randomBytes(std::uint8_t* out, std::size_t n) {
#ifdef _WIN32
    return NT_SUCCESS(BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
    return n <= static_cast<std::size_t>(INT_MAX) && RAND_bytes(out, static_cast<int>(n)) == 1;
#endif
}

Bytes sha1(const Bytes& data) {
#ifdef _WIN32
    return hashWith(BCRYPT_SHA1_ALGORITHM, data);
#else
    return hashWith(EVP_sha1(), data);
#endif
}

Bytes sha256(const Bytes& data) {
#ifdef _WIN32
    return hashWith(BCRYPT_SHA256_ALGORITHM, data);
#else
    return hashWith(EVP_sha256(), data);
#endif
}

std::string minecraftDigestHex(const Bytes& sha1Digest) {
    Bytes d = sha1Digest;
    if (d.empty()) return {};
    const bool negative = (d[0] & 0x80) != 0;
    if (negative) {  // two's complement: invert and add one
        for (std::uint8_t& b : d) b = static_cast<std::uint8_t>(~b);
        for (std::size_t i = d.size(); i-- > 0;) {
            if (++d[i] != 0) break;
        }
    }
    static const char* const kHex = "0123456789abcdef";
    std::string hex;
    for (const std::uint8_t b : d) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
    }
    const std::size_t first = hex.find_first_not_of('0');
    hex = first == std::string::npos ? "0" : hex.substr(first);
    return negative ? "-" + hex : hex;
}

std::string sessionHash(const std::string& serverId, const Bytes& sharedSecret, const Bytes& serverPublicKey) {
    Bytes input(serverId.begin(), serverId.end());
    input.insert(input.end(), sharedSecret.begin(), sharedSecret.end());
    input.insert(input.end(), serverPublicKey.begin(), serverPublicKey.end());
    return minecraftDigestHex(sha1(input));
}

bool rsaEncrypt(const Bytes& publicKey, const Bytes& plain, Bytes& out, std::string& error) {
#ifdef _WIN32
    Bytes n, e;
    if (!parsePublicKey(publicKey, n, e)) {
        error = "the server's public key cannot be read";
        return false;
    }
    const Bytes blob = rsaBlob(false, n, e, {}, {});
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        error = "the system's RSA is not available (" + statusText(status) + ")";
        return false;
    }
    BCRYPT_KEY_HANDLE key = nullptr;
    status = BCryptImportKeyPair(alg, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key, const_cast<PUCHAR>(blob.data()), static_cast<ULONG>(blob.size()), 0);
    bool ok = false;
    if (NT_SUCCESS(status)) {
        out.assign(n.size() + 16, 0);
        ULONG written = 0;
        status = BCryptEncrypt(key, const_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()), nullptr, nullptr, 0, out.data(), static_cast<ULONG>(out.size()), &written, BCRYPT_PAD_PKCS1);
        if (NT_SUCCESS(status)) {
            out.resize(written);
            ok = true;
        }
        BCryptDestroyKey(key);
    }
    if (!ok) error = "the key exchange failed (" + statusText(status) + ")";
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#else
    const unsigned char* cursor = publicKey.data();
    const std::unique_ptr<EVP_PKEY, KeyFree> key(d2i_PUBKEY(nullptr, &cursor, static_cast<long>(publicKey.size())));
    if (!key) {
        error = "the server's public key cannot be read";
        return false;
    }
    const std::unique_ptr<EVP_PKEY_CTX, ContextFree> context(EVP_PKEY_CTX_new(key.get(), nullptr));
    if (!context || EVP_PKEY_encrypt_init(context.get()) != 1 || EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_PADDING) <= 0) {
        error = "the key exchange failed (" + sslError() + ")";
        return false;
    }
    std::size_t size = 0;
    if (EVP_PKEY_encrypt(context.get(), nullptr, &size, plain.data(), plain.size()) != 1) {
        error = "the key exchange failed (" + sslError() + ")";
        return false;
    }
    out.assign(size, 0);
    if (EVP_PKEY_encrypt(context.get(), out.data(), &size, plain.data(), plain.size()) != 1) {
        error = "the key exchange failed (" + sslError() + ")";
        return false;
    }
    out.resize(size);
    return true;
#endif
}

bool rsaSign(const Bytes& privateKey, const Bytes& message, Bytes& signature, std::string& error) {
#ifdef _WIN32
    Bytes n, e, p, q;
    if (!parsePrivateKey(privateKey, n, e, p, q)) {
        error = "the private key cannot be read";
        return false;
    }
    const Bytes blob = rsaBlob(true, n, e, p, q);
    const Bytes digest = sha256(message);
    if (digest.empty()) {
        error = "the system's SHA-256 is not available";
        return false;
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        error = "the system's RSA is not available (" + statusText(status) + ")";
        return false;
    }
    BCRYPT_KEY_HANDLE key = nullptr;
    status = BCryptImportKeyPair(alg, nullptr, BCRYPT_RSAPRIVATE_BLOB, &key, const_cast<PUCHAR>(blob.data()), static_cast<ULONG>(blob.size()), 0);
    bool ok = false;
    if (NT_SUCCESS(status)) {
        BCRYPT_PKCS1_PADDING_INFO padding{};
        padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
        signature.assign(n.size() + 16, 0);
        ULONG written = 0;
        status = BCryptSignHash(key, &padding, const_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), signature.data(), static_cast<ULONG>(signature.size()), &written, BCRYPT_PAD_PKCS1);
        if (NT_SUCCESS(status)) {
            signature.resize(written);
            ok = true;
        }
        BCryptDestroyKey(key);
    }
    if (!ok) error = "signing failed (" + statusText(status) + ")";
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#else
    const unsigned char* cursor = privateKey.data();
    const std::unique_ptr<EVP_PKEY, KeyFree> key(d2i_AutoPrivateKey(nullptr, &cursor, static_cast<long>(privateKey.size())));  // (PKCS#8 or PKCS#1)
    if (!key) {
        error = "the private key cannot be read";
        return false;
    }
    const std::unique_ptr<EVP_MD_CTX, DigestFree> context(EVP_MD_CTX_new());
    std::size_t size = 0;
    if (!context || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1 ||
        EVP_DigestSign(context.get(), nullptr, &size, message.data(), message.size()) != 1) {
        error = "signing failed (" + sslError() + ")";
        return false;
    }
    signature.assign(size, 0);
    if (EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) != 1) {
        error = "signing failed (" + sslError() + ")";
        return false;
    }
    signature.resize(size);  // (PKCS#1 v1.5 padding, the default for an RSA key)
    return true;
#endif
}

// ---- Cfb8 ---------------------------------------------------------------------------------------------------------------------------------------------------

Cfb8::~Cfb8() {
#ifdef _WIN32
    if (key_ != nullptr) BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(key_));
    if (alg_ != nullptr) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(alg_), 0);
#else
    // (alg_ is the context that encrypts and key_ the one that decrypts: a stream in CFB mode goes one way, and each of the two directions has its own Cfb8)
    if (alg_ != nullptr) EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(alg_));
    if (key_ != nullptr) EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(key_));
#endif
}

bool Cfb8::init(const std::uint8_t* key, const std::uint8_t* iv) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
    ULONG feedback = 1;  // one byte of feedback: CFB-8
    bool ok = NT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CFB)), sizeof(BCRYPT_CHAIN_MODE_CFB), 0)) &&
              NT_SUCCESS(BCryptSetProperty(alg, BCRYPT_MESSAGE_BLOCK_LENGTH, reinterpret_cast<PUCHAR>(&feedback), sizeof feedback, 0));
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (ok) ok = NT_SUCCESS(BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, const_cast<PUCHAR>(key), 16, 0));
    if (!ok) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    alg_ = alg;
    key_ = handle;
    std::copy(iv, iv + 16, iv_);
    return true;
#else
    EVP_CIPHER_CTX* encrypting = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX* decrypting = EVP_CIPHER_CTX_new();
    const bool ok = encrypting != nullptr && decrypting != nullptr &&
                    EVP_EncryptInit_ex(encrypting, EVP_aes_128_cfb8(), nullptr, key, iv) == 1 &&
                    EVP_DecryptInit_ex(decrypting, EVP_aes_128_cfb8(), nullptr, key, iv) == 1;
    if (!ok) {
        EVP_CIPHER_CTX_free(encrypting);
        EVP_CIPHER_CTX_free(decrypting);
        return false;
    }
    alg_ = encrypting;
    key_ = decrypting;
    return true;
#endif
}

void Cfb8::process(std::uint8_t* data, std::size_t n, bool encrypt) {
#ifdef _WIN32
    if (key_ == nullptr || n == 0) return;
    const std::vector<std::uint8_t> input(data, data + n);  // (the system wants its own input and output apart to be safe)
    ULONG written = 0;
    const auto handle = static_cast<BCRYPT_KEY_HANDLE>(key_);
    if (encrypt) BCryptEncrypt(handle, const_cast<PUCHAR>(input.data()), static_cast<ULONG>(n), nullptr, iv_, 16, data, static_cast<ULONG>(n), &written, 0);
    else BCryptDecrypt(handle, const_cast<PUCHAR>(input.data()), static_cast<ULONG>(n), nullptr, iv_, 16, data, static_cast<ULONG>(n), &written, 0);
#else
    if (key_ == nullptr || n == 0) return;
    EVP_CIPHER_CTX* context = static_cast<EVP_CIPHER_CTX*>(encrypt ? alg_ : key_);
    int written = 0;
    // (in place is allowed for a stream mode; a chunk is far below the 2 GB an int holds)
    if (encrypt) EVP_EncryptUpdate(context, data, &written, data, static_cast<int>(n));
    else EVP_DecryptUpdate(context, data, &written, data, static_cast<int>(n));
#endif
}

}  // namespace falcon::net
