#pragma once

#include "apex/world/item_stack.hpp"

#include <array>
#include <functional>
#include <vector>

namespace apex {

// Something that holds item stacks in numbered slots (the game's Container).
class Container {
public:
    virtual ~Container() = default;
    [[nodiscard]] virtual int size() const = 0;
    // The live stack of a slot (an empty scratch stack for a slot that does not exist, never to be kept).
    virtual ItemStack& item(int slot) = 0;
    [[nodiscard]] const ItemStack& item(int slot) const { return const_cast<Container*>(this)->item(slot); }
    virtual void setItem(int slot, ItemStack stack) { item(slot) = std::move(stack); setChanged(); }
    // Takes up to `count` items out of a slot (ContainerHelper.removeItem).
    virtual ItemStack removeItem(int slot, int count) {
        ItemStack& s = item(slot);
        if (s.isEmpty() || count <= 0) return ItemStack();
        ItemStack out = s.split(count);
        if (!out.isEmpty()) setChanged();
        return out;
    }
    // The most any slot of this container holds (Container.getMaxStackSize: 99).
    [[nodiscard]] virtual int maxStackSize() const { return 99; }
    virtual void setChanged() {}
    virtual void clearContent() {
        for (int i = 0; i < size(); ++i) item(i) = ItemStack();
        setChanged();
    }
    [[nodiscard]] bool isEmpty() const {
        for (int i = 0; i < size(); ++i) {
            if (!item(i).isEmpty()) return false;
        }
        return true;
    }

protected:
    ItemStack scratch_;  // handed out for slots that do not exist
};

// A plain row of slots; `onChanged` runs after every change (a crafting grid tells its menu that way).
class SimpleContainer : public Container {
public:
    explicit SimpleContainer(int size = 0) : items_(static_cast<std::size_t>(size)) {}
    [[nodiscard]] int size() const override { return static_cast<int>(items_.size()); }
    ItemStack& item(int slot) override {
        if (slot < 0 || slot >= size()) {
            scratch_ = ItemStack();
            return scratch_;
        }
        return items_[static_cast<std::size_t>(slot)];
    }
    void setChanged() override {
        if (onChanged) onChanged();
    }
    std::function<void()> onChanged;

protected:
    std::vector<ItemStack> items_;
};

// The one slot a crafting result sits in. Taking from it takes the whole stack (ResultContainer.removeItem).
class ResultContainer : public SimpleContainer {
public:
    ResultContainer() : SimpleContainer(1) {}
    ItemStack removeItem(int slot, int) override {
        if (slot != 0 || item(0).isEmpty()) return ItemStack();
        ItemStack out = std::move(item(0));
        item(0) = ItemStack();
        return out;
    }
};

// The player's inventory, numbered like the game's: 0-8 the hotbar, 9-35 the rest, 36-39 boots, leggings, chestplate
// and helmet, 40 the offhand.
class Inventory : public Container {
public:
    static constexpr int kHotbarSize = 9;
    static constexpr int kMainSize = 36;
    static constexpr int kFeet = 36, kLegs = 37, kChest = 38, kHead = 39, kOffhand = 40;
    static constexpr int kSize = 41;

    [[nodiscard]] int size() const override { return kSize; }
    ItemStack& item(int slot) override {
        if (slot < 0 || slot >= kSize) {
            scratch_ = ItemStack();
            return scratch_;
        }
        return slots_[static_cast<std::size_t>(slot)];
    }
    [[nodiscard]] static bool isHotbarSlot(int slot) { return slot >= 0 && slot < kHotbarSize; }

    // The hotbar slot in the hand (0-8).
    int selected = 0;
    // Creative mode: Player.hasInfiniteMaterials, which makes add() always succeed.
    bool infiniteMaterials = false;

    [[nodiscard]] ItemStack& selectedItem() { return slots_[static_cast<std::size_t>(selected)]; }
    [[nodiscard]] ItemStack& offhand() { return slots_[kOffhand]; }
    void setSelected(int slot) { selected = ((slot % kHotbarSize) + kHotbarSize) % kHotbarSize; }

    // The first empty slot of the 36 (the hotbar first), or -1.
    [[nodiscard]] int freeSlot() const;
    // Inventory.getSlotWithRemainingSpace: the held slot, then the offhand, then the 36 in order: the first that holds
    // the same item with room left; -1 for none.
    [[nodiscard]] int slotWithRemainingSpace(const ItemStack& stack) const;
    // Inventory.add: puts the stack away (onto stacks that have room, then empty slots) and leaves what did not fit in
    // `stack`. True if anything was stored. With infinite materials it always succeeds (and takes the whole stack).
    bool add(ItemStack& stack);
    bool add(int slot, ItemStack& stack);
    // Inventory.findSlotMatchingItem: the first of the 36 holding the same item and components, or -1.
    [[nodiscard]] int findSlotMatching(const ItemStack& stack) const;
    // Inventory.getSuitableHotbarSlot: from the held slot on, the first empty hotbar slot (else the held one).
    [[nodiscard]] int suitableHotbarSlot() const;
    // Inventory.pickSlot: the stack at `index` swaps with the one in a suitable hotbar slot, which becomes the held one.
    void pickSlot(int index);
    // Inventory.addAndPickItem (creative pick block): the stack goes into a suitable hotbar slot and is held; what was
    // there moves to a free slot if there is one.
    void addAndPick(ItemStack stack);
    // Takes `count` from the held slot (Q drops one, Ctrl+Q the whole stack).
    ItemStack removeFromSelected(bool wholeStack);
    // Total of one item across the 36 + offhand (for tests and fuel).
    [[nodiscard]] int countOf(Item item) const;
    // Inventory.placeItemBackInInventory: what a closing menu does with a stack it still holds: onto stacks with room,
    // then empty slots; whatever does not fit goes to `drop` (the game throws it on the ground).
    void placeItemBack(ItemStack stack, const std::function<void(const ItemStack&)>& drop);

private:
    [[nodiscard]] bool hasRemainingSpaceFor(const ItemStack& destination, const ItemStack& origin) const;
    int addResource(ItemStack& stack);
    int addResource(int slot, ItemStack& stack);
    std::array<ItemStack, kSize> slots_;
};

}  // namespace apex
