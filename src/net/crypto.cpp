#include "falcon/net/crypto.hpp"

#include <algorithm>
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
#endif

namespace falcon::net {

namespace {

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

#ifdef _WIN32

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

#endif

}  // namespace

// ---- the public functions -----------------------------------------------------------------------------------------------------------------------------------

bool randomBytes(std::uint8_t* out, std::size_t n) {
#ifdef _WIN32
    return NT_SUCCESS(BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
    (void)out;
    (void)n;
    return false;
#endif
}

Bytes sha1(const Bytes& data) {
#ifdef _WIN32
    return hashWith(BCRYPT_SHA1_ALGORITHM, data);
#else
    (void)data;
    return {};
#endif
}

Bytes sha256(const Bytes& data) {
#ifdef _WIN32
    return hashWith(BCRYPT_SHA256_ALGORITHM, data);
#else
    (void)data;
    return {};
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
    (void)publicKey;
    (void)plain;
    (void)out;
    error = "no cryptography for this system";
    return false;
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
    (void)privateKey;
    (void)message;
    (void)signature;
    error = "no cryptography for this system";
    return false;
#endif
}

// ---- Cfb8 ---------------------------------------------------------------------------------------------------------------------------------------------------

Cfb8::~Cfb8() {
#ifdef _WIN32
    if (key_ != nullptr) BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(key_));
    if (alg_ != nullptr) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(alg_), 0);
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
    (void)key;
    (void)iv;
    return false;
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
    (void)data;
    (void)n;
    (void)encrypt;
#endif
}

}  // namespace falcon::net
