#pragma once

#include "apex/text/language.hpp"
#include "apex/ui/gui_graphics.hpp"
#include "apex/world/item_stack.hpp"

#include <string>
#include <vector>

namespace apex {

// ItemStack.getHoverName: the anvil name if there is one, else the name of the item ("Diamond Pickaxe") in the player's language.
[[nodiscard]] std::string itemDisplayName(const ItemStack& stack, const Language& language);

// ItemStack.getTooltipLines for the lines Falcon knows: the name in the colour of its rarity; for tools and armor the
// "When in Main Hand:" attribute lines; with `advanced` (F3 + H) the item id and the durability. `extra` lines (the blue creative
// tab names) go right after the name, like the game inserts them.
[[nodiscard]] std::vector<TooltipLine> itemTooltipLines(const ItemStack& stack, const Language& language, bool advanced,
                                                        const std::vector<TooltipLine>& extra = {});

// 5592405 and friends: the colours the tooltip lines use (ChatFormatting).
inline constexpr std::uint32_t kTooltipGray = 0xFFAAAAAAu;
inline constexpr std::uint32_t kTooltipDarkGray = 0xFF555555u;
inline constexpr std::uint32_t kTooltipBlue = 0xFF5555FFu;
inline constexpr std::uint32_t kTooltipDarkGreen = 0xFF00AA00u;
inline constexpr std::uint32_t kTooltipDarkPurple = 0xFFAA00AAu;

// A number the way the tooltips write it: at most two decimals, none when it is whole ("7", "1.6").
[[nodiscard]] std::string formatTooltipNumber(double value);

}  // namespace apex
