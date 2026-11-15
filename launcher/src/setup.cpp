#include "setup.hpp"

#include "sha1.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <fstream>

namespace launcher {

namespace fs = std::filesystem;

#ifdef _WIN32

Blob embeddedResource(const wchar_t* name) {
    HRSRC found = FindResourceW(nullptr, name, RT_RCDATA);
    if (found == nullptr) return {};
    HGLOBAL loaded = LoadResource(nullptr, found);
    if (loaded == nullptr) return {};
    return Blob{static_cast<const unsigned char*>(LockResource(loaded)), static_cast<std::size_t>(SizeofResource(nullptr, found))};
}

#else

// On Linux the pieces are put into the program by the linker (an assembler file with .incbin, see launcher/CMakeLists.txt), each between two symbols. A program built without
// them (the tests) finds the symbols missing, which is what "weak" is for: their address is then null, and the blob is empty.
extern "C" {
extern const unsigned char falcon_client_start[] __attribute__((weak));
extern const unsigned char falcon_client_end[] __attribute__((weak));
extern const unsigned char falcon_ui_start[] __attribute__((weak));
extern const unsigned char falcon_ui_end[] __attribute__((weak));
extern const unsigned char falcon_icon_start[] __attribute__((weak));
extern const unsigned char falcon_icon_end[] __attribute__((weak));
}

namespace {
Blob blobBetween(const unsigned char* begin, const unsigned char* end) {
    if (begin == nullptr || end == nullptr || end <= begin) return {};
    return Blob{begin, static_cast<std::size_t>(end - begin)};
}
}  // namespace

Blob embeddedResource(const wchar_t* name) {
    const std::string which = narrow(name);
    if (which == "FALCON_CLIENT") return blobBetween(falcon_client_start, falcon_client_end);
    if (which == "LAUNCHER_UI") return blobBetween(falcon_ui_start, falcon_ui_end);
    if (which == "FALCON_ICON") return blobBetween(falcon_icon_start, falcon_icon_end);
    return {};
}

#endif

namespace {

#ifdef _WIN32
constexpr const char* kClientFileName = "Falcon.exe";
#else
constexpr const char* kClientFileName = "Falcon";
#endif

fs::path clientFolder(const fs::path& dataRoot, const Blob& client) {
    const std::string hash = sha1Hex(std::string_view(reinterpret_cast<const char*>(client.data), client.size));
    return dataRoot / "client" / fs::path(widen(hash.substr(0, 12)));
}

}  // namespace

bool bundledClientInstalled(const fs::path& dataRoot, const Blob& client, fs::path* exe) {
    if (client.empty()) return false;
    const fs::path file = clientFolder(dataRoot, client) / kClientFileName;
    std::error_code ec;
    if (!fs::exists(file, ec) || fs::file_size(file, ec) != client.size) return false;
    const std::string want = sha1Hex(std::string_view(reinterpret_cast<const char*>(client.data), client.size));
    if (sha1FileHex(file) != want) return false;
    if (exe != nullptr) *exe = file;
    return true;
}

Result<fs::path> installBundledClient(const fs::path& dataRoot, const Blob& client) {
    using R = Result<fs::path>;
    if (client.empty()) return R::fail("no_client", "This launcher was built without the game client inside it.");
    fs::path exe;
    if (bundledClientInstalled(dataRoot, client, &exe)) return R::success(exe);
    const fs::path folder = clientFolder(dataRoot, client);
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return R::fail("disk", "Cannot create " + narrow(folder.wstring()) + ".");
    const fs::path file = folder / kClientFileName;
    if (!writeFileAtomic(file, std::string_view(reinterpret_cast<const char*>(client.data), client.size))) {
        return R::fail("disk", "Cannot write " + narrow(file.wstring()) + " (is it running? is the disk full?).");
    }
#ifndef _WIN32
    fs::permissions(file, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, ec);  // (a program that can be run)
#endif
    if (!bundledClientInstalled(dataRoot, client, &exe)) return R::fail("corrupt", "The game client was written but did not read back correctly.");
    // An older launcher's client is not needed any more.
    for (const auto& entry : fs::directory_iterator(dataRoot / "client", ec)) {
        if (entry.is_directory() && entry.path() != folder) fs::remove_all(entry.path(), ec);
    }
    return R::success(exe);
}

#ifdef _WIN32

bool createStartMenuShortcut() {
    wchar_t self[MAX_PATH * 2];
    if (GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self))) == 0) return false;
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) return false;
    const fs::path link = fs::path(programs) / L"Falcon Launcher.lnk";
    CoTaskMemFree(programs);
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* shell = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shell)))) {
        shell->SetPath(self);
        shell->SetDescription(L"Falcon Launcher");
        shell->SetIconLocation(self, 0);
        shell->SetWorkingDirectory(fs::path(self).parent_path().c_str());
        IPersistFile* file = nullptr;
        if (SUCCEEDED(shell->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            ok = SUCCEEDED(file->Save(link.c_str(), TRUE));
            file->Release();
        }
        shell->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

bool webView2RuntimeInstalled() {
    // Where the Evergreen runtime registers its version (per machine, 32-bit view, and per user).
    static const wchar_t* kKey = L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    static const wchar_t* kKeyWow = L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    const struct {
        HKEY root;
        const wchar_t* key;
    } places[] = {{HKEY_LOCAL_MACHINE, kKeyWow}, {HKEY_LOCAL_MACHINE, kKey}, {HKEY_CURRENT_USER, kKey}};
    for (const auto& p : places) {
        wchar_t version[64] = {};
        DWORD size = sizeof(version);
        if (RegGetValueW(p.root, p.key, L"pv", RRF_RT_REG_SZ, nullptr, version, &size) == ERROR_SUCCESS && version[0] != L'\0' && std::wstring(version) != L"0.0.0.0") return true;
    }
    return false;
}

Result<bool> installWebView2Runtime(HttpClient& http) {
    using R = Result<bool>;
    // Microsoft's own bootstrapper (about 1.7 MB): it downloads and installs the current runtime.
    const fs::path installer = fs::temp_directory_path() / "MicrosoftEdgeWebview2Setup.exe";
    const HttpResponse r = http.download("https://go.microsoft.com/fwlink/p/?LinkId=2124703", installer, nullptr, nullptr);
    if (!r.ok()) return R::fail("network", "Could not download the Microsoft Edge WebView2 installer: " + (r.error.empty() ? "status " + std::to_string(r.status) : r.error));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring command = L"\"" + installer.wstring() + L"\" /silent /install";
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return R::fail("install", "Could not start the WebView2 installer.");
    const DWORD waited = WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::error_code ec;
    fs::remove(installer, ec);
    if (waited != WAIT_OBJECT_0 || !webView2RuntimeInstalled()) return R::fail("install", "The WebView2 runtime did not install (exit code " + std::to_string(code) + ").");
    return R::success(true);
}

#else

// An entry in the application menu (a .desktop file in the user's own folder, with the icon next to it): "Falcon Launcher", for the program that is running.
bool createStartMenuShortcut() {
    char self[PATH_MAX] = {};
    const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0) return false;
    const fs::path data = dataRoot().parent_path();  // (~/.local/share, where dataRoot() is ~/.local/share/Falcon)
    std::string exec;  // the Exec line of the desktop file needs its quotes, backslashes, backticks and dollar signs escaped
    for (const char* c = self; *c != '\0'; ++c) {
        if (*c == '"' || *c == '\\' || *c == '`' || *c == '$') exec.push_back('\\');
        exec.push_back(*c);
    }
    const Blob icon = embeddedResource(L"FALCON_ICON");
    if (!icon.empty()) writeFileAtomic(data / "icons" / "hicolor" / "256x256" / "apps" / "falcon-launcher.png", std::string_view(reinterpret_cast<const char*>(icon.data), icon.size));
    const std::string entry =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Falcon Launcher\n"
        "Comment=Launcher for Falcon, a Minecraft: Java Edition client written in C++\n"
        "Exec=\"" + exec + "\"\n"
        "Icon=falcon-launcher\n"
        "Terminal=false\n"
        "Categories=Game;\n"
        "StartupWMClass=falcon-launcher\n";
    return writeFileAtomic(data / "applications" / "falcon-launcher.desktop", entry);
}

// The web view is a library the program is linked against: if it were missing the program would not have started at all.
bool webView2RuntimeInstalled() { return true; }

Result<bool> installWebView2Runtime(HttpClient&) { return Result<bool>::fail("unsupported", "There is nothing to install here: the web view is WebKitGTK, a package of the system."); }

#endif

}  // namespace launcher
