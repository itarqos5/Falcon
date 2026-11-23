#include "setup.hpp"

#include "sha1.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
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

// The folder of one client build: the start of its SHA-256 (stronger than the SHA-1 this used to be). Long enough that no two builds can share a
// folder, short enough to stay a readable path.
constexpr unsigned kClientIdLength = 16;

std::string clientIdentity(const Blob& client) { return sha256Hex(std::string_view(reinterpret_cast<const char*>(client.data), client.size)); }

fs::path clientFolder(const fs::path& dataRoot, const std::string& identity) { return dataRoot / "client" / fs::path(widen(identity.substr(0, kClientIdLength))); }

// True for the folder names this program makes, so the cleanup below never removes anything a player put next to them. 12 is the length of a folder made by an
// earlier launcher, which took the start of the SHA-1: those are removed on the first start of this one, as older folders always were.
bool isClientFolderName(const std::string& name) {
    if (name.size() != kClientIdLength && name.size() != 12) return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

}  // namespace

bool bundledClientInstalled(const fs::path& dataRoot, const Blob& client, fs::path* exe) {
    if (client.empty()) return false;
    const std::string identity = clientIdentity(client);
    const fs::path file = clientFolder(dataRoot, identity) / kClientFileName;
    std::error_code ec;
    if (!fs::exists(file, ec) || fs::file_size(file, ec) != client.size) return false;
    if (sha256FileHex(file) != identity) return false;
    if (exe != nullptr) *exe = file;
    return true;
}

Result<fs::path> installBundledClient(const fs::path& dataRoot, const Blob& client) {
    using R = Result<fs::path>;
    if (client.empty()) return R::fail("no_client", "This launcher was built without the game client inside it.");
    fs::path exe;
    if (bundledClientInstalled(dataRoot, client, &exe)) return R::success(exe);
    const std::string identity = clientIdentity(client);
    const fs::path folder = clientFolder(dataRoot, identity);
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
    // An older launcher's client is not needed any more (only the folders of earlier builds: nothing else in here is touched).
    for (const auto& entry : fs::directory_iterator(dataRoot / "client", ec)) {
        if (entry.is_directory() && entry.path() != folder && isClientFolderName(narrow(entry.path().filename().wstring()))) fs::remove_all(entry.path(), ec);
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

FileSignature verifyFileSignature(const fs::path& file, std::string_view expectedPublisher) {
    FileSignature out;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {  // (nothing to check, and nothing that could have been replaced either)
        out.state = FileSignature::State::Invalid;
        out.error = "the file is missing";
        return out;
    }
    const std::wstring path = file.wstring();
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;                    // never show a dialog: this runs before a window exists
    data.fdwRevocationChecks = WTD_REVOKE_NONE;       // (no network in this path: a PC offline must still be able to start the game)
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    const LONG status = WinVerifyTrust(nullptr, &action, &data);
    if (status == ERROR_SUCCESS) {
        // The signer's name, from the chain WinVerifyTrust just validated (no second parsing of the file).
        const CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        const CRYPT_PROVIDER_SGNR* signer = provider != nullptr ? WTHelperGetProvSignerFromChain(const_cast<CRYPT_PROVIDER_DATA*>(provider), 0, FALSE, 0) : nullptr;
        const PCCERT_CONTEXT signerCert = signer != nullptr && signer->csCertChain > 0 ? signer->pasCertChain[0].pCert : nullptr;
        if (signerCert != nullptr) {  // (the display name the certificate shows: what a player sees in the file's Properties)
            wchar_t name[512] = {};
            if (CertGetNameStringW(signerCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, static_cast<DWORD>(std::size(name))) > 1) out.publisher = narrow(name);
        }
        out.state = FileSignature::State::Signed;
        if (!expectedPublisher.empty() && lowerCase(out.publisher).find(lowerCase(std::string(expectedPublisher))) == std::string::npos) {
            out.state = FileSignature::State::Invalid;
            out.error = "it is signed by \"" + out.publisher + "\", not by the expected publisher";
        }
    } else if (status == TRUST_E_NOSIGNATURE || status == TRUST_E_SUBJECT_FORM_UNKNOWN || status == TRUST_E_PROVIDER_UNKNOWN) {
        out.state = FileSignature::State::Unsigned;  // (a build without a certificate: allowed for development, reported to the caller)
    } else {
        out.state = FileSignature::State::Invalid;
        char code[16] = {};
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(status));
        out.error = std::string("the signature does not verify (") + code + ")";
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return out;
}

namespace {

// A new, empty file only this process knows the name of (CREATE_NEW), so a file planted at a predictable path is never the one that gets run.
bool createNewFile(const fs::path& path) {
    // (the downloader removes this placeholder and moves the finished file into its place)
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

}  // namespace

Result<bool> installWebView2Runtime(HttpClient& http) {
    using R = Result<bool>;
    if (webView2RuntimeInstalled()) return R::success(true);  // (the caller asks too: this is cheap and makes the function safe on its own)
    std::error_code ec;
    const fs::path tempDir = fs::temp_directory_path(ec);
    if (ec) return R::fail("disk", "There is no temporary folder for the WebView2 installer.");
    // A name of its own for every download, taken before anything is written to it.
    fs::path installer;
    for (int attempt = 0; attempt < 8 && installer.empty(); ++attempt) {
        const fs::path candidate = tempDir / fs::path(widen("falcon-webview2-" + randomHex(8) + ".exe"));
        if (createNewFile(candidate)) installer = candidate;
    }
    if (installer.empty()) return R::fail("disk", "Could not create a temporary file for the WebView2 installer.");
    // Microsoft's own bootstrapper (about 1.7 MB): it downloads and installs the current runtime.
    const HttpResponse r = http.download("https://go.microsoft.com/fwlink/p/?LinkId=2124703", installer, nullptr, nullptr);
    if (!r.ok()) {
        fs::remove(installer, ec);
        return R::fail("network", "Could not download the Microsoft Edge WebView2 installer: " + (r.error.empty() ? "status " + std::to_string(r.status) : r.error));
    }
    std::error_code sizeErr;
    const auto size = fs::file_size(installer, sizeErr);
    logLine("web view installer: " + std::to_string(sizeErr ? 0 : size) + " bytes, sha256 " + sha256FileHex(installer));
    // Nothing downloaded over the network is started without checking who signed it: this one must be Microsoft's.
    const FileSignature signature = verifyFileSignature(installer, "Microsoft");
    if (signature.state == FileSignature::State::Invalid) {
        fs::remove(installer, ec);
        return R::fail("untrusted", "The WebView2 installer that was downloaded is not properly signed by Microsoft (" + signature.error + "). Nothing was run.");
    }
    if (signature.state == FileSignature::State::Unsigned) {
        fs::remove(installer, ec);
        return R::fail("untrusted", "The WebView2 installer that was downloaded has no signature. Nothing was run.");
    }
    logLine("web view installer signed by " + signature.publisher);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring command = L"\"" + installer.wstring() + L"\" /silent /install";
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        fs::remove(installer, ec);
        return R::fail("install", "Could not start the WebView2 installer.");
    }
    const DWORD waited = WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    fs::remove(installer, ec);
    if (waited != WAIT_OBJECT_0) return R::fail("install", "The WebView2 installer did not finish in five minutes.");
    if (!webView2RuntimeInstalled()) return R::fail("install", "The WebView2 runtime did not install (exit code " + std::to_string(code) + ").");
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

// A Linux program has no Authenticode signature to check: the report says Unsigned and the callers treat that as "nothing to verify here".
FileSignature verifyFileSignature(const fs::path&, std::string_view) { return FileSignature{FileSignature::State::Unsigned, {}, {}}; }

// The web view is a library the program is linked against: if it were missing the program would not have started at all.
bool webView2RuntimeInstalled() { return true; }

Result<bool> installWebView2Runtime(HttpClient&) { return Result<bool>::fail("unsupported", "There is nothing to install here: the web view is WebKitGTK, a package of the system."); }

#endif

}  // namespace launcher
