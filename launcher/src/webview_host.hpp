#pragma once

#include "setup.hpp"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace launcher {

struct WebHostOptions {
    std::wstring title = L"Falcon Launcher";
    int clientWidth = 1100;   // in 96-dpi pixels: scaled for the monitor
    int clientHeight = 700;
    bool devtools = false;    // F12 / right click -> Inspect
    std::wstring startPage;   // "settings" or "logs": the page opens there (for screenshots; the page reads it from the address)
    bool offscreen = false;   // the window sits outside every screen and never takes focus: for --screenshot, so a test run disturbs nobody
};

// The launcher's window and the web view in it.
//
// The web view is Microsoft Edge WebView2: the browser engine Windows already has (it is part of Windows 11 and of Windows 10 since 2022 and
// updates itself), loaded through a 190 KB loader DLL carried in this exe. No browser is bundled. The UI page is a single html file
// carried in the exe too and answered from memory to requests for https://falcon.launcher/, so there is nothing to read from disk.
// JavaScript talks to C++ with window.chrome.webview.postMessage(string) and hears back through the "message" event.
//
// Everything here runs on the thread that made it, except post(), setGameRunning() and close(), which any thread may call.
class WebHost {
public:
    struct Callbacks {
        std::function<std::string(const std::string&)> onMessage;  // a string from the page; returns the answer (empty: none)
        std::function<void()> onPageLoaded;
    };

    WebHost(HINSTANCE instance, WebHostOptions options, Callbacks callbacks, Blob page, Blob loaderDll);
    ~WebHost();
    WebHost(const WebHost&) = delete;
    WebHost& operator=(const WebHost&) = delete;

    // Makes the window at once (so there is something on screen while the web view starts) and the web view in the background.
    bool create();
    int run();  // the message loop; returns the exit code

    void post(const std::string& message);  // to the page, any thread
    void setGameRunning(bool running);       // any thread: the window minimises while the game runs, and comes back after
    void close();                            // any thread
    void minimize();                         // any thread
    void startDrag();                        // start window dragging (caption drag)
    // Microsoft's sign-in page in a window of its own (any thread; the answer comes on the window's thread). `done` gets the first address that
    // begins with redirectPrefix and "" (the navigation is cancelled and the window closed), or "" and "cancelled" when the player closes the
    // window, or "" and a reason when it cannot be shown. The window is private browsing: nothing of the sign-in is kept in the launcher's profile.
    using SignInDone = std::function<void(const std::string& redirectedUrl, const std::string& error)>;
    void beginSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done);
    void cancelSignIn();                     // any thread: closes that window ("cancelled")
    // Runs `fn` on the UI thread after `milliseconds` (UI thread only).
    void after(unsigned milliseconds, std::function<void()> fn);
    // The page as a PNG (UI thread). `done(true)` once written.
    void capture(const std::filesystem::path& file, std::function<void(bool)> done);

    [[nodiscard]] bool pageLoaded() const { return loaded_.load(); }
    [[nodiscard]] HWND window() const { return hwnd_; }
    // The processes the web view runs in (for measuring memory): its browser process id, 0 until it exists.
    [[nodiscard]] unsigned long browserProcessId() const { return browserPid_.load(); }

private:
    struct Impl;
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK signInProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    struct SignInRequest;
    void openSignIn(std::unique_ptr<SignInRequest> request);
    void finishSignIn(const std::string& url, const std::string& error);

    HINSTANCE instance_;
    WebHostOptions options_;
    Callbacks callbacks_;
    Blob page_;
    Blob loaderDll_;
    HWND hwnd_ = nullptr;
    std::atomic<bool> loaded_{false};
    std::atomic<unsigned long> browserPid_{0};
    std::unique_ptr<Impl> impl_;
    std::unordered_map<UINT_PTR, std::function<void()>> timers_;
    UINT_PTR nextTimer_ = 1;
};

}  // namespace launcher
