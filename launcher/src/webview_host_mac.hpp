#pragma once

#include "setup.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace launcher {

struct WebHostOptions {
    std::wstring title = L"Falcon Launcher";
    int clientWidth = 1100;
    int clientHeight = 700;
    bool devtools = false;    // the page's context menu -> Inspect Element
    std::wstring startPage;   // "settings" or "logs": the page opens there (for screenshots; the page reads it from the address)
    bool offscreen = false;   // the windows sit outside every screen and never take focus: for --screenshot and the self-tests, so a test run disturbs nobody
};

// The launcher's window and the web view in it, on a Mac: AppKit and WKWebView (the system's own web engine). The same class as the Windows one
// (webview_host.hpp, WebView2) and the Linux one (webview_host_gtk.hpp, WebKitGTK), with the same calls, so that the rest of the launcher does not
// know the difference.
//
// The UI page is one html file carried in the program and answered from memory to the address falcon://app/index.html through a WKURLSchemeHandler.
// It talks to C++ the way it does under WebView2 (window.chrome.webview.postMessage(string), and "message" events back): the same small script the
// Linux host uses is put in front of the page to make those two names exist.
//
// Everything runs on the thread that made it, except post(), setGameRunning(), close(), minimize(), startDrag(), beginSignIn() and cancelSignIn(),
// which any thread may call.
class WebHost {
public:
    struct Callbacks {
        std::function<std::string(const std::string&)> onMessage;  // a string from the page; returns the answer (empty: none)
        std::function<void()> onPageLoaded;
    };

    WebHost(WebHostOptions options, Callbacks callbacks, Blob page);
    ~WebHost();
    WebHost(const WebHost&) = delete;
    WebHost& operator=(const WebHost&) = delete;

    // Makes the window and the web view. False (with the reason in the log) when there is no display or no web engine.
    bool create();
    int run();  // the main loop; returns the exit code

    void post(const std::string& message);  // to the page, any thread
    void setGameRunning(bool running);       // any thread: the window minimises while the game runs, and comes back after
    void close();                            // any thread
    void minimize();                         // any thread
    void startDrag();                        // any thread: moves the window with the mouse, as a title bar does (the window has none of the system's)
    // Microsoft's sign-in page in a window of its own (any thread; the answer comes on the UI thread). `done` gets the first address that begins
    // with redirectPrefix and "" (the navigation is cancelled and the window closed), or "" and "cancelled" when the player closes the window, or
    // "" and a reason when it cannot be shown. The window keeps nothing: its web view has no storage on disk.
    using SignInDone = std::function<void(const std::string& redirectedUrl, const std::string& error)>;
    void beginSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done);
    void cancelSignIn();  // any thread: closes that window ("cancelled")
    // Runs `fn` on the UI thread after `milliseconds` (UI thread only).
    void after(unsigned milliseconds, std::function<void()> fn);
    // The page as a PNG (UI thread). `done(true)` once written.
    void capture(const std::filesystem::path& file, std::function<void(bool)> done);

    [[nodiscard]] bool pageLoaded() const { return loaded_.load(); }
    // (WebKit's web process is not this program's child that can be named: nothing to report.)
    [[nodiscard]] unsigned long browserProcessId() const { return 0; }

private:
    struct Impl;
    void runOnUi(std::function<void()> fn);
    void openSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done);
    void finishSignIn(const std::string& url, const std::string& error);
    void deliverToPage(const std::string& message);

    WebHostOptions options_;
    Callbacks callbacks_;
    Blob page_;
    std::atomic<bool> loaded_{false};
    std::unique_ptr<Impl> impl_;
};

}  // namespace launcher
