#include "app.hpp"

#include "falcon/mods/mod_info.hpp"
#include "avatar.hpp"
#include "secure_store.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

extern char** environ;
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>

#ifndef FALCON_LAUNCHER_VERSION
#define FALCON_LAUNCHER_VERSION "0.0.0"
#endif
// The publisher named when the launcher is built (-DFALCON_LAUNCHER_PUBLISHER=...): the client's signature must then be from it. Empty (the default,
// and every build made before a certificate exists) means any valid signature is accepted, and an unsigned client is reported and allowed.
#ifndef FALCON_LAUNCHER_PUBLISHER
#define FALCON_LAUNCHER_PUBLISHER ""
#endif

namespace launcher {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kSetupVersion = 1;  // raise it when setup does something new: it then runs again once


json errorJson(const Error& e) { return json{{"code", e.code}, {"message", e.message}}; }

json stepJson(const char* id, const char* label) { return json{{"id", id}, {"label", label}, {"status", "pending"}, {"detail", ""}}; }

// How long a full check of the account is good for (the token refreshed, and the entitlements and the profile read from Microsoft): a start inside that window goes straight
// on. The check used to be made on every start, which is what made starting the game slow; the client is still given a fresh token, which it needs for its own check.
constexpr std::int64_t kAccountCheckSeconds = 24 * 60 * 60;

std::int64_t nowSeconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// The account the last full check found, while that check is still fresh: what the home screen shows and what a launch goes with.
bool freshAccount(const json& config, json& account) {
    if (!config.contains("accountCheck") || !config["accountCheck"].is_object()) return false;
    const json& cached = config["accountCheck"];
    if (!cached.value("owns", false) || !cached.value("hasProfile", false)) return false;
    if (nowSeconds() - cached.value("at", static_cast<std::int64_t>(0)) > kAccountCheckSeconds) return false;
    account = json{{"name", cached.value("name", std::string())}, {"uuid", cached.value("uuid", std::string())}, {"owns", true}, {"hasProfile", true},
                   {"avatar", cached.value("avatar", std::string())}};
    return !account["name"].get<std::string>().empty();
}

json accountJson(const MinecraftAccount& a, const std::string& avatar) {
    return json{{"name", a.name}, {"uuid", a.uuid}, {"owns", a.ownsGame}, {"hasProfile", a.hasProfile}, {"avatar", avatar}};
}

// The demo mode's pretend player: a small blocky face, so the UI can be worked on and photographed without an account.
std::string mockAvatar() {
    static const std::string cached = [] {
        const unsigned char palette[][4] = {{70, 43, 26, 255}, {96, 60, 35, 255}, {198, 134, 98, 255}, {176, 114, 82, 255},
                                            {250, 250, 250, 255}, {64, 60, 150, 255}, {120, 70, 60, 255}, {94, 58, 40, 255}};
        enum { H, h, S, s, W, B, M, D };
        const int face[8][8] = {{H, h, H, H, H, H, h, H}, {H, H, H, h, H, H, H, H}, {H, S, S, S, S, S, S, H}, {S, S, S, S, S, S, S, S},
                                {S, W, B, S, S, B, W, S}, {S, S, S, s, s, S, S, S}, {S, D, M, M, M, M, D, S}, {S, D, D, D, D, D, D, S}};
        unsigned char rgba[8 * 8 * 4];
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) std::copy(palette[face[y][x]], palette[face[y][x]] + 4, rgba + (y * 8 + x) * 4);
        }
        return avatarFromFace(rgba);
    }();
    return cached;
}

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

std::string stringParam(const json& params, const char* key) {
    if (params.is_object() && params.contains(key) && params[key].is_string()) return params[key].get<std::string>();
    return {};
}

#ifdef _WIN32
// "a" "b c" -> "a" "b c" quoted for CreateProcess.
std::wstring quoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    for (const wchar_t c : arg) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    return out + L"\"";
}

constexpr const char* kPlatform = "windows";
constexpr const char* kRuntimeStep = "Checking the web runtime";
constexpr const char* kShortcutStep = "Adding a Start menu shortcut";
constexpr const char* kRuntimeDone = "Microsoft Edge WebView2 is installed";
#else
constexpr const char* kPlatform = "linux";
constexpr const char* kRuntimeStep = "Checking the web view";
constexpr const char* kShortcutStep = "Adding an application menu entry";
constexpr const char* kRuntimeDone = "WebKitGTK is installed";
#endif

// The client's log for the Logs tab (the game writes it, see docs/LOGS.md in the repository): latest.log while a run is in progress, else the newest log of a run that ended, which
// is named by the time it started (2026-11-17_22-40-57.log; -crash and -unclean when it did not end cleanly). The launcher's own log (launcher.log) and the crash reports are not it.
fs::path clientLogFile() {
    const fs::path dir = dataRoot() / "logs";
    std::error_code ec;
    if (fs::exists(dir / "latest.log", ec)) return dir / "latest.log";
    fs::path best;
    std::string bestName;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = narrow(entry.path().filename().wstring());
        const bool dated = name.size() >= 23 && name.compare(name.size() - 4, 4, ".log") == 0 && name[4] == '-' && name[7] == '-' && name[10] == '_' && name[13] == '-' && name[16] == '-' &&
                           name[0] >= '0' && name[0] <= '9';
        if (dated && name > bestName) {
            bestName = name;
            best = entry.path();
        }
    }
    return best;
}

}  // namespace

App::App(HttpClient& http, AppOptions options, AppHooks hooks, Blob bundledClient)
    : http_(http), options_(std::move(options)), hooks_(std::move(hooks)), client_(bundledClient), auth_(http, options_.clientId), files_(http, dataRoot() / "game", options_.tables), modrinth_(http) {
    const bool mock = options_.mock || !options_.mockState.empty();
    state_ = json{{"phase", "boot"},
                  {"app", {{"version", FALCON_LAUNCHER_VERSION}, {"mock", mock}, {"gameVersion", options_.gameVersion}, {"clientBundled", !client_.empty()}, {"gameDir", narrow(files_.versionDir(options_.gameVersion).wstring())}, {"platform", kPlatform}}},
                  {"setup", {{"steps", json::array()}, {"error", nullptr}}},
                  {"auth", {{"configured", auth_.configured() || mock}, {"status", "signedOut"}, {"message", ""}, {"error", nullptr}}},
                  {"account", nullptr},
                  {"game", {{"status", "unknown"}, {"phase", ""}, {"label", ""}, {"done", 0}, {"total", 0}, {"error", nullptr}, {"version", options_.gameVersion}, {"data", mock || files_.findGameData(options_.gameVersion).has_value()}}},
                  {"settings", {{"keepOpen", true}}},
                  {"mods", {{"dir", narrow(modsDir().wstring())}, {"gameVersion", options_.gameVersion}, {"installed", json::object()}, {"busy", json::object()}, {"blocked", json::array()}}}};
}

App::~App() { shutdown(); }

void App::shutdown() {
    stopping_ = true;
    cancel_ = true;
    modsCancel_ = true;
    {
        std::lock_guard<std::mutex> lock(modsMutex_);
        modsStop_ = true;
    }
    modsWake_.notify_all();
    if (modsThread_.joinable()) modsThread_.join();
    if (job_.joinable()) job_.join();
}

void App::waitIdle() {
    while (busy_.load() || modsPending_.load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (job_.joinable()) job_.join();
}

std::string App::stateJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dump(state_);
}

void App::mutate(const std::function<void(json&)>& change) {
    std::string text;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        change(state_);
        text = dump(state_);
    }
    if (hooks_.emit) hooks_.emit("{\"event\":\"state\",\"state\":" + text + "}");
}

void App::setStep(const std::string& id, const std::string& status, const std::string& detail) {
    mutate([&](json& s) {
        for (json& step : s["setup"]["steps"]) {
            if (step["id"] == id) {
                step["status"] = status;
                step["detail"] = detail;
            }
        }
    });
}

void App::setAuthError(const Error& error) {
    logLine("sign-in problem: " + error.code + ": " + error.message);
    mutate([&](json& s) {
        s["phase"] = "signin";
        s["auth"]["status"] = "signedOut";
        s["auth"]["message"] = "";
        s["auth"]["error"] = errorJson(error);
    });
}

void App::setGameError(const Error& error) {
    logLine("game problem: " + error.code + ": " + error.message);
    const std::string idle = gameIdleStatus();
    mutate([&](json& s) {
        s["game"]["status"] = idle;
        s["game"]["error"] = errorJson(error);
    });
}

void App::setGameProgress(const std::string& status, const InstallProgress& p) {
    mutate([&](json& s) {
        s["game"]["status"] = status;
        s["game"]["phase"] = p.phase;
        s["game"]["label"] = p.label;
        s["game"]["done"] = p.done;
        s["game"]["total"] = p.total;
        s["game"]["error"] = nullptr;
    });
}

std::string App::gameIdleStatus() const { return files_.isInstalled(options_.gameVersion) ? "ready" : "missing"; }

fs::path App::accountFile() const { return dataRoot() / "account.bin"; }

json App::loadConfig() const {
    std::string text;
    if (readFile(dataRoot() / "launcher.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object()) return j;
    }
    return json::object();
}

void App::saveConfig(const json& config) const { writeFileAtomic(dataRoot() / "launcher.json", dump(config)); }

bool App::sleepUnlessCancelled(int milliseconds) {
    for (int waited = 0; waited < milliseconds; waited += 100) {
        if (cancel_.load() || stopping_.load()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(100, milliseconds - waited)));
    }
    return !(cancel_.load() || stopping_.load());
}

bool App::startJob(const char* name, std::function<void()> fn) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    if (job_.joinable()) job_.join();
    cancel_ = false;
    job_ = std::thread([this, name = std::string(name), fn = std::move(fn)] {
        try {
            fn();
        } catch (const std::exception& e) {
            logLine("job '" + name + "' failed: " + e.what());
        }
        busy_ = false;
    });
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Messages from the UI
// ---------------------------------------------------------------------------------------------------------------------------

std::string App::handleMessage(const std::string& message) {
    const json request = json::parse(message, nullptr, false);
    if (request.is_discarded() || !request.is_object()) return dump(json{{"id", nullptr}, {"ok", false}, {"error", {{"code", "bad_message"}, {"message", "not JSON"}}}});
    const json id = request.contains("id") ? request["id"] : json(nullptr);
    const std::string method = request.contains("method") && request["method"].is_string() ? request["method"].get<std::string>() : std::string();
    const json params = request.contains("params") ? request["params"] : json::object();
    const auto ok = [&](json result = json::object()) { return dump(json{{"id", id}, {"ok", true}, {"result", std::move(result)}}); };
    const auto fail = [&](const char* code, const std::string& text) { return dump(json{{"id", id}, {"ok", false}, {"error", {{"code", code}, {"message", text}}}}); };

    if (method == "uiReady") {
        uiReady();
        return ok();
    }
    if (method == "getState") return ok(json::parse(stateJson()));
    if (method == "signIn") {
        if (!startJob("signin", [this] { options_.mock ? mockJob("signIn") : signInJob(); })) return fail("busy", "Something else is in progress.");
        return ok();
    }
    if (method == "cancelSignIn" || method == "cancelLaunch") {
        cancel_ = true;
        return ok();
    }
    if (method == "signOut") {
        cancel_ = true;
        if (hooks_.cancelSignIn) hooks_.cancelSignIn();
        std::error_code ec;
        fs::remove(accountFile(), ec);
        mutate([&](json& s) {
            avatar_.clear();
            s["phase"] = "signin";
            s["account"] = nullptr;
            s["auth"]["status"] = "signedOut";
            s["auth"]["error"] = nullptr;
            s["auth"]["message"] = "";
        });
        return ok();
    }
    if (method == "recheck") {
        if (!startJob("recheck", [this] { options_.mock ? mockJob("signIn") : silentSignInJob(); })) return fail("busy", "Something else is in progress.");
        return ok();
    }
    if (method == "launch") {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!state_["account"].is_object()) return fail("not_signed_in", "Sign in first.");
        }
        if (!startJob("launch", [this] { options_.mock ? mockJob("launch") : launchJob(); })) return fail("busy", "Something else is in progress.");
        return ok();
    }
    if (method == "retrySetup") {
        if (!startJob("setup", [this] { options_.mock ? mockJob("boot") : setupJob(); })) return fail("busy", "Something else is in progress.");
        return ok();
    }
    if (method == "openUrl") {
        const std::string url = stringParam(params, "url");
        if (!isSafeWebUrl(url)) return fail("bad_url", "Only https addresses can be opened.");
        openInBrowser(url);
        return ok();
    }
    if (method == "searchMods") {
        const std::string text = stringParam(params, "query");
        const std::string loader = stringParam(params, "loader");
        const int offset = params.is_object() && params.contains("offset") && params["offset"].is_number_integer() ? std::max(0, params["offset"].get<int>()) : 0;
        const int generation = ++modsSearchGeneration_;
        enqueueMods([this, id, generation, text, loader, offset] { searchModsTask(id, generation, text, loader, offset); });
        return {};  // (the answer comes through hooks_.emit when the network has answered)
    }
    if (method == "installMod") {
        const std::string projectId = stringParam(params, "projectId");
        if (!Modrinth::isProjectId(projectId)) return fail("bad_project", "That is not a Modrinth project.");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_["mods"]["busy"].contains(projectId)) return fail("busy", "That mod is already being installed.");
        }
        const std::string title = stringParam(params, "title").substr(0, 200);
        const std::string loader = stringParam(params, "loader");
        mutate([&](json& s) { s["mods"]["busy"][projectId] = "Looking for the file…"; });
        enqueueMods([this, id, projectId, title, loader] { installModTask(id, projectId, title, loader); });
        return {};
    }
    if (method == "openFolder") {
        const std::string which = stringParam(params, "which");
        fs::path folder = dataRoot();
        if (which == "logs") folder /= "logs";
        else if (which == "mods") folder = modsDir();
        else if (which == "game") folder = files_.versionDir(options_.gameVersion);
        std::error_code ec;
        fs::create_directories(folder, ec);
        openFolder(folder);
        return ok();
    }
    if (method == "setSetting") {
        const std::string key = stringParam(params, "key");
        if (key == "keepOpen" && params.contains("value") && params["value"].is_boolean()) {
            const bool value = params["value"].get<bool>();
            json config = loadConfig();
            config["keepOpen"] = value;
            saveConfig(config);
            mutate([&](json& s) { s["settings"]["keepOpen"] = value; });
            return ok();
        }
        return fail("bad_setting", "Unknown setting.");
    }
    if (method == "quit" || method == "close") {
        if (hooks_.quit) hooks_.quit();
        return ok();
    }
    if (method == "minimize") {
        if (hooks_.minimize) hooks_.minimize();
        return ok();
    }
    if (method == "getLog") {
        // The end of the client's log, for the page's Logs tab: the game's own log (the launcher's is not shown here). That is latest.log while the game runs, and when it does not,
        // the newest log of a run that ended (named by the time it started).
        std::string text;
        if (options_.mock || !options_.mockState.empty()) {
            text =
                "2026-11-14 18:02:19.150 INFO  [client] ---- Falcon 1.3.1 (Minecraft Java 26.3) ----\n"
                "2026-11-14 18:02:19.188 INFO  [gpu] vendor AMD; renderer Radeon RX 580; version 4.5.0 Core Profile Context\n"
                "2026-11-14 18:02:19.241 INFO  [options] render distance 8, simulation 6, LODs on (64 chunks, detail 2)\n"
                "2026-11-14 18:02:21.598 INFO  [world] playing world 'New World' (hills, seed 12345)\n"
                "2026-11-14 18:02:22.912 INFO  [world] spawn area loaded in 1.19 s\n"
                "2026-11-14 18:02:31.377 INFO  [render] 240 fps (4.2 ms); chunks 377, sections 310 of 1967 drawn\n";
        } else {
            const fs::path file = clientLogFile();
            if (file.empty() || !readFile(file, text)) {
                text = "The game has not been run yet: its log shows here, and is kept in the logs folder.";
            } else if (text.size() > 64 * 1024) {
                text.erase(0, text.size() - 64 * 1024);
                const std::size_t lineStart = text.find('\n');
                if (lineStart != std::string::npos) text.erase(0, lineStart + 1);  // (starts on a whole line)
            }
        }
        return ok(json{{"text", text}});
    }
    if (method == "startDrag") {
        if (hooks_.startDrag) hooks_.startDrag();
        return ok();
    }
    return fail("unknown_method", "Unknown method: " + method);
}

void App::uiReady() {
    logLine("the UI is ready after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
    const json installed = loadInstalledMods();
    const json blocked = scanBlockedMods();
    if (!booted_.exchange(true)) {
        mutate([&](json& s) {
            s["settings"]["keepOpen"] = loadConfig().value("keepOpen", true);
            s["mods"]["installed"] = installed;
            s["mods"]["blocked"] = blocked;
        });
        beginBoot();
    } else {
        mutate([&](json& s) {  // (the page was reloaded: it gets the state again)
            s["mods"]["installed"] = installed;
            s["mods"]["blocked"] = blocked;
        });
    }
}

void App::beginBoot() {
    if (!options_.mockState.empty()) {
        applyMockState(options_.mockState);
        return;
    }
    if (options_.mock) {
        startJob("mock", [this] { mockJob("boot"); });
        return;
    }
    const json config = loadConfig();
    const bool setupDone = config.value("setupVersion", 0) >= kSetupVersion && bundledClientInstalled(dataRoot(), client_);
    if (setupDone) {
        json account;
        if (freshAccount(config, account) && unprotectFromFile(accountFile())) {  // (checked in the last day: no request to Microsoft at all before the home screen)
            const std::string idle = gameIdleStatus();
            const bool dataFound = files_.findGameData(options_.gameVersion).has_value();
            logLine("the account was checked less than a day ago: going straight to the home screen");
            avatar_ = account.value("avatar", std::string());
            mutate([&](json& s) {
                s["phase"] = "home";
                s["auth"]["status"] = "signedIn";
                s["auth"]["message"] = "";
                s["auth"]["error"] = nullptr;
                s["account"] = account;
                s["game"]["status"] = idle;
                s["game"]["data"] = dataFound;
                s["game"]["error"] = nullptr;
            });
            return;
        }
        startJob("signin", [this] { silentSignInJob(); });
    } else {
        startJob("setup", [this] { setupJob(); });
    }
}

// ---------------------------------------------------------------------------------------------------------------------------
// Jobs
// ---------------------------------------------------------------------------------------------------------------------------

void App::setupJob() {
    mutate([&](json& s) {
        s["phase"] = "setup";
        s["setup"]["error"] = nullptr;
        s["setup"]["steps"] = json::array({stepJson("runtime", kRuntimeStep), stepJson("folders", "Creating the launcher's folders"),
                                           stepJson("client", "Installing the Falcon client"), stepJson("shortcut", kShortcutStep),
                                           stepJson("finish", "Finishing up")});
    });
    const auto pause = [&] { std::this_thread::sleep_for(std::chrono::milliseconds(140)); };  // (so each step can be read)
    const auto failStep = [&](const char* id, const Error& e) {
        logLine(std::string("setup step '") + id + "' failed: " + e.message);
        setStep(id, "error", e.message);
        mutate([&](json& s) { s["setup"]["error"] = errorJson(e); });
    };

    setStep("runtime", "active");
    pause();
    if (!webView2RuntimeInstalled()) {  // (it normally is: the UI is shown by it. The window installs it before it opens when it is not.)
        setStep("runtime", "error", "The Microsoft Edge WebView2 runtime is missing.");
        mutate([&](json& s) { s["setup"]["error"] = errorJson({"webview", "The Microsoft Edge WebView2 runtime is missing."}); });
        return;
    }
    setStep("runtime", "done", kRuntimeDone);

    setStep("folders", "active");
    pause();
    std::error_code ec;
    for (const char* sub : {"client", "game", "logs", "webview"}) fs::create_directories(dataRoot() / sub, ec);
    if (ec) return failStep("folders", {"disk", "Cannot create " + narrow(dataRoot().wstring()) + ": " + ec.message()});
    setStep("folders", "done", narrow(dataRoot().wstring()));

    setStep("client", "active");
    const auto exe = installBundledClient(dataRoot(), client_);
    if (!exe) return failStep("client", exe.error);
    pause();
    setStep("client", "done");

    setStep("shortcut", "active");
    pause();
    // (A launcher started with its own data folder, as the tests and development do, leaves the Start menu alone.)
    setStep("shortcut", std::getenv("FALCON_LAUNCHER_HOME") == nullptr && createStartMenuShortcut() ? "done" : "skipped");

    setStep("finish", "active");
    json config = loadConfig();
    config["setupVersion"] = kSetupVersion;
    saveConfig(config);
    pause();
    setStep("finish", "done");

    silentSignInJob();  // and straight on to the account
}

void App::silentSignInJob() {
    mutate([&](json& s) {
        s["phase"] = "signin";
        s["auth"]["error"] = nullptr;
    });
    const auto saved = unprotectFromFile(accountFile());
    if (!saved) {
        mutate([&](json& s) {
            s["auth"]["status"] = "signedOut";
            s["auth"]["message"] = "";
        });
        return;
    }
    if (!auth_.configured()) return setAuthError({"not_configured", "This launcher has no Microsoft application id, so it cannot check your account."});
    mutate([&](json& s) {
        s["auth"]["status"] = "working";
        s["auth"]["message"] = "Signing you in…";
    });
    const auto tokens = auth_.refresh(*saved);
    if (tokens) {
        protectToFile(accountFile(), tokens->refreshToken);
        return finishSignIn(*tokens);
    }
    if (tokens.error.code == "invalid_grant") {
        std::error_code ec;
        fs::remove(accountFile(), ec);
        mutate([&](json& s) {
            s["auth"]["status"] = "signedOut";
            s["auth"]["message"] = "Your sign-in has expired. Please sign in again.";
        });
        return;
    }
    setAuthError(tokens.error);
}

void App::signInJob() {
    if (!auth_.configured()) return setAuthError({"not_configured", "This launcher has no Microsoft application id, so it cannot sign anyone in. See launcher/README.md."});
    mutate([&](json& s) {
        s["auth"]["error"] = nullptr;
        s["auth"]["message"] = "";
    });
    // An earlier sign-in that is still good makes the window unnecessary.
    if (const auto saved = unprotectFromFile(accountFile())) {
        mutate([&](json& s) {
            s["auth"]["status"] = "working";
            s["auth"]["message"] = "Signing you in…";
        });
        const auto tokens = auth_.refresh(*saved);
        if (tokens) {
            protectToFile(accountFile(), tokens->refreshToken);
            return finishSignIn(*tokens);
        }
        if (tokens.error.code != "invalid_grant") return setAuthError(tokens.error);
        std::error_code ec;
        fs::remove(accountFile(), ec);
    }
    if (!hooks_.beginSignIn) return setAuthError({"window", "There is no window to sign in with."});

    // Microsoft's own page, in a window of the launcher. It ends at the redirect address with a code, which the window hands back here.
    struct Wait {
        std::mutex mutex;
        std::condition_variable changed;
        bool done = false;
        std::string url, error;
    };
    const auto wait = std::make_shared<Wait>();
    const std::string state = randomHex(16);
    mutate([&](json& s) {
        s["auth"]["status"] = "waiting";
        s["auth"]["message"] = "Finish signing in in the Microsoft window.";
        s["auth"]["message"] = "Finish signing in in the Microsoft window.";
    });
    hooks_.beginSignIn(auth_.authorizeUrl(state), kRedirectUri, [wait](const std::string& url, const std::string& error) {
        std::lock_guard<std::mutex> lock(wait->mutex);
        wait->url = url;
        wait->error = error;
        wait->done = true;
        wait->changed.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(wait->mutex);
        while (!wait->done && !cancel_.load() && !stopping_.load()) wait->changed.wait_for(lock, std::chrono::milliseconds(100));
        if (!wait->done) {  // cancelled in the UI, or the launcher is closing: the window goes away
            lock.unlock();
            if (hooks_.cancelSignIn) hooks_.cancelSignIn();
            return mutate([&](json& s) {
                s["auth"]["status"] = "signedOut";
                s["auth"]["message"] = "";
            });
        }
    }
    if (wait->error == "cancelled") {
        return mutate([&](json& s) {
            s["auth"]["status"] = "signedOut";
            s["auth"]["message"] = "";
        });
    }
    if (!wait->error.empty()) return setAuthError({"window", "The sign-in window could not be shown: " + wait->error});

    const AuthRedirect redirect = AuthService::parseRedirect(wait->url);
    if (!redirect.error.empty()) {
        if (redirect.error == "access_denied") return setAuthError({"declined", "The sign-in was declined."});
        return setAuthError({"service", "Microsoft stopped the sign-in (" + redirect.error + ")" + (redirect.errorDescription.empty() ? "" : ": " + redirect.errorDescription.substr(0, 200))});
    }
    if (redirect.code.empty() || redirect.state != state) return setAuthError({"bad_response", "Microsoft's answer to the sign-in was not understood. Please try again."});

    mutate([&](json& s) {
        s["auth"]["status"] = "working";
        s["auth"]["message"] = "Signing you in…";
    });
    const auto tokens = auth_.exchangeCode(redirect.code);
    if (!tokens) return setAuthError(tokens.error);
    if (!tokens->refreshToken.empty()) protectToFile(accountFile(), tokens->refreshToken);
    else logLine("Microsoft sent no refresh token: the player will have to sign in again next time");
    finishSignIn(*tokens);
}

std::string App::fetchAvatar(const std::string& skinUrl) {
    if (!isSkinUrl(skinUrl)) return {};
    std::string url = skinUrl;
    if (url.rfind("http://", 0) == 0) url.insert(4, "s");  // (the profile lists the address with http; the server speaks https)
    const HttpResponse r = http_.get(url, {{"Accept", "image/png"}});
    if (!r.ok() || r.body.empty() || r.body.size() > 1024 * 1024) return {};
    return avatarFromSkin(r.body);
}

void App::finishSignIn(const MicrosoftTokens& tokens) {
    mutate([&](json& s) {
        s["auth"]["status"] = "working";
        s["auth"]["message"] = "Checking your Minecraft account…";
    });
    const auto account = auth_.minecraftAccount(tokens.accessToken);
    if (!account) return setAuthError(account.error);
    logLine(std::string("signed in; owns Java Edition: ") + (account->ownsGame ? "yes" : "no") + "; profile: " + (account->hasProfile ? "yes" : "no"));
    const std::string avatar = account->hasProfile ? fetchAvatar(account->skinUrl) : std::string();
    {   // what was just checked is good for kAccountCheckSeconds: the next starts skip the entitlements and the profile
        json config = loadConfig();
        config["accountCheck"] = json{{"at", nowSeconds()}, {"name", account->name}, {"uuid", account->uuid}, {"avatar", avatar}};
        saveConfig(config);
    }
    const bool dataFound = files_.findGameData(options_.gameVersion).has_value();
    const std::string idle = gameIdleStatus();
    mutate([&](json& s) {
        avatar_ = avatar;
        s["phase"] = "home";
        s["auth"]["status"] = "signedIn";
        s["auth"]["message"] = "";
        s["auth"]["error"] = nullptr;
        s["account"] = accountJson(*account, avatar_);
        s["game"]["status"] = idle;
        s["game"]["data"] = dataFound;
        s["game"]["error"] = nullptr;
    });
}

void App::launchJob() {
    // 1. the bundled client is in place
    setGameProgress("working", {"client", "Preparing the Falcon client", 0, 0});
    const auto exe = installBundledClient(dataRoot(), client_);
    if (!exe) return setGameError(exe.error);

    // 2. the account is checked again: the game is only started for an account that owns it
    setGameProgress("working", {"account", "Checking your account", 0, 0});
    const auto saved = unprotectFromFile(accountFile());
    if (!saved) {
        mutate([&](json& s) {
            s["phase"] = "signin";
            s["account"] = nullptr;
            s["auth"]["status"] = "signedOut";
        });
        return;
    }
    const auto tokens = auth_.refresh(*saved);
    if (!tokens) {
        if (tokens.error.code == "invalid_grant") {
            std::error_code ec;
            fs::remove(accountFile(), ec);
            mutate([&](json& s) {
                s["phase"] = "signin";
                s["account"] = nullptr;
                s["auth"]["status"] = "signedOut";
                s["auth"]["message"] = "Your sign-in has expired. Please sign in again.";
            });
            return;
        }
        return setGameError(tokens.error);
    }
    protectToFile(accountFile(), tokens->refreshToken);
    // The account was checked in full less than kAccountCheckSeconds ago: the token is refreshed above (the client needs one), but the entitlements and the profile come from
    // that check instead of being asked for again on every start.
    MinecraftAccount account;
    json cached;
    if (freshAccount(loadConfig(), cached)) {
        account.name = cached.value("name", std::string());
        account.uuid = cached.value("uuid", std::string());
        account.ownsGame = true;
        account.hasProfile = true;
    } else {
        const auto checked = auth_.minecraftAccount(tokens->accessToken);
        if (!checked) return setGameError(checked.error);
        account = *checked;
        json config = loadConfig();
        config["accountCheck"] = json{{"at", nowSeconds()}, {"name", account.name}, {"uuid", account.uuid}, {"avatar", avatar_}};
        saveConfig(config);
    }
    mutate([&](json& s) { s["account"] = accountJson(account, avatar_); });
    if (!account.ownsGame || !account.hasProfile) {
        mutate([&](json& s) { s["game"]["status"] = gameIdleStatus(); });
        return;
    }

    // 3. the game's files, from Mojang
    if (!files_.isInstalled(options_.gameVersion)) {
        const auto installed = files_.install(options_.gameVersion, [&](const InstallProgress& p) { setGameProgress("working", p); }, cancel_);
        if (!installed) {
            if (installed.error.code == "cancelled") {
                mutate([&](json& s) {
                    s["game"]["status"] = "missing";
                    s["game"]["error"] = nullptr;
                });
                return;
            }
            return setGameError(installed.error);
        }
    }
    if (cancel_.load()) return mutate([&](json& s) { s["game"]["status"] = gameIdleStatus(); });

    // 4. start it
    setGameProgress("working", {"start", "Starting Falcon", 0, 0});
    // (Falcon's data tables were downloaded into the game folder with the game's files, which is where the client looks.)
    const std::optional<fs::path> exportedData = files_.findGameData(options_.gameVersion);
#ifdef _WIN32
    std::wstring command = quoteArg(exe->wstring()) + L" --assets " + quoteArg(files_.assetsDir(options_.gameVersion).wstring()) + L" --username " + quoteArg(widen(account.name)) + L" --uuid " +
                           quoteArg(widen(account.uuid));
    if (exportedData && *exportedData != files_.versionDir(options_.gameVersion)) command += L" --data " + quoteArg(exportedData->wstring());
    command += L" --size 1280x720";  // (an ordinary window with its system title bar, of a good size)
    logLine("starting the client; game data: " + (exportedData ? narrow(exportedData->wstring()) : std::string("none found")));
    mutate([&](json& s) { s["game"]["data"] = exportedData.has_value(); });
    // The client is checked once more immediately before it runs: a valid Authenticode signature, and when the launcher was built naming a publisher
    // (FALCON_LAUNCHER_PUBLISHER) the signer must be that publisher. A development build has no certificate; that is reported and allowed. A signature
    // that is present but does not verify, or one from somebody else, stops the launch: the file on disk is then not the client this launcher carries.
    const FileSignature clientSignature = verifyFileSignature(*exe, FALCON_LAUNCHER_PUBLISHER);
    if (clientSignature.state == FileSignature::State::Invalid) {
        return setGameError({"untrusted", "The Falcon client could not be verified (" + clientSignature.error + "). Nothing was started."});
    }
    logLine(clientSignature.state == FileSignature::State::Signed ? "client signed by " + clientSignature.publisher : std::string("client is unsigned (a development build)"));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCommand = command;
    // The client asks Mojang itself whether this account owns the game, with the account's Minecraft access token: it gets the token in its environment, for this
    // one start only (the variable is gone from the launcher again at once).
    SetEnvironmentVariableW(L"FALCON_MC_TOKEN", widen(tokens->accessToken).c_str());
    const BOOL started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, exe->parent_path().c_str(), &si, &pi);
    const DWORD startError = GetLastError();
    SetEnvironmentVariableW(L"FALCON_MC_TOKEN", nullptr);
    if (!started) {
        return setGameError({"spawn", "The game could not be started (error " + std::to_string(startError) + ")."});
    }
    CloseHandle(pi.hThread);
    logLine("started the client, process " + std::to_string(pi.dwProcessId));
    mutate([&](json& s) {
        s["game"]["status"] = "running";
        s["game"]["label"] = "Falcon is running";
        s["game"]["error"] = nullptr;
    });
    if (hooks_.gameRunning) hooks_.gameRunning(true);
    while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT) {
        if (stopping_.load()) break;  // (the launcher is closing: the game is left running)
    }
    CloseHandle(pi.hProcess);
#else
    std::vector<std::string> arguments = {exe->string(), "--assets", files_.assetsDir(options_.gameVersion).string(), "--username", account.name, "--uuid", account.uuid};
    if (exportedData && *exportedData != files_.versionDir(options_.gameVersion)) {
        arguments.push_back("--data");
        arguments.push_back(exportedData->string());
    }
    arguments.push_back("--size");
    arguments.push_back("1280x720");  // (an ordinary window with its system title bar, of a good size)
    logLine("starting the client; game data: " + (exportedData ? exportedData->string() : std::string("none found")));
    mutate([&](json& s) { s["game"]["data"] = exportedData.has_value(); });
    // The client asks Mojang itself whether this account owns the game, with the account's Minecraft access token: the child gets it in its own environment for this one start
    // (the launcher's own environment is never changed, and the child wipes it from its memory as soon as it has read it).
    std::vector<std::string> environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (std::strncmp(*entry, "FALCON_MC_TOKEN=", 16) != 0) environment.emplace_back(*entry);
    }
    environment.push_back("FALCON_MC_TOKEN=" + tokens->accessToken);
    std::vector<char*> argv, envp;
    for (std::string& a : arguments) argv.push_back(a.data());
    argv.push_back(nullptr);
    for (std::string& e : environment) envp.push_back(e.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    const std::string workingDirectory = exe->parent_path().string();
    posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str());
    pid_t child = 0;
    const int startError = posix_spawn(&child, exe->c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    std::fill(environment.back().begin(), environment.back().end(), '\0');  // (the copy of the token in this process)
    if (startError != 0) {
        return setGameError({"spawn", "The game could not be started (error " + std::to_string(startError) + ": " + std::strerror(startError) + ")."});
    }
    logLine("started the client, process " + std::to_string(child));
    mutate([&](json& s) {
        s["game"]["status"] = "running";
        s["game"]["label"] = "Falcon is running";
        s["game"]["error"] = nullptr;
    });
    if (hooks_.gameRunning) hooks_.gameRunning(true);
    for (;;) {
        int status = 0;
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child || (done < 0 && errno != EINTR)) break;
        if (stopping_.load()) break;  // (the launcher is closing: the game is left running)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
#endif
    if (hooks_.gameRunning) hooks_.gameRunning(false);
    mutate([&](json& s) { s["game"]["status"] = gameIdleStatus(); });
}

// ---------------------------------------------------------------------------------------------------------------------------
// Pretend flows (no network, no disk): for working on the UI and for tests of the state machine
// ---------------------------------------------------------------------------------------------------------------------------

void App::mockJob(const std::string& which) {
    // FALCON_LAUNCHER_MOCK_SPEED=0.05 plays the pretend flows twenty times faster (the tests).
    static const double speed = [] {
        const char* s = std::getenv("FALCON_LAUNCHER_MOCK_SPEED");
        return s != nullptr ? std::max(std::atof(s), 0.001) : 1.0;
    }();
    const auto wait = [&](int ms) { return sleepUnlessCancelled(std::max(1, static_cast<int>(ms * speed))); };
    if (which == "boot") {
        mutate([&](json& s) {
            s["phase"] = "setup";
            s["setup"]["error"] = nullptr;
            s["setup"]["steps"] = json::array({stepJson("runtime", kRuntimeStep), stepJson("folders", "Creating the launcher's folders"),
                                               stepJson("client", "Installing the Falcon client"), stepJson("shortcut", kShortcutStep),
                                               stepJson("finish", "Finishing up")});
        });
        for (const char* id : {"runtime", "folders", "client", "shortcut", "finish"}) {
            setStep(id, "active");
            if (!wait(350)) return;
            setStep(id, "done");
        }
        mutate([&](json& s) { s["phase"] = "signin"; });
    } else if (which == "signIn") {
        mutate([&](json& s) {
            s["auth"]["error"] = nullptr;
            s["auth"]["status"] = "waiting";
            s["auth"]["message"] = "Finish signing in in the Microsoft window.";
        });
        if (!wait(3500)) {
            mutate([&](json& s) {
                s["auth"]["status"] = "signedOut";
            });
            return;
        }
        mutate([&](json& s) {
            s["auth"]["status"] = "working";
            s["auth"]["message"] = "Checking your Minecraft account…";
        });
        wait(900);
        mutate([&](json& s) {
            s["phase"] = "home";
            s["auth"]["status"] = "signedIn";
            s["auth"]["message"] = "";
            avatar_ = mockAvatar();
            s["account"] = json{{"name", "FalconPlayer"}, {"uuid", "069a79f444e94726a5befca90e38aaf5"}, {"owns", true}, {"hasProfile", true}, {"avatar", avatar_}};
            s["game"]["status"] = "missing";
        });
    } else if (which == "launch") {
        for (int i = 0; i <= 20; ++i) {
            setGameProgress("working", {"assets", "Downloading sounds and languages", static_cast<std::uint64_t>(i) * 18'000'000, 360'000'000});
            if (!wait(150)) {
                mutate([&](json& s) { s["game"]["status"] = "missing"; });
                return;
            }
        }
        setGameProgress("working", {"start", "Starting Falcon", 0, 0});
        wait(500);
        mutate([&](json& s) { s["game"]["status"] = "running"; });
        if (hooks_.gameRunning) hooks_.gameRunning(true);
        wait(4000);
        if (hooks_.gameRunning) hooks_.gameRunning(false);
        mutate([&](json& s) { s["game"]["status"] = "ready"; });
    }
}

void App::applyMockState(const std::string& which) {
    const json steps = json::array({stepJson("runtime", "Checking the web runtime"), stepJson("folders", "Creating the launcher's folders"), stepJson("client", "Installing the Falcon client"),
                                    stepJson("shortcut", "Adding a Start menu shortcut"), stepJson("finish", "Finishing up")});
    const json owner = {{"name", "FalconPlayer"}, {"uuid", "069a79f444e94726a5befca90e38aaf5"}, {"owns", true}, {"hasProfile", true}, {"avatar", mockAvatar()}};
    mutate([&](json& s) {
        if (which == "setup") {
            s["phase"] = "setup";
            s["setup"]["steps"] = steps;
            const char* status[] = {"done", "done", "active", "pending", "pending"};
            for (int i = 0; i < 5; ++i) s["setup"]["steps"][static_cast<std::size_t>(i)]["status"] = status[i];
            s["setup"]["steps"][1]["detail"] = "C:\\Users\\you\\AppData\\Local\\Falcon";
        } else if (which == "signin" || which == "waiting" || which == "device" || which == "error") {
            s["phase"] = "signin";
            if (which == "waiting" || which == "device") {
                s["auth"]["status"] = "waiting";
                s["auth"]["message"] = "Finish signing in in the Microsoft window.";
            } else if (which == "error") {
                s["auth"]["error"] = json{{"code", "xbox_no_account"}, {"message", "This Microsoft account has no Xbox profile yet. Sign in once at xbox.com to create one, then try again."}};
            }
        } else {
            s["phase"] = "home";
            s["auth"]["status"] = "signedIn";
            s["account"] = owner;
            s["game"]["status"] = which == "missing" ? "missing" : "ready";
            if (which == "notowned") s["account"]["owns"] = false;
            if (which == "nodata") s["game"]["data"] = false;
            if (which == "noprofile") s["account"]["hasProfile"] = false;
            if (which == "installing") {
                s["game"]["status"] = "working";
                s["game"]["phase"] = "assets";
                s["game"]["label"] = "Downloading sounds and languages";
                s["game"]["done"] = 151'000'000;
                s["game"]["total"] = 360'000'000;
            }
            if (which == "running") {
                s["game"]["status"] = "running";
                s["game"]["label"] = "Falcon is running";
            }
        }
    });
}

// ---------------------------------------------------------------------------------------------------------------------------
// The Mods tab (Modrinth)
// ---------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr int kModsPageSize = 20;

// How the demo mode's mods look: made up, so that nothing of Modrinth is needed to work on the page (and nobody's real mod is shown).
struct MockMod {
    const char* id;
    const char* title;
    const char* description;
    const char* author;
    long long downloads;
    const char* color;
    const char* categories;  // comma separated
};
const MockMod kMockMods[] = {
    {"mock0001", "Sample Lighting", "Soft, smooth lighting for caves and nights. A made-up mod, shown in the launcher's demo mode.", "someone", 4'200'000, "#e0a33d", "fabric,neoforge,optimization"},
    {"mock0002", "Sample Minimap", "A small map of what is round you, with waypoints. Made up for the demo mode.", "somebody", 2'750'000, "#3d8fe0", "fabric,forge,utility"},
    {"mock0003", "Sample Zoom", "Hold a key to look far away, with a smooth field of view. Made up for the demo mode.", "someone", 1'900'000, "#9b59e0", "fabric,quilt"},
    {"mock0004", "Sample Inventory Tweaks", "Sort the inventory with one click and keep tools stocked. Made up for the demo mode.", "tinker", 980'000, "#3dbd6a", "neoforge,forge,utility"},
    {"mock0005", "Sample Sounds", "Quieter footsteps and a gentler rain. Made up for the demo mode.", "sound", 410'000, "#e0603d", "fabric,decoration"},
    {"mock0006", "Sample Fog", "Fog that follows the biome you are in. Made up for the demo mode.", "someone", 233'000, "#4dc4c4", "fabric,neoforge,quilt"},
    {"mock0007", "Sample Compass", "A compass that points at your spawn and your bed. Made up for the demo mode.", "somebody", 87'000, "#c4a14d", "forge,utility"},
};

std::string mockModIcon(const MockMod& mod) {
    const std::string svg = std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\"><rect width=\"64\" height=\"64\" rx=\"14\" fill=\"") + mod.color +
                            "\"/><text x=\"32\" y=\"44\" font-size=\"34\" font-weight=\"700\" font-family=\"sans-serif\" text-anchor=\"middle\" fill=\"#ffffff\">" + std::string(1, mod.title[7]) + "</text></svg>";
    return "data:image/svg+xml;base64," + base64(svg);
}

json mockSearch(const std::string& text, const std::string& loader, int offset) {
    const std::string needle = lowerCase(text);
    json hits = json::array();
    int total = 0;
    for (const MockMod& mod : kMockMods) {
        if (!needle.empty() && lowerCase(std::string(mod.title) + " " + mod.description).find(needle) == std::string::npos) continue;
        if (!loader.empty() && (std::string(",") + mod.categories + ",").find("," + loader + ",") == std::string::npos) continue;
        if (total++ < offset) continue;
        json categories = json::array();
        const std::string rest = mod.categories;
        for (std::size_t from = 0; from < rest.size();) {
            const std::size_t comma = rest.find(',', from);
            categories.push_back(rest.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        const falcon::OptimizationVerdict verdict = falcon::judgeOptimization("", mod.title, categories.get<std::vector<std::string>>(), {});
        hits.push_back(json{{"projectId", mod.id}, {"slug", ""}, {"title", mod.title}, {"description", mod.description}, {"author", mod.author}, {"icon", mockModIcon(mod)}, {"downloads", mod.downloads}, {"categories", categories},
                            {"blocked", verdict.blocked}, {"blockedReason", verdict.reason}});
    }
    return json{{"hits", hits}, {"offset", offset}, {"limit", kModsPageSize}, {"total", total}, {"query", text}};
}

double mockSpeed() {
    static const double speed = [] {
        const char* s = std::getenv("FALCON_LAUNCHER_MOCK_SPEED");
        return s != nullptr ? std::max(std::atof(s), 0.001) : 1.0;
    }();
    return speed;
}

}  // namespace

fs::path App::modsDir() const { return dataRoot() / "mods"; }

fs::path App::modsIndexFile() const { return dataRoot() / "mods.json"; }

// The mods this launcher installed, as it noted them (mods.json), those whose file is still in the mods folder.
json App::loadInstalledMods() const {
    json out = json::object();
    if (options_.mock || !options_.mockState.empty()) return out;
    std::string text;
    if (!readFile(modsIndexFile(), text)) return out;
    const json index = json::parse(text, nullptr, false);
    if (!index.is_object()) return out;
    std::error_code ec;
    for (const auto& [projectId, entry] : index.items()) {
        if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) continue;
        if (fs::exists(modsDir() / fs::path(widen(entry["file"].get<std::string>())), ec)) out[projectId] = entry;
    }
    return out;
}

json App::scanBlockedMods() const {
    json out = json::array();
    if (options_.mock || !options_.mockState.empty()) return out;
    for (const falcon::ModInfo& mod : falcon::readModsFolder(modsDir())) {
        std::vector<std::string> needs;
        for (const std::string& dependency : mod.depends) needs.push_back(dependency.substr(0, dependency.find(' ')));
        const falcon::OptimizationVerdict verdict = falcon::judgeOptimization(mod.id, mod.name, {}, needs);
        if (verdict.blocked) out.push_back(json{{"file", narrow(mod.file.filename().wstring())}, {"name", mod.name}, {"reason", verdict.reason}});
    }
    return out;
}

bool App::modsSleep(int milliseconds) {
    const int scaled = std::max(1, static_cast<int>(milliseconds * mockSpeed()));
    for (int waited = 0; waited < scaled; waited += 50) {
        if (modsCancel_.load() || stopping_.load()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, scaled - waited)));
    }
    return !(modsCancel_.load() || stopping_.load());
}

void App::enqueueMods(std::function<void()> task) {
    ++modsPending_;
    {
        std::lock_guard<std::mutex> lock(modsMutex_);
        if (modsStop_) {
            --modsPending_;
            return;
        }
        modsQueue_.push_back(std::move(task));
        if (!modsThread_.joinable()) modsThread_ = std::thread([this] { modsLoop(); });
    }
    modsWake_.notify_one();
}

void App::modsLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(modsMutex_);
            modsWake_.wait(lock, [this] { return modsStop_ || !modsQueue_.empty(); });
            if (modsStop_) {  // (closing: what was still waiting is dropped)
                modsPending_ -= static_cast<int>(modsQueue_.size());
                modsQueue_.clear();
                return;
            }
            task = std::move(modsQueue_.front());
            modsQueue_.pop_front();
        }
        try {
            task();
        } catch (const std::exception& e) {
            logLine(std::string("a mods task failed: ") + e.what());
        }
        --modsPending_;
    }
}

void App::answerLater(const json& id, bool ok, const json& resultOrError) {
    if (!hooks_.emit) return;
    json message = {{"id", id}, {"ok", ok}};
    message[ok ? "result" : "error"] = resultOrError;
    hooks_.emit(dump(message));
}

void App::searchModsTask(const json& id, int generation, std::string text, std::string loader, int offset) {
    // A search that a newer one has replaced (the player kept typing) is not made: the page drops its answer anyway.
    const auto superseded = [&] { return generation != modsSearchGeneration_.load() || stopping_.load(); };
    if (superseded()) return answerLater(id, true, json{{"superseded", true}});
    if (options_.mock || !options_.mockState.empty()) {
        if (!modsSleep(450)) return;
        return answerLater(id, true, mockSearch(text, loader, offset));
    }
    ModQuery query;
    query.text = text;
    query.gameVersion = options_.gameVersion;
    query.loader = loader;
    query.offset = offset;
    query.limit = kModsPageSize;
    {  // (the mods folder is the truth about what is installed: a file deleted by hand stops showing, and a restarted launcher knows)
        const json installed = loadInstalledMods();
        const json blocked = scanBlockedMods();
        mutate([&](json& s) {
            s["mods"]["installed"] = installed;
            s["mods"]["blocked"] = blocked;
        });
    }
    const auto page = modrinth_.search(query);
    if (!page) return answerLater(id, false, errorJson(page.error));
    if (superseded()) return answerLater(id, true, json{{"superseded", true}});
    std::vector<std::string> urls;
    for (const ModHit& hit : page->hits) urls.push_back(hit.iconUrl);
    const std::vector<std::string> icons = modrinth_.icons(urls);
    json hits = json::array();
    for (std::size_t i = 0; i < page->hits.size(); ++i) {
        const ModHit& hit = page->hits[i];
        hits.push_back(json{{"projectId", hit.projectId}, {"slug", hit.slug}, {"title", hit.title}, {"description", hit.description}, {"author", hit.author}, {"icon", icons[i]}, {"downloads", hit.downloads}, {"categories", hit.categories},
                            {"blocked", hit.blocked}, {"blockedReason", hit.blockedReason}});
    }
    answerLater(id, true, json{{"hits", hits}, {"offset", page->offset}, {"limit", page->limit}, {"total", page->total}, {"query", text}});
}

void App::installModTask(const json& id, std::string projectId, std::string title, std::string loader) {
    const auto finish = [&](const json& installedEntry, const Error* error) {
        mutate([&](json& s) {
            s["mods"]["busy"].erase(projectId);
            if (error == nullptr) s["mods"]["installed"][projectId] = installedEntry;
        });
        if (error != nullptr) {
            logLine("the mod " + projectId + " was not installed: " + error->code + ": " + error->message);
            answerLater(id, false, errorJson(*error));
        } else {
            answerLater(id, true, json{{"projectId", projectId}, {"file", installedEntry["file"]}, {"version", installedEntry["version"]}, {"loader", installedEntry["loader"]}});
        }
    };
    if (options_.mock || !options_.mockState.empty()) {  // (pretend: no network and no disk)
        if (!modsSleep(900)) {
            const Error closing{"cancelled", "The launcher is closing."};
            return finish(json::object(), &closing);
        }
        for (const MockMod& mod : kMockMods) {  // (the demo refuses what the real one refuses)
            if (projectId != mod.id) continue;
            std::vector<std::string> categories;
            for (std::size_t from = 0, comma = 0; comma != std::string::npos; from = comma + 1) {
                comma = std::string(mod.categories).find(',', from);
                categories.push_back(std::string(mod.categories).substr(from, comma == std::string::npos ? std::string::npos : comma - from));
            }
            const falcon::OptimizationVerdict verdict = falcon::judgeOptimization("", mod.title, categories, {});
            if (verdict.blocked) {
                const Error blockedError{"optimization_mod", verdict.reason};
                return finish(json::object(), &blockedError);
            }
        }
        const json entry = {{"title", title}, {"version", "1.0.0"}, {"file", "sample-" + projectId + "-1.0.0.jar"}, {"loader", loader.empty() ? "fabric" : loader}};
        return finish(entry, nullptr);
    }
    // Optimization mods are not installed (not compatible with Falcon, and never will be): asked again here, whatever the page showed.
    const auto project = modrinth_.vetProject(projectId);
    if (!project) {
        const Error error = project.error;
        return finish(json::object(), &error);
    }
    if (project->blocked) {
        logLine("the mod " + projectId + " was refused: " + project->reason);
        const Error refused{"optimization_mod", project->reason};
        return finish(json::object(), &refused);
    }
    const auto file = modrinth_.chooseFile(projectId, options_.gameVersion, loader);
    if (!file) {
        const Error error = file.error;
        return finish(json::object(), &error);
    }
    const auto needs = modrinth_.vetDependencies(*file, title);
    if (!needs) {
        const Error error = needs.error;
        return finish(json::object(), &error);
    }
    if (needs->blocked) {
        logLine("the mod " + projectId + " was refused: " + needs->reason);
        const Error refused{"optimization_mod", needs->reason};
        return finish(json::object(), &refused);
    }
    mutate([&](json& s) { s["mods"]["busy"][projectId] = "Downloading " + file->filename + "…"; });
    const auto done = modrinth_.install(*file, title.empty() ? projectId : title, modsDir(), modsCancel_);
    if (!done) {
        const Error error = done.error;
        return finish(json::object(), &error);
    }
    // Noted, so that the page can say "Installed" next time (the folder itself is the truth: a file that was deleted by hand stops showing).
    std::string text;
    json index = readFile(modsIndexFile(), text) ? json::parse(text, nullptr, false) : json::object();
    if (!index.is_object()) index = json::object();
    const json entry = {{"title", done->title}, {"version", done->versionNumber}, {"file", done->file}, {"loader", done->loader}};
    if (index.contains(projectId) && index[projectId].is_object() && index[projectId].value("file", "") != done->file) {
        std::error_code ec;
        fs::remove(modsDir() / fs::path(widen(index[projectId].value("file", ""))), ec);  // (an older version of the same mod: replaced, not kept beside it)
    }
    index[projectId] = entry;
    writeFileAtomic(modsIndexFile(), dump(index));
    finish(entry, nullptr);
}

}  // namespace launcher
