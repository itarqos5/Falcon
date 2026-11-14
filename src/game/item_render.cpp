#include "falcon/game/item_render.hpp"

namespace falcon {

void drawItem(GuiGraphics& g, ItemIcons& icons, const ItemStack& stack, int x, int y) {
    if (stack.isEmpty()) return;
    icons.setScale(g.guiScale());  // keep the atlas at 1:1 with the screen before taking an icon from it
    const ItemIcons::Slot icon = icons.slot(stack);
    const int px = icons.iconPixels();
    g.blit(icons.texture(), x, y, icon.u, icon.v, 16, 16, px, px, icons.texture().width(), icons.texture().height());
}

void drawItemDecorations(GuiGraphics& g, const ItemStack& stack, int x, int y, const std::string* countText, std::uint32_t countArgb) {
    if (stack.isEmpty()) return;
    if (stack.isDamaged()) {
        const int left = x + 2;
        const int top = y + 13;
        g.fill(left, top, left + 13, top + 2, 0xFF000000u);
        g.fill(left, top, left + stack.barWidth(), top + 1, 0xFF000000u | stack.barColor());
    }
    if (stack.count != 1 || countText != nullptr) {
        const std::string text = countText != nullptr ? *countText : std::to_string(stack.count);
        g.text(text, static_cast<float>(x + 19 - 2 - g.textWidth(text)), static_cast<float>(y + 6 + 3), countArgb);
    }
}

void drawItemStack(GuiGraphics& g, ItemIcons& icons, const ItemStack& stack, int x, int y, const std::string* countText, std::uint32_t countArgb) {
    drawItem(g, icons, stack, x, y);
    drawItemDecorations(g, stack, x, y, countText, countArgb);
}

}  // namespace falcon
