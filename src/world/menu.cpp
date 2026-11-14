#include "apex/world/menu.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apex {
namespace {
constexpr int kMax = std::numeric_limits<int>::max();

// Player.getEquipmentSlotForItem: where a stack is worn; anything that is not equippable goes in the hand.
EquipSlot equipmentSlotForItem(const ItemStack& stack) {
    const EquipSlot slot = stack.type().equipSlot;
    return slot == EquipSlot::None ? EquipSlot::MainHand : slot;
}

bool isHumanoidArmor(EquipSlot s) { return s == EquipSlot::Head || s == EquipSlot::Chest || s == EquipSlot::Legs || s == EquipSlot::Feet; }

// EquipSlot.getIndex for the four armor pieces: feet 0, legs 1, chest 2, head 3.
int armorIndex(EquipSlot s) {
    switch (s) {
        case EquipSlot::Feet: return 0;
        case EquipSlot::Legs: return 1;
        case EquipSlot::Chest: return 2;
        default: return 3;
    }
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Slot

bool Slot::tryRemove(int amount, int maxAmount, const MenuPlayer& player, ItemStack& out) {
    if (!mayPickup(player)) return false;
    if (!allowModification(player) && maxAmount < getItem().count) return false;
    amount = std::min(amount, maxAmount);
    ItemStack removed = remove(amount);
    if (removed.isEmpty()) return false;
    if (getItem().isEmpty()) setByPlayer(ItemStack());
    out = std::move(removed);
    return true;
}

ItemStack Slot::safeTake(int amount, int maxAmount, const MenuPlayer& player) {
    ItemStack taken;
    if (!tryRemove(amount, maxAmount, player, taken)) return ItemStack();
    onTake(player, taken);
    return taken;
}

ItemStack Slot::safeInsert(ItemStack stack, int increment) {
    if (stack.isEmpty() || !mayPlace(stack)) return stack;
    ItemStack& current = getItem();
    const int room = std::min(std::min(increment, stack.count), maxStackSize(stack) - current.count);
    if (room <= 0) return stack;
    if (current.isEmpty()) {
        setByPlayer(stack.split(room));
    } else if (isSameItemSameComponents(current, stack)) {
        stack.shrink(room);
        ItemStack grown = current;
        grown.grow(room);
        setByPlayer(std::move(grown));
    }
    return stack;
}

// ---------------------------------------------------------------------------------------------------------------
// ContainerMenu

Slot& ContainerMenu::addSlot(std::unique_ptr<Slot> slot) {
    slot->index = static_cast<int>(slots_.size());
    slots_.push_back(std::move(slot));
    return *slots_.back();
}

void ContainerMenu::addStandardInventorySlots(Container& inventory, int x, int y) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 9; ++col) addSlot(std::make_unique<Slot>(inventory, col + (row + 1) * 9, x + col * 18, y + row * 18));
    }
    for (int i = 0; i < 9; ++i) addSlot(std::make_unique<Slot>(inventory, i, x + i * 18, y + 58));
}

void ContainerMenu::resetQuickCraft() {
    quickcraftStatus_ = 0;
    quickcraftSlots_.clear();
}

bool ContainerMenu::isValidQuickcraftType(int type, const MenuPlayer& player) {
    if (type == 0 || type == 1) return true;
    return type == 2 && player.infiniteMaterials;
}

bool ContainerMenu::canItemQuickReplace(const Slot* slot, const ItemStack& stack, bool ignoreSize) {
    const bool empty = slot == nullptr || !slot->hasItem();
    if (!empty && isSameItemSameComponents(stack, slot->getItem())) {
        return slot->getItem().count + (ignoreSize ? 0 : stack.count) <= stack.maxStackSize();
    }
    return empty;
}

int ContainerMenu::getQuickCraftPlaceCount(int slotCount, int type, const ItemStack& stack) {
    switch (type) {
        case 0: return static_cast<int>(std::floor(static_cast<float>(stack.count) / static_cast<float>(slotCount)));
        case 1: return 1;
        case 2: return stack.maxStackSize();
        default: return stack.count;
    }
}

void ContainerMenu::clicked(int slotIndex, int button, ClickType type) {
    if (slotIndex != kOutsideSlot && (slotIndex < -1 || slotIndex >= slotCount())) return;
    doClick(slotIndex, button, type);
}

bool ContainerMenu::moveItemStackTo(ItemStack& stack, int start, int end, bool reverse) {
    bool changed = false;
    int i = reverse ? end - 1 : start;
    if (stack.isStackable()) {
        while (!stack.isEmpty() && (reverse ? i >= start : i < end)) {
            Slot& slot = *slots_[static_cast<std::size_t>(i)];
            ItemStack& target = slot.getItem();
            if (!target.isEmpty() && isSameItemSameComponents(stack, target)) {
                const int total = target.count + stack.count;
                const int max = slot.maxStackSize(target);
                if (total <= max) {
                    stack.setCount(0);
                    target.setCount(total);
                    slot.setChanged();
                    changed = true;
                } else if (target.count < max) {
                    stack.shrink(max - target.count);
                    target.setCount(max);
                    slot.setChanged();
                    changed = true;
                }
            }
            i += reverse ? -1 : 1;
        }
    }
    if (!stack.isEmpty()) {
        i = reverse ? end - 1 : start;
        while (reverse ? i >= start : i < end) {
            Slot& slot = *slots_[static_cast<std::size_t>(i)];
            if (slot.getItem().isEmpty() && slot.mayPlace(stack)) {
                const int max = slot.maxStackSize(stack);
                slot.setByPlayer(stack.split(std::min(stack.count, max)));
                slot.setChanged();
                changed = true;
                break;
            }
            i += reverse ? -1 : 1;
        }
    }
    return changed;
}

void ContainerMenu::clearContainer(Container& container) {
    for (int i = 0; i < container.size(); ++i) {
        ItemStack stack = std::move(container.item(i));
        container.item(i) = ItemStack();
        if (stack.isEmpty() || player_.inventory == nullptr) continue;
        player_.inventory->placeItemBack(std::move(stack), [this](const ItemStack& s) { player_.drop(s); });
    }
}

void ContainerMenu::removed() {
    if (!carried_.isEmpty() && player_.inventory != nullptr) {
        player_.inventory->placeItemBack(carried_, [this](const ItemStack& s) { player_.drop(s); });
    }
    carried_ = ItemStack();
}

// AbstractContainerMenu.doClick, step by step.
void ContainerMenu::doClick(int slotIndex, int button, ClickType type) {
    Inventory* inventory = player_.inventory;
    if (type == ClickType::QuickCraft) {
        const int previous = quickcraftStatus_;
        quickcraftStatus_ = getQuickcraftHeader(button);
        if ((previous != 1 || quickcraftStatus_ != 2) && previous != quickcraftStatus_) {
            resetQuickCraft();
        } else if (carried_.isEmpty()) {
            resetQuickCraft();
        } else if (quickcraftStatus_ == kDragStart) {
            quickcraftType_ = getQuickcraftType(button);
            if (isValidQuickcraftType(quickcraftType_, player_)) {
                quickcraftStatus_ = 1;
                quickcraftSlots_.clear();
            } else {
                resetQuickCraft();
            }
        } else if (quickcraftStatus_ == kDragAdd) {
            if (slotIndex < 0 || slotIndex >= slotCount()) return;
            Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
            if (canItemQuickReplace(&slot, carried_, true) && slot.mayPlace(carried_) &&
                (quickcraftType_ == 2 || carried_.count > static_cast<int>(quickcraftSlots_.size())) && canDragTo(slot)) {
                if (std::find(quickcraftSlots_.begin(), quickcraftSlots_.end(), slot.index) == quickcraftSlots_.end()) quickcraftSlots_.push_back(slot.index);
            }
        } else if (quickcraftStatus_ == kDragEnd) {
            if (!quickcraftSlots_.empty()) {
                if (quickcraftSlots_.size() == 1) {
                    const int only = quickcraftSlots_.front();
                    resetQuickCraft();
                    doClick(only, quickcraftType_, ClickType::Pickup);
                    return;
                }
                ItemStack copy = carried_;
                if (copy.isEmpty()) {
                    resetQuickCraft();
                    return;
                }
                int count = carried_.count;
                for (const int index : quickcraftSlots_) {
                    Slot& slot = *slots_[static_cast<std::size_t>(index)];
                    if (canItemQuickReplace(&slot, carried_, true) && slot.mayPlace(carried_) &&
                        (quickcraftType_ == 2 || carried_.count >= static_cast<int>(quickcraftSlots_.size())) && canDragTo(slot)) {
                        const int have = slot.hasItem() ? slot.getItem().count : 0;
                        const int max = std::min(copy.maxStackSize(), slot.maxStackSize(copy));
                        const int put = std::min(getQuickCraftPlaceCount(static_cast<int>(quickcraftSlots_.size()), quickcraftType_, copy) + have, max);
                        count -= put - have;
                        slot.setByPlayer(copy.copyWithCount(put));
                    }
                }
                copy.setCount(count);
                setCarried(std::move(copy));
            }
            resetQuickCraft();
        } else {
            resetQuickCraft();
        }
        return;
    }
    if (quickcraftStatus_ != 0) {
        resetQuickCraft();
        return;
    }

    if ((type == ClickType::Pickup || type == ClickType::QuickMove) && (button == 0 || button == 1)) {
        const bool primary = button == 0;
        if (slotIndex == kOutsideSlot) {
            if (!carried_.isEmpty()) {
                if (primary) {
                    player_.drop(carried_);
                    setCarried(ItemStack());
                } else {
                    player_.drop(carried_.split(1));
                }
            }
        } else if (type == ClickType::QuickMove) {
            if (slotIndex < 0) return;
            Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
            if (!slot.mayPickup(player_)) return;
            ItemStack moved = quickMoveStack(slotIndex);
            while (!moved.isEmpty() && isSameItem(slot.getItem(), moved)) moved = quickMoveStack(slotIndex);
        } else {
            if (slotIndex < 0) return;
            Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
            const ItemStack stack = slot.getItem();
            if (stack.isEmpty()) {
                if (!carried_.isEmpty()) {
                    const int n = primary ? carried_.count : 1;
                    setCarried(slot.safeInsert(carried_, n));
                }
            } else if (slot.mayPickup(player_)) {
                if (carried_.isEmpty()) {
                    const int n = primary ? stack.count : (stack.count + 1) / 2;
                    ItemStack removed;
                    if (slot.tryRemove(n, kMax, player_, removed)) {
                        setCarried(removed);
                        slot.onTake(player_, removed);
                    }
                } else if (slot.mayPlace(carried_)) {
                    if (isSameItemSameComponents(stack, carried_)) {
                        const int n = primary ? carried_.count : 1;
                        setCarried(slot.safeInsert(carried_, n));
                    } else if (carried_.count <= slot.maxStackSize(carried_)) {
                        ItemStack oldCarried = carried_;
                        setCarried(stack);
                        slot.setByPlayer(std::move(oldCarried));
                    }
                } else if (isSameItemSameComponents(stack, carried_)) {
                    ItemStack removed;
                    if (slot.tryRemove(stack.count, carried_.maxStackSize() - carried_.count, player_, removed)) {
                        carried_.grow(removed.count);
                        slot.onTake(player_, removed);
                    }
                }
            }
            slot.setChanged();
        }
    } else if (type == ClickType::Swap && inventory != nullptr && ((button >= 0 && button < 9) || button == 40)) {
        if (slotIndex < 0) return;
        Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
        ItemStack item = inventory->item(button);
        ItemStack slotItem = slot.getItem();
        if (!item.isEmpty() || !slotItem.isEmpty()) {
            if (item.isEmpty()) {
                if (slot.mayPickup(player_)) {
                    inventory->setItem(button, slotItem);
                    slot.onSwapCraft(slotItem.count);
                    slot.setByPlayer(ItemStack());
                    slot.onTake(player_, slotItem);
                }
            } else if (slotItem.isEmpty()) {
                if (slot.mayPlace(item)) {
                    const int max = slot.maxStackSize(item);
                    if (item.count > max) {
                        ItemStack part = inventory->item(button).split(max);
                        slot.setByPlayer(std::move(part));
                    } else {
                        inventory->setItem(button, ItemStack());
                        slot.setByPlayer(item);
                    }
                }
            } else if (slot.mayPickup(player_) && slot.mayPlace(item)) {
                const int max = slot.maxStackSize(item);
                if (item.count > max) {
                    ItemStack part = inventory->item(button).split(max);
                    slot.setByPlayer(std::move(part));
                    slot.onTake(player_, slotItem);
                    if (!inventory->add(slotItem)) player_.drop(slotItem);
                } else {
                    inventory->setItem(button, slotItem);
                    slot.setByPlayer(item);
                    slot.onTake(player_, slotItem);
                }
            }
        }
    } else if (type == ClickType::Clone && player_.infiniteMaterials && carried_.isEmpty() && slotIndex >= 0) {
        Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
        if (slot.hasItem()) setCarried(slot.safeClone());
    } else if (type == ClickType::Throw && carried_.isEmpty() && slotIndex >= 0) {
        Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
        const int n = button == 0 ? 1 : slot.getItem().count;
        if (!player_.canDrop) return;
        ItemStack thrown = slot.safeTake(n, kMax, player_);
        player_.drop(thrown);
        if (button == 1) {
            while (!thrown.isEmpty() && isSameItem(slot.getItem(), thrown)) {
                if (!player_.canDrop) return;
                thrown = slot.safeTake(n, kMax, player_);
                player_.drop(thrown);
            }
        }
    } else if (type == ClickType::PickupAll && slotIndex >= 0) {
        Slot& slot = *slots_[static_cast<std::size_t>(slotIndex)];
        if (!carried_.isEmpty() && (!slot.hasItem() || !slot.mayPickup(player_))) {
            const int first = button == 0 ? 0 : slotCount() - 1;
            const int step = button == 0 ? 1 : -1;
            for (int pass = 0; pass < 2; ++pass) {
                for (int i = first; i >= 0 && i < slotCount() && carried_.count < carried_.maxStackSize(); i += step) {
                    Slot& other = *slots_[static_cast<std::size_t>(i)];
                    if (other.hasItem() && canItemQuickReplace(&other, carried_, true) && other.mayPickup(player_) && canTakeItemForPickAll(carried_, other)) {
                        const ItemStack& item = other.getItem();
                        if (pass != 0 || item.count != item.maxStackSize()) {
                            const ItemStack taken = other.safeTake(item.count, carried_.maxStackSize() - carried_.count, player_);
                            carried_.grow(taken.count);
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The survival inventory

void ResultSlot::onTake(const MenuPlayer&, const ItemStack&) {
    // Uses up one of each ingredient: the crafted part of the grid (the game's CraftingInput.Positioned).
    std::vector<ItemStack> grid;
    for (int i = 0; i < gridWidth_ * gridHeight_; ++i) grid.push_back(craftSlots_.item(i));
    const CraftingInput input = CraftingInput::fromGrid(gridWidth_, gridHeight_, grid);
    for (int row = 0; row < input.height; ++row) {
        for (int col = 0; col < input.width; ++col) {
            const int cell = (col + input.left) + (row + input.top) * gridWidth_;
            if (!craftSlots_.item(cell).isEmpty()) craftSlots_.removeItem(cell, 1);
        }
    }
}

InventoryMenu::InventoryMenu(MenuPlayer player) : ContainerMenu(std::move(player)) {
    Inventory& inv = *player_.inventory;
    craftSlots_.onChanged = [this] { slotsChanged(); };
    addSlot(std::make_unique<ResultSlot>(resultSlots_, craftSlots_, 2, 2, 154, 28));
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) addSlot(std::make_unique<Slot>(craftSlots_, col + row * 2, 98 + col * 18, 18 + row * 18));
    }
    static constexpr EquipSlot kWorn[4] = {EquipSlot::Head, EquipSlot::Chest, EquipSlot::Legs, EquipSlot::Feet};
    static constexpr const char* kIcons[4] = {"container/slot/helmet", "container/slot/chestplate", "container/slot/leggings", "container/slot/boots"};
    for (int i = 0; i < 4; ++i) addSlot(std::make_unique<ArmorSlot>(inv, 39 - i, kWorn[i], kIcons[i], 8, 8 + i * 18));
    addStandardInventorySlots(inv, 8, 84);
    addSlot(std::make_unique<OffhandSlot>(inv, 77, 62));
}

bool InventoryMenu::canTakeItemForPickAll(const ItemStack&, const Slot& slot) const { return slot.container != &resultSlots_; }

void InventoryMenu::updateResult() {
    if (updating_) return;
    updating_ = true;
    std::vector<ItemStack> grid;
    for (int i = 0; i < 4; ++i) grid.push_back(craftSlots_.item(i));
    ItemStack result;
    if (const auto recipes = currentRecipeManager()) {
        if (const auto match = recipes->find(CraftingInput::fromGrid(2, 2, grid))) result = match->result;
    }
    resultSlots_.item(0) = std::move(result);
    updating_ = false;
}

void InventoryMenu::slotsChanged() { updateResult(); }

void InventoryMenu::removed() {
    ContainerMenu::removed();
    resultSlots_.clearContent();
    craftSlots_.onChanged = nullptr;  // putting the grid back away must not work out recipes
    clearContainer(craftSlots_);
    craftSlots_.onChanged = [this] { slotsChanged(); };
}

// InventoryMenu.quickMoveStack.
ItemStack InventoryMenu::quickMoveStack(int index) {
    ItemStack original;
    Slot& slot = *slots_[static_cast<std::size_t>(index)];
    if (!slot.hasItem()) return original;
    ItemStack& stack = slot.getItem();
    original = stack;
    const EquipSlot equip = equipmentSlotForItem(stack);
    if (index == kResult) {
        if (!moveItemStackTo(stack, 9, 45, true)) return ItemStack();
        slot.onQuickCraft(stack, original);
    } else if (index >= kCraftStart && index < kCraftEnd) {
        if (!moveItemStackTo(stack, 9, 45, false)) return ItemStack();
    } else if (index >= kArmorStart && index < kArmorEnd) {
        if (!moveItemStackTo(stack, 9, 45, false)) return ItemStack();
    } else if (isHumanoidArmor(equip) && !slots_[static_cast<std::size_t>(8 - armorIndex(equip))]->hasItem()) {
        const int target = 8 - armorIndex(equip);
        if (!moveItemStackTo(stack, target, target + 1, false)) return ItemStack();
    } else if (equip == EquipSlot::OffHand && !slots_[kOffhandSlot]->hasItem()) {
        if (!moveItemStackTo(stack, 45, 46, false)) return ItemStack();
    } else if (index >= kInvStart && index < kInvEnd) {
        if (!moveItemStackTo(stack, 36, 45, false)) return ItemStack();
    } else if (index >= kHotbarStart && index < kHotbarEnd) {
        if (!moveItemStackTo(stack, 9, 36, false)) return ItemStack();
    } else if (!moveItemStackTo(stack, 9, 45, false)) {
        return ItemStack();
    }
    if (stack.isEmpty()) {
        slot.setByPlayer(ItemStack());
    } else {
        slot.setChanged();
    }
    if (stack.count == original.count) return ItemStack();
    const ItemStack leftover = stack;
    slot.onTake(player_, leftover);
    if (index == kResult) player_.drop(leftover);
    return original;
}

}  // namespace apex
