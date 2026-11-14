#include "falcon/game/mining.hpp"

namespace falcon {

bool MiningRules::ruleNames(const std::string& blocks, Block block) const {
    if (blocks.empty()) return false;
    const std::size_t index = static_cast<std::size_t>(registry_.state(block).block);
    if (blocks[0] == '#') return tags_.contains(blocks, index);
    std::string name = blocks;
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return registry_.blocks()[index].name == name;
}

float MiningRules::toolSpeed(const ItemStack& tool, Block block) const {
    if (tool.isEmpty()) return 1.0f;
    for (const ToolRule& rule : tool.type().toolRules) {
        if (rule.hasSpeed && ruleNames(rule.blocks, block)) return rule.speed;
    }
    return 1.0f;
}

bool MiningRules::correctForDrops(const ItemStack& tool, Block block) const {
    if (!registry_.state(block).has(kStateRequiresTool)) return true;
    if (tool.isEmpty()) return false;
    for (const ToolRule& rule : tool.type().toolRules) {
        if (rule.correctForDrops >= 0 && ruleNames(rule.blocks, block)) return rule.correctForDrops == 1;
    }
    return false;
}

float MiningRules::destroySpeed(const ItemStack& tool, Block block, bool onGround, bool eyesInWater) const {
    float speed = toolSpeed(tool, block);
    if (eyesInWater) speed *= 0.2f;  // the submerged mining speed attribute
    if (!onGround) speed /= 5.0f;
    return speed;
}

float MiningRules::progressPerTick(const ItemStack& tool, Block block, bool onGround, bool eyesInWater) const {
    return destroyProgress(hardness(block), destroySpeed(tool, block, onGround, eyesInWater), correctForDrops(tool, block));
}

}  // namespace falcon
