#pragma once

#include "falcon/world/item_stack.hpp"

#include <vector>

namespace falcon {

class Inventory;
class InventoryMenu;

// The recipe book of the survival inventory (RecipeBookComponent), cut down to what the inventory's 2 x 2 grid can make: the recipes the player can
// make right now with what the inventory holds, each one laid out as the grid it fills, and putting one of them into that grid.
//
// It knows nothing of what is on screen: the inventory screen draws the page it asks for and tells it what was clicked.
class RecipeBook {
public:
    struct Entry {
        std::vector<ItemStack> grid;  // the ingredients as they go into the grid, row by row (an empty cell holds nothing)
        ItemStack result;
    };

    static constexpr int kColumns = 4, kRows = 5, kPerPage = kColumns * kRows;

    [[nodiscard]] bool open() const { return open_; }
    // Opening looks the recipes up again, so what it lists is what the inventory holds now.
    void setOpen(bool open, const Inventory& inventory);
    void toggle(const Inventory& inventory) { setOpen(!open_, inventory); }

    // The recipes the 2 x 2 grid can make from the inventory, in the game's own order (the shaped ones first), looked up again.
    void refresh(const Inventory& inventory);
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] int page() const { return page_; }
    void scroll(int steps);
    // How many times the recipe can be made with what the inventory holds (0: not any more); the count on its button.
    [[nodiscard]] int craftableCount(int index, const Inventory& inventory) const;
    // Puts one of each ingredient into the menu's crafting grid, taking them out of the inventory; what the grid held goes back. False when the
    // inventory no longer holds everything it takes.
    bool place(int index, InventoryMenu& menu, Inventory& inventory) const;

private:
    bool open_ = false;
    int page_ = 0;
    std::vector<Entry> entries_;
};

}  // namespace falcon
