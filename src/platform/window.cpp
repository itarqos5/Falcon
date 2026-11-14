#include "falcon/platform/window.hpp"

#include "falcon/gfx/gl.hpp"
#include "falcon/platform/dialog.hpp"

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
#include <stdexcept>

namespace falcon {
namespace {

void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "[glfw] error %d: %s\n", code, description);
}

// Gives the window a dark title bar and dark caption buttons (Windows 10 version 1809 and later, and 11), to
// match the dark menus, whatever the system's own colour theme is. Older Windows ignores the request.
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

Window::Window(int width, int height, const std::string& title, bool background, bool startHidden)
    : background_(background) {
    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() == GLFW_FALSE) throw std::runtime_error("failed to initialise GLFW");
#ifdef _WIN32
    // Windows wakes sleeping programs only every 15.6 ms unless asked for better, which would turn a 16 ms frame
    // pause into 31 ms and make the frame rate limit wrong. Ask for 1 ms for as long as the window exists.
    timeBeginPeriod(1);
#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    if (startHidden && !background) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (background) {  // never appears, never takes focus from the person's game
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    handle_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (handle_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("failed to create a window with an OpenGL 3.3 core context");
    }
    useDarkTitleBar(handle_);  // before the window is first shown, so the title bar never flashes light
    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(background ? 0 : 1);
    gl::load();

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
    glfwSwapInterval(vsync_ ? 1 : 0);  // a new surface forgets the swap interval
}

void Window::setVsync(bool vsync) {
    vsync_ = vsync;
    if (!background_) glfwSwapInterval(vsync ? 1 : 0);
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
    if (handle_ != nullptr) glfwDestroyWindow(handle_);
    glfwTerminate();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

bool Window::shouldClose() const { return glfwWindowShouldClose(handle_) != 0; }
void Window::requestClose() { glfwSetWindowShouldClose(handle_, GLFW_TRUE); }
void Window::pollEvents() { glfwPollEvents(); }
void Window::swapBuffers() { glfwSwapBuffers(handle_); }

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

bool Window::keyDown(int glfwKey) const { return glfwGetKey(handle_, glfwKey) == GLFW_PRESS; }

bool Window::mouseDown(int glfwButton) const { return glfwGetMouseButton(handle_, glfwButton) == GLFW_PRESS; }

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
