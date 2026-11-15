#include "sha1.hpp"

#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

#include <fstream>
#include <vector>

namespace launcher {

namespace {

#ifndef _WIN32

// Incremental SHA-1 or SHA-256 on OpenSSL.
class Digest {
public:
    explicit Digest(bool sha256 = false) : size_(sha256 ? 32 : 20), context_(EVP_MD_CTX_new()) {
        if (context_ != nullptr && EVP_DigestInit_ex(context_, sha256 ? EVP_sha256() : EVP_sha1(), nullptr) != 1) {
            EVP_MD_CTX_free(context_);
            context_ = nullptr;
        }
    }
    ~Digest() {
        if (context_ != nullptr) EVP_MD_CTX_free(context_);
    }
    Digest(const Digest&) = delete;
    Digest& operator=(const Digest&) = delete;
    void add(const void* data, std::size_t size) {
        if (context_ != nullptr && size > 0) EVP_DigestUpdate(context_, data, size);
    }
    std::string finish() {
        unsigned char digest[EVP_MAX_MD_SIZE] = {};
        unsigned int length = 0;
        if (context_ == nullptr || EVP_DigestFinal_ex(context_, digest, &length) != 1 || length != size_) return {};
        return toHex(digest, size_);
    }

private:
    std::size_t size_;
    EVP_MD_CTX* context_ = nullptr;
};

#else

// Incremental SHA-1 or SHA-256 on CNG.
class Digest {
public:
    explicit Digest(bool sha256 = false) : size_(sha256 ? 32 : 20) {
        if (BCryptOpenAlgorithmProvider(&alg_, sha256 ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return;
        if (BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0) != 0) hash_ = nullptr;
    }
    ~Digest() {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
        if (alg_ != nullptr) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    void add(const void* data, std::size_t size) {
        if (hash_ != nullptr && size > 0) BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0);
    }
    std::string finish() {
        unsigned char digest[32] = {};
        if (hash_ == nullptr || BCryptFinishHash(hash_, digest, static_cast<ULONG>(size_), 0) != 0) return {};
        return toHex(digest, size_);
    }

private:
    std::size_t size_;
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

#endif

std::string fileDigest(const std::filesystem::path& file, bool sha256) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    Digest h(sha256);
    std::vector<char> buffer(256 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        h.add(buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    return h.finish();
}

}  // namespace

std::string sha1Hex(std::string_view data) {
    Digest h;
    h.add(data.data(), data.size());
    return h.finish();
}

std::string sha1FileHex(const std::filesystem::path& file) { return fileDigest(file, false); }

std::string sha256Hex(std::string_view data) {
    Digest h(true);
    h.add(data.data(), data.size());
    return h.finish();
}

std::string sha256FileHex(const std::filesystem::path& file) { return fileDigest(file, true); }

}  // namespace launcher
