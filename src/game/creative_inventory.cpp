#include "apex/game/creative_inventory.hpp"

#include <algorithm>

namespace apex {

// ---------------------------------------------------------------------------------------------------------------
// The picker menu

CreativePickerMenu::CreativePickerMenu(MenuPlayer player) : ContainerMenu(std::move(player)) {
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) addSlot(std::make_unique<CreativePaletteSlot>(palette_, col + row * kColumns, 9 + col * 18, 18 + row * 18));
    }
    for (int i = 0; i < 9; ++i) addSlot(std::make_unique<Slot>(*player_.inventory, i, 9 + i * 18, 112));
}

void CreativePickerMenu::setItems(std::vector<ItemStack> items) {
    items_ = std::move(items);
    scrollTo(0.0f);
}

void CreativePickerMenu::scrollTo(float offs) {
    const int rows = (static_cast<int>(items_.size()) + kColumns - 1) / kColumns - kRows;
    int first = static_cast<int>(static_cast<double>(offs * static_cast<float>(rows)) + 0.5);
    if (first < 0) first = 0;
    firstRow_ = first;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int at = col + (row + first) * kColumns;
            palette_.item(col + row * kColumns) = at >= 0 && at < static_cast<int>(items_.size()) ? items_[static_cast<std::size_t>(at)] : ItemStack();
        }
    }
}

float CreativePickerMenu::subtractInputFromScroll(float offs, double scrollY) const {
    const int rows = (static_cast<int>(items_.size()) + kColumns - 1) / kColumns - kRows;
    if (rows <= 0) return offs;
    const float step = static_cast<float>(scrollY) / static_cast<float>(rows);
    return std::clamp(offs - step, 0.0f, 1.0f);
}

ItemStack CreativePickerMenu::quickMoveStack(int index) {
    if (index >= kHotbarStart && index < slotCount()) {
        Slot& slot = *slots_[static_cast<std::size_t>(index)];
        if (slot.hasItem()) slot.setByPlayer(ItemStack());
    }
    return ItemStack();
}

// ---------------------------------------------------------------------------------------------------------------
// The creative inventory

CreativeInventory::CreativeInventory(InventoryMenu& inventoryMenu, MenuPlayer player, const CreativeTabs& tabs)
    : tabs_(tabs), inventory_(inventoryMenu), player_(player), picker_(player) {
    trash_ = std::make_unique<Slot>(trashContainer_, 0, 173, 112);
    trash_->index = 99;
    // The survival slots as this screen arranges them: the four armor slots in two columns, the offhand, the 27 and the hotbar.
    for (int i = InventoryMenu::kArmorStart; i < InventoryMenu::kSlotCount; ++i) {
        int x = 0, y = 0;
        if (i < InventoryMenu::kArmorEnd) {
            const int k = i - InventoryMenu::kArmorStart;
            x = 54 + (k / 2) * 54;
            y = 6 + (k % 2) * 27;
        } else if (i == InventoryMenu::kOffhandSlot) {
            x = 35;
            y = 20;
        } else {
            x = 9 + ((i - 9) % 9) * 18;
            y = i >= InventoryMenu::kHotbarStart ? 112 : 54 + ((i - 9) / 9) * 18;
        }
        inventoryViews_.push_back({&inventory_.slot(i), x, y});
    }
    inventoryViews_.push_back({trash_.get(), 173, 112});
    tab_ = &tabs.defaultTab();
    refreshItems();
}

ContainerMenu& CreativeInventory::activeMenu() {
    if (isInventoryTab()) return inventory_;
    return picker_;
}

void CreativeInventory::moveCarriedTo(ContainerMenu& from, ContainerMenu& to) {
    if (&from == &to) return;
    to.setCarried(from.carried());
    from.setCarried(ItemStack());
}

void CreativeInventory::refreshItems() {
    std::vector<ItemStack> items;
    if (tab_->type == CreativeTabType::Search) {
        for (const CreativeEntry* e : tabs_.search(search_)) items.push_back(e->stack);
    } else if (tab_->type == CreativeTabType::Category) {
        for (const CreativeEntry& e : tab_->entries) items.push_back(e.stack);
    }
    // (the saved hotbars of the "Saved Hotbars" tab are not kept: it lists nothing)
    picker_.setItems(std::move(items));
    scrollOffs_ = 0.0f;
}

void CreativeInventory::selectTab(const CreativeTab& tab) {
    const bool wasInventory = isInventoryTab();
    const CreativeTabType previous = tab_->type;
    tab_ = &tab;
    if (wasInventory != isInventoryTab()) {
        if (isInventoryTab()) {
            moveCarriedTo(picker_, inventory_);
        } else {
            moveCarriedTo(inventory_, picker_);
        }
    }
    if (tab.type == CreativeTabType::Search && previous != CreativeTabType::Search) search_.clear();
    if (tab.type != CreativeTabType::Search) search_.clear();
    refreshItems();
}

void CreativeInventory::setSearchText(const std::string& text) {
    search_ = text;
    refreshItems();
}

void CreativeInventory::setScroll(float offs) {
    scrollOffs_ = std::clamp(offs, 0.0f, 1.0f);
    picker_.scrollTo(scrollOffs_);
}

bool CreativeInventory::scrollBy(double wheelUp) {
    if (!canScroll()) return false;
    scrollOffs_ = picker_.subtractInputFromScroll(scrollOffs_, wheelUp);
    picker_.scrollTo(scrollOffs_);
    return true;
}

int CreativeInventory::rowCount() const { return (static_cast<int>(picker_.items().size()) + CreativePickerMenu::kColumns - 1) / CreativePickerMenu::kColumns; }

std::vector<SlotView> CreativeInventory::slotViews() {
    if (isInventoryTab()) return inventoryViews_;
    std::vector<SlotView> views;
    for (int i = 0; i < picker_.slotCount(); ++i) views.push_back({&picker_.slot(i), picker_.slot(i).x, picker_.slot(i).y});
    return views;
}

void CreativeInventory::close() {
    if (!isInventoryTab()) picker_.setCarried(ItemStack());  // what was taken from the grid is not a real stack: it just goes
    inventory_.removed();
}

// CreativeModeInventoryScreen.slotClicked, from the game's class.
void CreativeInventory::slotClicked(const SlotView* view, int slotId, int button, ClickType type, bool clickedOutside) {
    Slot* slot = view != nullptr ? view->slot : nullptr;
    const bool hasShift = type == ClickType::QuickMove;
    if (slotId == kOutsideSlot && type == ClickType::Pickup) type = ClickType::Throw;
    if (type == ClickType::Throw && !player_.canDrop) return;
    ContainerMenu& menu = activeMenu();
    Inventory& inventory = *player_.inventory;

    if (slot == nullptr && !isInventoryTab() && type != ClickType::QuickCraft) {
        // Beside the window with a stack on the cursor: throw it (the whole stack with the left button, one with the right).
        if (!menu.carried().isEmpty() && clickedOutside) {
            if (!player_.canDrop) return;
            if (button == 0) {
                player_.drop(menu.carried());
                menu.setCarried(ItemStack());
            }
            if (button == 1) player_.drop(menu.carried().split(1));
        }
        return;
    }
    if (slot != nullptr && !slot->mayPickup(player_)) return;

    if (slot == trash_.get() && hasShift) {  // shift + click on the trash: everything is destroyed
        for (int i = 0; i < inventory_.slotCount(); ++i) inventory_.slot(i).set(ItemStack());
        return;
    }
    if (isInventoryTab()) {
        if (slot == trash_.get()) {
            menu.setCarried(ItemStack());
        } else if (type == ClickType::Throw && slot != nullptr && slot->hasItem()) {
            const ItemStack removed = slot->remove(button == 0 ? 1 : slot->getItem().maxStackSize());
            player_.drop(removed);
        } else if (type == ClickType::Throw && slotId == kOutsideSlot && !menu.carried().isEmpty()) {
            player_.drop(menu.carried());
            menu.setCarried(ItemStack());
        } else {
            inventory_.clicked(slot == nullptr ? slotId : slot->index, button, type);
        }
        return;
    }
    if (type != ClickType::QuickCraft && slot != nullptr && slot->container == &picker_.palette()) {
        ItemStack& carried = menu.carried();
        const ItemStack slotItem = slot->getItem();
        if (type == ClickType::Swap) {  // a number key over an item of the grid: a full stack goes to that hotbar slot
            if (!slotItem.isEmpty() && button >= 0 && button < Inventory::kSize) inventory.setItem(button, slotItem.copyWithCount(slotItem.maxStackSize()));
            return;
        }
        if (type == ClickType::Clone) {  // middle button
            if (carried.isEmpty() && slot->hasItem()) menu.setCarried(slotItem.copyWithCount(slotItem.maxStackSize()));
            return;
        }
        if (type == ClickType::Throw) {  // Q: one, Ctrl+Q a full stack, thrown out of thin air
            if (!slotItem.isEmpty()) player_.drop(slotItem.copyWithCount(button == 0 ? 1 : slotItem.maxStackSize()));
            return;
        }
        if (!carried.isEmpty() && !slotItem.isEmpty() && isSameItemSameComponents(slotItem, carried)) {
            if (button == 0) {
                if (hasShift) {
                    carried.setCount(carried.maxStackSize());
                } else if (carried.count < carried.maxStackSize()) {
                    carried.grow(1);
                }
            } else {
                carried.shrink(1);
            }
        } else if (slotItem.isEmpty() || !carried.isEmpty()) {
            // An empty grid slot, or another item than the cursor holds: the cursor stack is put back (deleted), or one of it with
            // the right button.
            if (button == 0) {
                menu.setCarried(ItemStack());
            } else if (!carried.isEmpty()) {
                carried.shrink(1);
            }
        } else {
            // A click on an item with an empty cursor: one of it, or with shift a full stack.
            const int count = hasShift ? slotItem.maxStackSize() : slotItem.count;
            menu.setCarried(slotItem.copyWithCount(count));
        }
        return;
    }
    // The hotbar row (and a drag over it): the plain menu rules.
    menu.clicked(slot == nullptr ? slotId : slot->index, button, type);
}

}  // namespace apex
