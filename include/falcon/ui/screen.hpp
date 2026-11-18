#pragma once

#include "falcon/ui/context.hpp"
#include "falcon/ui/layout.hpp"
#include "falcon/ui/metrics.hpp"
#include "falcon/ui/rect.hpp"
#include "falcon/ui/widgets.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

// Per-frame timing handed to screens.
struct FrameTime {
    double millis = 0.0;     // monotonic clock, milliseconds
    float deltaTicks = 0.0f;  // real time since the previous frame, in 50 ms game ticks
};

class Screen;

// How screens talk to the application: change screens, quit, play the UI click, and reach the
// UiContext (translations, font, backdrop). Screen changes are applied between frames, so push/pop
// are safe to call from a button handler (the screen is not destroyed mid-click).
class ScreenNavigator {
public:
    virtual ~ScreenNavigator() = default;
    virtual void push(std::unique_ptr<Screen> screen) = 0;  // open on top of the current screen
    virtual void pop() = 0;                                  // back to the previous screen
    virtual void quit() = 0;
    virtual void playButtonClick() = 0;                      // the UI click ("ui.button.click")
    [[nodiscard]] virtual bool isShiftDown() const = 0;      // shift+click cycles backwards
    [[nodiscard]] virtual bool isControlDown() const { return false; }
    [[nodiscard]] virtual UiContext& ui() = 0;
};

// Base class of every screen. A subclass implements init(), which is called whenever the screen is
// (re)built for a window size, and adds its widgets there with the add*() helpers:
//
//     void MyScreen::init() {
//         addLabel(text("my.title"), width_ / 2 - 30, 20);
//         auto row = layout::centeredRow(width_, 100, {150, 150}, 8);
//         addButton(text("gui.yes"), row[0], [this] { accept(); });
//         addButton(text("gui.no"),  row[1], [this] { onClose(); });
//     }
//
// Widgets are owned by the screen. Positions are in GUI units; width_ and height_ are the size.
class Screen {
public:
    explicit Screen(ScreenNavigator& navigator) : navigator_(navigator) {}
    virtual ~Screen() = default;

    // (Re)builds the widgets for a GUI-scaled window size.
    void resize(int width, int height);

    // Called after any screen has built its widgets (each time it is built for a window size): this is where the mods add their buttons (ScreenEvents.AFTER_INIT in Fabric).
    using PostInitHook = std::function<void(Screen&)>;
    static void setPostInitHook(PostInitHook hook);
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    // A button put on the screen from outside (a mod's), on top of the screen's own.
    Button& addExternalButton(const std::string& label, const Rect& rect, std::function<void()> onPress) { return addButton(label, rect, std::move(onPress)); }

    virtual void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time);
    // Plays the click sound for a pressed button (sliders click on release instead).
    virtual bool mouseClicked(double x, double y, int button);
    virtual void mouseReleased(double x, double y, int button);
    // Called every frame while the left button is held (so sliders can follow the cursor).
    virtual void mouseDragged(double x, double y);
    virtual bool mouseScrolled(double x, double y, double dy);
    // Escape is handled by the client; everything else goes to the focused widget.
    virtual bool keyPressed(int key, int mods);
    // A key that was held has been let go (only the screens that care, like the game mode switcher, listen).
    virtual void keyReleased(int /*key*/, int /*mods*/) {}
    virtual void charTyped(unsigned int codePoint);
    // Whether Escape closes this screen (the main menu is the root and ignores it).
    [[nodiscard]] virtual bool shouldCloseOnEsc() const { return true; }
    // True while the screen wants every key for itself (the chat box: the hide-GUI key is typing there), so the client keeps that key out of it.
    [[nodiscard]] virtual bool wantsAllKeys() const { return false; }
    // True while the screen is waiting for a key to bind (the Key Binds screen): then even F11 and F2 are the key being bound. Everywhere else, chat included, they work
    // (Minecraft.handleGlobalKeyPress runs before any screen hears a key).
    [[nodiscard]] virtual bool bindingKey() const { return false; }
    // True for a screen drawn over a world that keeps running behind it (the inventory): the game goes on ticking while it is open,
    // only the player cannot steer. Menus that pause the game (every other screen) leave it false.
    [[nodiscard]] virtual bool keepsWorldTicking() const { return false; }
    // Done and Escape: leave the screen (screens that save something override this).
    virtual void onClose() { navigator_.pop(); }

protected:
    virtual void init() = 0;

    // ---- the application's services ----
    [[nodiscard]] UiContext& ui() const { return navigator_.ui(); }
    [[nodiscard]] std::string text(const std::string& key) const { return ui().text(key); }
    [[nodiscard]] const Font& font() const { return ui().font(); }

    // ---- adding widgets ----
    template <class T, class... Args>
    T& addWidget(Args&&... args) {
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *widget;
        widgets_.push_back(std::move(widget));
        return ref;
    }
    // A standard button with an already translated label.
    Button& addButton(const std::string& label, const Rect& rect, std::function<void()> onPress = nullptr);
    // A square button showing a sprite (the Friends / Language / Accessibility icons).
    IconButton& addIconButton(const std::string& sprite, int iconSize, const Rect& rect,
                              std::function<void()> onPress = nullptr);
    // A button that is shown but cannot be pressed, with the padlock (Telemetry Data).
    LockedButton& addLockedButton(const std::string& label, const Rect& rect);
    // White text with a shadow at (x, y), sized to fit the text; `rgb` is 0xRRGGBB.
    LabelWidget& addLabel(const std::string& label, int x, int y, std::uint32_t rgb = 0xFFFFFFu);
    // The same, centred horizontally on `centerX`.
    LabelWidget& addCenteredLabel(const std::string& label, int centerX, int y, std::uint32_t rgb = 0xFFFFFFu);
    // A one-line text box with a grey hint shown while it is empty and unfocused.
    EditBox& addEditBox(const Rect& rect, const std::string& hint, std::size_t maxLength = 32);
    // A tick box; its label is to the right of the 17x17 box, `x` and `y` are the box's top-left corner.
    CheckboxWidget& addCheckbox(const std::string& label, int x, int y, bool checked, std::function<void(bool)> onChange = nullptr);
    // A 0..1 slider whose label is computed from its value; `onChange` gets every new value.
    SliderWidget& addSlider(const Rect& rect, double initial, std::function<std::string(double)> label,
                            std::function<void(double)> onChange);

    void setFocus(Widget* widget);

    ScreenNavigator& navigator_;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::unique_ptr<Widget>> widgets_;
};

}  // namespace falcon
