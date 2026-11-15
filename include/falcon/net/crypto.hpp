#pragma once

#include "falcon/net/bytes.hpp"

#include <cstdint>
#include <string>

namespace falcon::net {

// The cryptography a server in online mode asks for, on the system's own library (Windows CNG): no code of its own for the ciphers.

// Random bytes from the system's generator. False when it fails.
[[nodiscard]] bool randomBytes(std::uint8_t* out, std::size_t n);

[[nodiscard]] Bytes sha1(const Bytes& data);    // empty when the system's library fails
[[nodiscard]] Bytes sha256(const Bytes& data);

// The hash that Mojang's session server is told about a login: SHA-1 of the server's id, the shared secret and the server's public key (as it sent it, X.509), written
// the way Java's BigInteger.toString(16) writes it (the digest as a signed number, so a leading '-' when the first bit is set). "Notch" alone gives
// 4ed1f46bbe04bc756bcb17c0c7ce3e4632f06a48.
[[nodiscard]] std::string minecraftDigestHex(const Bytes& sha1Digest);
[[nodiscard]] std::string sessionHash(const std::string& serverId, const Bytes& sharedSecret, const Bytes& serverPublicKey);

// RSA with PKCS#1 v1.5 padding (what the login uses to send the shared secret and the verify token): `publicKey` is X.509 SubjectPublicKeyInfo DER.
[[nodiscard]] bool rsaEncrypt(const Bytes& publicKey, const Bytes& plain, Bytes& out, std::string& error);
// SHA256withRSA (what chat is signed with): `privateKey` is DER, PKCS#8 or PKCS#1.
[[nodiscard]] bool rsaSign(const Bytes& privateKey, const Bytes& message, Bytes& signature, std::string& error);

// AES-128 in CFB mode with a feedback of 8 bits: the stream cipher of an encrypted connection (the key is also the first IV). Each direction has its own, and each keeps its
// place in the stream between calls.
class Cfb8 {
public:
    Cfb8() = default;
    ~Cfb8();
    Cfb8(const Cfb8&) = delete;
    Cfb8& operator=(const Cfb8&) = delete;

    // `key` is 16 bytes; `iv` is 16 bytes. False when the system's library fails.
    [[nodiscard]] bool init(const std::uint8_t* key, const std::uint8_t* iv);
    [[nodiscard]] bool ready() const { return key_ != nullptr; }
    // Encrypts or decrypts `n` bytes in place, continuing the stream.
    void encrypt(std::uint8_t* data, std::size_t n) { process(data, n, true); }
    void decrypt(std::uint8_t* data, std::size_t n) { process(data, n, false); }

private:
    void process(std::uint8_t* data, std::size_t n, bool encrypt);
    void* alg_ = nullptr;
    void* key_ = nullptr;
    std::uint8_t iv_[16] = {};
};

}  // namespace falcon::net
