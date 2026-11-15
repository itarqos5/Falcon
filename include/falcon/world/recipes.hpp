#pragma once

#include "falcon/world/item_stack.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace falcon {

// What an ingredient of a recipe accepts: a set of items (one item, a list, or an item tag already resolved).
// A default ingredient accepts nothing and stands for an empty cell of a shaped recipe's pattern.
struct Ingredient {
    std::vector<std::uint16_t> items;  // sorted item ids
    [[nodiscard]] bool empty() const { return items.empty(); }
    [[nodiscard]] bool test(Item item) const { return std::binary_search(items.begin(), items.end(), itemId(item)); }
};

// The crafting grid as a recipe sees it: the part of the grid that holds items, cut to its bounding box (the game's
// CraftingInput), row by row. `left` and `top` say where that box sits in the grid.
struct CraftingInput {
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    int ingredientCount = 0;  // non-empty cells
    std::vector<ItemStack> items;  // width * height

    [[nodiscard]] const ItemStack& at(int x, int y) const { return items[static_cast<std::size_t>(y * width + x)]; }
    // `grid` is gridWidth x gridHeight stacks row by row.
    [[nodiscard]] static CraftingInput fromGrid(int gridWidth, int gridHeight, const std::vector<ItemStack>& grid);
};

struct ShapedRecipe {
    std::string id;
    int width = 0;
    int height = 0;
    std::vector<Ingredient> cells;  // width * height, row by row; an empty ingredient is an empty cell
    bool symmetrical = false;       // the pattern equals its mirror image, so mirroring cannot match anything new
    ItemStack result;
    std::string group;
    int ingredientCount = 0;
    [[nodiscard]] bool matches(const CraftingInput& input) const;
};

struct ShapelessRecipe {
    std::string id;
    std::vector<Ingredient> ingredients;
    ItemStack result;
    std::string group;
    [[nodiscard]] bool matches(const CraftingInput& input) const;
};

// crafting_transmute: one `input` item and one `material` give the result item carrying the input's components
// (a shulker box or bundle being dyed).
struct TransmuteRecipe {
    std::string id;
    Ingredient input;
    Ingredient material;
    Item result = Item::Air;
    [[nodiscard]] bool matches(const CraftingInput& input) const;
};

// smelting (a furnace), blasting (a blast furnace: ores and metal) and smoking (a smoker: food): one ingredient cooked into one result.
enum class CookingKind : std::uint8_t { Smelting, Blasting, Smoking };
struct CookingRecipe {
    std::string id;
    CookingKind kind = CookingKind::Smelting;
    Ingredient ingredient;
    ItemStack result;
    int cookingTime = 200;     // ticks a furnace takes (a blast furnace and a smoker go through it twice as fast, see AbstractFurnaceBlockEntity)
    float experience = 0.0f;   // what the player gets for one result when taking it out
};

// The item tags of the game's data (data/minecraft/tags/item), resolved: "#minecraft:planks" -> its items.
class ItemTags {
public:
    // Reads <dataDir>/data/minecraft/tags/item/**.json. Empty (and `error`) if the folder is missing.
    static ItemTags load(const std::filesystem::path& gameDataDir, const ItemRegistry& items, std::string& error);
    // Defines a tag directly (tests).
    void define(const std::string& name, std::vector<std::uint16_t> items);
    // The sorted item ids of a tag ("minecraft:planks" or "planks"); empty for an unknown tag.
    [[nodiscard]] const std::vector<std::uint16_t>& items(std::string_view name) const;
    [[nodiscard]] bool contains(std::string_view name) const;
    [[nodiscard]] std::size_t size() const { return tags_.size(); }

private:
    std::map<std::string, std::vector<std::uint16_t>, std::less<>> tags_;
};

// The result of looking a crafting grid up.
struct CraftingMatch {
    ItemStack result;
    int recipeIndex = -1;
};

class RecipeManager {
public:
    // Reads the crafting recipes (crafting_shaped, crafting_shapeless, crafting_transmute) of
    // <gameDataDir>/data/minecraft/recipe; item tags come from the tag folder. Reading the ~2000 small files takes seconds,
    // so the result is kept in `cacheDir` as one small file (rebuilt when the recipe or tag folders change); a recipe manager
    // made from that cache has no tags() (they are only needed to build the recipes). Null and `error` if there are none.
    [[nodiscard]] static std::shared_ptr<const RecipeManager> loadFromGameData(const std::filesystem::path& gameDataDir,
                                                                               const std::filesystem::path& cacheDir,
                                                                               const ItemRegistry& items, std::string& error);
    // A handful of recipes (planks, sticks, crafting table, torches, dyes) so tests run without the game's files.
    [[nodiscard]] static std::shared_ptr<const RecipeManager> makeMinimal(const ItemRegistry& items);

    [[nodiscard]] bool isFromGameData() const { return fromGameData_; }
    [[nodiscard]] bool isFromCache() const { return fromCache_; }
    [[nodiscard]] std::size_t recipeCount() const { return shaped_.size() + shapeless_.size() + transmute_.size(); }
    [[nodiscard]] const std::vector<ShapedRecipe>& shaped() const { return shaped_; }
    [[nodiscard]] const std::vector<ShapelessRecipe>& shapeless() const { return shapeless_; }
    [[nodiscard]] const std::vector<TransmuteRecipe>& transmute() const { return transmute_; }
    [[nodiscard]] const ItemTags& tags() const { return tags_; }
    [[nodiscard]] const std::vector<CookingRecipe>& cooking() const { return cooking_; }
    // The recipe a furnace of that kind cooks `input` by, or null (RecipeManager.getRecipeFor for a SingleRecipeInput).
    [[nodiscard]] const CookingRecipe* findCooking(CookingKind kind, const ItemStack& input) const;

    // The first recipe that matches the grid (RecipeManager.getRecipeFor): its assembled result.
    [[nodiscard]] std::optional<CraftingMatch> find(const CraftingInput& input) const;

private:
    friend class RecipeBuilder;
    bool fromGameData_ = false;
    bool fromCache_ = false;
    ItemTags tags_;
    std::vector<ShapedRecipe> shaped_;
    std::vector<ShapelessRecipe> shapeless_;
    std::vector<TransmuteRecipe> transmute_;
    std::vector<CookingRecipe> cooking_;
};

// The installed recipes (null until installed; the session loads them on a background thread).
[[nodiscard]] const RecipeManager* recipeManager();
void installRecipeManager(std::shared_ptr<const RecipeManager> manager);
[[nodiscard]] std::shared_ptr<const RecipeManager> currentRecipeManager();

}  // namespace falcon
