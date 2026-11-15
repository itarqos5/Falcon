#include "util.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace launcher {

namespace fs = std::filesystem;

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(n, 0)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

fs::path dataRoot() {
    if (const char* home = std::getenv("FALCON_LAUNCHER_HOME"); home != nullptr && *home != '\0') return fs::path(widen(home));
    wchar_t buffer[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    fs::path base = n > 0 && n < std::size(buffer) ? fs::path(buffer) : fs::temp_directory_path();
    return base / "Falcon";
}

namespace {
const auto g_start = std::chrono::steady_clock::now();
std::mutex g_logMutex;
}  // namespace

double millisSinceStart() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_start).count(); }

void logLine(std::string_view text) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[48];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (std::getenv("FALCON_LAUNCHER_LOG_STDERR") != nullptr) std::fprintf(stderr, "%s%.*s\n", stamp, static_cast<int>(text.size()), text.data());
    std::error_code ec;
    const fs::path dir = dataRoot() / "logs";
    fs::create_directories(dir, ec);
    const fs::path file = dir / "launcher.log";
    if (fs::exists(file, ec) && fs::file_size(file, ec) > 1024 * 1024) fs::rename(file, dir / "launcher.old.log", ec);  // (one old copy is kept)
    std::ofstream out(file, std::ios::app | std::ios::binary);
    out << stamp << text << "\n";
}

std::string toHex(const unsigned char* bytes, std::size_t count) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

std::string randomHex(std::size_t byteCount) {
    std::string bytes(byteCount, '\0');
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(byteCount), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return toHex(reinterpret_cast<const unsigned char*>(bytes.data()), byteCount);
}

std::string lowerCase(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

bool writeFileAtomic(const fs::path& path, std::string_view contents) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out) return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool isSafeWebUrl(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size() || url.size() > 2048 || url.substr(0, kScheme.size()) != kScheme) return false;
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F || c == '"' || c == '<' || c == '>') return false;
    }
    return true;
}

void openInBrowser(const std::string& url) {
    if (std::getenv("FALCON_LAUNCHER_NO_BROWSER") != nullptr) {  // (tests: never open the real browser)
        logLine("would open " + url);
        return;
    }
    if (!isSafeWebUrl(url)) return;
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace launcher
