// Falcon Launcher: sign in with a Microsoft account, check that the account owns Minecraft, install the game's files from Mojang, start
// the (bundled) Falcon client. The window is a web view showing a React page; everything it can ask for is in app.hpp.
//
// Options (all for development):  --mock [state]         pretend flows with no network or disk; with a state the UI is frozen in it
//                                  --screenshot <file.png> [--wait <ms>]   draw the UI off-screen, save it, write <file>.txt with the timings, exit
//                                  --selftest-signin      open the Microsoft sign-in window invisibly, check that it hands back the redirect and that it can be cancelled (no network), exit code 0 when it does
//                                  --selftest-tables <dir>   download Falcon's data tables into <dir>/26.3 like the installation does (real network), check them, write <dir>/selftest-tables.txt, exit code 0 when they are good
//                                  --selftest-install <dir>  download the game's files and Falcon's data tables into <dir>/26.3 like the installation does (real network, a few hundred MB), write <dir>/selftest-install.txt, exit code 0 when it worked
//                                  --selftest-mods <dir>     search Modrinth for mods of 26.3, fetch their icons and install the first one that has a .jar into <dir>/mods (real network), write <dir>/selftest-mods.txt, exit code 0 when it worked
//                                  --devtools             F12 and the page's context menu
//                                  --verify-signature <file>  make the check the launch path makes of the client (verifyFileSignature, with the publisher this launcher was built naming): the answer goes to the log, exit 0 signed and accepted, 2 unsigned, 1 refused
//
// One program for Windows (WebView2) and Linux (WebKitGTK): the few places where they differ are marked.
#include "app.hpp"
#include "http.hpp"
#include "mods.hpp"
#include "setup.hpp"
#include "util.hpp"

#ifdef _WIN32
#include "webview_host.hpp"

#include <windows.h>
#include <shellapi.h>
#else
#include "webview_host_gtk.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <fstream>
#include <memory>

// (the publisher a launcher may require of its client: app.cpp reads it the same way, and an empty one accepts any valid signature)
#ifndef FALCON_LAUNCHER_PUBLISHER
#define FALCON_LAUNCHER_PUBLISHER ""
#endif

namespace {

using namespace launcher;
namespace fs = std::filesystem;

struct Args {
    bool mock = false;
    std::string mockState;
    std::wstring screenshot;
    int waitMs = 1500;
    bool devtools = false;
    bool selfTestSignIn = false;
    std::wstring verifySignature;  // --verify-signature <file>
    std::wstring selfTestTables;   // --selftest-tables <dir>
    std::wstring selfTestInstall;  // --selftest-install <dir>
    std::wstring selfTestMods;     // --selftest-mods <dir>
    std::string page;  // --page settings|logs: open the page there (for screenshots)
};

#ifdef _WIN32
Args parseArgs(int, char**) {
    Args args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv != nullptr && i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--mock") {
            args.mock = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') args.mockState = narrow(argv[++i]);
        } else if (a == L"--screenshot" && i + 1 < argc) {
            args.screenshot = argv[++i];
        } else if (a == L"--wait" && i + 1 < argc) {
            args.waitMs = _wtoi(argv[++i]);
        } else if (a == L"--page" && i + 1 < argc) {
            args.page = narrow(argv[++i]);
        } else if (a == L"--selftest-tables" && i + 1 < argc) {
            args.selfTestTables = argv[++i];
        } else if (a == L"--selftest-install" && i + 1 < argc) {
            args.selfTestInstall = argv[++i];
        } else if (a == L"--selftest-mods" && i + 1 < argc) {
            args.selfTestMods = argv[++i];
        } else if (a == L"--selftest-signin") {
            args.selfTestSignIn = true;
        } else if (a == L"--verify-signature" && i + 1 < argc) {
            args.verifySignature = argv[++i];
        } else if (a == L"--devtools") {
            args.devtools = true;
        }
    }
    if (argv != nullptr) LocalFree(argv);
    return args;
}
#else
Args parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mock") {
            args.mock = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') args.mockState = argv[++i];
        } else if (a == "--screenshot" && i + 1 < argc) {
            args.screenshot = widen(argv[++i]);
        } else if (a == "--wait" && i + 1 < argc) {
            args.waitMs = std::atoi(argv[++i]);
        } else if (a == "--page" && i + 1 < argc) {
            args.page = argv[++i];
        } else if (a == "--selftest-tables" && i + 1 < argc) {
            args.selfTestTables = widen(argv[++i]);
        } else if (a == "--selftest-install" && i + 1 < argc) {
            args.selfTestInstall = widen(argv[++i]);
        } else if (a == "--selftest-mods" && i + 1 < argc) {
            args.selfTestMods = widen(argv[++i]);
        } else if (a == "--selftest-signin") {
            args.selfTestSignIn = true;
        } else if (a == "--verify-signature" && i + 1 < argc) {
            args.verifySignature = widen(argv[++i]);
        } else if (a == "--devtools") {
            args.devtools = true;
        }
    }
    return args;
}
#endif

// The Microsoft application id: the environment, then launcher.json ("msClientId"), then build-time override,
// and defaults to the official Minecraft public client id (00000000402b5328) for zero-config sign-in.
std::string clientIdFromSettings() {
    if (const char* env = std::getenv("FALCON_MS_CLIENT_ID"); env != nullptr && *env != '\0') return env;
    std::string text;
    if (readFile(dataRoot() / "launcher.json", text)) {
        const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("msClientId") && j["msClientId"].is_string() && !j["msClientId"].get<std::string>().empty()) return j["msClientId"].get<std::string>();
    }
    if (std::string(FALCON_LAUNCHER_MS_CLIENT_ID).size() > 0) return FALCON_LAUNCHER_MS_CLIENT_ID;
    return kDefaultMinecraftClientId;
}

// --selftest-tables: the installation's download of the data tables, with the real network and the real address and checksum this launcher carries, into a folder of its own.
int selfTestTables(const std::wstring& dir) {
    const std::unique_ptr<HttpClient> http = makeSystemHttpClient(std::string("FalconLauncher/") + FALCON_LAUNCHER_VERSION);
    GameFiles files(*http, std::filesystem::path(dir));
    const std::atomic<bool> cancel{false};
    std::string said;
    const auto result = files.installTables("26.3", [&](const InstallProgress& p) { said = p.label; }, cancel);
    bool good = result.ok();  // (the tables only: the game's own data/ folder is not part of this check)
    for (const std::string& name : GameFiles::dataTableFiles()) {
        std::error_code ec;
        good = good && std::filesystem::is_regular_file(std::filesystem::path(dir) / "26.3" / std::filesystem::path(widen(name)), ec);
    }
    std::string text = good ? std::string("ok: ") + said : std::string("failed: ") + (result.ok() ? std::string("the tables are not complete") : result.error.message);
    writeFileAtomic(std::filesystem::path(dir) / "selftest-tables.txt", text);
    return good ? 0 : 1;
}

// --selftest-mods: the Mods tab's whole way, with the real network: a search for mods of 26.3 (the most downloaded first), their icons, and the install of the first one that has a .jar.
int selfTestMods(const std::wstring& dir) {
    const std::unique_ptr<HttpClient> http = makeSystemHttpClient(std::string("FalconLauncher/") + FALCON_LAUNCHER_VERSION);
    Modrinth modrinth(*http);
    const std::filesystem::path out = std::filesystem::path(dir);
    const auto finish = [&](bool good, const std::string& text) {
        writeFileAtomic(out / "selftest-mods.txt", (good ? "ok: " : "failed: ") + text);
        return good ? 0 : 1;
    };
    ModQuery query;
    query.gameVersion = "26.3";
    const auto page = modrinth.search(query);
    if (!page || page->hits.empty()) return finish(false, page ? "the search found nothing for 26.3" : "the search: " + page.error.message);
    std::vector<std::string> urls;
    for (const ModHit& hit : page->hits) urls.push_back(hit.iconUrl);
    int pictures = 0;
    for (const std::string& icon : modrinth.icons(urls)) pictures += icon.empty() ? 0 : 1;
    const std::atomic<bool> cancel{false};
    std::string last, refused;
    int blocked = 0;
    // The optimization mods of the page (Sodium and the like) are marked in the search, and the check that asks Modrinth about the project refuses the first of them too.
    for (const ModHit& hit : page->hits) {
        if (!hit.blocked) continue;
        ++blocked;
        if (!refused.empty()) continue;
        const auto verdict = modrinth.vetProject(hit.projectId);
        if (!verdict) return finish(false, hit.title + ": the check: " + verdict.error.message);
        if (!verdict->blocked) return finish(false, hit.title + " is marked as an optimization mod in the search but the check let it through");
        refused = hit.title;
    }
    for (const ModHit& hit : page->hits) {
        if (hit.blocked) continue;
        const auto project = modrinth.vetProject(hit.projectId);
        if (!project || project->blocked) {
            last = hit.title + ": " + (project ? project->reason : project.error.message);
            continue;
        }
        const auto file = modrinth.chooseFile(hit.projectId, "26.3", "");
        if (!file) {
            last = hit.title + ": " + file.error.message;
            continue;
        }
        const auto needs = modrinth.vetDependencies(*file, hit.title);
        if (!needs || needs->blocked) {
            last = hit.title + ": " + (needs ? needs->reason : needs.error.message);
            continue;
        }
        const auto done = modrinth.install(*file, hit.title, out / "mods", cancel);
        if (!done) return finish(false, hit.title + ": " + done.error.message);
        return finish(true, std::to_string(page->hits.size()) + " mods of " + std::to_string(page->total) + ", " + std::to_string(pictures) + " pictures, " + std::to_string(blocked) + " optimization mods marked" +
                                (refused.empty() ? std::string() : " (" + refused + " refused by the check)") + ", installed " + hit.title + " " + done->versionNumber + " as " + done->file);
    }
    return finish(false, "no mod of the first page had a .jar for 26.3 (" + last + ")");
}

// --selftest-install: everything Play downloads before it starts the game (the client jar's assets and data, the sounds and languages, the data tables), into a folder of its own.
// What the Linux build's tests run on: the game's own data is where the checks of the world generator get theirs.
int selfTestInstall(const std::wstring& dir) {
    const std::unique_ptr<HttpClient> http = makeSystemHttpClient(std::string("FalconLauncher/") + FALCON_LAUNCHER_VERSION);
    GameFiles files(*http, std::filesystem::path(dir));
    const std::atomic<bool> cancel{false};
    std::string said;
    int lastPercent = -1;
    const auto result = files.install("26.3", [&](const InstallProgress& p) {
        said = p.label;
        if (p.total > 0) {
            const int percent = static_cast<int>(p.done * 100 / p.total);
            if (percent / 10 != lastPercent / 10) logLine("install: " + p.label + " " + std::to_string(percent) + "%");
            lastPercent = percent;
        }
    }, cancel);
    const bool good = result.ok() && files.isInstalled("26.3");
    const std::string text = good ? std::string("ok: ") + said : std::string("failed: ") + (result.ok() ? std::string("the files are not complete") : result.error.message);
    writeFileAtomic(std::filesystem::path(dir) / "selftest-install.txt", text);
    return good ? 0 : 1;
}

// --verify-signature: the check the launch path makes of the client, once, without a window and without starting anything. The answer goes to the
// launcher's log (it may be a read-only file that is being asked about) and to the exit code: 0 signed and accepted, 2 unsigned, 1 refused.
int verifySignature(const std::wstring& file) {
    const fs::path path(file);
    const FileSignature signature = verifyFileSignature(path, FALCON_LAUNCHER_PUBLISHER);
    if (signature.state == FileSignature::State::Signed) {
        logLine("signature of " + path.string() + ": signed by " + signature.publisher);
        return 0;
    }
    if (signature.state == FileSignature::State::Unsigned) {
        logLine("signature of " + path.string() + ": unsigned (accepted only while this launcher expects no publisher)");
        return 2;
    }
    logLine("signature of " + path.string() + ": refused, " + signature.error);
    return 1;
}

#ifdef _WIN32
using Instance = HINSTANCE;
#else
using Instance = void*;
#endif

int launcherMain(Instance instance, const Args& args) {
    [[maybe_unused]] const Instance unusedOnLinux = instance;
    const bool testRun = args.mock || args.selfTestSignIn || !args.screenshot.empty();
    logLine(std::string("Falcon Launcher ") + FALCON_LAUNCHER_VERSION + " starting");
    if (!args.verifySignature.empty()) return verifySignature(args.verifySignature);
    if (!args.selfTestTables.empty()) return selfTestTables(args.selfTestTables);
    if (!args.selfTestInstall.empty()) return selfTestInstall(args.selfTestInstall);
    if (!args.selfTestMods.empty()) return selfTestMods(args.selfTestMods);

    // One launcher at a time: a second start just brings the first to the front (Windows) or leaves it be (Linux, where a program cannot raise another's window).
#ifdef _WIN32
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\FalconLauncherSingleInstance");
    if (!testRun && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"FalconLauncherWindow", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
#else
    int single = -1;
    if (!testRun) {
        std::error_code ec;
        fs::create_directories(dataRoot(), ec);
        single = ::open((dataRoot() / "launcher.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);  // (the lock goes with the process, however it ends)
        if (single >= 0 && ::flock(single, LOCK_EX | LOCK_NB) != 0) {
            logLine("another launcher is already running");
            return 0;
        }
    }
#endif

    std::unique_ptr<HttpClient> http = makeSystemHttpClient(std::string("FalconLauncher/") + FALCON_LAUNCHER_VERSION);

#ifdef _WIN32
    // The web view needs the WebView2 runtime. Nearly every PC has it; if this one does not, the player is asked and it is installed first.
    if (!webView2RuntimeInstalled()) {
        const int answer = MessageBoxW(nullptr,
                                       L"Falcon Launcher shows its window with the Microsoft Edge WebView2 runtime, which this PC does not have yet.\n\n"
                                       L"Install it now? (A small download from Microsoft, nothing else is installed.)",
                                       L"Falcon Launcher", MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) return 1;
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        const auto installed = installWebView2Runtime(*http);
        if (!installed) {
            MessageBoxW(nullptr, widen(installed.error.message).c_str(), L"Falcon Launcher", MB_ICONERROR);
            return 1;
        }
    }
#endif

    AppOptions options;
    options.clientId = clientIdFromSettings();
    options.mock = args.mock || args.selfTestSignIn;
    options.mockState = args.selfTestSignIn ? std::string("signin") : args.mockState;  // (the self-test needs no account and no setup)

    WebHost* hostPointer = nullptr;
    App* appPointer = nullptr;
    AppHooks hooks;
    hooks.emit = [&hostPointer](const std::string& json) {
        if (hostPointer != nullptr) hostPointer->post(json);
    };
    hooks.gameRunning = [&hostPointer](bool running) {
        if (hostPointer != nullptr) hostPointer->setGameRunning(running);
    };
    hooks.quit = [&hostPointer] {
        if (hostPointer != nullptr) hostPointer->close();
    };
    hooks.minimize = [&hostPointer] {
        if (hostPointer != nullptr) hostPointer->minimize();
    };
    hooks.startDrag = [&hostPointer] {
        if (hostPointer != nullptr) hostPointer->startDrag();
    };
    hooks.beginSignIn = [&hostPointer](const std::string& url, const std::string& redirectPrefix, std::function<void(const std::string&, const std::string&)> done) {
        if (hostPointer != nullptr) hostPointer->beginSignIn(url, redirectPrefix, std::move(done));
        else done("", "there is no window");
    };
    hooks.cancelSignIn = [&hostPointer] {
        if (hostPointer != nullptr) hostPointer->cancelSignIn();
    };
    App app(*http, options, hooks, embeddedResource(L"FALCON_CLIENT"));
    appPointer = &app;

    WebHostOptions hostOptions;
    hostOptions.devtools = args.devtools;
    hostOptions.startPage = widen(args.page);
    hostOptions.offscreen = !args.screenshot.empty() || args.selfTestSignIn;
    WebHost::Callbacks callbacks;
    callbacks.onMessage = [&appPointer](const std::string& message) { return appPointer != nullptr ? appPointer->handleMessage(message) : std::string(); };
#ifdef _WIN32
    WebHost host(instance, hostOptions, callbacks, embeddedResource(L"LAUNCHER_UI"), embeddedResource(L"WEBVIEW2_LOADER"));
#else
    WebHost host(hostOptions, callbacks, embeddedResource(L"LAUNCHER_UI"));
#endif
    hostPointer = &host;

    if (!host.create()) {
        logLine("the window could not be created");
        return 1;
    }
    logLine("window shown after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");

    if (!args.screenshot.empty()) {
        // For checking the UI without anybody looking: wait for the page and for the state to settle, save a picture, leave.
        const std::filesystem::path file = args.screenshot;
        const int wait = args.waitMs;
        struct Poll {
            WebHost* host;
            std::filesystem::path file;
            int wait;
            int waitedForPage = 0;
            void operator()() {
                if (!host->pageLoaded() && waitedForPage < 30000) {
                    waitedForPage += 100;
                    host->after(100, *this);
                    return;
                }
                host->after(static_cast<unsigned>(wait), [h = host, f = file] {
                    h->capture(f, [h, f](bool ok) {
                        fs::path infoPath = f;
                        infoPath += ".txt";
                        std::ofstream info(infoPath);
                        info << "{\"captured\": " << (ok ? "true" : "false") << ", \"startedMs\": " << static_cast<int>(millisSinceStart()) << ", \"browserPid\": " << h->browserProcessId() << "}\n";
                        h->close();
                    });
                });
            }
        };
        host.after(100, Poll{&host, file, wait});
    }

    // --selftest-signin: the sign-in window, invisibly. Its start address is already the redirect, which the window must catch before any
    // network is used, close itself and hand back; then a second one is opened and cancelled from outside.
    bool redirectOk = false, cancelOk = false;
    if (args.selfTestSignIn) {
        struct Wait {
            WebHost* host;
            bool* redirectOk;
            bool* cancelOk;
            int waited = 0;
            void operator()() {
                if (!host->pageLoaded() && waited < 20000) {
                    waited += 100;
                    host->after(100, *this);
                    return;
                }
                const std::string redirect = kRedirectUri;
                const std::string expected = redirect + "?code=selftest&state=s1";
                WebHost* h = host;
                bool* redirectResult = redirectOk;
                bool* cancelResult = cancelOk;
                h->beginSignIn(expected, redirect, [h, redirect, expected, redirectResult, cancelResult](const std::string& url, const std::string& error) {
                    *redirectResult = url == expected && error.empty();
                    logLine(std::string("sign-in self-test, redirect: ") + (*redirectResult ? "ok" : "FAILED (" + url + " / " + error + ")"));
                    h->beginSignIn("about:blank", redirect, [h, cancelResult](const std::string& url2, const std::string& error2) {
                        *cancelResult = url2.empty() && error2 == "cancelled";
                        logLine(std::string("sign-in self-test, cancel: ") + (*cancelResult ? "ok" : "FAILED (" + url2 + " / " + error2 + ")"));
                        h->close();
                    });
                    h->after(700, [h] { h->cancelSignIn(); });
                });
            }
        };
        host.after(100, Wait{&host, &redirectOk, &cancelOk});
        host.after(30000, [&host] {
            logLine("sign-in self-test: timed out");
            host.close();
        });
    }

    int code = host.run();
    hostPointer = nullptr;
    app.shutdown();
    appPointer = nullptr;
#ifdef _WIN32
    if (single != nullptr) CloseHandle(single);
#else
    if (single >= 0) ::close(single);
#endif
    logLine("Falcon Launcher closed");
    if (args.selfTestSignIn) code = redirectOk && cancelOk ? 0 : 3;
    return code;
}

}  // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) { return launcherMain(instance, parseArgs(0, nullptr)); }
#else
int main(int argc, char** argv) { return launcherMain(nullptr, parseArgs(argc, argv)); }
#endif
