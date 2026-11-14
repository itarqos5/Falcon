#pragma once

#include "apex/world/block_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace apex {

// What keeps a plant in the world, from the game's own rules (VegetationBlock.canSurvive, DoublePlantBlock.updateShape, DoorBlock):
//   * flowers, grass, ferns, saplings, sweet berry bushes need a block of the tag `supports_vegetation` below them (dirt, grass,
//     podzol, farmland, ...); crops `supports_crops` (farmland); dead bushes and the like `supports_dry_vegetation` (sand, terracotta,
//     dirt);
//   * a block with a `half` of lower/upper (tall grass, large ferns, sunflowers, lilacs, roses, peonies, tall seagrass, doors) is
//     two blocks: when one half is gone the other half goes too.
// The block classes come from the game's `block-behaviours.json`, the tags from `data/minecraft/tags/block`. A block this does not
// know is left alone.
class BlockSupport {
public:
    // Null (and `error`) if the game's files are not there.
    [[nodiscard]] static std::shared_ptr<const BlockSupport> loadFromGameData(const std::filesystem::path& gameDataDir, const BlockRegistry& registry,
                                                                              std::string& error);

    // `block` is a state that is about to be, or is, at (x, y, z); `at(x, y, z)` reads the world. True if it may stay there.
    using Reader = std::function<Block(int, int, int)>;
    [[nodiscard]] bool canSurvive(Block block, int x, int y, int z, const Reader& at) const;
    // The state `block` at (x, y, z) must become when a neighbour changed: itself, or air if it cannot stay any more.
    [[nodiscard]] Block afterNeighborChange(Block block, int x, int y, int z, const Reader& at) const;
    // A block that is the lower half of a two-block-high block (tall grass, a door ...); its upper half is `upperHalf(block)`.
    [[nodiscard]] bool isLowerHalf(Block block) const;
    [[nodiscard]] Block upperHalf(Block lower) const;
    [[nodiscard]] bool isDoubleHalf(Block block) const;  // lower or upper
    // A plant that needs soil under it (flowers, grass, saplings, crops ...).
    [[nodiscard]] bool needsSoil(Block block) const;

private:
    enum class Soil : std::uint8_t { None, Vegetation, Crops, Dry };
    const BlockRegistry* registry_ = nullptr;
    std::vector<Soil> soil_;                  // per block index
    std::vector<std::uint8_t> halfProperty_;  // per block index: 1 if it has a half = lower/upper
    std::vector<std::uint8_t> vegetationTag_, cropsTag_, dryTag_;  // per block index: 1 if the tag holds it
};

}  // namespace apex
