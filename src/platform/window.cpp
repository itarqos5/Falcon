#include "falcon/platform/window.hpp"

#include "falcon/gfx/gl.hpp"
#include "falcon/platform/dialog.hpp"
#ifdef FALCON_HAS_VULKAN
#include "falcon/vulkan/vulkan_backend.hpp"
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <timeapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <stdexcept>

namespace falcon {
namespace {

void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "[glfw] error %d: %s\n", code, description);
}

// Gives the window a dark title bar and dark caption buttons (Windows 10 version 1809 and later, and 11), to
// match the dark menus, whatever the system's own colour theme is. Older Windows ignores the request.
#ifdef _WIN32
// Borderless windows (--borderless): the window has no frame, so there is nothing to grab. The top edge answers Windows' "what is under the
// mouse" with "the title bar", which lets the person move the window by it. Not while the window is fullscreen.
WNDPROC g_originalProc = nullptr;
bool g_dragStrip = false;

LRESULT CALLBACK borderlessProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST && g_dragStrip) {
        const LRESULT hit = CallWindowProcW(g_originalProc, hwnd, message, wParam, lParam);
        if (hit == HTCLIENT) {
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            const int y = static_cast<short>((static_cast<unsigned long long>(lParam) >> 16) & 0xFFFF);
            if (y >= rect.top && y < rect.top + MulDiv(GetSystemMetrics(SM_CYCAPTION), 5, 4)) return HTCAPTION;  // (about 29 px at 100%, more on a scaled display)
        }
        return hit;
    }
    return CallWindowProcW(g_originalProc, hwnd, message, wParam, lParam);
}

void makeBorderless(HWND hwnd) {
    g_originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&borderlessProc)));
    g_dragStrip = true;
    const MARGINS margins{1, 1, 1, 1};  // (keeps the window's shadow, which a frameless window would otherwise lose)
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    const DWORD rounded = 2;  // DWMWCP_ROUND: Windows 11 rounds the corners
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &rounded, sizeof(rounded));
}
#endif

void useDarkTitleBar([[maybe_unused]] GLFWwindow* window) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) return;
    const BOOL dark = TRUE;
    // The attribute number is 20 from Windows 10 build 18985 on; builds 17763-18362 used 19 for the same thing.
    constexpr DWORD kImmersiveDarkMode = 20;
    constexpr DWORD kImmersiveDarkModeBefore20H1 = 19;
    if (FAILED(DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, kImmersiveDarkModeBefore20H1, &dark, sizeof(dark));
    }
#endif
}

}  // namespace

Window::Window(int width, int height, const std::string& title, bool background, bool startHidden, bool borderless, GraphicsApi api)
    : background_(background), borderless_(borderless && !background) {
    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() == GLFW_FALSE) throw std::runtime_error("failed to initialise GLFW");
#ifdef _WIN32
    // Windows wakes sleeping programs only every 15.6 ms unless asked for better, which would turn a 16 ms frame
    // pause into 31 ms and make the frame rate limit wrong. Ask for 1 ms for as long as the window exists.
    timeBeginPeriod(1);
#endif

#if defined(__linux__)
    // What a desktop matches the window to its launcher entry (and so its icon and its place in the task bar) by: the window class on X11, the app id on Wayland.
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "Falcon");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "falcon");
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "falcon");
#endif
    if (startHidden && !background) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (borderless_) glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    if (background) {  // never appears, never takes focus from the person's game
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    handle_ = nullptr;
    // Vulkan, when it was chosen and this PC can do it: a window without an OpenGL context, with the Vulkan renderer put in the place of OpenGL (src/vulkan/). Anything that goes
    // wrong makes an OpenGL window instead, and the reason is kept for the log.
    if (api == GraphicsApi::Vulkan) {
#ifdef FALCON_HAS_VULKAN
        std::string reason;
        if (!vulkan::usable(&reason)) {
            apiNote_ = "vulkan cannot be used (" + reason + "): drawing with opengl";
        } else {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            handle_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
            if (handle_ != nullptr && vulkan::start(handle_, !background, !background, reason)) {
                api_ = GraphicsApi::Vulkan;
            } else {
                if (handle_ != nullptr) glfwDestroyWindow(handle_);
                handle_ = nullptr;
                apiNote_ = "vulkan could not be started (" + reason + "): drawing with opengl";
            }
        }
#else
        apiNote_ = "this build has no vulkan renderer: drawing with opengl";
#endif
    }

    if (handle_ == nullptr) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        // OpenGL 4.5 first: its multi-draw-indirect lets the world be drawn in a few calls instead of thousands. Everything the game uses is
        // part of 3.3 core, so a graphics driver that cannot do 4.5 gets a 3.3 context and the game draws sections one by one (FALCON_GL33
        // asks for that on purpose, to compare the two).
        static const bool only33 = std::getenv("FALCON_GL33") != nullptr;
        for (const auto& [major, minor] : {std::pair<int, int>{4, 5}, std::pair<int, int>{3, 3}}) {
            if (only33 && major != 3) continue;
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
            handle_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
            if (handle_ != nullptr) break;
        }
        if (handle_ == nullptr) {
            glfwTerminate();
            throw std::runtime_error("failed to create a window with an OpenGL 3.3 core context");
        }
    }
    useDarkTitleBar(handle_);  // before the window is first shown, so the title bar never flashes light
#ifdef _WIN32
    if (borderless_) makeBorderless(glfwGetWin32Window(handle_));
#endif
    if (api_ == GraphicsApi::OpenGL) {
        glfwMakeContextCurrent(handle_);
        glfwSwapInterval(background ? 0 : 1);
        gl::load();
    }

    glfwSetWindowUserPointer(handle_, this);
    glfwSetMouseButtonCallback(handle_, [](GLFWwindow* w, int button, int action, int) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self != nullptr && self->onMouseButton) self->onMouseButton(button, action);
    });

    glfwSetKeyCallback(handle_, [](GLFWwindow* w, int key, int, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self != nullptr && self->onKey) self->onKey(key, action, mods);
    });
    glfwSetCharCallback(handle_, [](GLFWwindow* w, unsigned int codePoint) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self != nullptr && self->onChar) self->onChar(codePoint);
    });
    glfwSetScrollCallback(handle_, [](GLFWwindow* w, double xOffset, double yOffset) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self != nullptr && self->onScroll) self->onScroll(xOffset, yOffset);
    });

    cursors_[static_cast<std::size_t>(CursorType::Default)] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    cursors_[static_cast<std::size_t>(CursorType::PointingHand)] =
        glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
    cursors_[static_cast<std::size_t>(CursorType::NotAllowed)] =
        glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
    cursors_[static_cast<std::size_t>(CursorType::ResizeHorizontal)] =
        glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
    cursors_[static_cast<std::size_t>(CursorType::ResizeVertical)] =
        glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
    cursors_[static_cast<std::size_t>(CursorType::Text)] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
}

void Window::show() {
    if (!background_ && handle_ != nullptr) glfwShowWindow(handle_);
}

void Window::setFullscreen(bool fullscreen) {
    if (background_ || fullscreen == fullscreen_) return;
    if (fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (mode == nullptr) return;
        glfwGetWindowPos(handle_, &windowedX_, &windowedY_);
        glfwGetWindowSize(handle_, &windowedWidth_, &windowedHeight_);
        glfwSetWindowMonitor(handle_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(handle_, nullptr, windowedX_, windowedY_, std::max(windowedWidth_, 1),
                             std::max(windowedHeight_, 1), 0);
    }
    fullscreen_ = fullscreen;
#ifdef _WIN32
    if (borderless_) g_dragStrip = !fullscreen;  // (no title bar to grab while the window is fullscreen)
#endif
    if (api_ == GraphicsApi::Vulkan) {
#ifdef FALCON_HAS_VULKAN
        vulkan::resized();  // (the swapchain is made again for the new window)
#endif
    } else {
        glfwSwapInterval(vsync_ ? 1 : 0);  // a new surface forgets the swap interval
    }
}

void Window::setVsync(bool vsync) {
    vsync_ = vsync;
    if (background_) return;
    if (api_ == GraphicsApi::Vulkan) {
#ifdef FALCON_HAS_VULKAN
        vulkan::setVsync(vsync);
#endif
    } else {
        glfwSwapInterval(vsync ? 1 : 0);
    }
}

std::vector<std::string> Window::videoModes() const {
    std::vector<std::string> labels;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) return labels;
    int count = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
    for (int i = 0; i < count; ++i) {
        char label[64];
        std::snprintf(label, sizeof(label), "%dx%d@%d (%dbit)", modes[i].width, modes[i].height, modes[i].refreshRate,
                      modes[i].redBits + modes[i].greenBits + modes[i].blueBits);
        labels.emplace_back(label);
    }
    return labels;
}

Window::~Window() {
    for (GLFWcursor* cursor : cursors_) {
        if (cursor != nullptr) glfwDestroyCursor(cursor);
    }
#ifdef FALCON_HAS_VULKAN
    if (api_ == GraphicsApi::Vulkan) vulkan::stop();  // (before the window it shows frames in goes)
#endif
    if (handle_ != nullptr) glfwDestroyWindow(handle_);
    glfwTerminate();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

bool Window::shouldClose() const { return glfwWindowShouldClose(handle_) != 0; }
void Window::requestClose() { glfwSetWindowShouldClose(handle_, GLFW_TRUE); }
void Window::pollEvents() { glfwPollEvents(); }
void Window::swapBuffers() {
    if (api_ == GraphicsApi::Vulkan) {
#ifdef FALCON_HAS_VULKAN
        vulkan::present();
#endif
        return;
    }
    glfwSwapBuffers(handle_);
}

void Window::framebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(handle_, &width, &height);
}

void Window::windowSize(int& width, int& height) const { glfwGetWindowSize(handle_, &width, &height); }

void Window::cursorPosition(double& x, double& y) const { glfwGetCursorPos(handle_, &x, &y); }

void Window::openFolder(const std::string& path) { openFolderInFileManager(path); }

bool Window::shiftDown() const {
    return glfwGetKey(handle_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
           glfwGetKey(handle_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

bool Window::keyDown(int glfwKey) const {
    return glfwGetKey(handle_, glfwKey) == GLFW_PRESS || (glfwKey >= 0 && glfwKey < static_cast<int>(simKeys_.size()) && simKeys_[static_cast<std::size_t>(glfwKey)]);
}

bool Window::mouseDown(int glfwButton) const {
    return glfwGetMouseButton(handle_, glfwButton) == GLFW_PRESS || (glfwButton >= 0 && glfwButton < static_cast<int>(simButtons_.size()) && simButtons_[static_cast<std::size_t>(glfwButton)]);
}

void Window::setCursorCaptured(bool captured) {
    if (background_) captured = false;  // a hidden window must never take the cursor away from whatever you are doing
    if (captured == captured_) return;
    captured_ = captured;
    haveLastCursor_ = false;  // the next delta starts from here
    glfwSetInputMode(handle_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
}

void Window::takeCursorDelta(double& dx, double& dy) {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(handle_, &x, &y);
    dx = haveLastCursor_ ? x - lastCursorX_ : 0.0;
    dy = haveLastCursor_ ? y - lastCursorY_ : 0.0;
    lastCursorX_ = x;
    lastCursorY_ = y;
    haveLastCursor_ = true;
}

bool Window::focused() const { return glfwGetWindowAttrib(handle_, GLFW_FOCUSED) == GLFW_TRUE; }

void Window::setIconFromExecutable() {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(handle_);
    if (hwnd == nullptr) return;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    // The ICO holds 16 to 256 px; asking for the exact sizes Windows uses here (they depend on the screen's
    // DPI) gets the sharpest one instead of a scaled copy. LR_SHARED icons are owned by the system.
    const auto load = [&](int width, int height) {
        return static_cast<HICON>(LoadImageW(instance, L"GLFW_ICON", IMAGE_ICON, width, height, LR_SHARED));
    };
    if (HICON bigIcon = load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }
    if (HICON smallIcon = load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
#endif
}

void Window::setIcon(const std::vector<Image>& images) {
    std::vector<GLFWimage> glfwImages;
    glfwImages.reserve(images.size());
    for (const Image& image : images) {
        if (image.empty()) continue;
        GLFWimage g{};
        g.width = image.width;
        g.height = image.height;
        g.pixels = const_cast<unsigned char*>(image.rgba.data());  // GLFW only reads it
        glfwImages.push_back(g);
    }
    if (!glfwImages.empty()) {
        glfwSetWindowIcon(handle_, static_cast<int>(glfwImages.size()), glfwImages.data());
    }
}

void Window::setCursor(CursorType type) {
    if (type == currentCursor_) return;
    currentCursor_ = type;
    // A null cursor (platform without that shape) restores the default arrow.
    glfwSetCursor(handle_, cursors_[static_cast<std::size_t>(type)]);
}

}  // namespace falcon
