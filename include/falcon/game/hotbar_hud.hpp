#pragma once

#include "falcon/world/item_stack.hpp"

#include <algorithm>

namespace falcon {

// The name of the held item that shows above the hotbar and fades (Gui.toolHighlightTimer). Every game tick it is fed the held
// stack: a different item or name starts it again at 40 ticks (2 seconds) times the "notification display time" option; the
// same one counts it down; an empty hand ends it. It is fully opaque until 10 ticks are left and fades over those.
class SelectedItemHighlight {
public:
    void tick(const ItemStack& held, double displayTimeScale = 1.0) {
        if (held.isEmpty()) {
            timer_ = 0;
        } else if (!last_.isEmpty() && held.item == last_.item && held.customName == last_.customName && held.variantName == last_.variantName) {
            if (timer_ > 0) --timer_;
        } else {
            timer_ = static_cast<int>(40.0 * displayTimeScale);
        }
        last_ = held;
    }
    [[nodiscard]] int timer() const { return timer_; }
    // The alpha of the name this frame, 0..255 (nothing is drawn at 0).
    [[nodiscard]] int alpha() const {
        if (timer_ <= 0 || last_.isEmpty()) return 0;
        return std::min(255, static_cast<int>(static_cast<float>(timer_) * 256.0f / 10.0f));
    }
    [[nodiscard]] const ItemStack& stack() const { return last_; }

private:
    int timer_ = 0;
    ItemStack last_;
};

// Where the hotbar and what goes with it sit on a GUI of the given size (Hud.extractItemHotbar): the bar is 182x22 at the
// bottom middle, the slot icons 3 pixels in, the offhand slot to its left with its own 29x24 sprite.
struct HotbarLayout {
    int left = 0;     // x of the bar
    int top = 0;      // y of the bar
    int offhandSpriteX = 0, offhandSpriteY = 0;  // the 29x24 sprite left of the bar
    int offhandItemX = 0, offhandItemY = 0;
    int nameY = 0;    // the selected item's name (centred)
    [[nodiscard]] int slotX(int slot) const { return left + 3 + slot * 20; }
    [[nodiscard]] int slotY() const { return top + 3; }
    [[nodiscard]] int selectionX(int slot) const { return left - 1 + slot * 20; }
    [[nodiscard]] int selectionY() const { return top - 1; }
};
[[nodiscard]] inline HotbarLayout computeHotbarLayout(int guiWidth, int guiHeight, bool survival) {
    HotbarLayout l;
    l.left = guiWidth / 2 - 91;
    l.top = guiHeight - 22;
    l.offhandSpriteX = guiWidth / 2 - 91 - 29;
    l.offhandSpriteY = guiHeight - 23;
    l.offhandItemX = guiWidth / 2 - 91 - 26;
    l.offhandItemY = guiHeight - 16 - 3;
    l.nameY = guiHeight - 59 + (survival ? 0 : 14);
    return l;
}

}  // namespace falcon
