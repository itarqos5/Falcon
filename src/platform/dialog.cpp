#include "falcon/platform/dialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace falcon {

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
namespace {

// Starts `argv[0]` (looked up in PATH) with its output thrown away and waits for it when `wait`. False when it cannot be started or ends with an error.
// (No shell is involved: the arguments are never read as commands.)
bool runTool(const std::vector<std::string>& args, bool wait) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int started = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (started != 0) return false;
    if (!wait) return true;  // (reaped by the system when the program ends; a few such children at most)
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

// There is no message box in the C library: the message goes to the terminal, and to a dialog when the desktop has one of the usual tools (the game may have been
// started from a file manager, where nobody sees the terminal).
void showErrorDialog(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "[falcon] %s: %s\n", title.c_str(), message.c_str());
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if ((display == nullptr || *display == '\0') && (wayland == nullptr || *wayland == '\0')) return;
    if (runTool({"zenity", "--error", "--no-markup", "--title", title, "--text", message}, true)) return;
    if (runTool({"kdialog", "--error", message, "--title", title}, true)) return;
    runTool({"xmessage", "-center", title + ": " + message}, true);
}

void openFolderInFileManager(const std::string& path) { runTool({"xdg-open", path}, false); }

void attachParentConsole() {}  // (a terminal is already the program's own when it was started from one)
#endif

}  // namespace falcon
