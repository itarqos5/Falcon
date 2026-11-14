#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/block_tags.hpp"
#include "falcon/world/item_stack.hpp"

namespace falcon {

// How long a block takes to dig, as the game works it out: the block's hardness, the speed of the tool for that block (a pickaxe on stone), and whether the tool is
// the right one to get anything out of the block. One tick of digging takes speed / hardness / 30 of the block away (/ 100 when the tool is wrong for it); the
// block breaks when the total reaches 1.
//
//   hardness 0 (grass, flowers, torches)   at once
//   dirt (0.5) by hand                     15 ticks, 0.75 s
//   stone (1.5) by hand                    150 ticks, 7.5 s (a pickaxe is the right tool); with a wooden pickaxe 23 ticks, 1.15 s
//   obsidian (50), diamond pickaxe         188 ticks, 9.4 s
//   bedrock, -1                             never
class MiningRules {
public:
    MiningRules(const BlockRegistry& registry, const BlockTags& tags) : registry_(registry), tags_(tags) {}

    // ToolComponent.getMiningSpeed: the first rule of the tool that names the block and has a speed, else 1 (a bare hand, or a tool that is no tool for this block).
    [[nodiscard]] float toolSpeed(const ItemStack& tool, Block block) const;
    // Player.hasCorrectToolForDrops: a block that needs no tool always is; otherwise the first rule that names the block and says (true or false) decides.
    [[nodiscard]] bool correctForDrops(const ItemStack& tool, Block block) const;
    // Player.getDestroySpeed: the tool's speed, a fifth of it when the digger is not on the ground, another fifth with the eyes under water.
    [[nodiscard]] float destroySpeed(const ItemStack& tool, Block block, bool onGround, bool eyesInWater) const;
    // BlockBehaviour.getDestroyProgress: how much of the block one tick of digging removes (0 for an unbreakable block, enormous for hardness 0).
    [[nodiscard]] float progressPerTick(const ItemStack& tool, Block block, bool onGround, bool eyesInWater) const;
    [[nodiscard]] float hardness(Block block) const { return registry_.state(block).hardness; }

private:
    [[nodiscard]] bool ruleNames(const std::string& blocks, Block block) const;

    const BlockRegistry& registry_;
    const BlockTags& tags_;
};

// The share of a block one tick removes (BlockBehaviour.getDestroyProgress): speed / hardness / 30 (100 when the tool is wrong), 0 when it cannot be broken.
[[nodiscard]] inline float destroyProgress(float hardness, float speed, bool correctTool) {
    if (hardness < 0.0f) return 0.0f;
    if (hardness == 0.0f) return 1.0e9f;
    return speed / hardness / (correctTool ? 30.0f : 100.0f);
}

// The crack picture drawn on the block (MultiPlayerGameMode.getDestroyStage): 0..9 as the progress grows, -1 before any.
[[nodiscard]] inline int destroyStage(float progress) { return progress > 0.0f ? static_cast<int>(progress * 10.0f) : -1; }

}  // namespace falcon
