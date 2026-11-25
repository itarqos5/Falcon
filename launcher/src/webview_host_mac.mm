#include "webview_host_mac.hpp"

#include "util.hpp"

#include <Cocoa/Cocoa.h>
#include <WebKit/WebKit.h>
#include <dispatch/dispatch.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <memory>

// The launcher's window on a Mac: AppKit and WKWebView. It mirrors webview_host_gtk.cpp line for line where the two engines allow it: the same custom
// scheme (falcon://app/index.html answered from memory), the same shim in front of the page (window.chrome.webview over window.webkit.messageHandlers),
// the same private sign-in window, and the same offscreen mode for --screenshot and the self-tests.
//
// The window has no system title bar (the page draws its own, as on the other systems): it is a borderless NSWindow that can become key, and the page's
// startDrag call performs the system's window drag with the mouse event that is being handled.

namespace launcher {

namespace fs = std::filesystem;

// (Internal linkage: these are this file's helpers, but the Objective-C code below is outside this namespace and reaches them by their qualified names.)
constexpr const char* kOrigin = "falcon://app/";

// The two names the UI page talks to the launcher with (window.chrome.webview, as under WebView2), and the function the host calls to hand it a message.
constexpr const char* kShim = R"JS(
(function () {
  var listeners = [];
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    postMessage: function (message) { window.webkit.messageHandlers.falcon.postMessage(String(message)); },
    addEventListener: function (type, listener) { if (type === 'message') listeners.push(listener); }
  };
  window.__falconDeliver = function (text) {
    var event = { data: text };
    listeners.slice().forEach(function (listener) { try { listener(event); } catch (e) {} });
  };
})();
)JS";

std::string fromNSString(NSString* text) {
    if (text == nil) return {};
    const char* utf8 = text.UTF8String;
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

bool startsWithNoCase(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && strncasecmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

// What a message handler and the host share: the C++ callback, and the way back into the page. The handler only holds a pointer to it (Impl owns it).
struct MacBridge {
    std::function<std::string(const std::string&)> onMessage;
    std::function<void(const std::string&)> deliver;
};

}  // namespace launcher

// The Objective-C code below is at the file's global scope; these are the launcher's helpers it calls by their plain names.
using falcon::launcher::fromNSString;
using falcon::launcher::startsWithNoCase;

// -------------------------------------------------------------------------------------------------------------------------------
// The Objective-C pieces (one file, so the C++ and the AppKit side can share the Bridge above without a header)

@class FalconSchemeHandler;
@class FalconNavigation;
@class FalconMessageHandler;
@class FalconWindowDelegate;

// A borderless window cannot become key by default; without that the page could not take a click or a key.
@interface FalconWindow : NSWindow
@end

@implementation FalconWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

// The page, answered from memory for falcon://app/index.html (and /): there is nothing else to serve.
@interface FalconSchemeHandler : NSObject <WKURLSchemeHandler> {
    falcon::launcher::Blob _page;
}
- (instancetype)initWithPage:(falcon::launcher::Blob)page;
@end

@implementation FalconSchemeHandler
- (instancetype)initWithPage:(falcon::launcher::Blob)page {
    self = [super init];
    if (self != nil) _page = page;
    return self;
}

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    NSURL* url = task.request.URL;
    NSString* urlPath = url.path;
    const std::string path = fromNSString(urlPath != nil && urlPath.length > 0 ? urlPath : @"/");
    if (!_page.empty() && (path == "/index.html" || path == "/")) {
        NSData* data = [NSData dataWithBytes:_page.data length:_page.size];
        NSURLResponse* response = [[NSURLResponse alloc] initWithURL:url MIMEType:@"text/html; charset=utf-8" expectedContentLength:static_cast<NSInteger>(_page.size) textEncodingName:@"utf-8"];
        [task didReceiveResponse:response];
        [task didReceiveData:data];
        [task didFinish];
    } else {
        NSError* error = [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil];
        [task didFailWithError:error];
    }
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    (void)task;
}
@end

// The navigations of the launcher's own view: the page may only be the launcher's own, links go to the system browser, and the end of the load is what
// "the page is ready" means.
@interface FalconNavigation : NSObject <WKNavigationDelegate> {
@public
    std::function<void()> onLoaded;
    bool isMain;
}
@end

@implementation FalconNavigation
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    (void)webView;
    (void)navigation;
    if (isMain && onLoaded) onLoaded();
}

- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView;
    (void)navigation;
    if (isMain) falcon::launcher::logLine(std::string("the page could not be loaded: ") + fromNSString(error.localizedDescription));
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView;
    (void)navigation;
    if (isMain) falcon::launcher::logLine(std::string("the page broke while loading: ") + fromNSString(error.localizedDescription));
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
    if (!isMain) return;
    falcon::launcher::logLine("the web process ended, the page is loaded again");
    [webView reload];
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    const std::string address = fromNSString(action.request.URL.absoluteString);
    if (isMain) {
        if (address.rfind(falcon::launcher::kOrigin, 0) == 0) {
            decisionHandler(WKNavigationActionPolicyAllow);
        } else {
            decisionHandler(WKNavigationActionPolicyCancel);
            if (falcon::launcher::isSafeWebUrl(address)) falcon::launcher::openInBrowser(address);
        }
        return;
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}
@end

// The sign-in window's view: the page may only be an https one (or about:blank, the self-test's), and the redirect address ends the sign-in.
@interface FalconSignInNavigation : NSObject <WKNavigationDelegate> {
@public
    std::string redirectPrefix;
    std::function<void(const std::string&)> onRedirect;
}
@end

@implementation FalconSignInNavigation
- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    const std::string address = fromNSString(action.request.URL.absoluteString);
    if (!redirectPrefix.empty() && startsWithNoCase(address, redirectPrefix)) {
        decisionHandler(WKNavigationActionPolicyCancel);
        auto callback = onRedirect;  // (not inside the decision: the window and its view go away in it)
        dispatch_async(dispatch_get_main_queue(), ^{
            if (callback) callback(address);
        });
        return;
    }
    if (action.navigationType == WKNavigationTypeLinkActivated || action.targetFrame == nil) {
        decisionHandler(WKNavigationActionPolicyCancel);
        if (falcon::launcher::isSafeWebUrl(address)) falcon::launcher::openInBrowser(address);
        return;
    }
    if (address.rfind("https://", 0) == 0 || address == "about:blank") {
        decisionHandler(WKNavigationActionPolicyAllow);
    } else {
        decisionHandler(WKNavigationActionPolicyCancel);
    }
}

- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView;
    (void)navigation;
    falcon::launcher::logLine(std::string("the sign-in page could not be loaded: ") + fromNSString(error.localizedDescription));
}
@end

// The page's messages, handled on the main thread and answered through MacBridge::deliver.
@interface FalconMessageHandler : NSObject <WKScriptMessageHandler> {
@public
    falcon::launcher::MacBridge* bridge;
}
@end

@implementation FalconMessageHandler
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    if (bridge == nullptr || !bridge->onMessage) return;
    const std::string request = fromNSString([message.body isKindOfClass:[NSString class]] ? static_cast<NSString*>(message.body) : [message.body description]);
    const std::string answer = bridge->onMessage(request);
    if (!answer.empty() && bridge->deliver) bridge->deliver(answer);
}
@end

// A window of the launcher's own said it is going away: the sign-in window calls back ("cancelled").
@interface FalconWindowDelegate : NSObject <NSWindowDelegate> {
@public
    std::function<void()> onClose;
}
@end

@implementation FalconWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (onClose) onClose();
}
@end

namespace launcher {

struct WebHost::Impl {
    __strong FalconWindow* window = nil;
    __strong WKWebView* view = nil;
    __strong FalconSchemeHandler* scheme = nil;
    __strong FalconNavigation* navigation = nil;
    __strong FalconMessageHandler* messages = nil;
    __strong FalconWindowDelegate* windowDelegate = nil;
    MacBridge bridge;
    bool closing = false;
    bool stopped = false;

    struct SignIn {
        __strong NSWindow* window = nil;
        __strong WKWebView* view = nil;
        __strong FalconSignInNavigation* navigation = nil;
        __strong FalconWindowDelegate* windowDelegate = nil;
        std::string redirectPrefix;
        SignInDone done;
    };
    std::unique_ptr<SignIn> signIn;
};

WebHost::WebHost(WebHostOptions options, Callbacks callbacks, Blob page) : options_(std::move(options)), callbacks_(std::move(callbacks)), page_(page), impl_(std::make_unique<Impl>()) {}

WebHost::~WebHost() = default;

void WebHost::runOnUi(std::function<void()> fn) {
    auto function = std::make_shared<std::function<void()>>(std::move(fn));
    dispatch_async(dispatch_get_main_queue(), ^{
        (*function)();
    });
}

// A minimal main menu, so Cmd+Q and "Quit Falcon Launcher" exist as they do in every Mac program.
static void installMainMenu() {
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* applicationItem = [[NSMenuItem alloc] init];
    [menuBar addItem:applicationItem];
    NSMenu* applicationMenu = [[NSMenu alloc] init];
    NSString* title = [[NSProcessInfo processInfo] processName];
    [applicationMenu addItemWithTitle:[@"Hide " stringByAppendingString:title] action:@selector(hide:) keyEquivalent:@"h"];
    [applicationMenu addItem:[NSMenuItem separatorItem]];
    [applicationMenu addItemWithTitle:[@"Quit " stringByAppendingString:title] action:@selector(terminate:) keyEquivalent:@"q"];
    applicationItem.submenu = applicationMenu;
    [NSApp setMainMenu:menuBar];
}

bool WebHost::create() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        if (options_.offscreen) {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];  // (no Dock icon, no menu bar: a test run)
        } else {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            installMainMenu();
        }
        [NSApp finishLaunching];

        impl_->bridge.onMessage = callbacks_.onMessage;
        impl_->bridge.deliver = [this](const std::string& text) { deliverToPage(text); };

        impl_->window = [[FalconWindow alloc] initWithContentRect:NSMakeRect(0, 0, options_.clientWidth, options_.clientHeight)
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        impl_->window.title = narrow(options_.title).empty() ? @"Falcon Launcher" : [NSString stringWithUTF8String:narrow(options_.title).c_str()];
        impl_->window.releasedWhenClosed = NO;
        impl_->window.movableByWindowBackground = NO;
        impl_->window.backgroundColor = [NSColor colorWithSRGBRed:13.0 / 255.0 green:17.0 / 255.0 blue:23.0 / 255.0 alpha:1.0];
        [impl_->window center];

        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];  // (nothing is kept: the page needs no storage)
        impl_->scheme = [[FalconSchemeHandler alloc] initWithPage:page_];
        [configuration setURLSchemeHandler:impl_->scheme forURLScheme:@"falcon"];

        WKUserContentController* content = configuration.userContentController;
        WKUserScript* shim = [[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:kShim]
                                                       injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                    forMainFrameOnly:YES];
        [content addUserScript:shim];
        impl_->messages = [[FalconMessageHandler alloc] init];
        impl_->messages->bridge = &impl_->bridge;
        [content addScriptMessageHandler:impl_->messages name:@"falcon"];

        impl_->view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, options_.clientWidth, options_.clientHeight) configuration:configuration];
        impl_->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        if ([impl_->view respondsToSelector:@selector(setInspectable:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
            [impl_->view performSelector:@selector(setInspectable:) withObject:(options_.devtools ? @YES : @NO)];
#pragma clang diagnostic pop
        }
        if (@available(macOS 12.0, *)) {
            impl_->view.underPageBackgroundColor = impl_->window.backgroundColor;
        }
        impl_->navigation = [[FalconNavigation alloc] init];
        impl_->navigation->isMain = true;
        impl_->navigation->onLoaded = [this] {
            if (loaded_.exchange(true)) return;
            logLine("the page was loaded after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
            if (callbacks_.onPageLoaded) callbacks_.onPageLoaded();
        };
        impl_->view.navigationDelegate = impl_->navigation;
        impl_->window.contentView = impl_->view;

        const Blob icon = embeddedResource(L"FALCON_ICON");
        if (!icon.empty()) {
            NSData* data = [NSData dataWithBytes:icon.data length:icon.size];
            NSImage* image = [[NSImage alloc] initWithData:data];
            if (image != nil) [NSApp setApplicationIconImage:image];
        }

        if (options_.offscreen) {
            [impl_->window setFrameOrigin:NSMakePoint(-20000, -20000)];  // (off every screen, and never key or focused)
            [impl_->window orderFrontRegardless];                        // (the web view still needs a window to lay out and draw in)
        } else {
            [impl_->window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
        logLine("navigating after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
        std::string address = std::string(kOrigin) + "index.html" + (options_.startPage.empty() ? "" : "#" + narrow(options_.startPage));
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:address.c_str()]];
        if (url == nil) {
            logLine("the launcher's own address could not be made");
            return false;
        }
        [impl_->view loadRequest:[NSURLRequest requestWithURL:url]];
    }
    return true;
}

int WebHost::run() {
    @autoreleasepool {
        [NSApp run];
    }
    return 0;
}

void WebHost::deliverToPage(const std::string& message) {
    if (impl_->view == nil) return;
    // The message as a JavaScript string literal (JSON is one, with every character outside ASCII written as \u escapes).
    const std::string literal = nlohmann::json(message).dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
    const std::string script = "if (window.__falconDeliver) window.__falconDeliver(" + literal + ");";
    [impl_->view evaluateJavaScript:[NSString stringWithUTF8String:script.c_str()] completionHandler:nil];
}

void WebHost::post(const std::string& message) {
    runOnUi([this, message] { deliverToPage(message); });
}

void WebHost::setGameRunning(bool running) {
    runOnUi([this, running] {
        if (impl_->window == nil) return;
        if (running) {
            [impl_->window miniaturize:nil];
        } else {
            [impl_->window deminiaturize:nil];
            if (!options_.offscreen) [impl_->window makeKeyAndOrderFront:nil];
        }
    });
}

// Ends the main loop once: the stop is only noticed when an event comes, so one is posted.
static void stopApplication() {
    [NSApp stop:nil];
    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:NO];
}

void WebHost::close() {
    runOnUi([this] {
        if (impl_->closing) return;
        impl_->closing = true;
        finishSignIn("", "cancelled");
        if (impl_->window != nil) [impl_->window close];
        impl_->window = nil;
        impl_->view = nil;
        stopApplication();
    });
}

void WebHost::minimize() {
    runOnUi([this] {
        if (impl_->window != nil) [impl_->window miniaturize:nil];
    });
}

void WebHost::startDrag() {
    // The page's title bar calls this while its mouse-down is being handled. WKWebView does not implement WebView2's window drag, so the window is
    // moved here: the mouse's travel is read in a tracking loop of its own until the button comes up (only ever entered while it is down).
    runOnUi([this] {
        if ((NSEvent.pressedMouseButtons & 1) == 0) return;
        NSWindow* window = impl_->window;
        if (window == nil) return;
        const NSPoint start = NSEvent.mouseLocation;
        const NSPoint origin = window.frame.origin;
        for (;;) {
            NSEvent* next = [NSApp nextEventMatchingMask:NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged
                                               untilDate:[NSDate distantFuture]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES];
            if (next == nil || next.type == NSEventTypeLeftMouseUp) break;
            const NSPoint now = NSEvent.mouseLocation;
            [window setFrameOrigin:NSMakePoint(origin.x + (now.x - start.x), origin.y + (now.y - start.y))];
        }
    });
}

void WebHost::after(unsigned milliseconds, std::function<void()> fn) {
    auto function = std::make_shared<std::function<void()>>(std::move(fn));
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(milliseconds) * 1000000), dispatch_get_main_queue(), ^{
        (*function)();
    });
}

void WebHost::capture(const fs::path& file, std::function<void(bool)> done) {
    if (impl_->view == nil) {
        done(false);
        return;
    }
    auto answer = std::make_shared<std::function<void(bool)>>(std::move(done));
    const std::string path = file.string();
    [impl_->view takeSnapshotWithConfiguration:nil
                             completionHandler:^(NSImage* image, NSError* error) {
                                 bool ok = false;
                                 if (image != nil && error == nil) {
                                     NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:[image TIFFRepresentation]];
                                     if (rep != nil) {
                                         NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                                         if (png != nil) ok = [png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
                                     }
                                 }
                                 if (!ok && error != nil) logLine("the screenshot could not be taken: " + fromNSString(error.localizedDescription));
                                 (*answer)(ok);
                             }];
}

void WebHost::beginSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done) {
    runOnUi([this, startUrl, redirectPrefix, done = std::move(done)]() mutable { openSignIn(startUrl, redirectPrefix, std::move(done)); });
}

void WebHost::cancelSignIn() {
    runOnUi([this] { finishSignIn("", "cancelled"); });
}

void WebHost::openSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done) {
    if (impl_->signIn) finishSignIn("", "cancelled");  // (one at a time: an older window is dismissed)
    if (impl_->window == nil) {
        if (done) done("", "there is no window");
        return;
    }
    auto sign = std::make_unique<Impl::SignIn>();
    sign->redirectPrefix = redirectPrefix;
    sign->done = std::move(done);

    // A fixed-size window of the usual kind, with its title: it is Microsoft's page, and the player should see that it is a separate window.
    sign->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 480, 700)
                                               styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    sign->window.title = @"Sign in to your Microsoft account";
    sign->window.releasedWhenClosed = NO;
    [sign->window center];

    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];  // (private browsing: "use another account" really starts fresh)
    sign->view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 480, 700) configuration:configuration];
    sign->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    sign->navigation = [[FalconSignInNavigation alloc] init];
    sign->navigation->redirectPrefix = redirectPrefix;
    sign->navigation->onRedirect = [this](const std::string& address) { finishSignIn(address, ""); };
    sign->view.navigationDelegate = sign->navigation;
    sign->window.contentView = sign->view;
    // Closing it is the player saying no.
    sign->windowDelegate = [[FalconWindowDelegate alloc] init];
    sign->windowDelegate->onClose = [this] { finishSignIn("", "cancelled"); };
    sign->window.delegate = sign->windowDelegate;

    if (options_.offscreen) {
        [sign->window setFrameOrigin:NSMakePoint(-20000, -20000)];
    } else {
        [sign->window makeKeyAndOrderFront:nil];
    }
    impl_->signIn = std::move(sign);
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:startUrl.c_str()]];
    if (url == nil) {
        finishSignIn("", "the sign-in address is not a valid address");
        return;
    }
    [impl_->signIn->view loadRequest:[NSURLRequest requestWithURL:url]];
}

// Ends the sign-in: the window goes away and the answer is given, once.
void WebHost::finishSignIn(const std::string& url, const std::string& error) {
    if (!impl_->signIn) return;
    std::unique_ptr<Impl::SignIn> sign = std::move(impl_->signIn);
    sign->view.navigationDelegate = nil;
    sign->window.delegate = nil;
    [sign->window close];
    if (impl_->window != nil && !options_.offscreen) [impl_->window makeKeyAndOrderFront:nil];
    if (sign->done) sign->done(url, error);
}

}  // namespace launcher

