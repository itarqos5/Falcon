#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/item_stack.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace falcon {

// What a broken block gives: the game's own block loot tables (data/minecraft/loot_table/blocks/<block>.json), worked out for a player with the tool in their hand.
// The parts that need things Falcon does not have are answered the way a bare game would: no enchantment is ever present (so silk touch never applies and fortune
// is level 0), explosions do not happen (survives_explosion is always true). Conditions and functions it does not know are taken as met and as not changing
// anything. Whether the tool is the right one to get drops from the block at all is a separate question (see MiningRules::correctForDrops).
// Main thread only: the tables are read when first needed and kept.
class BlockLoot {
public:
    // `dataRoot` is the folder that holds data/minecraft/... (ResourceManager::dataRoot()).
    BlockLoot(std::filesystem::path dataRoot, const BlockRegistry& registry);
    ~BlockLoot();

    [[nodiscard]] std::vector<ItemStack> drops(Block block, const ItemStack& tool, std::mt19937& random) const;

private:
    struct Context;
    [[nodiscard]] const nlohmann::json* table(const std::string& blockName) const;
    [[nodiscard]] const nlohmann::json* predicate(const std::string& id) const;
    [[nodiscard]] bool met(const nlohmann::json& condition, Context& c, int depth = 0) const;
    [[nodiscard]] bool allMet(const nlohmann::json& holder, const char* key, Context& c) const;
    void runEntry(const nlohmann::json& entry, Context& c, std::vector<ItemStack>& out, bool conditionsChecked = false) const;
    void applyModifiers(const nlohmann::json& modifiers, Context& c, ItemStack& stack) const;
    [[nodiscard]] int sampleCount(const nlohmann::json& provider, Context& c) const;

    std::filesystem::path root_;
    const BlockRegistry& registry_;
    mutable std::unordered_map<std::string, std::shared_ptr<nlohmann::json>> tables_;
    mutable std::unordered_map<std::string, std::shared_ptr<nlohmann::json>> predicates_;
};

}  // namespace falcon
