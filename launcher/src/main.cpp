// Falcon Launcher: sign in with a Microsoft account, check that the account owns Minecraft, install the game's files from Mojang, start
// the (bundled) Falcon client. The window is a web view showing a React page; everything it can ask for is in app.hpp.
//
// Options (all for development):  --mock [state]         pretend flows with no network or disk; with a state the UI is frozen in it
//                                  --screenshot <file.png> [--wait <ms>]   draw the UI off-screen, save it, write <file>.txt with the timings, exit
//                                  --selftest-signin      open the Microsoft sign-in window invisibly, check that it hands back the redirect and that it can be cancelled (no network), exit code 0 when it does
//                                  --devtools             F12 and the page's context menu
#include "app.hpp"
#include "http.hpp"
#include "setup.hpp"
#include "util.hpp"
#include "webview_host.hpp"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <memory>

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
    std::string page;  // --page settings|logs: open the page there (for screenshots)
};

Args parseArgs() {
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
        } else if (a == L"--selftest-signin") {
            args.selfTestSignIn = true;
        } else if (a == L"--devtools") {
            args.devtools = true;
        }
    }
    if (argv != nullptr) LocalFree(argv);
    return args;
}

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

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    const Args args = parseArgs();
    const bool testRun = args.mock || args.selfTestSignIn || !args.screenshot.empty();
    logLine(std::string("Falcon Launcher ") + FALCON_LAUNCHER_VERSION + " starting");

    // One launcher at a time: a second start just brings the first to the front.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\FalconLauncherSingleInstance");
    if (!testRun && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"FalconLauncherWindow", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    std::unique_ptr<HttpClient> http = makeWinHttpClient(std::string("FalconLauncher/") + FALCON_LAUNCHER_VERSION);

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
    WebHost host(instance, hostOptions, callbacks, embeddedResource(L"LAUNCHER_UI"), embeddedResource(L"WEBVIEW2_LOADER"));
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
                        infoPath += L".txt";
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
    if (single != nullptr) CloseHandle(single);
    logLine("Falcon Launcher closed");
    if (args.selfTestSignIn) code = redirectOk && cancelOk ? 0 : 3;
    return code;
}
