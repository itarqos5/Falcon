#pragma once

#include "falcon/world/item_registry.hpp"

#include <algorithm>
#include <string>

namespace falcon {

// A pile of one kind of item, like the game's ItemStack: the item, how many, its wear, and the few "components" Falcon
// keeps (a custom name, and the raw text of any other components, which only decides whether two stacks are alike).
// An empty stack is Air or a count of 0 or less; any operation may leave one behind, like the game's.
struct ItemStack {
    Item item = Item::Air;
    int count = 0;
    int damage = 0;           // wear (the damage component), 0..maxDamage
    std::string customName;   // an anvil name: shown in italics
    std::string components;   // other components as the game wrote them (SNBT); "" for none
    std::string variantName;  // the name those components give it ("Potion of Healing"); not part of what makes stacks alike
    int tintColor = -1;       // 0xRRGGBB from the stack's own components (dyed leather, a potion with a colour of its own), or -1: the item model's own colour then

    ItemStack() = default;
    explicit ItemStack(Item i, int n = 1) : item(i), count(n) {}

    [[nodiscard]] bool isEmpty() const { return item == Item::Air || count <= 0; }
    [[nodiscard]] const ItemType& type() const { return itemRegistry().item(item); }
    [[nodiscard]] int maxStackSize() const { return type().maxStack; }
    [[nodiscard]] int maxDamage() const { return type().maxDamage; }
    [[nodiscard]] bool isDamageable() const { return !isEmpty() && maxDamage() > 0; }
    [[nodiscard]] bool isDamaged() const { return isDamageable() && damage > 0; }
    // ItemStack.isStackable: more than one fits in a slot, and a worn item never stacks.
    [[nodiscard]] bool isStackable() const { return maxStackSize() > 1 && (!isDamageable() || !isDamaged()); }
    [[nodiscard]] bool is(Item other) const { return item == other; }

    void grow(int n) { count += n; }
    void shrink(int n) { count -= n; }
    void setCount(int n) { count = n; }
    [[nodiscard]] ItemStack copy() const { return *this; }
    [[nodiscard]] ItemStack copyWithCount(int n) const {
        if (isEmpty()) return ItemStack();
        ItemStack c = *this;
        c.count = n;
        return c;
    }
    // Takes up to n items off this stack and returns them.
    ItemStack split(int n) {
        const int taken = std::min(n, count);
        ItemStack out = copyWithCount(taken);
        shrink(taken);
        return out;
    }

    // The bar under a worn item: its width in pixels (0..13) and its colour (0xRRGGBB, green to red).
    [[nodiscard]] int barWidth() const;
    [[nodiscard]] std::uint32_t barColor() const;
    // The name the item shows ("Diamond Pickaxe") needs the language: see itemDisplayName() in item_names.hpp.
};

// ItemStack.isSameItem: the same kind of item whatever the count and components.
[[nodiscard]] inline bool isSameItem(const ItemStack& a, const ItemStack& b) { return a.item == b.item; }
// ItemStack.isSameItemSameComponents: the same kind of item with the same wear, name and components; two empty
// stacks are alike.
[[nodiscard]] inline bool isSameItemSameComponents(const ItemStack& a, const ItemStack& b) {
    if (a.item != b.item) return false;
    if (a.isEmpty() && b.isEmpty()) return true;
    return a.damage == b.damage && a.customName == b.customName && a.components == b.components;
}

}  // namespace falcon
