#pragma once

#include "falcon/world/block_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

// What the game's block classes are, from its exported block-behaviours.json: which blocks fall (sand, gravel, concrete powder, anvils), which are fire, TNT or
// anvils, which concrete a powder turns into, and how easily each block catches fire and burns away (FireBlock's igniteOdds and burnOdds tables).
class BlockBehaviours {
public:
    // Reads <gameDataDir>/block-behaviours.json (the big "states" list in it is skipped). Null and `error` when the file is unusable.
    [[nodiscard]] static std::shared_ptr<const BlockBehaviours> loadFromGameData(const std::filesystem::path& gameDataDir, const BlockRegistry& registry, std::string& error);

    [[nodiscard]] bool isFallable(int blockIndex) const { return has(blockIndex, kFallable); }
    [[nodiscard]] bool isFire(int blockIndex) const { return has(blockIndex, kFire); }      // fire and soul fire (BaseFireBlock)
    [[nodiscard]] bool isSpreadingFire(int blockIndex) const { return has(blockIndex, kSpreadingFire); }  // fire only (FireBlock)
    [[nodiscard]] bool isTnt(int blockIndex) const { return has(blockIndex, kTnt); }
    [[nodiscard]] bool isAnvil(int blockIndex) const { return has(blockIndex, kAnvil); }
    [[nodiscard]] bool isConcretePowder(int blockIndex) const { return has(blockIndex, kPowder); }
    // The block a concrete powder sets into when it touches water; -1 for any other block.
    [[nodiscard]] int concreteOf(int blockIndex) const { return static_cast<std::size_t>(blockIndex) < concrete_.size() ? concrete_[static_cast<std::size_t>(blockIndex)] : -1; }
    // FireBlock.getIgniteOdds / getBurnOdds: 0 for a block that cannot burn.
    [[nodiscard]] int igniteOdds(int blockIndex) const { return static_cast<std::size_t>(blockIndex) < ignite_.size() ? ignite_[static_cast<std::size_t>(blockIndex)] : 0; }
    [[nodiscard]] int burnOdds(int blockIndex) const { return static_cast<std::size_t>(blockIndex) < burn_.size() ? burn_[static_cast<std::size_t>(blockIndex)] : 0; }

private:
    enum Flag : std::uint8_t { kFallable = 1, kFire = 2, kSpreadingFire = 4, kTnt = 8, kAnvil = 16, kPowder = 32 };
    [[nodiscard]] bool has(int blockIndex, Flag f) const { return static_cast<std::size_t>(blockIndex) < flags_.size() && (flags_[static_cast<std::size_t>(blockIndex)] & f) != 0; }

    std::vector<std::uint8_t> flags_;
    std::vector<std::uint16_t> ignite_, burn_;
    std::vector<int> concrete_;
};

}  // namespace falcon
