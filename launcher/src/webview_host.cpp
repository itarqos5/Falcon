#include "webview_host.hpp"

#include "com.hpp"
#include "util.hpp"

#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <fstream>

namespace launcher {

namespace fs = std::filesystem;

namespace {

constexpr UINT WM_POST = WM_APP + 1;  // lParam: a std::string* for the page
constexpr UINT WM_GAME = WM_APP + 2;  // wParam: 1 the game started, 0 it stopped
constexpr UINT WM_SIGNIN_OPEN = WM_APP + 3;    // lParam: a SignInRequest*
constexpr UINT WM_SIGNIN_CLOSE = WM_APP + 4;   // the player cancelled in the page
constexpr UINT WM_SIGNIN_FINISH = WM_APP + 5;  // wParam: the sign-in window's number, lParam: a std::string* (the redirect address)
constexpr wchar_t kSignInClass[] = L"FalconLauncherSignIn";
constexpr wchar_t kWindowClass[] = L"FalconLauncherWindow";
constexpr wchar_t kOrigin[] = L"https://falcon.launcher/";
constexpr COLORREF kBackground = RGB(13, 17, 23);
constexpr COLORREF kAccent = RGB(56, 189, 164);

// Memory-lean browser settings: the UI is one small page, so it gets one renderer, the GPU work done inside the browser process, and none of
// the browser's background services (updates, sync, translation, predictive networking ...).
constexpr wchar_t kBrowserArguments[] =
    L"--disable-features=msSmartScreenProtection,Translate,msEdgeTranslate,AutofillServerCommunication,OptimizationHints,MediaRouter,msWebOOUI,msPdfOOUI,msEdgeShopping "
    L"--disable-background-networking --disable-component-update --disable-sync --disable-default-apps --disable-extensions --no-first-run "
    L"--renderer-process-limit=1 --in-process-gpu";

HRESULT copyString(const std::wstring& text, LPWSTR* out) {
    if (out == nullptr) return E_POINTER;
    *out = static_cast<LPWSTR>(CoTaskMemAlloc((text.size() + 1) * sizeof(wchar_t)));
    if (*out == nullptr) return E_OUTOFMEMORY;
    std::memcpy(*out, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    return S_OK;
}

// The options WebView2 takes when it starts (the SDK's own class needs Microsoft's WRL).
class EnvironmentOptions final : public ICoreWebView2EnvironmentOptions {
public:
    explicit EnvironmentOptions(std::wstring arguments) : arguments_(std::move(arguments)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IidOf<ICoreWebView2EnvironmentOptions>::value)) {
            *out = static_cast<ICoreWebView2EnvironmentOptions*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(LPWSTR* value) override { return copyString(arguments_, value); }
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(LPCWSTR value) override {
        arguments_ = value != nullptr ? value : L"";
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* value) override { return copyString(language_, value); }
    HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR value) override {
        language_ = value != nullptr ? value : L"";
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(LPWSTR* value) override { return copyString(L"110.0.0.0", value); }
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* allow) override {
        if (allow == nullptr) return E_POINTER;
        *allow = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL) override { return S_OK; }

private:
    std::atomic<ULONG> refs_{1};
    std::wstring arguments_;
    std::wstring language_ = L"en-US";
};

using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

void setDwm(HWND hwnd, DWORD attribute, const void* value, DWORD size) { DwmSetWindowAttribute(hwnd, attribute, value, size); }

}  // namespace

struct WebHost::SignInRequest {
    std::string startUrl;
    std::string redirectPrefix;
    SignInDone done;
};

struct WebHost::Impl {
    HMODULE loader = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    bool suspended = false;
    bool comInitialised = false;

    // The Microsoft sign-in window, when one is open (UI thread only).
    struct SignIn {
        UINT_PTR id = 0;
        HWND hwnd = nullptr;
        ComPtr<ICoreWebView2Controller> controller;
        ComPtr<ICoreWebView2> view;
        SignInDone done;
    };
    std::unique_ptr<SignIn> signIn;
    UINT_PTR nextSignInId = 1;
};

WebHost::WebHost(HINSTANCE instance, WebHostOptions options, Callbacks callbacks, Blob page, Blob loaderDll)
    : instance_(instance), options_(std::move(options)), callbacks_(std::move(callbacks)), page_(page), loaderDll_(loaderDll), impl_(std::make_unique<Impl>()) {}

WebHost::~WebHost() {
    impl_->controller.reset();
    impl_->view.reset();
    impl_->environment.reset();
    if (impl_->comInitialised) CoUninitialize();
}

bool WebHost::create() {
    impl_->comInitialised = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WebHost::windowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBackground);
    wc.lpszClassName = kWindowClass;
    wc.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    RegisterClassExW(&wc);
    WNDCLASSEXW sc = wc;  // the sign-in window: same look, its own procedure
    sc.lpfnWndProc = &WebHost::signInProc;
    sc.lpszClassName = kSignInClass;
    RegisterClassExW(&sc);

    const UINT dpi = GetDpiForSystem();
    // A fixed-size window (the page is designed for one size) with no system frame at all: no WS_CAPTION, so Windows has no title bar to draw
    // (hiding one with WM_NCCALCSIZE still left Windows' buttons and bar showing on some PCs). The page draws its own title bar and moves the window.
    const DWORD style = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX;
    // (The capture mode of tests and screenshots: a window in the normal place but see-through, click-through and never active, so that the page believes it is shown and runs at full speed; far off-screen it is treated as hidden.)
    const DWORD exStyle = options_.offscreen ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT) : 0;
    const int width = MulDiv(options_.clientWidth, static_cast<int>(dpi), 96);
    const int height = MulDiv(options_.clientHeight, static_cast<int>(dpi), 96);
    int x = 0, y = 0;
    {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - width) / 2;
        y = work.top + ((work.bottom - work.top) - height) / 2;
    }
    hwnd_ = CreateWindowExW(exStyle, kWindowClass, options_.title.c_str(), style, x, y, width, height, nullptr, nullptr, instance_, this);
    if (hwnd_ == nullptr) return false;

    // The window shadow of a frameless window: a one pixel frame extended into the page (the page covers it, the shadow stays).
    MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);

    const COLORREF border = RGB(46, 56, 72);  // a thin outline, so the page's edge is visible on a dark desktop
    setDwm(hwnd_, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    const DWORD corners = 2;  // DWMWCP_ROUND: rounded corners on Windows 11 (the frame is gone, the system would otherwise square them)
    setDwm(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));

    if (options_.offscreen) SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
    ShowWindow(hwnd_, options_.offscreen ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(hwnd_);
    {
        RECT outer{}, inner{};
        GetWindowRect(hwnd_, &outer);
        GetClientRect(hwnd_, &inner);
        const bool caption = (GetWindowLongW(hwnd_, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
        logLine("window " + std::to_string(outer.right - outer.left) + "x" + std::to_string(outer.bottom - outer.top) + ", client " + std::to_string(inner.right) + "x" + std::to_string(inner.bottom) +
                (!caption && outer.right - outer.left == inner.right && outer.bottom - outer.top == inner.bottom ? " (no system frame or title bar)" : " (the system draws a frame or title bar)"));
    }

    // The web view: the loader DLL is carried in this exe and put in the data folder once.
    fs::path dll = dataRoot() / "bin" / "WebView2Loader.dll";
    if (!loaderDll_.empty()) {
        std::error_code ec;
        if (!fs::exists(dll, ec) || fs::file_size(dll, ec) != loaderDll_.size) {
            writeFileAtomic(dll, std::string_view(reinterpret_cast<const char*>(loaderDll_.data), loaderDll_.size));
        }
    } else {
        wchar_t self[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
        dll = fs::path(self).parent_path() / "WebView2Loader.dll";
    }
    impl_->loader = LoadLibraryW(dll.c_str());
    logLine("web view loader loaded after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
    const auto createEnvironment = impl_->loader != nullptr ? reinterpret_cast<CreateEnvironmentFn>(reinterpret_cast<void*>(GetProcAddress(impl_->loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (createEnvironment == nullptr) {
        logLine("cannot load the WebView2 loader from " + narrow(dll.wstring()));
        MessageBoxW(hwnd_, L"The web view could not be loaded (WebView2Loader.dll).", L"Falcon Launcher", MB_ICONERROR);
        return false;
    }

    std::wstring arguments = kBrowserArguments;
    if (const wchar_t* extra = _wgetenv(L"FALCON_LAUNCHER_BROWSER_ARGS")) arguments = extra;  // (to try other settings)
    if (options_.offscreen) {  // (the window is under everything else: keep Chromium from deciding it is hidden)
        const std::size_t at = arguments.find(L"--disable-features=");
        if (at != std::wstring::npos) arguments.insert(at + std::wcslen(L"--disable-features="), L"CalculateNativeWinOcclusion,");
        else arguments += L" --disable-features=CalculateNativeWinOcclusion";
    }
    ComPtr<ICoreWebView2EnvironmentOptions> envOptions;
    envOptions.attach(new EnvironmentOptions(arguments));
    // (Test and screenshot runs use a profile of their own: one that is already open in the player's launcher cannot be opened again with other settings.)
    const fs::path profile = dataRoot() / (options_.offscreen ? "webview-test" : "webview");
    std::error_code ec;
    fs::create_directories(profile, ec);

    auto onEnvironment = makeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || environment == nullptr) {
                logLine("the WebView2 environment could not be created: " + std::to_string(static_cast<unsigned long>(result)));
                MessageBoxW(hwnd_, L"The Microsoft Edge WebView2 runtime could not be started.", L"Falcon Launcher", MB_ICONERROR);
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return S_OK;
            }
            impl_->environment.set(environment);
            logLine("web view environment ready after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
            auto onController = makeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(controllerResult) || controller == nullptr) {
                        logLine("the WebView2 controller could not be created: " + std::to_string(static_cast<unsigned long>(controllerResult)));
                        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                        return S_OK;
                    }
                    impl_->controller.set(controller);
                    logLine("web view controller ready after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
                    controller->get_CoreWebView2(impl_->view.put());
                    ICoreWebView2* view = impl_->view.get();

                    UINT32 pid = 0;
                    ComPtr<ICoreWebView2_2> view2 = impl_->view.as<ICoreWebView2_2>();
                    if (view2) {
                        // (the browser process id is on the view in newer SDKs; the environment is asked below)
                    }
                    (void)pid;

                    ComPtr<ICoreWebView2Settings> settings;
                    view->get_Settings(settings.put());
                    if (settings) {
                        settings->put_IsScriptEnabled(TRUE);
                        settings->put_IsWebMessageEnabled(TRUE);
                        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_IsZoomControlEnabled(FALSE);
                        settings->put_IsBuiltInErrorPageEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(options_.devtools ? TRUE : FALSE);
                        settings->put_AreDefaultContextMenusEnabled(options_.devtools ? TRUE : FALSE);
                        if (auto settings3 = settings.as<ICoreWebView2Settings3>()) settings3->put_AreBrowserAcceleratorKeysEnabled(options_.devtools ? TRUE : FALSE);
                        if (auto settings9 = settings.as<ICoreWebView2Settings9>()) settings9->put_IsNonClientRegionSupportEnabled(TRUE);
                    }
                    if (auto controller2 = impl_->controller.as<ICoreWebView2Controller2>()) controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 13, 17, 23});
                    RECT bounds{};
                    GetClientRect(hwnd_, &bounds);
                    controller->put_Bounds(bounds);
                    controller->put_IsVisible(TRUE);

                    EventRegistrationToken token{};
                    // The page is answered from memory.
                    view->AddWebResourceRequestedFilter(L"https://falcon.launcher/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                    view->add_WebResourceRequested(
                        makeHandler<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
                            [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                ComPtr<ICoreWebView2WebResourceRequest> request;
                                args->get_Request(request.put());
                                LPWSTR uri = nullptr;
                                if (request) request->get_Uri(&uri);
                                std::wstring path = uri != nullptr ? uri : L"";
                                CoTaskMemFree(uri);
                                path = path.size() >= std::wcslen(kOrigin) ? path.substr(std::wcslen(kOrigin) - 1) : L"/";
                                path = path.substr(0, path.find_first_of(L"?#"));
                                ComPtr<ICoreWebView2WebResourceResponse> response;
                                if ((path == L"/" || path == L"/index.html") && !page_.empty()) {
                                    logLine("the page was asked for after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
                                    IStream* stream = SHCreateMemStream(page_.data, static_cast<UINT>(page_.size));
                                    impl_->environment->CreateWebResourceResponse(
                                        stream, 200, L"OK",
                                        L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                                        L"Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; font-src data:; base-uri 'none'; form-action 'none'",
                                        response.put());
                                    if (stream != nullptr) stream->Release();
                                } else {
                                    impl_->environment->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", response.put());
                                }
                                if (response) args->put_Response(response.get());
                                return S_OK;
                            })
                            .get(),
                        &token);

                    // Messages from the page.
                    view->add_WebMessageReceived(
                        makeHandler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
                            [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                LPWSTR source = nullptr;
                                args->get_Source(&source);
                                const bool ours = source != nullptr && std::wcsncmp(source, kOrigin, std::wcslen(kOrigin)) == 0;
                                CoTaskMemFree(source);
                                if (!ours) return S_OK;  // only the launcher's own page may give orders
                                LPWSTR message = nullptr;
                                if (FAILED(args->TryGetWebMessageAsString(&message)) || message == nullptr) return S_OK;
                                const std::string request = narrow(message);
                                CoTaskMemFree(message);
                                if (!callbacks_.onMessage) return S_OK;
                                const std::string answer = callbacks_.onMessage(request);
                                if (!answer.empty()) sender->PostWebMessageAsString(widen(answer).c_str());
                                return S_OK;
                            })
                            .get(),
                        &token);

                    // Nothing but the launcher's own page: links go to the system browser.
                    view->add_NavigationStarting(
                        makeHandler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
                            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                LPWSTR uri = nullptr;
                                args->get_Uri(&uri);
                                if (uri != nullptr && std::wcsncmp(uri, kOrigin, std::wcslen(kOrigin)) != 0) {
                                    args->put_Cancel(TRUE);
                                    openInBrowser(narrow(uri));
                                }
                                CoTaskMemFree(uri);
                                return S_OK;
                            })
                            .get(),
                        &token);
                    view->add_NewWindowRequested(
                        makeHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                args->put_Handled(TRUE);
                                LPWSTR uri = nullptr;
                                args->get_Uri(&uri);
                                if (uri != nullptr) openInBrowser(narrow(uri));
                                CoTaskMemFree(uri);
                                return S_OK;
                            })
                            .get(),
                        &token);
                    view->add_NavigationCompleted(
                        makeHandler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                if (!loaded_.exchange(true)) {
                                    logLine("the page was loaded after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
                                    InvalidateRect(hwnd_, nullptr, TRUE);
                                    if (callbacks_.onPageLoaded) callbacks_.onPageLoaded();
                                }
                                return S_OK;
                            })
                            .get(),
                        &token);
                    // If the renderer dies the page is simply loaded again.
                    view->add_ProcessFailed(
                        makeHandler<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
                            [this](ICoreWebView2* sender, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                                args->get_ProcessFailedKind(&kind);
                                logLine("a web view process failed, kind " + std::to_string(static_cast<int>(kind)));
                                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED || kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
                                    loaded_ = false;
                                    sender->Reload();
                                }
                                return S_OK;
                            })
                            .get(),
                        &token);

                    logLine("navigating after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
                    view->Navigate((std::wstring(L"https://falcon.launcher/index.html") + (options_.startPage.empty() ? L"" : L"#" + options_.startPage)).c_str());
                    return S_OK;
                });
            environment->CreateCoreWebView2Controller(hwnd_, onController.get());
            return S_OK;
        });
    const HRESULT started = createEnvironment(nullptr, profile.c_str(), envOptions.get(), onEnvironment.get());
    if (FAILED(started)) {
        logLine("CreateCoreWebView2EnvironmentWithOptions failed: " + std::to_string(static_cast<unsigned long>(started)));
        MessageBoxW(hwnd_, L"The Microsoft Edge WebView2 runtime is not available.", L"Falcon Launcher", MB_ICONERROR);
        return false;
    }
    return true;
}

int WebHost::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void WebHost::post(const std::string& message) {
    if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_POST, 0, reinterpret_cast<LPARAM>(new std::string(message)));
}

void WebHost::setGameRunning(bool running) {
    if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_GAME, running ? 1 : 0, 0);
}

void WebHost::close() {
    if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void WebHost::minimize() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_MINIMIZE);
}

void WebHost::startDrag() {
    if (hwnd_ != nullptr) {
        // Moves the window with the mouse the way a title bar does (SC_MOVE | 2 is "drag move"; frameless windows have no caption to
        // press, and this is what they use). Posted, so the page's message returns first.
        ReleaseCapture();
        PostMessageW(hwnd_, WM_SYSCOMMAND, SC_MOVE | 2, 0);
    }
}

void WebHost::after(unsigned milliseconds, std::function<void()> fn) {
    const UINT_PTR id = nextTimer_++;
    timers_[id] = std::move(fn);
    SetTimer(hwnd_, id, milliseconds, nullptr);
}

void WebHost::capture(const fs::path& file, std::function<void(bool)> done) {
    if (!impl_->view) {
        done(false);
        return;
    }
    IStream* stream = nullptr;
    if (FAILED(SHCreateStreamOnFileW(file.c_str(), STGM_CREATE | STGM_WRITE, &stream)) || stream == nullptr) {
        done(false);
        return;
    }
    impl_->view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream,
                                makeHandler<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>([stream, done](HRESULT result) -> HRESULT {
                                    stream->Release();
                                    done(SUCCEEDED(result));
                                    return S_OK;
                                }).get());
}

void WebHost::beginSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done) {
    auto request = std::make_unique<SignInRequest>(SignInRequest{startUrl, redirectPrefix, std::move(done)});
    if (hwnd_ == nullptr || !PostMessageW(hwnd_, WM_SIGNIN_OPEN, 0, reinterpret_cast<LPARAM>(request.get()))) {
        if (request->done) request->done("", "there is no window");
        return;
    }
    request.release();  // (the window procedure owns it now)
}

void WebHost::cancelSignIn() {
    if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_SIGNIN_CLOSE, 0, 0);
}

// The window procedure of the sign-in window: closing it is the player saying no.
LRESULT CALLBACK WebHost::signInProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* self = reinterpret_cast<WebHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        switch (message) {
            case WM_CLOSE:
                self->finishSignIn("", "cancelled");
                return 0;
            case WM_SIZE:
                if (self->impl_->signIn && self->impl_->signIn->hwnd == hwnd && self->impl_->signIn->controller) {
                    RECT bounds{};
                    GetClientRect(hwnd, &bounds);
                    self->impl_->signIn->controller->put_Bounds(bounds);
                }
                return 0;
            case WM_ERASEBKGND:
                return 1;
            default:
                break;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void WebHost::openSignIn(std::unique_ptr<SignInRequest> request) {
    if (impl_->signIn) finishSignIn("", "cancelled");  // (one at a time: an older window is dismissed)
    if (!impl_->environment) {
        if (request->done) request->done("", "the web view is not ready yet");
        return;
    }
    auto win = std::make_unique<Impl::SignIn>();
    win->id = impl_->nextSignInId++;
    win->done = std::move(request->done);

    // A fixed-size window of the usual kind (caption and all: it is Microsoft's page, the player should see it is a separate window), centred over the launcher.
    const UINT dpi = GetDpiForWindow(hwnd_);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT frame{0, 0, MulDiv(480, static_cast<int>(dpi), 96), MulDiv(700, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
    const int width = frame.right - frame.left, height = frame.bottom - frame.top;
    RECT owner{};
    GetWindowRect(hwnd_, &owner);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = std::clamp(static_cast<int>(owner.left + ((owner.right - owner.left) - width) / 2), static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    const int y = std::clamp(static_cast<int>(owner.top + ((owner.bottom - owner.top) - height) / 2), static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    win->hwnd = CreateWindowExW(options_.offscreen ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT) : 0, kSignInClass, L"Sign in to your Microsoft account", style, x, y, width, height, hwnd_, nullptr, instance_, this);
    if (win->hwnd == nullptr) {
        if (win->done) win->done("", "the window could not be created");
        return;
    }
    const BOOL dark = TRUE;
    setDwm(win->hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = kBackground, text = RGB(230, 237, 243);
    setDwm(win->hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    setDwm(win->hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
    if (options_.offscreen) SetLayeredWindowAttributes(win->hwnd, 0, 1, LWA_ALPHA);  // (tests: see-through and click-through, as the main window is)
    ShowWindow(win->hwnd, options_.offscreen ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!options_.offscreen) SetForegroundWindow(win->hwnd);

    const UINT_PTR id = win->id;
    const HWND signInWindow = win->hwnd;
    impl_->signIn = std::move(win);

    const std::wstring prefix = widen(request->redirectPrefix);
    const std::wstring startUrl = widen(request->startUrl);
    auto onController = makeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
        [this, id, signInWindow, prefix, startUrl](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (FAILED(result) || controller == nullptr) {
                // (a leading 0x01 marks a reason rather than an address)
                PostMessageW(hwnd_, WM_SIGNIN_FINISH, id, reinterpret_cast<LPARAM>(new std::string("\x01the sign-in page could not be started")));
                return S_OK;
            }
            if (!impl_->signIn || impl_->signIn->id != id) {  // (closed while the web view was starting)
                controller->Close();
                return S_OK;
            }
            Impl::SignIn& sign = *impl_->signIn;
            sign.controller.set(controller);
            controller->get_CoreWebView2(sign.view.put());
            ICoreWebView2* view = sign.view.get();
            RECT bounds{};
            GetClientRect(signInWindow, &bounds);
            controller->put_Bounds(bounds);
            controller->put_IsVisible(TRUE);

            ComPtr<ICoreWebView2Settings> settings;
            view->get_Settings(settings.put());
            if (settings) {
                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_IsZoomControlEnabled(FALSE);
                settings->put_AreDevToolsEnabled(options_.devtools ? TRUE : FALSE);
                settings->put_AreDefaultContextMenusEnabled(options_.devtools ? TRUE : FALSE);
            }
            if (auto controller2 = sign.controller.as<ICoreWebView2Controller2>()) controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 13, 17, 23});

            EventRegistrationToken token{};
            // The sign-in is over when the page goes to the redirect address: the navigation is stopped and the address (with the code) is
            // handed on. Nothing but https pages may be shown.
            view->add_NavigationStarting(
                makeHandler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
                    [this, id, prefix](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                        LPWSTR uri = nullptr;
                        args->get_Uri(&uri);
                        const std::wstring address = uri != nullptr ? uri : L"";
                        CoTaskMemFree(uri);
                        if (address.size() >= prefix.size() && _wcsnicmp(address.c_str(), prefix.c_str(), prefix.size()) == 0) {
                            args->put_Cancel(TRUE);
                            PostMessageW(hwnd_, WM_SIGNIN_FINISH, id, reinterpret_cast<LPARAM>(new std::string(narrow(address))));
                        } else if (address.rfind(L"https://", 0) != 0 && address != L"about:blank") {
                            args->put_Cancel(TRUE);
                        }
                        return S_OK;
                    })
                    .get(),
                &token);
            view->add_NewWindowRequested(
                makeHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                        args->put_Handled(TRUE);  // (links out of the sign-in page, such as help and privacy, open in the browser)
                        LPWSTR uri = nullptr;
                        args->get_Uri(&uri);
                        if (uri != nullptr) openInBrowser(narrow(uri));
                        CoTaskMemFree(uri);
                        return S_OK;
                    })
                    .get(),
                &token);
            view->Navigate(startUrl.c_str());
            return S_OK;
        });
    // Private browsing where the runtime has it (nothing of the sign-in is kept, so "use another account" really starts fresh); otherwise the same profile.
    auto environment10 = impl_->environment.as<ICoreWebView2Environment10>();
    ComPtr<ICoreWebView2ControllerOptions> controllerOptions;
    if (environment10 && SUCCEEDED(environment10->CreateCoreWebView2ControllerOptions(controllerOptions.put())) && controllerOptions) {
        controllerOptions->put_IsInPrivateModeEnabled(TRUE);
        environment10->CreateCoreWebView2ControllerWithOptions(signInWindow, controllerOptions.get(), onController.get());
    } else {
        impl_->environment->CreateCoreWebView2Controller(signInWindow, onController.get());
    }
}

// Ends the sign-in: the window goes away and the answer is given, once.
void WebHost::finishSignIn(const std::string& url, const std::string& error) {
    if (!impl_->signIn) return;
    std::unique_ptr<Impl::SignIn> win = std::move(impl_->signIn);
    if (win->controller) win->controller->Close();
    win->view.reset();
    win->controller.reset();
    if (win->hwnd != nullptr) DestroyWindow(win->hwnd);
    if (hwnd_ != nullptr && IsWindow(hwnd_) && !options_.offscreen) SetForegroundWindow(hwnd_);
    if (win->done) win->done(url, error);
}

LRESULT CALLBACK WebHost::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WebHost* self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<WebHost*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<WebHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) return self->handle(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT WebHost::handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCALCSIZE:
            if (wParam == TRUE) return 0;
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc;
            GetClientRect(hwnd_, &rc);
            HBRUSH brush = CreateSolidBrush(kBackground);
            FillRect(dc, &rc, brush);
            DeleteObject(brush);
            if (!loaded_.load()) {  // something on screen while the web view starts: the name and a bar in the accent colour
                const int dpi = static_cast<int>(GetDpiForWindow(hwnd_));
                HFONT font = CreateFontW(-MulDiv(40, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                HGDIOBJ old = SelectObject(dc, font);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(230, 237, 243));
                RECT text = rc;
                text.top = (rc.bottom - rc.top) / 2 - MulDiv(44, dpi, 96);
                DrawTextW(dc, L"Falcon", -1, &text, DT_CENTER | DT_TOP | DT_SINGLELINE);
                SelectObject(dc, old);
                DeleteObject(font);
                const int barWidth = MulDiv(64, dpi, 96), barHeight = MulDiv(3, dpi, 96);
                RECT bar{(rc.right - barWidth) / 2, (rc.bottom - rc.top) / 2 + MulDiv(14, dpi, 96), (rc.right + barWidth) / 2, (rc.bottom - rc.top) / 2 + MulDiv(14, dpi, 96) + barHeight};
                HBRUSH accent = CreateSolidBrush(kAccent);
                FillRect(dc, &bar, accent);
                DeleteObject(accent);
            }
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_SIZE: {
            if (impl_->controller) {
                if (wParam == SIZE_MINIMIZED) {
                    // Minimised: the page is hidden and asked to give its memory back; it is woken when the window returns.
                    impl_->controller->put_IsVisible(FALSE);
                    if (auto v19 = impl_->view.as<ICoreWebView2_19>()) v19->put_MemoryUsageTargetLevel(COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW);
                    if (auto v3 = impl_->view.as<ICoreWebView2_3>()) {
                        v3->TrySuspend(makeHandler<ICoreWebView2TrySuspendCompletedHandler, HRESULT, BOOL>([this](HRESULT, BOOL) -> HRESULT {
                                           impl_->suspended = true;
                                           return S_OK;
                                       }).get());
                    }
                } else {
                    if (auto v3 = impl_->view.as<ICoreWebView2_3>()) v3->Resume();
                    impl_->suspended = false;
                    if (auto v19 = impl_->view.as<ICoreWebView2_19>()) v19->put_MemoryUsageTargetLevel(COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL);
                    RECT bounds{};
                    GetClientRect(hwnd_, &bounds);
                    impl_->controller->put_Bounds(bounds);
                    impl_->controller->put_IsVisible(TRUE);
                }
            }
            return 0;
        }
        case WM_POST: {  // (not gated on the page having finished loading: its script announces itself with uiReady, which is before NavigationCompleted)
            std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lParam));
            if (impl_->view && text) impl_->view->PostWebMessageAsString(widen(*text).c_str());
            return 0;
        }
        case WM_TIMER: {
            const auto it = timers_.find(static_cast<UINT_PTR>(wParam));
            if (it != timers_.end()) {
                KillTimer(hwnd_, wParam);
                std::function<void()> fn = std::move(it->second);
                timers_.erase(it);
                if (fn) fn();
            }
            return 0;
        }
        case WM_GAME:
            if (wParam != 0) {
                ShowWindow(hwnd_, SW_MINIMIZE);
            } else {
                ShowWindow(hwnd_, SW_RESTORE);
                SetForegroundWindow(hwnd_);
            }
            return 0;
        case WM_SIGNIN_OPEN:
            openSignIn(std::unique_ptr<SignInRequest>(reinterpret_cast<SignInRequest*>(lParam)));
            return 0;
        case WM_SIGNIN_CLOSE:
            finishSignIn("", "cancelled");
            return 0;
        case WM_SIGNIN_FINISH: {
            std::unique_ptr<std::string> text(reinterpret_cast<std::string*>(lParam));
            if (impl_->signIn && impl_->signIn->id == static_cast<UINT_PTR>(wParam) && text) {
                if (!text->empty() && (*text)[0] == '\x01') finishSignIn("", text->substr(1));
                else finishSignIn(*text, "");
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            finishSignIn("", "cancelled");  // (a sign-in window still open goes with the launcher)
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace launcher
