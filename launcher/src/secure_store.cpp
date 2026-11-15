#include "secure_store.hpp"

#include "util.hpp"

#ifdef _WIN32

#include <windows.h>
#include <wincrypt.h>

namespace launcher {

namespace {
// Mixed into the key, so another program running as the same user does not decrypt it by accident.
DATA_BLOB entropy() {
    static const char kEntropy[] = "Falcon Launcher account v1";
    return DATA_BLOB{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
}
}  // namespace

bool protectToFile(const std::filesystem::path& file, const std::string& secret) {
    DATA_BLOB in{static_cast<DWORD>(secret.size()), reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))};
    DATA_BLOB salt = entropy();
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Falcon Launcher", &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    const bool ok = writeFileAtomic(file, std::string_view(reinterpret_cast<const char*>(out.pbData), out.cbData));
    LocalFree(out.pbData);
    return ok;
}

std::optional<std::string> unprotectFromFile(const std::filesystem::path& file) {
    std::string bytes;
    if (!readFile(file, bytes) || bytes.empty()) return std::nullopt;
    DATA_BLOB in{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB salt = entropy();
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string secret(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return secret;
}

}  // namespace launcher

#else

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <system_error>

namespace launcher {

// Linux has no per-user encryption for a program to call without a keyring service running (and none is there on every desktop): the sign-in is kept in a file that only this user
// can read (mode 0600, in a folder of mode 0700), the way the official Minecraft launcher and most others keep theirs. It is the refresh token, which is revocable by signing out.
bool protectToFile(const std::filesystem::path& file, const std::string& secret) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    ::chmod(file.parent_path().c_str(), 0700);
    std::filesystem::path temp = file;
    temp += ".tmp";
    const int descriptor = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);  // (never readable by anybody else, not even for a moment)
    if (descriptor < 0) return false;
    std::size_t written = 0;
    bool ok = true;
    while (ok && written < secret.size()) {
        const ssize_t n = ::write(descriptor, secret.data() + written, secret.size() - written);
        if (n <= 0) ok = false;
        else written += static_cast<std::size_t>(n);
    }
    ok = ok && ::fsync(descriptor) == 0;
    ::close(descriptor);
    if (ok) std::filesystem::rename(temp, file, ec);
    if (!ok || ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::optional<std::string> unprotectFromFile(const std::filesystem::path& file) {
    std::string bytes;
    if (!readFile(file, bytes) || bytes.empty()) return std::nullopt;
    return bytes;
}

}  // namespace launcher

#endif
