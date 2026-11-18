#pragma once

#include "auth.hpp"
#include "game_files.hpp"
#include "http.hpp"
#include "mods.hpp"
#include "setup.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace launcher {

struct AppOptions {
    std::string gameVersion = "26.3";  // the Minecraft version the bundled client is made for
    DataTablesSource tables = builtinDataTables();  // where Falcon's data tables are downloaded from (tests point it at a fake)
    std::string clientId;              // the Microsoft application (client) id; defaults to the Minecraft launcher's public one; empty: sign-in is off
    // Development and tests. `mock`: no network and no disk, the flows are acted out with delays. `mockState` (implies mock): the UI is put
    // in one frozen state, for screenshots and for working on the UI: setup, signin, waiting (device is the same), home, missing, notowned, noprofile, installing,
    // running, error.
    bool mock = false;
    std::string mockState;
};

// What the controller needs from the window around it.
struct AppHooks {
    std::function<void(const std::string& json)> emit;  // an event for the UI. Called from worker threads: the window marshals it.
    std::function<void(bool running)> gameRunning;       // the game started / stopped (the window minimises itself meanwhile)
    std::function<void()> quit;                          // the UI asked to close
    std::function<void()> minimize;                      // the UI asked to minimize
    std::function<void()> startDrag;                     // titlebar drag started
    // Microsoft's sign-in page in a window of the launcher: opens startUrl, and calls done(address, "") with the first address that begins with
    // redirectPrefix (the window is closed), or done("", "cancelled") when the player closes the window, or done("", reason) when it cannot be
    // shown. done runs on the window's thread.
    std::function<void(const std::string& startUrl, const std::string& redirectPrefix, std::function<void(const std::string& redirectedUrl, const std::string& error)> done)> beginSignIn;
    std::function<void()> cancelSignIn;                  // close that window (the player cancelled in the UI)
};

// Everything the launcher does, behind one door: the UI sends JSON messages (handleMessage) and is sent the whole state as JSON
// whenever it changes. No window code in here, so the logic runs and is tested without one.
//
//   phase  setup -> signin -> home          setup: the first-run installation (and again after an update that changes what it does)
//                                            signin: Microsoft account (its own page in a window of the launcher); home: signed in, ownership checked, Launch
//
// Messages (JSON {"id", "method", "params"}; the answer is {"id", "ok", "result" | "error"}):
//   uiReady, getState, signIn, cancelSignIn, signOut, recheck, launch, cancelLaunch, retrySetup, openUrl{url}, openFolder{which}, setSetting{key,value}, quit
//   searchMods{query,loader,offset}, installMod{projectId,title,loader}: the Mods tab (Modrinth). They need the network, which the window's thread must not wait for, so handleMessage returns
//   at once with an empty string and the answer ({"id", "ok", ...}, the same form) is sent through hooks.emit when it is there. state.mods holds the mods that are installed.
class App {
public:
    App(HttpClient& http, AppOptions options, AppHooks hooks, Blob bundledClient);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] std::string handleMessage(const std::string& message);
    [[nodiscard]] std::string stateJson() const;
    void shutdown();
    void waitIdle();  // until the current background job (if any) has finished: for tests

private:
    void uiReady();
    void beginBoot();

    bool startJob(const char* name, std::function<void()> fn);
    void setupJob();
    void signInJob();
    void silentSignInJob();
    void finishSignIn(const MicrosoftTokens& tokens);
    [[nodiscard]] std::string fetchAvatar(const std::string& skinUrl);
    void launchJob();
    void mockJob(const std::string& which);
    void applyMockState(const std::string& which);

    // The Mods tab: its own worker, so that a search or a download never waits for (or holds up) the game's own jobs.
    void enqueueMods(std::function<void()> task);
    void modsLoop();
    void answerLater(const nlohmann::json& id, bool ok, const nlohmann::json& resultOrError);
    void searchModsTask(const nlohmann::json& id, int generation, std::string text, std::string loader, int offset);
    void installModTask(const nlohmann::json& id, std::string projectId, std::string title, std::string loader);
    [[nodiscard]] nlohmann::json loadInstalledMods() const;
    [[nodiscard]] std::filesystem::path modsDir() const;
    [[nodiscard]] std::filesystem::path modsIndexFile() const;
    bool modsSleep(int milliseconds);

    bool sleepUnlessCancelled(int milliseconds);
    void mutate(const std::function<void(nlohmann::json&)>& change);
    void setStep(const std::string& id, const std::string& status, const std::string& detail = {});
    void setAuthError(const Error& error);
    void setGameError(const Error& error);
    void setGameProgress(const std::string& status, const InstallProgress& p);
    [[nodiscard]] std::string gameIdleStatus() const;

    [[nodiscard]] nlohmann::json loadConfig() const;
    void saveConfig(const nlohmann::json& config) const;
    [[nodiscard]] std::filesystem::path accountFile() const;

    HttpClient& http_;
    AppOptions options_;
    AppHooks hooks_;
    Blob client_;
    AuthService auth_;
    GameFiles files_;
    Modrinth modrinth_;

    mutable std::mutex mutex_;
    nlohmann::json state_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> booted_{false};
    std::thread job_;
    std::string avatar_;  // the signed-in player's head as a data address (guarded by mutex_, like state_)

    std::mutex modsMutex_;
    std::condition_variable modsWake_;
    std::deque<std::function<void()>> modsQueue_;
    std::thread modsThread_;
    bool modsStop_ = false;           // (guarded by modsMutex_)
    std::atomic<int> modsPending_{0};  // tasks queued or running
    std::atomic<int> modsSearchGeneration_{0};
    std::atomic<bool> modsCancel_{false};
};

}  // namespace launcher
