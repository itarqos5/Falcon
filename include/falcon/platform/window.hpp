#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/platform/cursor.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWcursor;

namespace falcon {

// A GLFW window with an OpenGL 3.3 core context, made current on construction.
//
// A `background` window is never shown: it is created hidden and unfocused, so automated runs
// (captures, scripted clicks) cannot pop up over whatever the person is doing. It still renders
// and can be read back. Its buffer swaps do not wait for vsync, so callers pace the frame loop.
class Window {
public:
    // `startHidden` creates the window invisible, so nothing blank is shown while the game loads;
    // call show() once the first frame has been drawn.
    // `borderless`: no title bar and no frame at all; the top edge of the window moves it (Windows). Not for background windows.
    Window(int width, int height, const std::string& title, bool background = false, bool startHidden = false, bool borderless = false);
    void show();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool shouldClose() const;
    void requestClose();
    void pollEvents();
    void swapBuffers();

    void framebufferSize(int& width, int& height) const;
    void windowSize(int& width, int& height) const;
    void cursorPosition(double& x, double& y) const;
    void setCursor(CursorType type);
    // Title bar / taskbar icon; the OS picks the best-fitting size from the list.
    void setIcon(const std::vector<Image>& images);
    // Windows: gives the window the icon linked into the exe (the GLFW_ICON resource), picking the right size
    // for the title bar and the taskbar at the current DPI. Does nothing on other systems.
    void setIconFromExecutable();

    // Called with GLFW button/action codes (left button = 0, press = 1).
    std::function<void(int button, int action)> onMouseButton;
    // Called with GLFW key/action/modifier codes (Escape = 256; press = 1, repeat = 2).
    std::function<void(int key, int action, int mods)> onKey;
    // A typed character (Unicode code point).
    std::function<void(unsigned int codePoint)> onChar;
    // Mouse wheel (vertical offset > 0 is "up").
    std::function<void(double xOffset, double yOffset)> onScroll;

    [[nodiscard]] bool background() const { return background_; }

    // Fullscreen on the primary monitor (the window's old size and place are restored when leaving
    // it). Background windows never go fullscreen.
    [[nodiscard]] bool isFullscreen() const { return fullscreen_; }
    void setFullscreen(bool fullscreen);
    // Waits for the display's refresh before showing a frame (background windows never wait).
    void setVsync(bool vsync);
    // The primary monitor's video modes, e.g. "1920x1080@144 (24bit)"; empty without a monitor.
    [[nodiscard]] std::vector<std::string> videoModes() const;
    [[nodiscard]] bool shiftDown() const;

    // ---- polled input, for playing ----
    [[nodiscard]] bool keyDown(int glfwKey) const;
    [[nodiscard]] bool mouseDown(int glfwButton) const;
    // For scripted tests (--press, --key ...@s+): pretend a key or a mouse button is held, so keyDown() and mouseDown() report it as well.
    void setSimulatedKey(int glfwKey, bool down) {
        if (glfwKey >= 0 && glfwKey < static_cast<int>(simKeys_.size())) simKeys_[static_cast<std::size_t>(glfwKey)] = down;
    }
    void setSimulatedButton(int glfwButton, bool down) {
        if (glfwButton >= 0 && glfwButton < static_cast<int>(simButtons_.size())) simButtons_[static_cast<std::size_t>(glfwButton)] = down;
    }
    // Hides the cursor and lets it move without limit, so the view can turn for ever (a background window never
    // grabs the cursor). takeCursorDelta() then returns how far it moved since the last call, in pixels.
    void setCursorCaptured(bool captured);
    [[nodiscard]] bool cursorCaptured() const { return captured_; }
    void takeCursorDelta(double& dx, double& dy);
    // Whether the window has the keyboard focus.
    [[nodiscard]] bool focused() const;
    // Opens a folder in the system file manager.
    static void openFolder(const std::string& path);

private:
    GLFWwindow* handle_ = nullptr;
    bool background_ = false;
    bool borderless_ = false;
    std::array<bool, 349> simKeys_{};
    std::array<bool, 8> simButtons_{};
    bool fullscreen_ = false;
    bool vsync_ = true;
    bool captured_ = false;
    bool haveLastCursor_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    int windowedX_ = 0;  // where to return to after fullscreen
    int windowedY_ = 0;
    int windowedWidth_ = 0;
    int windowedHeight_ = 0;
    std::array<GLFWcursor*, 6> cursors_{};
    CursorType currentCursor_ = CursorType::Default;
};

}  // namespace falcon
