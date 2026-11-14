#include "apex/game/inventory_screen.hpp"

#include "apex/game/game_session.hpp"
#include "apex/game/item_names.hpp"
#include "apex/game/item_render.hpp"
#include "apex/util/text_util.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace apex {
namespace {

constexpr std::uint32_t kLabelColor = 0xFF404040u;  // -12566464


}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Survival

InventoryScreen::InventoryScreen(ScreenNavigator& navigator, ScreenServices& services) : ContainerScreen(navigator, services, 176, 166) {}

ContainerMenu& InventoryScreen::menu() { return session().inventoryMenu(); }

std::vector<SlotView> InventoryScreen::slotViews() {
    std::vector<SlotView> views;
    InventoryMenu& m = session().inventoryMenu();
    for (int i = 0; i < m.slotCount(); ++i) views.push_back({&m.slot(i), m.slot(i).x, m.slot(i).y});
    return views;
}

void InventoryScreen::drawBackground(GuiGraphics& g, int mouseX, int mouseY) {
    g.blit(g.textures().get("gui/container/inventory.png"), leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, imageHeight_, 256, 256);
    session().drawPlayerPreview(g, leftPos_ + 26, topPos_ + 8, leftPos_ + 75, topPos_ + 78, 30.0f, static_cast<float>(mouseX), static_cast<float>(mouseY));  // InventoryScreen: box (26, 8)-(75, 78), scale 30
}

void InventoryScreen::drawLabels(GuiGraphics& g, int, int) {
    g.text(text("container.crafting"), static_cast<float>(leftPos_ + 97), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
}

// ---------------------------------------------------------------------------------------------------------------
// Creative

namespace {
std::string g_lastTab = "minecraft:building_blocks";  // CreativeModeInventoryScreen.selectedTab: the tab stays when the screen is closed
}

CreativeScreen::CreativeScreen(ScreenNavigator& navigator, ScreenServices& services) : ContainerScreen(navigator, services, 195, 136) {
    tabs_ = currentCreativeTabs();
    if (!tabs_) tabs_ = CreativeTabs::makeMinimal(itemRegistry());
    logic_ = std::make_unique<CreativeInventory>(session().inventoryMenu(), session().inventoryMenu().player(), *tabs_);
    if (const CreativeTab* last = tabs_->find(g_lastTab); last != nullptr && last->visible) logic_->selectTab(*last);
}

void CreativeScreen::init() { ContainerScreen::init(); }

ContainerMenu& CreativeScreen::menu() { return logic_->activeMenu(); }

std::vector<SlotView> CreativeScreen::slotViews() { return logic_->slotViews(); }

std::string CreativeScreen::tabName(const CreativeTab& tab) const {
    if (!tab.translationKey.empty()) return services_.language.get(tab.translationKey);
    return tab.title;
}

int CreativeScreen::tabX(const CreativeTab& tab) const { return tab.alignedRight() ? imageWidth_ - 27 * (7 - tab.column) + 1 : 27 * tab.column; }
int CreativeScreen::tabY(const CreativeTab& tab) const { return tab.top ? -32 : imageHeight_; }

bool CreativeScreen::tabHit(const CreativeTab& tab, double x, double y) const {
    const int tx = tabX(tab), ty = tabY(tab);
    return x >= tx && x <= tx + 26 && y >= ty && y <= ty + 32;
}

bool CreativeScreen::hasClickedOutside(double x, double y) const {
    const bool outside = ContainerScreen::hasClickedOutside(x, y);
    if (!outside) return false;
    for (const CreativeTab& tab : tabs_->tabs()) {
        if (tab.visible && tabHit(tab, x - leftPos_, y - topPos_)) return false;
    }
    return true;
}

bool CreativeScreen::insideScrollbar(double x, double y) const {
    const int left = leftPos_ + 175, top = topPos_ + 18;
    return x >= left && y >= top && x < left + 14 && y < top + 112 && logic_->tab().canScroll();
}

void CreativeScreen::scrollTo(double mouseY) {
    const int top = topPos_ + 18;
    const int bottom = top + 112;
    const float offs = (static_cast<float>(mouseY) - static_cast<float>(top) - 7.5f) / (static_cast<float>(bottom - top) - 15.0f);
    logic_->setScroll(offs);
}

void CreativeScreen::drawTab(GuiGraphics& g, const CreativeTab& tab, int mouseX, int mouseY) {
    const bool selected = &tab == &logic_->tab();
    const int left = leftPos_ + tabX(tab);
    const int top = tab.top ? topPos_ - 28 : topPos_ + imageHeight_ - 4;
    const std::string sprite = std::string("container/creative_inventory/tab_") + (tab.top ? "top_" : "bottom_") + (selected ? "selected_" : "unselected_") +
                               std::to_string(std::clamp(tab.column, 0, 6) + 1);
    g.blitSprite(sprite, left, top, 26, 32);
    drawItem(g, session().itemIcons(), tab.icon, left + 5, top + 8 + (tab.top ? 1 : -1));
    // The name of the tab beside the cursor (isHovering(tabX + 3, tabY + 3, 21, 27)).
    const int hx = tabX(tab) + 3, hy = tabY(tab) + 3;
    const double px = mouseX - leftPos_, py = mouseY - topPos_;
    if (px >= hx && px < hx + 21 && py >= hy && py < hy + 27) g.setItemTooltip({{tabName(tab), kWhite}}, mouseX, mouseY);
}

void CreativeScreen::drawBackground(GuiGraphics& g, int mouseX, int mouseY) {
    const CreativeTab& selected = logic_->tab();
    // The pictures of the tabs, then of the items on screen and the next rows are made in the background, so that scrolling and
    // opening a tab do not stop the game while the files of a few dozen items are read.
    ItemIcons& icons = session().itemIcons();
    for (const CreativeTab& tab : tabs_->tabs()) icons.prefetch(tab.icon);
    if (!logic_->isInventoryTab()) {
        const std::vector<ItemStack>& items = logic_->picker().items();
        const int first = std::max(0, logic_->picker().firstRow() - 2) * CreativePickerMenu::kColumns;
        const int last = std::min(static_cast<int>(items.size()), (logic_->picker().firstRow() + 5 + 5) * CreativePickerMenu::kColumns);
        for (int i = first; i < last; ++i) icons.prefetch(items[static_cast<std::size_t>(i)]);
    }
    // The tabs that are not open go behind the window, the open one in front of it.
    for (const CreativeTab& tab : tabs_->tabs()) {
        if (tab.visible && &tab != &selected) drawTab(g, tab, mouseX, mouseY);
    }
    const char* texture = selected.type == CreativeTabType::Search ? "tab_item_search.png" : selected.type == CreativeTabType::Inventory ? "tab_inventory.png" : "tab_items.png";
    g.blit(g.textures().get(std::string("gui/container/creative_inventory/") + texture), leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, imageHeight_, 256, 256);
    if (selected.type == CreativeTabType::Search) {
        const std::string& text = logic_->searchText();
        const bool caret = static_cast<long long>(nowMillis() / 500.0) % 2 == 0;
        g.text(caret ? text + "_" : text, static_cast<float>(leftPos_ + 82), static_cast<float>(topPos_ + 6), kWhite);
    }
    if (selected.canScroll()) {
        const int y = topPos_ + 18 + static_cast<int>(77.0f * logic_->scrollOffs());
        g.blitSprite(logic_->canScroll() ? "container/creative_inventory/scroller" : "container/creative_inventory/scroller_disabled", leftPos_ + 175, y, 12, 15);
    }
    drawTab(g, selected, mouseX, mouseY);
    if (selected.type == CreativeTabType::Inventory) {
        session().drawPlayerPreview(g, leftPos_ + 73, topPos_ + 6, leftPos_ + 105, topPos_ + 49, 20.0f, static_cast<float>(mouseX), static_cast<float>(mouseY));  // the creative Survival Inventory tab: box (73, 6)-(105, 49), scale 20
        if (const SlotView* h = hovered(); h != nullptr && h->slot == &logic_->trashSlot()) {
            g.setItemTooltip({{text("inventory.binSlot"), kWhite}}, mouseX, mouseY);
        }
    }
}

void CreativeScreen::drawLabels(GuiGraphics& g, int, int) {
    const CreativeTab& tab = logic_->tab();
    if (tab.showTitle()) g.text(tabName(tab), static_cast<float>(leftPos_ + 8), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
}

void CreativeScreen::slotClicked(const SlotView* view, int slotId, int button, ClickType type) {
    logic_->slotClicked(view, slotId, button, type, slotId == kOutsideSlot);
}

// CreativeModeInventoryScreen.getTooltipFromContainerItem: the plain tooltip, then the names of the tabs the item is in in blue
// (not when a tab of its own lists it and the cursor is over that tab's grid), and in the search tab the item tags a "#" search found.
std::vector<TooltipLine> CreativeScreen::tooltipLines(const SlotView& view) {
    const ItemStack& stack = view.slot->getItem();
    const bool overGrid = view.slot->container == &logic_->picker().palette();
    const bool category = logic_->tab().type == CreativeTabType::Category;
    std::vector<TooltipLine> extra;
    if (!(category && overGrid)) {
        if (logic_->tab().type == CreativeTabType::Search && overGrid && !logic_->searchText().empty() && logic_->searchText()[0] == '#') {
            if (const CreativeTab* search = tabs_->find("minecraft:search")) {
                for (const CreativeEntry& e : search->entries) {
                    if (e.stack.item != stack.item || e.stack.components != stack.components) continue;
                    const std::string word = logic_->searchText().substr(1);
                    for (const std::string& tag : e.tags) {
                        if (word.empty() || tag.find(word) != std::string::npos) extra.push_back({"#" + tag, kTooltipDarkPurple});
                    }
                    break;
                }
            }
        }
        for (const CreativeTab* tab : tabs_->tabsContaining(stack)) extra.push_back({tabName(*tab), kTooltipBlue});
    }
    return itemTooltipLines(stack, services_.language, session().advancedTooltips(), extra);
}

bool CreativeScreen::mouseClicked(double x, double y, int button) {
    if (button == 0) {
        for (const CreativeTab& tab : tabs_->tabs()) {
            if (tab.visible && tabHit(tab, x - leftPos_, y - topPos_)) return true;  // (the tab opens when the button is released)
        }
        if (!logic_->isInventoryTab() && insideScrollbar(x, y)) {
            scrolling_ = logic_->canScroll();
            if (scrolling_) scrollTo(y);
            return true;
        }
    }
    return ContainerScreen::mouseClicked(x, y, button);
}

void CreativeScreen::mouseReleased(double x, double y, int button) {
    if (button == 0) {
        scrolling_ = false;
        for (const CreativeTab& tab : tabs_->tabs()) {
            if (tab.visible && tabHit(tab, x - leftPos_, y - topPos_)) {
                logic_->selectTab(tab);
                g_lastTab = tab.id;
                return;
            }
        }
    }
    ContainerScreen::mouseReleased(x, y, button);
}

void CreativeScreen::mouseDragged(double, double y) {
    if (scrolling_) scrollTo(y);
}

bool CreativeScreen::mouseScrolled(double, double, double dy) { return logic_->scrollBy(dy); }

void CreativeScreen::charTyped(unsigned int codePoint) {
    if (ignoreChar_) {  // the digit that was used as a hotbar key over an item is not text
        ignoreChar_ = false;
        return;
    }
    if (logic_->tab().type != CreativeTabType::Search || codePoint < 32 || codePoint == 127) return;
    std::string text = logic_->searchText();
    if (text.size() >= 50) return;
    text += encodeUtf8(static_cast<char32_t>(codePoint));
    logic_->setSearchText(text);
}

bool CreativeScreen::keyPressed(int key, int mods) {
    if (logic_->tab().type == CreativeTabType::Search) {
        // A number key over an item of the grid takes it to that hotbar slot; every other key is the search box's.
        const bool isDigit = key >= GLFW_KEY_0 && key <= GLFW_KEY_9;
        const SlotView* h = hovered();
        if (isDigit && (h == nullptr || h->slot->hasItem() || h->slot->container != &logic_->picker().palette()) && checkHotbarKeyPressed(key)) {
            ignoreChar_ = true;
            return true;
        }
        if (key == GLFW_KEY_BACKSPACE) {
            std::string text = logic_->searchText();
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();  // a whole character
            if (!text.empty()) text.pop_back();
            logic_->setSearchText(text);
            return true;
        }
        return key != GLFW_KEY_ESCAPE;  // the text box takes the key (the inventory key types its letter instead of closing)
    }
    return ContainerScreen::keyPressed(key, mods);
}

void CreativeScreen::onClose() {
    logic_->close();
    Screen::onClose();
}

}  // namespace apex
