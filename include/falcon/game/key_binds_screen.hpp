#pragma once

#include "falcon/game/key_mappings.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/ui/rect.hpp"
#include "falcon/ui/screen.hpp"
#include "falcon/ui/scroll_list.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace falcon {

// KeyBindsScreen's geometry (GUI units). Every row is 20 tall and 340 wide, centred; inside a row the name
// starts 2 in, the key button (75 wide) 208 in and the Reset button (50 wide) 288 in.
inline constexpr int kKeyBindsRowWidth = 340;
inline constexpr int kKeyBindsRowHeight = 20;
inline constexpr int kKeyButtonWidth = 75;
inline constexpr int kResetButtonWidth = 50;
inline constexpr int kKeyNameOffset = 2;
inline constexpr int kKeyButtonOffset = 208;
inline constexpr int kResetButtonOffset = 288;

struct KeyBindsLayout {
    int titleX = 0;
    int titleY = 0;
    Rect list;
    Rect resetAll;  // "Reset Keys"
    Rect done;
};
[[nodiscard]] KeyBindsLayout computeKeyBindsLayout(int width, int height, int titleWidth);

// The text of a key button as coloured pieces: the key name (white), "[ KEY ]" in yellow brackets while another
// action shares the key, and "> KEY <" with the name underlined while the button waits for a new key.
struct KeyLabelPiece {
    std::string text;
    std::uint32_t rgb = 0xFFFFFF;
    bool underlined = false;
    bool operator==(const KeyLabelPiece&) const = default;
};
[[nodiscard]] std::vector<KeyLabelPiece> keyButtonLabel(const std::string& keyName, bool conflict, bool waiting);

// Options -> Controls -> "Key Binds...": every action grouped by category, each with a button showing its key
// (click it, then press a key or click a mouse button to rebind; Escape unbinds), a Reset button, a yellow mark
// and tooltip when another action uses the same key, and Reset Keys at the bottom. The bindings are saved with
// the other settings. Nothing in this client reacts to them yet: there is no game to play.
class KeyBindsScreen : public Screen {
public:
    KeyBindsScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    // While an action waits for its key, any mouse button or key binds it and Escape unbinds it (the screen
    // does not close then).
    bool mouseClicked(double x, double y, int button) override;
    bool keyPressed(int key, int mods) override;
    [[nodiscard]] bool shouldCloseOnEsc() const override { return waiting_ < 0; }
    [[nodiscard]] bool wantsAllKeys() const override { return waiting_ >= 0; }

protected:
    void init() override;

private:
    KeyBindings& bindings() const { return services_.options.keyBindings; }

    ScreenServices& services_;
    int waiting_ = -1;  // index (into keyMappings()) of the action waiting for a new key, or -1
    Button* resetAll_ = nullptr;
};

}  // namespace falcon
