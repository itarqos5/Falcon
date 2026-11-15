#include "webview_host_gtk.hpp"

#include "util.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <nlohmann/json.hpp>

#include <strings.h>

#include <algorithm>
#include <cstring>

namespace launcher {

namespace fs = std::filesystem;

namespace {

constexpr const char* kOrigin = "falcon://app/";
constexpr GdkRGBA kBackground = {13.0 / 255.0, 17.0 / 255.0, 23.0 / 255.0, 1.0};

// Put in front of the page: the two names the UI page talks to the launcher with (window.chrome.webview, as under WebView2), and the function the host calls to hand it a message.
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

// A function handed to the main loop, run once there.
gboolean runFunction(gpointer data) {
    auto* fn = static_cast<std::function<void()>*>(data);
    (*fn)();
    delete fn;
    return G_SOURCE_REMOVE;
}

bool startsWithNoCase(const std::string& text, const std::string& prefix) { return text.size() >= prefix.size() && strncasecmp(text.c_str(), prefix.c_str(), prefix.size()) == 0; }

// The window's icon, from the picture that is in the program: what an X11 task bar shows from the first moment (a Wayland desktop takes it from the application menu entry that setup adds).
void setWindowIcon(GtkWindow* window) {
    gtk_window_set_icon_name(window, "falcon-launcher");  // (the entry's icon, when setup has installed it)
    const Blob icon = embeddedResource(L"FALCON_ICON");
    if (icon.empty()) return;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (gdk_pixbuf_loader_write(loader, icon.data, icon.size, nullptr) && gdk_pixbuf_loader_close(loader, nullptr)) {
        if (GdkPixbuf* picture = gdk_pixbuf_loader_get_pixbuf(loader)) gtk_window_set_icon(window, picture);
    }
    g_object_unref(loader);
}

}  // namespace

struct WebHost::Impl {
    GtkWidget* window = nullptr;
    GtkWidget* view = nullptr;
    WebKitUserContentManager* content = nullptr;
    // The last mouse press on the page: what begin_move_drag needs to move a window that has no title bar.
    int pressButton = 1;
    double pressRootX = 0.0, pressRootY = 0.0;
    guint32 pressTime = 0;

    struct SignIn {
        GtkWidget* window = nullptr;
        GtkWidget* view = nullptr;
        std::string redirectPrefix;
        SignInDone done;
    };
    std::unique_ptr<SignIn> signIn;
};

WebHost::WebHost(WebHostOptions options, Callbacks callbacks, Blob page) : options_(std::move(options)), callbacks_(std::move(callbacks)), page_(page), impl_(std::make_unique<Impl>()) {}

WebHost::~WebHost() = default;

void WebHost::runOnUi(std::function<void()> fn) { g_idle_add(runFunction, new std::function<void()>(std::move(fn))); }

bool WebHost::create() {
    if (!gtk_init_check(nullptr, nullptr)) {
        logLine("there is no display to open the window on (GTK could not start)");
        return false;
    }
    g_set_prgname("falcon-launcher");  // (the name a desktop matches the window to the application menu entry by: the window class on X11, the app id on Wayland)
    g_set_application_name("Falcon Launcher");

    impl_->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWindow* window = GTK_WINDOW(impl_->window);
    gtk_window_set_title(window, narrow(options_.title).c_str());
    setWindowIcon(window);
    gtk_window_set_default_size(window, options_.clientWidth, options_.clientHeight);
    gtk_window_set_resizable(window, FALSE);
    gtk_window_set_decorated(window, FALSE);  // (the page draws its own title bar, as in the Windows launcher)
    gtk_window_set_position(window, GTK_WIN_POS_CENTER);
    if (options_.offscreen) {
        gtk_window_set_accept_focus(window, FALSE);
        gtk_window_set_focus_on_map(window, FALSE);
        gtk_window_set_skip_taskbar_hint(window, TRUE);
        gtk_window_move(window, -20000, -20000);
    }
    g_signal_connect(impl_->window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
                         auto* self = static_cast<WebHost*>(data);
                         self->finishSignIn("", "cancelled");  // (a sign-in window still open goes with the launcher)
                         gtk_main_quit();
                     }),
                     this);

    // The page may only be the launcher's own, and the profile is not kept on disk: everything the page needs is in the program, and nothing it does needs to be remembered.
    WebKitWebContext* context = webkit_web_context_new_ephemeral();
    webkit_web_context_register_uri_scheme(
        context, "falcon",
        +[](WebKitURISchemeRequest* request, gpointer data) {
            auto* self = static_cast<WebHost*>(data);
            const char* path = webkit_uri_scheme_request_get_path(request);
            if (path != nullptr && (std::strcmp(path, "/index.html") == 0 || std::strcmp(path, "/") == 0) && !self->page_.empty()) {
                gpointer copy = g_malloc(self->page_.size);
                std::memcpy(copy, self->page_.data, self->page_.size);
                GInputStream* stream = g_memory_input_stream_new_from_data(copy, static_cast<gssize>(self->page_.size), g_free);
                webkit_uri_scheme_request_finish(request, stream, static_cast<gint64>(self->page_.size), "text/html; charset=utf-8");
                g_object_unref(stream);
            } else {
                GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Not found");
                webkit_uri_scheme_request_finish_error(request, error);
                g_error_free(error);
            }
        },
        this, nullptr);

    impl_->content = webkit_user_content_manager_new();
    WebKitUserScript* shim = webkit_user_script_new(kShim, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(impl_->content, shim);
    webkit_user_script_unref(shim);
    webkit_user_content_manager_register_script_message_handler(impl_->content, "falcon");
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_signal_connect(impl_->content, "script-message-received::falcon", G_CALLBACK(+[](WebKitUserContentManager*, WebKitJavascriptResult* result, gpointer data) {
                         auto* self = static_cast<WebHost*>(data);
                         JSCValue* value = webkit_javascript_result_get_js_value(result);
                         if (!jsc_value_is_string(value) || !self->callbacks_.onMessage) return;
                         char* text = jsc_value_to_string(value);
                         const std::string request = text != nullptr ? text : "";
                         g_free(text);
                         const std::string answer = self->callbacks_.onMessage(request);
                         if (!answer.empty()) self->post(answer);
                     }),
                     this);
    G_GNUC_END_IGNORE_DEPRECATIONS

    impl_->view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", context, "user-content-manager", impl_->content, nullptr));
    g_object_unref(context);  // (the view holds it)
    WebKitWebView* view = WEBKIT_WEB_VIEW(impl_->view);
    webkit_web_view_set_background_color(view, &kBackground);
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_developer_extras(settings, options_.devtools);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, options_.devtools);
    webkit_settings_set_enable_site_specific_quirks(settings, FALSE);

    if (!options_.devtools) {
        g_signal_connect(impl_->view, "context-menu", G_CALLBACK(+[](WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) -> gboolean { return TRUE; }), nullptr);
    }
    // Nothing but the launcher's own page: links go to the system browser (only https ones).
    g_signal_connect(impl_->view, "decide-policy",
                     G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer) -> gboolean {
                         if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) return FALSE;
                         WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
                         const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
                         const std::string address = uri != nullptr ? uri : "";
                         if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && address.rfind(kOrigin, 0) == 0) {
                             webkit_policy_decision_use(decision);
                         } else {
                             webkit_policy_decision_ignore(decision);
                             if (isSafeWebUrl(address)) openInBrowser(address);
                         }
                         return TRUE;
                     }),
                     nullptr);
    g_signal_connect(impl_->view, "load-changed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent event, gpointer data) {
                         auto* self = static_cast<WebHost*>(data);
                         if (event != WEBKIT_LOAD_FINISHED || self->loaded_.exchange(true)) return;
                         logLine("the page was loaded after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
                         if (self->callbacks_.onPageLoaded) self->callbacks_.onPageLoaded();
                     }),
                     this);
    // If the web process dies the page is simply loaded again.
    g_signal_connect(impl_->view, "web-process-terminated", G_CALLBACK(+[](WebKitWebView* v, WebKitWebProcessTerminationReason reason, gpointer data) {
                         auto* self = static_cast<WebHost*>(data);
                         logLine("the web process ended, reason " + std::to_string(static_cast<int>(reason)));
                         self->loaded_ = false;
                         webkit_web_view_reload(v);
                     }),
                     this);
    // What a frameless window needs to be moved: where the last press was.
    gtk_widget_add_events(impl_->view, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(impl_->view, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
                         auto* self = static_cast<WebHost*>(data);
                         self->impl_->pressButton = static_cast<int>(event->button);
                         self->impl_->pressRootX = event->x_root;
                         self->impl_->pressRootY = event->y_root;
                         self->impl_->pressTime = event->time;
                         return FALSE;  // (the page gets the press as well)
                     }),
                     this);

    gtk_container_add(GTK_CONTAINER(impl_->window), impl_->view);
    gtk_widget_show_all(impl_->window);
    logLine("navigating after " + std::to_string(static_cast<int>(millisSinceStart())) + " ms");
    const std::string address = std::string(kOrigin) + "index.html" + (options_.startPage.empty() ? "" : "#" + narrow(options_.startPage));
    webkit_web_view_load_uri(view, address.c_str());
    return true;
}

int WebHost::run() {
    gtk_main();
    return 0;
}

void WebHost::post(const std::string& message) {
    runOnUi([this, message] {
        if (impl_->view == nullptr) return;
        // The message as a JavaScript string literal (JSON is one, with every character outside ASCII written as \u escapes).
        const std::string literal = nlohmann::json(message).dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
        const std::string script = "if (window.__falconDeliver) window.__falconDeliver(" + literal + ");";
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(impl_->view), script.c_str(), nullptr, nullptr, nullptr);
        G_GNUC_END_IGNORE_DEPRECATIONS
    });
}

void WebHost::setGameRunning(bool running) {
    runOnUi([this, running] {
        if (impl_->window == nullptr) return;
        if (running) {
            gtk_window_iconify(GTK_WINDOW(impl_->window));
        } else {
            gtk_window_deiconify(GTK_WINDOW(impl_->window));
            if (!options_.offscreen) gtk_window_present(GTK_WINDOW(impl_->window));
        }
    });
}

void WebHost::close() {
    runOnUi([this] {
        if (impl_->window != nullptr) gtk_widget_destroy(impl_->window);  // (its "destroy" ends the main loop)
        impl_->window = nullptr;
        impl_->view = nullptr;
    });
}

void WebHost::minimize() {
    runOnUi([this] {
        if (impl_->window != nullptr) gtk_window_iconify(GTK_WINDOW(impl_->window));
    });
}

void WebHost::startDrag() {
    runOnUi([this] {
        if (impl_->window == nullptr) return;
        gtk_window_begin_move_drag(GTK_WINDOW(impl_->window), impl_->pressButton, static_cast<gint>(impl_->pressRootX), static_cast<gint>(impl_->pressRootY), impl_->pressTime);
    });
}

void WebHost::after(unsigned milliseconds, std::function<void()> fn) { g_timeout_add(milliseconds, runFunction, new std::function<void()>(std::move(fn))); }

void WebHost::capture(const fs::path& file, std::function<void(bool)> done) {
    if (impl_->view == nullptr) {
        done(false);
        return;
    }
    struct Request {
        fs::path file;
        std::function<void(bool)> done;
    };
    webkit_web_view_get_snapshot(
        WEBKIT_WEB_VIEW(impl_->view), WEBKIT_SNAPSHOT_REGION_VISIBLE, WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer data) {
            std::unique_ptr<Request> request(static_cast<Request*>(data));
            GError* error = nullptr;
            cairo_surface_t* surface = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), result, &error);
            bool ok = false;
            if (surface != nullptr) {
                ok = cairo_surface_write_to_png(surface, request->file.c_str()) == CAIRO_STATUS_SUCCESS;
                cairo_surface_destroy(surface);
            }
            if (error != nullptr) g_error_free(error);
            request->done(ok);
        },
        new Request{file, std::move(done)});
}

void WebHost::beginSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done) {
    runOnUi([this, startUrl, redirectPrefix, done = std::move(done)]() mutable { openSignIn(startUrl, redirectPrefix, std::move(done)); });
}

void WebHost::cancelSignIn() {
    runOnUi([this] { finishSignIn("", "cancelled"); });
}

void WebHost::openSignIn(const std::string& startUrl, const std::string& redirectPrefix, SignInDone done) {
    if (impl_->signIn) finishSignIn("", "cancelled");  // (one at a time: an older window is dismissed)
    if (impl_->window == nullptr) {
        if (done) done("", "there is no window");
        return;
    }
    auto sign = std::make_unique<Impl::SignIn>();
    sign->redirectPrefix = redirectPrefix;
    sign->done = std::move(done);

    // A fixed-size window of the usual kind (with its title: it is Microsoft's page, the player should see it is a separate window), over the launcher.
    sign->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWindow* window = GTK_WINDOW(sign->window);
    gtk_window_set_title(window, "Sign in to your Microsoft account");
    setWindowIcon(window);
    gtk_window_set_default_size(window, 480, 700);
    gtk_window_set_resizable(window, FALSE);
    gtk_window_set_transient_for(window, GTK_WINDOW(impl_->window));
    gtk_window_set_position(window, GTK_WIN_POS_CENTER_ON_PARENT);
    if (options_.offscreen) {
        gtk_window_set_accept_focus(window, FALSE);
        gtk_window_set_focus_on_map(window, FALSE);
        gtk_window_set_skip_taskbar_hint(window, TRUE);
        gtk_window_move(window, -20000, -20000);
    }
    // Closing it is the player saying no.
    g_signal_connect(sign->window, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
                         static_cast<WebHost*>(data)->finishSignIn("", "cancelled");
                         return TRUE;
                     }),
                     this);

    // Private browsing: a web context with nothing on disk, so "use another account" really starts fresh and the launcher's profile never holds a login.
    WebKitWebContext* context = webkit_web_context_new_ephemeral();
    sign->view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", context, nullptr));
    g_object_unref(context);
    WebKitWebView* view = WEBKIT_WEB_VIEW(sign->view);
    webkit_web_view_set_background_color(view, &kBackground);
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_developer_extras(settings, options_.devtools);
    // The sign-in is over when the page goes to the redirect address: the navigation is stopped and the address (with the code) is handed on. Nothing but https pages may be shown;
    // links out of the page (help, privacy) open in the browser.
    g_signal_connect(sign->view, "decide-policy", G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer data) -> gboolean {
                         auto* self = static_cast<WebHost*>(data);
                         if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) return FALSE;
                         WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
                         const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
                         const std::string address = uri != nullptr ? uri : "";
                         if (!self->impl_->signIn) return FALSE;
                         if (startsWithNoCase(address, self->impl_->signIn->redirectPrefix)) {
                             webkit_policy_decision_ignore(decision);
                             // (not from inside the signal: the window and its web view are taken down after it)
                             self->runOnUi([self, address] { self->finishSignIn(address, ""); });
                         } else if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
                             webkit_policy_decision_ignore(decision);
                             if (isSafeWebUrl(address)) openInBrowser(address);
                         } else if (address.rfind("https://", 0) == 0 || address == "about:blank") {
                             webkit_policy_decision_use(decision);
                         } else {
                             webkit_policy_decision_ignore(decision);
                         }
                         return TRUE;
                     }),
                     this);
    if (!options_.devtools) {
        g_signal_connect(sign->view, "context-menu", G_CALLBACK(+[](WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) -> gboolean { return TRUE; }), nullptr);
    }
    gtk_container_add(GTK_CONTAINER(sign->window), sign->view);
    gtk_widget_show_all(sign->window);
    if (!options_.offscreen) gtk_window_present(window);
    impl_->signIn = std::move(sign);
    webkit_web_view_load_uri(view, startUrl.c_str());
}

// Ends the sign-in: the window goes away and the answer is given, once.
void WebHost::finishSignIn(const std::string& url, const std::string& error) {
    if (!impl_->signIn) return;
    std::unique_ptr<Impl::SignIn> sign = std::move(impl_->signIn);
    if (sign->window != nullptr) gtk_widget_destroy(sign->window);
    if (impl_->window != nullptr && !options_.offscreen) gtk_window_present(GTK_WINDOW(impl_->window));
    if (sign->done) sign->done(url, error);
}

}  // namespace launcher
