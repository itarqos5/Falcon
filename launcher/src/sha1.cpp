#include "sha1.hpp"

#include "util.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <vector>

namespace launcher {

namespace {

// Incremental SHA-1 on CNG.
class Sha1 {
public:
    Sha1() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return;
        if (BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0) != 0) hash_ = nullptr;
    }
    ~Sha1() {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
        if (alg_ != nullptr) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    void add(const void* data, std::size_t size) {
        if (hash_ != nullptr && size > 0) BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0);
    }
    std::string finish() {
        unsigned char digest[20] = {};
        if (hash_ == nullptr || BCryptFinishHash(hash_, digest, sizeof(digest), 0) != 0) return {};
        return toHex(digest, sizeof(digest));
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

}  // namespace

std::string sha1Hex(std::string_view data) {
    Sha1 h;
    h.add(data.data(), data.size());
    return h.finish();
}

std::string sha1FileHex(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    Sha1 h;
    std::vector<char> buffer(256 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        h.add(buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    return h.finish();
}

}  // namespace launcher
