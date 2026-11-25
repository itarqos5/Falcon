#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#else
#include "falcon/platform/dirs.hpp"

#include <fcntl.h>
#include <openssl/rand.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ctime>

extern char** environ;
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace launcher {

namespace fs = std::filesystem;

#ifdef _WIN32

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

#else

// (wchar_t is 32 bits here: the code points themselves. A byte that is not UTF-8 becomes U+FFFD.)
std::wstring widen(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
        bool good = extra >= 0 && i + static_cast<std::size_t>(extra) < utf8.size();
        char32_t code = 0;
        if (good) {
            code = extra == 0 ? c : static_cast<char32_t>(c & (0x3F >> extra));
            for (int k = 1; k <= extra; ++k) {
                const unsigned char next = static_cast<unsigned char>(utf8[i + static_cast<std::size_t>(k)]);
                if ((next & 0xC0) != 0x80) {
                    good = false;
                    break;
                }
                code = (code << 6) | (next & 0x3F);
            }
        }
        out.push_back(good ? static_cast<wchar_t>(code) : static_cast<wchar_t>(0xFFFD));
        i += good ? static_cast<std::size_t>(extra) + 1 : 1;
    }
    return out;
}

std::string narrow(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());
    for (const wchar_t w : wide) {
        const char32_t c = static_cast<char32_t>(w);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

#endif

fs::path dataRoot() {
    if (const char* home = std::getenv("FALCON_LAUNCHER_HOME"); home != nullptr && *home != '\0') return fs::path(widen(home));
#ifdef _WIN32
    wchar_t buffer[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    fs::path base = n > 0 && n < std::size(buffer) ? fs::path(buffer) : fs::temp_directory_path();
    return base / "Falcon";
#else
    return falcon::falconDataDir().value_or(fs::temp_directory_path() / "Falcon");  // (~/.local/share/Falcon, the same folder the game uses)
#endif
}

namespace {
const auto g_start = std::chrono::steady_clock::now();
std::mutex g_logMutex;
}  // namespace

double millisSinceStart() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_start).count(); }

void logLine(std::string_view text) {
    char stamp[48];
#ifdef _WIN32
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
#else
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm t{};
    localtime_r(&seconds, &t);
    const int millis = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, millis);
#endif
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
#ifdef _WIN32
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(byteCount), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(byteCount));
#endif
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
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temp, ec);
        return false;
    }
#else
    fs::rename(temp, path, ec);  // (replaces the old file in one step)
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
#endif
    return true;
}

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

#ifndef _WIN32
namespace {

// xdg-open (Linux) or open (a Mac) starts the program the desktop has for an address or a folder. (No shell: the argument is never read as a command.)
void openWithDesktop(const std::string& target) {
#ifdef __APPLE__
    std::string program = "open";
#else
    std::string program = "xdg-open";
#endif
    std::string argument = target;
    char* argv[] = {program.data(), argument.data(), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    if (posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv, environ) != 0) logLine(program + " could not be started");
    posix_spawn_file_actions_destroy(&actions);
}

}  // namespace
#endif

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
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    openWithDesktop(url);
#endif
}

void openFolder(const fs::path& folder) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    openWithDesktop(folder.string());
#endif
}

}  // namespace launcher
