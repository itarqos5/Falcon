#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/worldgen/lod_surface.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace falcon {

// Plants, trees and snow on top of the generated terrain, chosen by biome: the trees each biome grows (oak, birch, spruce,
// acacia, jungle, cherry, dark oak), grass, ferns, flowers, cacti and dead bushes, seagrass and kelp in the sea, snow layers and
// ice in the cold biomes.
//
// This is this client's own approximation of the game's decoration step, NOT a port of it: the number and shape of the trees
// follow the game's biomes, but a seed does not give the game's exact trees and flowers. It is deterministic (the same seed
// always gives the same trees) and seamless across chunk borders: a tree is decided from its own position alone, so each chunk
// draws the parts of its neighbours' trees that reach into it.
class Decorator {
public:
    Decorator(const worldgen::OverworldGenerator& generator, const BlockRegistry& registry, std::int64_t seed);

    // What a biome grows, for the far levels of detail (worldgen/lod_surface.hpp), which draw forests without making a tree: how many trees a chunk of it has, the kind of the
    // commonest (0 none, 1 oak, 2 birch, 3 spruce, 4 acacia, 5 jungle, 6 cherry, 7 dark oak) and how tall such a tree usually is.
    [[nodiscard]] worldgen::LodTrees treeFacts(worldgen::BiomeId biome) const;

    // Decorates a freshly generated chunk (before it is shared with other threads).
    void decorate(Chunk& chunk, int chunkX, int chunkZ, const worldgen::ChunkExtras& extras) const;

private:
    struct Edit {
        std::uint8_t x, z;  // chunk-local
        std::int16_t y;     // world height
        Block block;
        bool onlyIfAir;     // leaves and plants do not replace anything solid
    };
    enum class Tree : std::uint8_t { None, Oak, Birch, Spruce, Acacia, Jungle, Cherry, DarkOak };
    // What a biome grows. Plants come in patches, as the game's do: a patch starts at one spot and scatters `tries`
    // plants around it, so there are clumps with bare ground between them rather than an even carpet. Flowers and tall
    // grass are behind a chance per chunk (the game's rarity filter), so most chunks have none at all.
    enum class Plant : std::uint8_t { Grass, Fern, TallGrass, Flower, Sunflower, Cactus, DeadBush, Count };
    struct Profile {
        float treesPerChunk = 0.0f;            // trees per 16x16 columns
        Tree trees[3] = {Tree::None, Tree::None, Tree::None};
        // Patches of each plant per chunk: a whole number is always that many, a fraction is the chance of one.
        float patches[static_cast<std::size_t>(Plant::Count)] = {};
        bool snow = false;
        bool seaPlants = false;
        const char* flowerSet = "plains";
        [[nodiscard]] float patchesOf(Plant p) const { return patches[static_cast<std::size_t>(p)]; }
    };

    [[nodiscard]] Profile profileFor(const std::string& biome) const;
    void placeTree(std::vector<Edit>& edits, Tree kind, int chunkX, int chunkZ, int wx, int groundY, int wz, std::uint64_t h) const;
    [[nodiscard]] Block named(const char* text) const;

    const worldgen::OverworldGenerator& generator_;
    const BlockRegistry& registry_;
    std::int64_t seed_;
    std::vector<Profile> profiles_;  // by biome id
    struct Blocks {
        Block oakLog, oakLeaves, birchLog, birchLeaves, spruceLog, spruceLeaves, acaciaLog, acaciaLeaves, jungleLog, jungleLeaves,
            cherryLog, cherryLeaves, darkOakLog, darkOakLeaves, shortGrass, tallGrassLower, tallGrassUpper, fern, deadBush, cactus, snow,
            ice, seagrass, kelp, kelpPlant, sugarCane, grassBlock, dirt, podzol, sand, redSand, water, packedIce, snowBlock, moss;
        std::vector<Block> flowersPlains, flowersForest, flowersMeadow, flowersFlower, flowersSwamp;
        Block sunflowerLower, sunflowerUpper, roseBushLower, roseBushUpper, lilacLower, lilacUpper, peonyLower, peonyUpper, lilyPad;
    } b_;
};

}  // namespace falcon
