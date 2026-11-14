#pragma once

#include "falcon/game/creative_tabs.hpp"
#include "falcon/world/menu.hpp"

#include <memory>
#include <string>
#include <vector>

namespace falcon {

// A slot of the grid of items in the creative inventory: it shows an item to take, and nothing can be put in it.
class CreativePaletteSlot final : public Slot {
public:
    CreativePaletteSlot(Container& palette, int number, int px, int py) : Slot(palette, number, px, py) {}
    [[nodiscard]] bool mayPlace(const ItemStack&) const override { return false; }
};

// CreativeModeInventoryScreen.ItemPickerMenu: the 9 x 5 slots that show the tab's items (scrolled by rows) and the 9 hotbar
// slots under them.
class CreativePickerMenu final : public ContainerMenu {
public:
    static constexpr int kColumns = 9;
    static constexpr int kRows = 5;
    static constexpr int kPaletteSlots = kColumns * kRows;
    static constexpr int kHotbarStart = kPaletteSlots;

    explicit CreativePickerMenu(MenuPlayer player);

    [[nodiscard]] SimpleContainer& palette() { return palette_; }
    // The items the grid shows (a tab's, or the found ones); scrolled to the top.
    void setItems(std::vector<ItemStack> items);
    [[nodiscard]] const std::vector<ItemStack>& items() const { return items_; }
    [[nodiscard]] bool canScroll() const { return static_cast<int>(items_.size()) > kPaletteSlots; }
    // ItemPickerMenu.scrollTo: row `(int)(offs * (rows - 5) + 0.5)` of the items is the first one shown.
    void scrollTo(float offs);
    [[nodiscard]] int firstRow() const { return firstRow_; }
    // The scroll offset after the wheel turned by `scrollY` (up is positive): one row per notch.
    [[nodiscard]] float subtractInputFromScroll(float offs, double scrollY) const;
    // Shift-click on a hotbar slot destroys what it holds; the grid is never a source or target of a shift-click.
    ItemStack quickMoveStack(int index) override;

private:
    SimpleContainer palette_{kPaletteSlots};
    std::vector<ItemStack> items_;
    int firstRow_ = 0;
};

// The logic of the creative inventory screen, apart from drawing it: which tab is open, what it lists (a search finds items by
// their tooltip text), how it is scrolled, where the slots are, and the game's rules for a click (taking an item from the grid,
// a full stack with shift or the middle button, throwing it back to delete it, the trash slot of the survival tab).
class CreativeInventory {
public:
    CreativeInventory(InventoryMenu& inventoryMenu, MenuPlayer player, const CreativeTabs& tabs);

    [[nodiscard]] const CreativeTab& tab() const { return *tab_; }
    // CreativeModeInventoryScreen.selectTab.
    void selectTab(const CreativeTab& tab);
    [[nodiscard]] const CreativeTabs& tabs() const { return tabs_; }

    // The search tab's text (setting it lists the matching items).
    void setSearchText(const std::string& text);
    [[nodiscard]] const std::string& searchText() const { return search_; }

    [[nodiscard]] float scrollOffs() const { return scrollOffs_; }
    void setScroll(float offs);
    [[nodiscard]] bool canScroll() const { return tab_->canScroll() && picker_.canScroll(); }
    // The wheel: scrolls a row per notch (false when the tab does not scroll).
    bool scrollBy(double wheelUp);
    // The number of rows of items and the first row shown.
    [[nodiscard]] int rowCount() const;

    [[nodiscard]] bool isInventoryTab() const { return tab_->type == CreativeTabType::Inventory; }
    // The menu a click goes to, and the one whose cursor stack is the cursor stack.
    [[nodiscard]] ContainerMenu& activeMenu();
    [[nodiscard]] CreativePickerMenu& picker() { return picker_; }
    [[nodiscard]] InventoryMenu& inventoryMenu() { return inventory_; }
    [[nodiscard]] Slot& trashSlot() { return *trash_; }
    // The slots of the open tab with their places (relative to the window).
    [[nodiscard]] std::vector<SlotView> slotViews();

    // CreativeModeInventoryScreen.slotClicked. `view` is null for a click outside every slot; `clickedOutside` says it was beside
    // the window; `slotId` is the menu number of the slot, or -999 outside the window.
    void slotClicked(const SlotView* view, int slotId, int button, ClickType type, bool clickedOutside);
    // The screen closes: a stack carried from the grid is lost, one carried in the survival tab goes back to the inventory.
    void close();
    [[nodiscard]] const MenuPlayer& player() const { return player_; }

private:
    void refreshItems();
    void moveCarriedTo(ContainerMenu& from, ContainerMenu& to);

    const CreativeTabs& tabs_;
    InventoryMenu& inventory_;
    MenuPlayer player_;
    CreativePickerMenu picker_;
    SimpleContainer trashContainer_{1};
    std::unique_ptr<Slot> trash_;
    const CreativeTab* tab_ = nullptr;
    std::string search_;
    float scrollOffs_ = 0.0f;
    std::vector<SlotView> inventoryViews_;  // the survival tab: the player's slots, arranged like the creative screen does
};

}  // namespace falcon
