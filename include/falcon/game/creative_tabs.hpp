#pragma once

#include "falcon/world/item_registry.hpp"
#include "falcon/world/item_stack.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

enum class CreativeTabType : std::uint8_t { Category, Hotbar, Search, Inventory };

// One item of a creative tab, with what the search tab looks it up by.
struct CreativeEntry {
    ItemStack stack;                // count 1 (a stack of the item, with its components when it has some)
    std::string searchText;         // the tooltip lines of the item, lower case, one per line (search tab only)
    std::vector<std::string> tags;  // the item tags it is in ("minecraft:logs"), for "#" searches (search tab only)
};

// A tab of the creative inventory, from the game's exported creative-inventory.json.
struct CreativeTab {
    std::string id;              // "minecraft:building_blocks"
    CreativeTabType type = CreativeTabType::Category;
    bool top = true;             // the row above the window (else below it)
    int column = 0;              // 0..6 along that row
    std::string title;           // the English name in the file
    std::string translationKey;  // "itemGroup.buildingBlocks"
    ItemStack icon;
    bool visible = true;         // false for the operator tab (shown with the "Operator Items Tab" option)
    bool permissionsOnly = false;
    std::vector<CreativeEntry> entries;

    [[nodiscard]] bool alignedRight() const { return column == 6; }
    // (only the survival inventory tab hides its title: the search tab shows "Search Items" at the left of the box)
    [[nodiscard]] bool showTitle() const { return type != CreativeTabType::Inventory; }
    [[nodiscard]] bool canScroll() const { return type != CreativeTabType::Inventory; }
    // Whether the tab lists this item (any stack of it, whatever the components).
    [[nodiscard]] bool contains(const ItemStack& stack) const;
};

class CreativeTabs {
public:
    static constexpr int kColumns = 9;
    static constexpr int kRows = 5;

    // Reads <gameDataDir>/creative-inventory.json. Null and `error` if it is missing or unreadable.
    [[nodiscard]] static std::shared_ptr<const CreativeTabs> loadFromGameData(const std::filesystem::path& gameDataDir, const ItemRegistry& items, std::string& error);
    // An item a mod put into a tab, with the name the search tab finds it by.
    struct ExtraEntry {
        std::string tab;  // "minecraft:ingredients"
        std::string item; // "examplemod:ruby"
        std::string displayName;
    };
    // The tabs with these items added at the end of their tab, and to the search tab.
    [[nodiscard]] static std::shared_ptr<const CreativeTabs> extended(const CreativeTabs& base, const ItemRegistry& items, const std::vector<ExtraEntry>& extra);
    // A few tabs of a few items (tests without the game's files).
    [[nodiscard]] static std::shared_ptr<const CreativeTabs> makeMinimal(const ItemRegistry& items);

    [[nodiscard]] const std::vector<CreativeTab>& tabs() const { return tabs_; }
    [[nodiscard]] const CreativeTab* find(const std::string& id) const;
    // The tab shown first (Building Blocks).
    [[nodiscard]] const CreativeTab& defaultTab() const { return tabs_.front(); }
    // The entries of the search tab that match what was typed: every word of the text must appear in the item's tooltip lines
    // or id; "#word" lists the items that are in an item tag containing "word". Case does not matter.
    [[nodiscard]] std::vector<const CreativeEntry*> search(const std::string& text) const;
    // The tabs (not the search tab) that list this item, for the blue lines of a creative tooltip.
    [[nodiscard]] std::vector<const CreativeTab*> tabsContaining(const ItemStack& stack) const;
    // The shown name of a stack with components ("Potion of Healing"), found by its components; "" if unknown.
    [[nodiscard]] std::string variantNameOf(Item item, const std::string& components) const;

private:
    std::vector<CreativeTab> tabs_;
};

[[nodiscard]] std::shared_ptr<const CreativeTabs> currentCreativeTabs();
void installCreativeTabs(std::shared_ptr<const CreativeTabs> tabs);

// Reads one stack written in the game's text form: {count:1,id:"minecraft:oak_log"} (components kept as text).
[[nodiscard]] std::optional<ItemStack> parseStackText(const std::string& text, const ItemRegistry& items);

}  // namespace falcon
