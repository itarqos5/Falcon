#pragma once

// NoiseBasedChunkGenerator.doFill, minus the aquifer: the loop order a block-substance stage must follow, and the
// plain "density > 0 is stone" fill used until the aquifer exists.
//
// doFill order (it matters for the stateful aquifer): z outermost, then x, then y from the top (yIndex height-1) down:
//
//   forEachFillCell(volume, chunkX, chunkZ, minY, height, [&](int worldX, int y, int worldZ, int localX, int localZ,
//                                                              int yIndex, float density) { ... aquifer.computeSubstance(worldX, y, worldZ, (double)density) ... });

#include <cstddef>
#include <cstdint>
#include <vector>

namespace falcon::worldgen {

template <class F>
void forEachFillCell(const float* volume, int chunkX, int chunkZ, int minY, int height, F&& fn) {
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const float* col = volume + static_cast<std::size_t>(height) * (static_cast<std::size_t>(x) + 16u * static_cast<std::size_t>(z));
            for (int yIndex = height - 1; yIndex >= 0; --yIndex) {
                fn(chunkX * 16 + x, minY + yIndex, chunkZ * 16 + z, x, z, yIndex, col[yIndex]);
            }
        }
    }
}

enum class FillBlock : std::uint8_t { Air = 0, Stone = 1 };

// density > 0 -> stone, else air; the same index order as the volume. (The game's aquifer additionally turns some
// air into water or lava below sea level and a few cave walls into stone; see the aquifer stage.)
[[nodiscard]] std::vector<std::uint8_t> stoneAirFromDensity(const float* volume, std::size_t count);

// A run of identical blocks in one column, from topY down to bottomY (inclusive), as the tests record columns.
struct ColumnRun {
    int topY;
    int bottomY;
    std::uint8_t block;
};
// The runs of column (localX, localZ) of a block volume (index (y - minY) + height * (x + 16 * z)), top to bottom.
[[nodiscard]] std::vector<ColumnRun> columnRuns(const std::uint8_t* blocks, int localX, int localZ, int minY, int height);

// FNV-1a over the raw bits of the floats in index order (the checksum the spec records for a chunk volume).
[[nodiscard]] std::uint64_t densityChecksum(const float* values, std::size_t count);

}  // namespace falcon::worldgen
