#pragma once

#include "falcon/render/item_icons.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/world/item_stack.hpp"

#include <string>

namespace falcon {

// GuiGraphicsExtractor.item: the picture of a stack at (x, y), 16 x 16 GUI units.
void drawItem(GuiGraphics& graphics, ItemIcons& icons, const ItemStack& stack, int x, int y);

// GuiGraphicsExtractor.itemDecorations: the durability bar of a worn item (13 x 2 pixels, 2 in and 13 down) and the count at the
// bottom right with a shadow (nothing for a single item). `countText`, when given, replaces the number (the drag shows
// a yellow count).
void drawItemDecorations(GuiGraphics& graphics, const ItemStack& stack, int x, int y, const std::string* countText = nullptr, std::uint32_t countArgb = 0xFFFFFFFFu);

// Both: the picture and its decorations.
void drawItemStack(GuiGraphics& graphics, ItemIcons& icons, const ItemStack& stack, int x, int y, const std::string* countText = nullptr,
                   std::uint32_t countArgb = 0xFFFFFFFFu);

}  // namespace falcon
