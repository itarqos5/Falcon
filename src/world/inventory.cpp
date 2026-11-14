#include "apex/world/inventory.hpp"

namespace apex {

int Inventory::freeSlot() const {
    for (int i = 0; i < kMainSize; ++i) {
        if (slots_[static_cast<std::size_t>(i)].isEmpty()) return i;
    }
    return -1;
}

bool Inventory::hasRemainingSpaceFor(const ItemStack& destination, const ItemStack& origin) const {
    return !destination.isEmpty() && isSameItemSameComponents(destination, origin) && destination.isStackable() &&
           destination.count < std::min(maxStackSize(), destination.maxStackSize());
}

int Inventory::slotWithRemainingSpace(const ItemStack& stack) const {
    if (hasRemainingSpaceFor(slots_[static_cast<std::size_t>(selected)], stack)) return selected;
    if (hasRemainingSpaceFor(slots_[kOffhand], stack)) return kOffhand;
    for (int i = 0; i < kMainSize; ++i) {
        if (hasRemainingSpaceFor(slots_[static_cast<std::size_t>(i)], stack)) return i;
    }
    return -1;
}

// Puts as much of `stack` as fits into slot `slot` and returns how many are left over (Inventory.addResource).
int Inventory::addResource(int slot, ItemStack& stack) {
    int remaining = stack.count;
    ItemStack& target = item(slot);
    if (target.isEmpty()) target = stack.copyWithCount(0);
    const int room = std::min(maxStackSize(), target.maxStackSize()) - target.count;
    const int moved = std::min(remaining, room);
    if (moved == 0) return remaining;
    remaining -= moved;
    target.grow(moved);
    return remaining;
}

int Inventory::addResource(ItemStack& stack) {
    int slot = slotWithRemainingSpace(stack);
    if (slot == -1) slot = freeSlot();
    return slot == -1 ? stack.count : addResource(slot, stack);
}

bool Inventory::add(ItemStack& stack) { return add(-1, stack); }

bool Inventory::add(int slot, ItemStack& stack) {
    if (stack.isEmpty()) return false;
    if (stack.isDamaged()) {  // a worn item does not stack: it needs a slot of its own
        if (slot == -1) slot = freeSlot();
        if (slot >= 0) {
            item(slot) = std::move(stack);
            stack = ItemStack();
            return true;
        }
        if (infiniteMaterials) {
            stack.setCount(0);
            return true;
        }
        return false;
    }
    int before;
    do {
        before = stack.count;
        stack.setCount(slot == -1 ? addResource(stack) : addResource(slot, stack));
    } while (!stack.isEmpty() && stack.count < before);
    if (stack.count == before && infiniteMaterials) {
        stack.setCount(0);
        return true;
    }
    return stack.count < before;
}

int Inventory::findSlotMatching(const ItemStack& stack) const {
    for (int i = 0; i < kMainSize; ++i) {
        const ItemStack& s = slots_[static_cast<std::size_t>(i)];
        if (!s.isEmpty() && isSameItemSameComponents(stack, s)) return i;
    }
    return -1;
}

int Inventory::suitableHotbarSlot() const {
    for (int i = 0; i < kHotbarSize; ++i) {
        const int j = (selected + i) % kHotbarSize;
        if (slots_[static_cast<std::size_t>(j)].isEmpty()) return j;
    }
    return selected;  // (the game prefers an unenchanted stack to a held enchanted one; nothing is enchanted here)
}

void Inventory::pickSlot(int index) {
    setSelected(suitableHotbarSlot());
    std::swap(slots_[static_cast<std::size_t>(selected)], slots_[static_cast<std::size_t>(index)]);
}

void Inventory::addAndPick(ItemStack stack) {
    setSelected(suitableHotbarSlot());
    if (!slots_[static_cast<std::size_t>(selected)].isEmpty()) {
        const int free = freeSlot();
        if (free != -1) slots_[static_cast<std::size_t>(free)] = slots_[static_cast<std::size_t>(selected)];
    }
    slots_[static_cast<std::size_t>(selected)] = std::move(stack);
}

ItemStack Inventory::removeFromSelected(bool wholeStack) {
    ItemStack& held = selectedItem();
    if (held.isEmpty()) return ItemStack();
    return removeItem(selected, wholeStack ? held.count : 1);
}

void Inventory::placeItemBack(ItemStack stack, const std::function<void(const ItemStack&)>& drop) {
    while (!stack.isEmpty()) {
        int slot = slotWithRemainingSpace(stack);
        if (slot == -1) slot = freeSlot();
        if (slot == -1) {
            if (drop) drop(stack);
            return;
        }
        const int room = stack.maxStackSize() - item(slot).count;
        ItemStack part = stack.split(room);
        add(slot, part);
        if (!part.isEmpty() && room <= 0) {  // cannot happen (a slot with room was chosen); never loop for ever
            if (drop) drop(part);
            return;
        }
    }
}

int Inventory::countOf(Item wanted) const {
    int total = 0;
    for (int i = 0; i < kMainSize; ++i) {
        const ItemStack& s = slots_[static_cast<std::size_t>(i)];
        if (!s.isEmpty() && s.item == wanted) total += s.count;
    }
    const ItemStack& off = slots_[kOffhand];
    if (!off.isEmpty() && off.item == wanted) total += off.count;
    return total;
}

}  // namespace apex
