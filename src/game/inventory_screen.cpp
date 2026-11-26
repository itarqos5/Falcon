#include "falcon/game/inventory_screen.hpp"

#include "falcon/game/game_session.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/util/text_util.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace falcon {
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

void InventoryScreen::drawLabels(GuiGraphics& g, int, int) {
    g.text(text("container.crafting"), static_cast<float>(leftPos_ + 97), static_cast<float>(topPos_ + 6), kLabelColor, /*shadow=*/false);
}

// ---- the recipe book -------------------------------------------------------------------------------------------

namespace {
constexpr int kBookWidth = 147, kBookHeight = 166;
constexpr int kBookGridX = 11, kBookGridY = 31, kBookCell = 25;
// InventoryScreen.getRecipeBookButtonPosition: leftPos + 104, height / 2 - 22 (the empty row under the crafting label, beside the player).
constexpr int kBookButtonX = 104, kBookButtonY = 61, kBookButtonWidth = 20, kBookButtonHeight = 18;
constexpr int kBookShift = 77;  // how far the window steps right to leave room for the page
constexpr int kPageArrowY = 137, kPageArrowWidth = 23, kPageArrowHeight = 13;
}  // namespace

void InventoryScreen::init() {
    ContainerScreen::init();
    layout();
}

void InventoryScreen::layout() { leftPos_ = (width_ - imageWidth_) / 2 + (book_.open() ? kBookShift : 0); }

void InventoryScreen::toggleBook() {
    book_.toggle(session().inventory());
    layout();
}

bool InventoryScreen::insideBook(double x, double y) const {
    const int px = leftPos_ - kBookWidth;
    return x >= px && x < px + kBookWidth && y >= topPos_ && y < topPos_ + kBookHeight;
}

int InventoryScreen::bookEntryAt(double x, double y) const {
    if (!book_.open() || !insideBook(x, y)) return -1;
    const int px = leftPos_ - kBookWidth;
    const int column = static_cast<int>(x - px - kBookGridX) / kBookCell;
    const int row = static_cast<int>(y - topPos_ - kBookGridY) / kBookCell;
    if (column < 0 || column >= RecipeBook::kColumns || row < 0 || row >= RecipeBook::kRows) return -1;
    const int index = book_.page() * RecipeBook::kPerPage + row * RecipeBook::kColumns + column;
    return index < static_cast<int>(book_.entries().size()) ? index : -1;
}

// RecipeBookComponent's page: the background, a 4 x 5 grid of results (the button behind each says whether it can be made, and how many times), the
// page arrows, and the name of the recipe under the mouse.
void InventoryScreen::drawBook(GuiGraphics& g, int mouseX, int mouseY) {
    const int px = leftPos_ - kBookWidth, py = topPos_;
    g.blit(g.textures().get("gui/recipe_book.png"), px, py, 0.0f, 0.0f, kBookWidth, kBookHeight, 256, 256);
    const std::vector<RecipeBook::Entry>& entries = book_.entries();
    const int first = book_.page() * RecipeBook::kPerPage;
    const Inventory& inventory = session().inventory();
    for (int i = 0; i < RecipeBook::kPerPage && first + i < static_cast<int>(entries.size()); ++i) {
        const int x = px + kBookGridX + (i % RecipeBook::kColumns) * kBookCell;
        const int y = py + kBookGridY + (i / RecipeBook::kColumns) * kBookCell;
        const int times = book_.craftableCount(first + i, inventory);
        g.blitSprite(times <= 0 ? "recipe_book/slot_uncraftable" : times == 1 ? "recipe_book/slot_craftable" : "recipe_book/slot_many_craftable", x, y, kBookCell, kBookCell);
        drawItem(g, session().itemIcons(), entries[static_cast<std::size_t>(first + i)].result, x + 4, y + 4);
    }
    if (const int pages = book_.pageCount(); pages > 1) {
        g.blitSprite(book_.page() > 0 ? "recipe_book/page_backward" : "recipe_book/page_backward_highlighted", px + 14, py + kPageArrowY, kPageArrowWidth, kPageArrowHeight);
        g.blitSprite(book_.page() < pages - 1 ? "recipe_book/page_forward" : "recipe_book/page_forward_highlighted", px + 108, py + kPageArrowY, kPageArrowWidth, kPageArrowHeight);
    }
    if (const int hovered = bookEntryAt(mouseX, mouseY); hovered >= 0) {
        const ItemStack& stack = entries[static_cast<std::size_t>(hovered)].result;
        g.setItemTooltip({{itemDisplayName(stack, services_.language), 0xFFFFFFFFu}}, mouseX, mouseY);
    }
}

void InventoryScreen::drawBackground(GuiGraphics& g, int mouseX, int mouseY) {
    if (book_.open()) drawBook(g, mouseX, mouseY);
    g.blit(g.textures().get("gui/container/inventory.png"), leftPos_, topPos_, 0.0f, 0.0f, imageWidth_, imageHeight_, 256, 256);
    session().drawPlayerPreview(g, leftPos_ + 26, topPos_ + 8, leftPos_ + 75, topPos_ + 78, 30.0f, static_cast<float>(mouseX), static_cast<float>(mouseY));  // InventoryScreen: box (26, 8)-(75, 78), scale 30
    const bool overButton = mouseX >= leftPos_ + kBookButtonX && mouseX < leftPos_ + kBookButtonX + kBookButtonWidth && mouseY >= topPos_ + kBookButtonY &&
                            mouseY < topPos_ + kBookButtonY + kBookButtonHeight;
    g.blitSprite(overButton || book_.open() ? "recipe_book/button_highlighted" : "recipe_book/button", leftPos_ + kBookButtonX, topPos_ + kBookButtonY, kBookButtonWidth, kBookButtonHeight);
}

bool InventoryScreen::mouseClicked(double x, double y, int button) {
    if (button == 0) {
        if (x >= leftPos_ + kBookButtonX && x < leftPos_ + kBookButtonX + kBookButtonWidth && y >= topPos_ + kBookButtonY &&
            y < topPos_ + kBookButtonY + kBookButtonHeight) {
            toggleBook();
            return true;
        }
        if (insideBook(x, y)) {
            const int px = leftPos_ - kBookWidth;
            if (book_.pageCount() > 1 && y >= topPos_ + kPageArrowY && y < topPos_ + kPageArrowY + kPageArrowHeight &&
                ((x >= px + 14 && x < px + 14 + kPageArrowWidth) || (x >= px + 108 && x < px + 108 + kPageArrowWidth))) {
                book_.scroll(x < px + kBookWidth / 2 ? -1 : 1);
                return true;
            }
            if (const int entry = bookEntryAt(x, y); entry >= 0) {
                if (book_.place(entry, session().inventoryMenu(), session().inventory())) book_.refresh(session().inventory());
                return true;
            }
            return true;  // (the page covers the world behind it: a click on it puts nothing down)
        }
    }
    return ContainerScreen::mouseClicked(x, y, button);
}

bool InventoryScreen::mouseScrolled(double x, double y, double dy) {
    if (book_.open() && insideBook(x, y)) {
        book_.scroll(dy > 0.0 ? -1 : dy < 0.0 ? 1 : 0);
        return true;
    }
    return ContainerScreen::mouseScrolled(x, y, dy);
}

void InventoryScreen::slotClicked(const SlotView* view, int slotId, int button, ClickType type) {
    ContainerScreen::slotClicked(view, slotId, button, type);
    if (book_.open()) book_.refresh(session().inventory());  // (what can be made changed with what moved)
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

}  // namespace falcon
