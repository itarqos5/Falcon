#pragma once

// What the Nether and the End have on top of their generated terrain: Falcon's own approximation of the game's features for the two dimensions (like world/decoration.hpp is for the
// Overworld; the game's placed features are not ported). The Nether: glowstone hanging from the ceiling, quartz and gold ore in the rock, magma at the shore of the lava sea, fire and
// mushrooms on the floor, roots, fungi and vines in the two forests, and columns of basalt in the deltas. The End: the ten obsidian pillars round the main island, two of them in iron
// cages. Everything is decided from the seed and the chunk, so a chunk looks the same whenever it is made, and each chunk only writes inside itself.

#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace falcon {

// The ten pillars of the End: where each stands, how wide and how tall, and whether it is in a cage (EndSpikeFeature.SpikeCacheLoader).
struct EndSpike {
    int centerX = 0, centerZ = 0, radius = 2, height = 76;
    bool guarded = false;
};
[[nodiscard]] std::array<EndSpike, 10> endSpikes(std::int64_t seed);

class NetherDecorator {
public:
    // `dimension` is 1 for the Nether and 2 for the End.
    NetherDecorator(const worldgen::OverworldGenerator& generator, const BlockRegistry& registry, std::int64_t seed, int dimension);
    void decorate(Chunk& chunk, int chunkX, int chunkZ, const worldgen::ChunkExtras& extras) const;

private:
    struct Blocks {
        Block air, netherrack, glowstone, quartzOre, goldOre, magma, fire, soulFire, brownMushroom, redMushroom, crimsonRoots, warpedRoots, crimsonFungus, warpedFungus, sprouts, crimsonStem,
            warpedStem, wartBlock, warpedWartBlock, shroomlight, weepingVines, twistingVines, basalt, crimsonNylium, warpedNylium, soulSand, soulSoil, lava, obsidian, bedrock, ironBars, endStone;
    };
    const worldgen::OverworldGenerator& generator_;
    const BlockRegistry& registry_;
    std::int64_t seed_;
    int dimension_;
    Blocks b_{};
    std::array<EndSpike, 10> spikes_{};
};

}  // namespace falcon
