#pragma once

#include "falcon/world/block_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace falcon {

// An item: its protocol id in the game's item registry (26.3 has 1658; id 0 is air). The ids are the game's own.
enum class Item : std::uint16_t { Air = 0 };

[[nodiscard]] constexpr std::uint16_t itemId(Item i) { return static_cast<std::uint16_t>(i); }

enum class Rarity : std::uint8_t { Common, Uncommon, Rare, Epic };
// The chat colour of a rarity's name (0xRRGGBB): white, yellow, aqua, light purple.
[[nodiscard]] constexpr std::uint32_t rarityColor(Rarity r) {
    switch (r) {
        case Rarity::Uncommon: return 0xFFFF55;
        case Rarity::Rare: return 0x55FFFF;
        case Rarity::Epic: return 0xFF55FF;
        default: return 0xFFFFFF;
    }
}

// Where an item can be worn or held (the "slot" of its equippable component).
enum class EquipSlot : std::uint8_t { None, MainHand, OffHand, Feet, Legs, Chest, Head, Body, Saddle };

struct FoodInfo {
    int nutrition = 0;
    float saturation = 0.0f;
    bool canAlwaysEat = false;
};

// One rule of a tool's "tool" component: which blocks, how fast, and whether they drop. `blocks` is "#minecraft:mineable/pickaxe"
// (a block tag) or a block id; the rule is data only (the tags are resolved where breaking is worked out).
struct ToolRule {
    std::string blocks;
    bool hasSpeed = false;
    float speed = 1.0f;
    int correctForDrops = -1;  // -1: not stated, 0 false, 1 true
};

// Everything the client needs to know about one item, from the game's exported items.json.
struct ItemType {
    std::string name;            // "diamond_pickaxe": no "minecraft:" prefix
    std::uint16_t protocolId = 0;
    int maxStack = 64;           // max_stack_size
    int maxDamage = 0;           // max_damage: 0 = cannot be damaged
    std::string translationKey;  // "item.minecraft.diamond_pickaxe" or "block.minecraft.stone"
    std::string itemModel;       // the assets/items/<model>.json that draws it ("diamond_pickaxe")
    Rarity rarity = Rarity::Common;
    bool glint = false;          // enchantment_glint_override: true
    std::optional<FoodInfo> food;
    std::vector<ToolRule> toolRules;
    bool isTool = false;
    int toolDamagePerBlock = 1;
    // Attributes of the main hand (the modifiers add to the player's base 1 attack damage and 4 attack speed).
    float attackDamageBonus = 0.0f;
    float attackSpeedBonus = 0.0f;
    bool hasAttackModifiers = false;
    float armor = 0.0f;
    float armorToughness = 0.0f;
    EquipSlot armorSlot = EquipSlot::None;      // the slot the armor attributes apply to
    EquipSlot equipSlot = EquipSlot::None;      // equippable.slot
    int fuelBurnTicks = 0;                      // cooking_fuel burn time: 0 = not a fuel
    // The "consumable" component: food, potions, milk and the like are used up over consumeSeconds, with the animation "eat" or "drink" and a sound (a sound id such as
    // "entity.generic.eat"); crumbs fly while eating if consumeParticles.
    bool consumable = false;
    float consumeSeconds = 1.6f;
    std::string consumeAnimation = "eat";
    std::string consumeSound = "entity.generic.eat";
    bool consumeParticles = true;
    int blockIndex = -1;                        // the block this item places (index into BlockRegistry::blocks()), or -1
    std::string useRemainder;                   // item left after using it ("bowl" for stews), empty if none

    [[nodiscard]] float attackDamage() const { return 1.0f + attackDamageBonus; }
    [[nodiscard]] float attackSpeed() const { return 4.0f + attackSpeedBonus; }
    [[nodiscard]] bool isDamageable() const { return maxDamage > 0; }
    [[nodiscard]] bool isBlockItem() const { return blockIndex >= 0; }
};

// An item a mod added (see jvm/mod_content.hpp).
struct ExtraItem {
    std::string name;            // "examplemod:ruby": the namespace stays
    std::string translationKey;  // "item.examplemod.ruby" or "block.examplemod.ruby_block"
    std::string itemModel;       // "examplemod:ruby": assets/examplemod/items/ruby.json draws it
    int maxStack = 64;
    int maxDamage = 0;
    Rarity rarity = Rarity::Common;
    std::optional<FoodInfo> food;
    bool fireResistant = false;
};

class ItemRegistry {
public:
    // Reads <gameDataDir>/items.json and registries.json (protocol ids), plus the fuel times in
    // data/minecraft/context_int_provider/cooking, and keeps the result as a small binary cache in `cacheDir` (made from
    // those two files, so a new game version rebuilds it). `blocks` (may be null) links block items to their blocks.
    // Null and `error` on failure.
    [[nodiscard]] static std::shared_ptr<const ItemRegistry> loadFromGameData(const std::filesystem::path& gameDataDir,
                                                                              const std::filesystem::path& cacheDir,
                                                                              const BlockRegistry* blocks,
                                                                              std::string& error);
    // The registry with these items after the game's (protocol ids go on from the last), linked to the blocks of the same names.
    [[nodiscard]] static std::shared_ptr<const ItemRegistry> extended(const ItemRegistry& base, const std::vector<ExtraItem>& extra, const BlockRegistry* blocks);
    // A few items with real facts, so tests run without the game's files (stone, dirt, planks, stick, apple,
    // diamond_pickaxe, iron_chestplate, ...). Block items are linked when `blocks` is given.
    [[nodiscard]] static std::shared_ptr<const ItemRegistry> makeMinimal(const BlockRegistry* blocks = nullptr);

    [[nodiscard]] std::size_t size() const { return items_.size(); }
    // False for the small built-in set used by tests.
    [[nodiscard]] bool isFromGameData() const { return fromGameData_; }
    [[nodiscard]] const std::vector<ItemType>& items() const { return items_; }
    [[nodiscard]] const ItemType& item(Item i) const {
        const auto id = static_cast<std::size_t>(itemId(i));
        return items_[id < items_.size() ? id : 0];
    }
    // "diamond_pickaxe" or "minecraft:diamond_pickaxe".
    [[nodiscard]] std::optional<Item> find(std::string_view name) const;
    // The block an item places (by index), if it is a block item.
    [[nodiscard]] std::optional<int> blockOf(Item i) const {
        const int b = item(i).blockIndex;
        if (b < 0) return std::nullopt;
        return b;
    }
    // The item that stands for a block (Air if there is none).
    [[nodiscard]] Item itemOfBlock(int blockIndex) const {
        const auto it = itemOfBlock_.find(blockIndex);
        return it == itemOfBlock_.end() ? Item::Air : it->second;
    }
    [[nodiscard]] bool isAir(Item i) const { return i == Item::Air; }

private:
    friend class ItemRegistryBuilder;
    bool fromGameData_ = false;
    std::vector<ItemType> items_;
    std::unordered_map<std::string, std::uint16_t> byName_;
    std::unordered_map<int, Item> itemOfBlock_;
};

// The registry the item code uses. Installed once at start-up (the session) or per test.
[[nodiscard]] const ItemRegistry& itemRegistry();
void installItemRegistry(std::shared_ptr<const ItemRegistry> registry);
[[nodiscard]] bool hasItemRegistry();
[[nodiscard]] std::shared_ptr<const ItemRegistry> currentItemRegistry();

}  // namespace falcon
