#include "apex/platform/dialog.hpp"

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace apex {

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                        nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

}  // namespace

void showErrorDialog(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "[falcon] %s: %s\n", title.c_str(), message.c_str());
    MessageBoxW(nullptr, widen(message).c_str(), widen(title).c_str(), MB_OK | MB_ICONERROR);
}

void openFolderInFileManager(const std::string& path) {
    ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void attachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;  // already redirected to a file/pipe
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;          // started by double-click
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}
#else
void showErrorDialog(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "[falcon] %s: %s\n", title.c_str(), message.c_str());
}

void openFolderInFileManager(const std::string&) {}

void attachParentConsole() {}
#endif

}  // namespace apex
