#pragma once

#include "falcon/world/block_registry.hpp"

#include <cstdint>
#include <vector>

namespace falcon::worldgen {

class OverworldGenerator;

// What the game does to the rock after the terrain is shaped and its surface laid, and before the plants:
//
//  * The carvers (NoiseBasedChunkGenerator.applyCarvers): classic caves and the deeper "extra underground" caves, and canyons. These are the game's algorithms
//    (CaveWorldCarver, CanyonWorldCarver, WorldCarver.carveEllipsoid) with the game's own random numbers (LegacyRandomSource, setLargeFeatureSeed) and the settings
//    of data/minecraft/worldgen/carver/*.json (NOT yet checked against what the real game cuts for a seed): every chunk replays the carvers of the 17 x 17 chunks round it and keeps what reaches into itself, so caves cross
//    chunk borders without a seam. Not the game's: the fluid in a cave. The game asks its aquifers; here a cave is lava below y -56, water under an ocean (below sea
//    level, in a column that has water above) and air otherwise.
//  * The ores and the blobs of other rock (UNDERGROUND_ORES: coal, iron, copper, gold, redstone, lapis, diamond, emerald in the windswept hills, tuff, granite,
//    diorite, andesite, dirt and gravel), with the game's veins (OreFeature: a blob along a line), counts, sizes and height ranges, the stone or deepslate variant
//    of the ore by what it replaces, and the chance of being thrown away when next to air. NOT the game's random numbers: each vein has a generator of its own
//    seeded from its chunk, so a seed does not give the game's exact ore positions. Veins reach into the chunks beside theirs: each chunk makes the veins of the
//    nine chunks round it and keeps what falls inside.
class Underground {
public:
    Underground(const BlockRegistry& registry, std::int64_t seed);

    // `blocks` is the chunk as the generator holds it while it works: index (y - minY) + 384 * (x + 16 * z), x and z local to the chunk, minY -64.
    void apply(std::vector<Block>& blocks, int chunkX, int chunkZ, const OverworldGenerator& generator) const;

    static constexpr int kMinY = -64;
    static constexpr int kHeight = 384;

private:
    struct Context;
    void carve(Context& c, int sourceX, int sourceZ) const;
    void placeOres(Context& c, int sourceX, int sourceZ, const OverworldGenerator& generator) const;

    const BlockRegistry& registry_;
    std::int64_t seed_;
    std::vector<std::uint8_t> carvable_;     // by block (not state): the carvers may replace it
    std::vector<std::uint8_t> surfaceKind_;  // by block: 1 grass block, 2 mycelium (the soil that shows at the top of the world)
    struct Blocks {
        Block lava, water, grass, mycelium, dirt, stone, deepslate, granite, diorite, andesite, tuff, dirtBlob, gravel;
    } b_;
    struct Ore {
        const char* name;       // "coal": coal_ore and deepslate_coal_ore
        int count;              // attempts per chunk (rarity 0: always that many)
        int rarity;             // > 0: one attempt, with a chance of 1 in rarity
        int size;
        int minY, maxY;
        bool triangle;          // a height more likely near the middle (the sum of two draws), or an even one
        float discard;          // chance of leaving out a block that touches air
        enum class Kind : std::uint8_t { Ore, Granite, Diorite, Andesite, Tuff, Dirt, Gravel } kind;
        enum class Where : std::uint8_t { Everywhere, Badlands, Windswept } where;
    };
    std::vector<Ore> ores_;
    // The block an ore or blob puts where `target` stands, or Block::Air for "not here".
    struct OreBlocks {
        Block stone = Block::Air, deepslate = Block::Air;
    };
    std::vector<OreBlocks> oreBlocks_;  // by index in ores_
};

}  // namespace falcon::worldgen
