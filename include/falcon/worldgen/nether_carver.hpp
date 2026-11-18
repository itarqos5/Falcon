#pragma once

#include "falcon/world/block_registry.hpp"

#include <cstdint>
#include <vector>

namespace falcon::worldgen {

// The Nether's caves: the game's cave carver with the settings of data/minecraft/worldgen/carver/nether_cave.json (CaveWorldCarver of 26.3, read from its bytecode): one chance in five
// per chunk of up to nine caves at any height from 0 to 126, each a tunnel of 0 to 6 blocks thick whose height is five times its width (start_vertical_radius_multiplier 5, the branches
// after a split are round again), now and then with a round room at its start, cut out of the netherrack and the blocks laid on it. A cell below the lava sea (y < 32) is filled with
// lava, the rest is air. The random numbers are the game's own (LegacyRandomSource, setLargeFeatureSeed, one tunnel seed each), so a seed gives the game's caves in the game's places:
// every chunk replays the caves of the 17 x 17 chunks round it and keeps what reaches into itself. NOT checked against the real game's blocks.
class NetherCarver {
public:
    NetherCarver(const BlockRegistry& registry, std::int64_t seed);

    // `blocks` is the Nether chunk as the generator holds it while it works: index (y - 0) + 128 * (x + 16 * z), x and z local to the chunk. Thread-safe (nothing here is changed).
    void apply(std::vector<Block>& blocks, int chunkX, int chunkZ) const;

    static constexpr int kHeight = 128;
    static constexpr int kSeaLevel = 32;  // the lava sea

private:
    struct Context;
    void tunnel(Context& c, std::int64_t tunnelSeed, double x, double y, double z, float thickness, float yaw, float pitch, int step, int distance, double yScale) const;
    void carveEllipsoid(Context& c, double x, double y, double z, double hr, double vr) const;

    const BlockRegistry& registry_;
    std::int64_t seed_;
    std::vector<std::uint8_t> carvable_;  // by block: the carver may cut it
    Block lava_;
};

}  // namespace falcon::worldgen
