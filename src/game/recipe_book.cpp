#include "falcon/game/recipe_book.hpp"

#include "falcon/world/inventory.hpp"
#include "falcon/world/menu.hpp"
#include "falcon/world/recipes.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace falcon {
namespace {

// The first slot of the 36 (or the offhand) holding this item, or -1.
int slotOf(const Inventory& inventory, Item item) {
    for (int slot = 0; slot < 36; ++slot) {
        const ItemStack& stack = inventory.item(slot);
        if (!stack.isEmpty() && stack.item == item) return slot;
    }
    const ItemStack& off = inventory.offhand();
    return !off.isEmpty() && off.item == item ? Inventory::kOffhand : -1;
}

bool takeOne(Inventory& inventory, Item item) {
    const int slot = slotOf(inventory, item);
    if (slot < 0) return false;
    ItemStack& stack = slot == Inventory::kOffhand ? inventory.offhand() : inventory.item(slot);
    stack.shrink(1);
    if (stack.isEmpty()) stack = ItemStack();
    return true;
}

// What goes in each cell of the grid for a recipe's ingredients: the first item each one accepts that the inventory still has spare (a recipe can
// ask for the same item in two cells, which takes two). False when one of them cannot be had.
bool chooseCells(const std::vector<Ingredient>& ingredients, const Inventory& inventory, std::vector<ItemStack>& out) {
    out.assign(ingredients.size(), ItemStack());
    std::map<Item, int> taken;  // how many of each item the cells chosen so far take
    for (std::size_t cell = 0; cell < ingredients.size(); ++cell) {
        if (ingredients[cell].empty()) continue;
        bool chosen = false;
        for (const std::uint16_t id : ingredients[cell].items) {
            const Item item = static_cast<Item>(id);
            if (inventory.countOf(item) <= taken[item]) continue;
            ++taken[item];
            out[cell] = ItemStack(item, 1);
            chosen = true;
            break;
        }
        if (!chosen) return false;
    }
    return true;
}

bool buildEntry(const std::vector<Ingredient>& ingredients, const ItemStack& result, const Inventory& inventory, RecipeBook::Entry& entry) {
    if (result.isEmpty() || ingredients.empty()) return false;
    entry.result = result;
    return chooseCells(ingredients, inventory, entry.grid);
}

}  // namespace

int RecipeBook::pageCount() const { return (static_cast<int>(entries_.size()) + kPerPage - 1) / kPerPage; }

void RecipeBook::setOpen(bool open, const Inventory& inventory) {
    open_ = open;
    page_ = 0;
    if (open) refresh(inventory);
}

// RecipeManager.getRecipesToDisplay plus what RecipeBookComponent shows: the ones this grid can hold (2 x 2 at most) that the inventory has the
// ingredients for. A recipe that needs the 3 x 3 grid is not listed here; it is the crafting table's, not the inventory's.
void RecipeBook::refresh(const Inventory& inventory) {
    entries_.clear();
    const RecipeManager* recipes = recipeManager();
    if (recipes == nullptr) {
        page_ = 0;
        return;
    }
    for (const ShapedRecipe& recipe : recipes->shaped()) {
        if (recipe.width < 1 || recipe.height < 1 || recipe.width > 2 || recipe.height > 2) continue;
        Entry entry;
        if (buildEntry(recipe.cells, recipe.result, inventory, entry)) entries_.push_back(std::move(entry));
    }
    for (const ShapelessRecipe& recipe : recipes->shapeless()) {
        if (recipe.ingredients.size() > 4) continue;
        Entry entry;
        if (buildEntry(recipe.ingredients, recipe.result, inventory, entry)) entries_.push_back(std::move(entry));
    }
    page_ = std::clamp(page_, 0, std::max(0, pageCount() - 1));
}

void RecipeBook::scroll(int steps) {
    const int pages = pageCount();
    if (pages <= 1) return;
    page_ = std::clamp(page_ + steps, 0, pages - 1);
}

int RecipeBook::craftableCount(int index, const Inventory& inventory) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return 0;
    int times = -1;
    for (const ItemStack& stack : entries_[static_cast<std::size_t>(index)].grid) {
        if (stack.isEmpty()) continue;
        const int held = inventory.countOf(stack.item);
        times = times < 0 ? held : std::min(times, held);
    }
    return std::max(0, times);
}

bool RecipeBook::place(int index, InventoryMenu& menu, Inventory& inventory) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return false;
    const Entry& entry = entries_[static_cast<std::size_t>(index)];
    for (const ItemStack& stack : entry.grid) {
        if (!stack.isEmpty() && slotOf(inventory, stack.item) < 0) return false;
    }
    SimpleContainer& grid = menu.craftSlots();
    // What the grid held goes back into the inventory first, so the ingredients can be taken out of it.
    for (int slot = 0; slot < grid.size(); ++slot) {
        if (grid.item(slot).isEmpty()) continue;
        ItemStack back = grid.item(slot);
        grid.setItem(slot, ItemStack());
        inventory.placeItemBack(std::move(back), [](const ItemStack&) {});
    }
    for (std::size_t cell = 0; cell < entry.grid.size() && static_cast<int>(cell) < grid.size(); ++cell) {
        const ItemStack& wanted = entry.grid[cell];
        if (!wanted.isEmpty() && takeOne(inventory, wanted.item)) grid.setItem(static_cast<int>(cell), ItemStack(wanted.item, 1));
    }
    menu.slotsChanged();  // the result is worked out again
    return true;
}

}  // namespace falcon
