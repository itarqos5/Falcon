#include "falcon/game/remote_container_screen.hpp"

#include "falcon/game/game_session.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/item_render.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace falcon {

namespace {
constexpr std::uint32_t kLabelColor = 0xFF404040u;
}

RemoteContainerScreen::RemoteContainerScreen(ScreenNavigator& navigator, ScreenServices& services, RemoteMenu& menu, std::string title)
    : ContainerScreen(navigator, services, menu.imageWidth(), menu.imageHeight()), menu_(menu), title_(std::move(title)) {}

namespace {

// "%s . . . ?" and "Enchantment Cost: %1$d": the arguments put in the places of %s, %d, %1$s, %1$d.
std::string formatText(const std::string& pattern, const std::vector<std::string>& args) {
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            out += pattern[i];
            continue;
        }
        if (pattern[i + 1] == 's' || pattern[i + 1] == 'd') {
            if (next < args.size()) out += args[next++];
            ++i;
        } else if (i + 3 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9' && pattern[i + 2] == '$') {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '1');
            if (n < args.size()) out += args[n];
            i += 3;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

}  // namespace

void RemoteContainerScreen::init() {
    ContainerScreen::init();
    rename_ = nullptr;
    renamedFor_ = -2;
    if (menu_.kind() == RemoteMenuKind::Anvil) {  // AnvilScreen: the name box over the picture's own
        rename_ = &addEditBox({leftPos_ + 62, topPos_ + 24, 103, 12}, "", 50);
        rename_->bordered = false;
        rename_->onChange = [this](const std::string& value) {
            if (!settingName_ && menu_.onRename) menu_.onRename(value);
        };
    }
}

// The offers of a villager in the window's list: seven rows (MerchantScreen), a click chooses one, and the wheel moves the list.
int RemoteContainerScreen::tradeRowAt(double x, double y) const {
    for (int i = 0; i < 7; ++i) {
        const int index = scrollOff_ + i;
        if (index >= static_cast<int>(menu_.offers.size())) break;
        const double rx = leftPos_ + 5, ry = topPos_ + 18 + i * 20;
        if (x >= rx && x < rx + 88 && y >= ry && y < ry + 20) return index;
    }
    return -1;
}

void RemoteContainerScreen::selectTrade(int index) {
    if (index < 0 || index >= static_cast<int>(menu_.offers.size())) return;
    selectedTrade_ = index;
    if (menu_.onSelectTrade) menu_.onSelectTrade(index);  // (the server puts what the offer costs into the two slots)
}

bool RemoteContainerScreen::mouseScrolled(double x, double y, double dy) {
    if (menu_.kind() == RemoteMenuKind::Merchant && menu_.offers.size() > 7) {
        scrollOff_ = std::clamp(scrollOff_ - static_cast<int>(dy > 0 ? 1 : -1), 0, static_cast<int>(menu_.offers.size()) - 7);
        return true;
    }
    return ContainerScreen::mouseScrolled(x, y, dy);
}

void RemoteContainerScreen::render(GuiGraphics& g, int mouseX, int mouseY, const FrameTime& time) {
    if (menu_.kind() == RemoteMenuKind::Merchant && selectedTrade_ < 0 && !menu_.offers.empty()) selectTrade(0);  // the first offer is chosen when they come
    if (rename_ != nullptr) {  // the box shows the name of the thing in the first slot, and can be used only when there is one
        const ItemStack& first = menu_.slot(0).getItem();
        const int key = first.isEmpty() ? -1 : static_cast<int>(first.item);
        if (key != renamedFor_) {
            renamedFor_ = key;
            settingName_ = true;
            rename_->setText(first.isEmpty() ? std::string() : itemDisplayName(first, services_.language));
            settingName_ = false;
            if (!first.isEmpty()) setFocus(rename_);
        }
        rename_->active = !first.isEmpty();
    }
    ContainerScreen::render(g, mouseX, mouseY, time);
}

bool RemoteContainerScreen::keyPressed(int key, int mods) {
    if (rename_ != nullptr && rename_->focused && key != GLFW_KEY_ESCAPE) {  // typing a name: the keys are the box's (E must not close the window)
        if (rename_->keyPressed(key, mods)) return true;
        return true;
    }
    return ContainerScreen::keyPressed(key, mods);
}

void RemoteContainerScreen::charTyped(unsigned int codePoint) {
    if (rename_ != nullptr && rename_->focused) {
        rename_->charTyped(codePoint);
        return;
    }
    ContainerScreen::charTyped(codePoint);
}

int RemoteContainerScreen::enchantButtonAt(double x, double y) const {
    for (int i = 0; i < 3; ++i) {
        const double bx = leftPos_ + 60, by = topPos_ + 14 + 19 * i;
        if (x >= bx && x < bx + 108 && y >= by && y < by + 19) return i;
    }
    return -1;
}

// EnchantmentMenu.clickMenuButton: the button has an offer, the lapis is there (one, two or three for the three buttons) and so are the levels (or the player builds freely).
bool RemoteContainerScreen::canEnchant(int button) const {
    const int cost = menu_.data(button);
    if (cost <= 0) return false;
    if (menu_.player().infiniteMaterials) return true;
    const int lapis = menu_.slot(1).getItem().isEmpty() ? 0 : menu_.slot(1).getItem().count;
    return lapis >= button + 1 && menu_.experienceLevel && menu_.experienceLevel() >= cost;
}

bool RemoteContainerScreen::mouseClicked(double x, double y, int button) {
    if (menu_.kind() == RemoteMenuKind::Merchant && button == 0) {
        const int row = tradeRowAt(x, y);
        if (row >= 0) {
            selectTrade(row);
            return true;
        }
    }
    if (menu_.kind() == RemoteMenuKind::Enchanting && button == 0) {
        const int which = enchantButtonAt(x, y);
        if (which >= 0) {
            if (canEnchant(which) && menu_.onButton) menu_.onButton(which);
            return true;
        }
    }
    return ContainerScreen::mouseClicked(x, y, button);
}

std::vector<SlotView> RemoteContainerScreen::slotViews() {
    std::vector<SlotView> views;
    for (int i = 0; i < menu_.slotCount(); ++i) views.push_back({&menu_.slot(i), menu_.slot(i).x, menu_.slot(i).y});
    return views;
}

void RemoteContainerScreen::drawBackground(GuiGraphics& g, int mouseX, int mouseY) {
    const auto picture = [&](const char* name) { g.blit(g.textures().get(name), leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, imageHeight_, 256, 256); };
    switch (menu_.kind()) {
        case RemoteMenuKind::Chest: {  // ContainerScreen: the top (the rows) and the bottom (the inventory) of the same picture
            const Texture2D& texture = g.textures().get("gui/container/generic_54.png");
            const int rowsHeight = menu_.rows() * 18 + 17;
            g.blit(texture, leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, rowsHeight, 256, 256);
            g.blit(texture, leftPos_, topPos_ + rowsHeight, 0.0f, 126.0f, imageWidth_, 96, 256, 256);
            break;
        }
        case RemoteMenuKind::Shulker: picture("gui/container/shulker_box.png"); break;
        case RemoteMenuKind::Dispenser: picture("gui/container/dispenser.png"); break;
        case RemoteMenuKind::Hopper: picture("gui/container/hopper.png"); break;
        case RemoteMenuKind::Crafting: picture("gui/container/crafting_table.png"); break;
        case RemoteMenuKind::Merchant: {
            g.blit(g.textures().get("gui/container/villager.png"), leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, imageHeight_, 512, 256);
            drawTrades(g, mouseX, mouseY);
            break;
        }
        case RemoteMenuKind::Anvil: {
            picture("gui/container/anvil.png");
            const bool hasFirst = menu_.slot(0).hasItem();
            g.blitSprite(hasFirst ? "container/anvil/text_field" : "container/anvil/text_field_disabled", leftPos_ + 59, topPos_ + 20, 110, 16);
            if ((hasFirst || menu_.slot(1).hasItem()) && !menu_.slot(2).hasItem()) g.blitSprite("container/anvil/error", leftPos_ + 99, topPos_ + 45, 28, 21);  // nothing can be made of these
            break;
        }
        case RemoteMenuKind::Enchanting: {
            picture("gui/container/enchanting_table.png");
            for (int i = 0; i < 3; ++i) {  // EnchantmentScreen: a button for each offer: its level icon and what it costs in levels
                const int cost = menu_.data(i);
                const int bx = leftPos_ + 60, by = topPos_ + 14 + 19 * i;
                if (cost <= 0) {
                    g.blitSprite("container/enchanting_table/enchantment_slot_disabled", bx, by, 108, 19);
                    continue;
                }
                const bool can = canEnchant(i);
                const bool over = enchantButtonAt(mouseX, mouseY) == i;
                g.blitSprite(!can ? "container/enchanting_table/enchantment_slot_disabled" : over ? "container/enchanting_table/enchantment_slot_highlighted" : "container/enchanting_table/enchantment_slot", bx, by, 108, 19);
                g.blitSprite(std::string("container/enchanting_table/level_") + std::to_string(i + 1) + (can ? "" : "_disabled"), bx + 1, by + 1, 16, 16);
                const std::string number = std::to_string(cost);
                g.text(number, static_cast<float>(bx + 106 - g.textWidth(number)), static_cast<float>(by + 9), can ? 0xFF80FF20u : 0xFF407F10u, /*shadow=*/true);
            }
            break;
        }
        case RemoteMenuKind::Furnace:
        case RemoteMenuKind::BlastFurnace:
        case RemoteMenuKind::Smoker: {
            const char* texture = menu_.kind() == RemoteMenuKind::Furnace ? "gui/container/furnace.png" : menu_.kind() == RemoteMenuKind::BlastFurnace ? "gui/container/blast_furnace.png" : "gui/container/smoker.png";
            const char* sprites = menu_.kind() == RemoteMenuKind::Furnace ? "container/furnace/" : menu_.kind() == RemoteMenuKind::BlastFurnace ? "container/blast_furnace/" : "container/smoker/";
            picture(texture);
            // AbstractFurnaceScreen: the flame (14 x 14, shrinking from the top as the fuel runs down) and the arrow (24 x 16, filling from the left as the food cooks).
            if (menu_.data(0) > 0) {
                const int total = menu_.data(1) == 0 ? 200 : menu_.data(1);
                const float lit = std::clamp(static_cast<float>(menu_.data(0)) / static_cast<float>(total), 0.0f, 1.0f);
                const int k = static_cast<int>(std::ceil(lit * 13.0f)) + 1;
                g.pushScissor(leftPos_ + 56, topPos_ + 36 + 14 - k, leftPos_ + 56 + 14, topPos_ + 36 + 14);
                g.blitSprite(std::string(sprites) + "lit_progress", leftPos_ + 56, topPos_ + 36, 14, 14);
                g.popScissor();
            }
            const int cookingTotal = menu_.data(3) == 0 ? 200 : menu_.data(3);
            const float burn = std::clamp(static_cast<float>(menu_.data(2)) / static_cast<float>(cookingTotal), 0.0f, 1.0f);
            const int width = static_cast<int>(std::ceil(burn * 24.0f));
            if (width > 0) {
                g.pushScissor(leftPos_ + 79, topPos_ + 34, leftPos_ + 79 + width, topPos_ + 34 + 16);
                g.blitSprite(std::string(sprites) + "burn_progress", leftPos_ + 79, topPos_ + 34, 24, 16);
                g.popScissor();
            }
            break;
        }
    }
}

// MerchantScreen: the list of offers (the thing paid, a second one, an arrow, what is given), the scroller, the villager's level bar and the sign when the chosen offer is used up.
void RemoteContainerScreen::drawTrades(GuiGraphics& g, int mouseX, int mouseY) {
    ItemIcons& icons = session().itemIcons();
    const int total = static_cast<int>(menu_.offers.size());
    std::vector<TooltipLine> tip;
    for (int i = 0; i < 7; ++i) {
        const int index = scrollOff_ + i;
        if (index >= total) break;
        const net::TradeOffer& offer = menu_.offers[static_cast<std::size_t>(index)];
        const int x = leftPos_ + 5, y = topPos_ + 18 + i * 20;
        const bool over = tradeRowAt(mouseX, mouseY) == index;
        g.fill(x, y, x + 88, y + 20, index == selectedTrade_ ? 0x60FFFFFFu : over ? 0x40FFFFFFu : 0x18FFFFFFu);
        // the price as the game works it out: the count, a share of it by the demand, and the special price of this player (TradeOffer.getCostA)
        ItemStack cost = offer.costA;
        const int share = std::max(0, static_cast<int>(std::floor(static_cast<float>(cost.count * offer.demand) * offer.priceMultiplier)));
        cost.count = std::clamp(cost.count + share + offer.specialPriceDiff, 1, std::max(1, cost.maxStackSize()));
        drawItemStack(g, icons, cost, x + 5, y + 2);
        if (!offer.costB.isEmpty()) drawItemStack(g, icons, offer.costB, x + 28, y + 2);
        g.blitSprite(offer.outOfStock ? "container/villager/trade_arrow_out_of_stock" : "container/villager/trade_arrow", x + 52, y + 6, 10, 9);
        drawItemStack(g, icons, offer.result, x + 68, y + 2);
        if (over) {  // what the pointer is on, by name
            const auto named = [&](const ItemStack& s, int ix) {
                if (!s.isEmpty() && mouseX >= ix && mouseX < ix + 16 && mouseY >= y + 2 && mouseY < y + 18) tip.push_back({itemDisplayName(s, services_.language), 0xFFFFFFFFu});
            };
            named(cost, x + 5);
            named(offer.costB, x + 28);
            named(offer.result, x + 68);
        }
    }
    // the scroller: 27 high, a travel of 113 over the list (disabled when it all fits)
    const int extra = total - 6;
    if (extra > 1) {
        const int l = 139 - (27 + (extra - 1) * 139 / extra);
        const int step = 1 + l / extra + 139 / extra;
        int travel = std::min(113, scrollOff_ * step);
        if (scrollOff_ == extra - 1) travel = 113;
        g.blitSprite("container/villager/scroller", leftPos_ + 94, topPos_ + 18 + travel, 6, 27);
    } else {
        g.blitSprite("container/villager/scroller_disabled", leftPos_ + 94, topPos_ + 18, 6, 27);
    }
    // the villager's level bar: from where this level starts to where the next begins
    const int level = menu_.traderLevel;
    if (menu_.showProgress && level >= 1 && level < 5) {
        static const int kMin[] = {0, 10, 70, 150, 250}, kMax[] = {10, 70, 150, 250};
        const int low = kMin[level - 1], high = kMax[level - 1];
        g.blitSprite("container/villager/experience_bar_background", leftPos_ + 136, topPos_ + 16, 102, 5);
        if (menu_.traderXp >= low) {
            const int width = std::min(100, static_cast<int>(std::floor(100.0f / static_cast<float>(high - low) * static_cast<float>(menu_.traderXp - low)))) + 1;
            g.pushScissor(leftPos_ + 136, topPos_ + 16, leftPos_ + 136 + width, topPos_ + 21);
            g.blitSprite("container/villager/experience_bar_current", leftPos_ + 136, topPos_ + 16, 102, 5);
            g.popScissor();
        }
    }
    if (selectedTrade_ >= 0 && selectedTrade_ < total && menu_.offers[static_cast<std::size_t>(selectedTrade_)].outOfStock) g.blitSprite("container/villager/out_of_stock", leftPos_ + 186, topPos_ + 35, 28, 21);
    if (!tip.empty()) g.setItemTooltip(tip, mouseX, mouseY);
}

void RemoteContainerScreen::drawLabels(GuiGraphics& g, int mouseX, int mouseY) {
    if (menu_.kind() == RemoteMenuKind::Merchant) {  // the villager's name (with its level) over the slots, "Trades" over the list, "Inventory" in its place
        std::string title = title_;
        if (menu_.showProgress && menu_.traderLevel >= 1 && menu_.traderLevel <= 5) title = formatText(text("merchant.title"), {title_, text("merchant.level." + std::to_string(menu_.traderLevel))});
        g.text(title, static_cast<float>(leftPos_ + 49 + imageWidth_ / 2 - g.textWidth(title) / 2), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
        const std::string trades = text("merchant.trades");
        g.text(trades, static_cast<float>(leftPos_ + 5 - g.textWidth(trades) / 2 + 48), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
        g.text(text("container.inventory"), static_cast<float>(leftPos_ + 107), static_cast<float>(topPos_ + imageHeight_ - 94), kLabelColor, /*shadow=*/false);
        return;
    }
    if (menu_.kind() == RemoteMenuKind::Anvil) {  // AnvilScreen: the cost, in green, or red when it cannot be paid or is too much
        const int cost = menu_.data(0);
        if (cost > 0) {
            std::uint32_t color = 0xFF80FF20u;
            std::string label = formatText(text("container.repair.cost"), {std::to_string(cost)});
            bool show = true;
            if (cost >= 40 && !menu_.player().infiniteMaterials) {
                label = text("container.repair.expensive");
                color = 0xFFFF6060u;
            } else if (!menu_.slot(2).hasItem()) {
                show = false;
            } else if (!menu_.slot(2).mayPickup(menu_.player())) {
                color = 0xFFFF6060u;
            }
            if (show) {
                const int x = imageWidth_ - 8 - g.textWidth(label) - 2;
                g.fill(leftPos_ + x - 2, topPos_ + 67, leftPos_ + imageWidth_ - 8, topPos_ + 79, 0x4F000000u);
                g.text(label, static_cast<float>(leftPos_ + x), static_cast<float>(topPos_ + 69), color, /*shadow=*/true);
            }
        }
    }
    if (menu_.kind() == RemoteMenuKind::Enchanting) {  // the clue of an offer, its cost in lapis and levels, when the pointer is on it
        const int which = enchantButtonAt(mouseX, mouseY);
        if (which >= 0 && menu_.data(which) > 0) {
            std::vector<TooltipLine> lines;
            const int enchantment = menu_.data(4 + which), level = menu_.data(7 + which);
            if (enchantment >= 0 && enchantmentName) {
                lines.push_back({formatText(text("container.enchant.clue"), {enchantmentName(enchantment, level)}), 0xFFFFFFFFu});
            }
            if (!menu_.player().infiniteMaterials) {
                const int cost = menu_.data(which), lapis = menu_.slot(1).getItem().isEmpty() ? 0 : menu_.slot(1).getItem().count;
                const int need = which + 1;
                lines.push_back({need == 1 ? text("container.enchant.lapis.one") : formatText(text("container.enchant.lapis.many"), {std::to_string(need)}), lapis >= need ? 0xFFAAAAAAu : 0xFFFF5555u});
                lines.push_back({need == 1 ? text("container.enchant.level.one") : formatText(text("container.enchant.level.many"), {std::to_string(need)}), 0xFFAAAAAAu});
                if (menu_.experienceLevel && menu_.experienceLevel() < cost) lines.push_back({formatText(text("container.enchant.level.requirement"), {std::to_string(cost)}), 0xFFFF5555u});
            }
            if (!lines.empty()) g.setItemTooltip(lines, mouseX, mouseY);
        }
    }
    (void)mouseY;
    const bool centered = menu_.kind() == RemoteMenuKind::Dispenser || menu_.kind() == RemoteMenuKind::Furnace || menu_.kind() == RemoteMenuKind::BlastFurnace ||
                          menu_.kind() == RemoteMenuKind::Smoker;
    const int titleX = menu_.kind() == RemoteMenuKind::Anvil ? 60 : centered ? (imageWidth_ - g.textWidth(title_)) / 2 : 8;  // (the anvil's title is beside its hammer)
    g.text(title_, static_cast<float>(leftPos_ + titleX), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
    const std::string inventory = text("container.inventory");
    g.text(inventory, static_cast<float>(leftPos_ + 8), static_cast<float>(topPos_ + imageHeight_ - 94), kLabelColor, /*shadow=*/false);
}

}  // namespace falcon
