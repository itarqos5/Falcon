#pragma once

#include "auth.hpp"
#include "game_files.hpp"
#include "http.hpp"
#include "setup.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace launcher {

struct AppOptions {
    std::string gameVersion = "26.3";  // the Minecraft version the bundled client is made for
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

    mutable std::mutex mutex_;
    nlohmann::json state_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> booted_{false};
    std::thread job_;
    std::string avatar_;  // the signed-in player's head as a data address (guarded by mutex_, like state_)
};

}  // namespace launcher
