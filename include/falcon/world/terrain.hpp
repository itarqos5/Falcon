#pragma once

#include "falcon/world/chunk.hpp"

#include <array>
#include <cstdint>

namespace falcon {

enum class TerrainStyle {
    Flat,       // Superflat: bedrock, two layers of dirt, grass
    Hills,      // seeded noise hills, a sea at y 62, sand beaches
    Amplified,  // the same with much taller hills
};

// Makes the blocks of a chunk from the world seed, the same way every time. The "Hills" terrain is this client's
// own simple generator (fractal noise, no caves, trees or biomes): it is NOT the game's world generator, so a
// seed does not give the world it gives in Minecraft.
class TerrainGenerator {
public:
    TerrainGenerator(std::int64_t seed, TerrainStyle style);

    [[nodiscard]] std::shared_ptr<Chunk> generate(int chunkX, int chunkZ) const;
    // The height of the highest solid block of a column (the top of the grass), without making the chunk.
    [[nodiscard]] int surfaceHeight(int x, int z) const;
    // Where a new player stands: the middle of a block column on dry land close to the origin.
    struct Spawn {
        double x;
        double y;
        double z;
    };
    [[nodiscard]] Spawn findSpawn() const;

    static constexpr int kSeaLevel = 63;  // water fills up to y = 62
    static constexpr int kFlatSurface = -61;

private:
    [[nodiscard]] double noise(double x, double z) const;  // 2D gradient noise, about -1..1

    TerrainStyle style_;
    std::array<std::uint8_t, 512> perm_{};
};

}  // namespace falcon
