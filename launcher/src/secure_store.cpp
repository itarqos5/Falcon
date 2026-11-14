#include "secure_store.hpp"

#include "util.hpp"

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
