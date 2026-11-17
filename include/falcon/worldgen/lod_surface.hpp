#pragma once

// The surface of the world without making the world: what a distant level of detail (LOD) is drawn from. A column's height is predicted from the seed's noise
// (no chunk is generated, no block is placed), and what its top is made of (grass, sand, snow, leaves ...) and the tint of the biome come from the biome, the height and the sea
// level, so a LOD can be made for any place in a few microseconds a column instead of the milliseconds a chunk takes. The renderer draws it with the block textures.

#include "falcon/worldgen/overworld_generator.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace falcon::worldgen {

// How the height of a column is predicted.
enum class LodPredictor : std::uint8_t {
    Preliminary,  // the game's preliminary surface level: one evaluation of a function of x and z (smooth terrain, no jagged detail), for the far levels
    Density,      // the final density point-sampled downward from there (what a chunk would hold, to a block, caves apart), for the near ones
};

// What a biome grows, as far as a distant forest needs to be drawn: how many trees a chunk has, the kind of the commonest (0 none, 1 oak, 2 birch, 3 spruce, 4 acacia,
// 5 jungle, 6 cherry, 7 dark oak) and its usual height. The decorator knows (Decorator::treeFacts); the surface only uses the numbers.
struct LodTrees {
    float perChunk = 0.0f;
    int kind = 0;
    int height = 0;
};

// What the top of the ground is made of, which says what lies under it: the side of a cliff shows the grass, then the dirt, then the stone (render/lod_renderer.cpp).
enum class LodGround : std::uint8_t { Grass, Sand, Terracotta, Snow, Stone, Mycelium, Gravel, Water };

// The game's colour maps (the grass and the foliage picture of the resource pack), asked by temperature and downfall for 0xRRGGBB; empty: the corners of the maps, interpolated.
struct LodColourMaps {
    std::function<std::uint32_t(float, float)> grass, foliage;
};

struct LodColumn {
    std::int16_t height = 0;  // the top of the column: the tree tops, the ground, or the sea surface over water
    std::int16_t floor = 0;   // the ground (under water: the sea floor)
    std::uint8_t r = 0, g = 0, b = 0;  // the tint of the top: what the texture of the top is multiplied by (the biome's grass, foliage or water colour; white where nothing tints it)
    bool water = false;
    bool ice = false;         // the sea is frozen over
    std::uint8_t tree = 0;    // the kind of the leaves on top (LodTrees::kind), 0 for bare ground and water
    LodGround ground = LodGround::Grass;
    bool bareSnow = false;    // snow over rock (a mountain top) rather than over earth (a cold plain)
    std::uint8_t canopy = 0;  // how many blocks of leaves stand on the ground in this column (height - canopy is the ground)
};

// How a grid of columns gets its heights.
enum class LodMode : std::uint8_t {
    Preliminary,  // the preliminary surface everywhere: the cheapest, smooth terrain
    Residual,     // the preliminary surface, corrected by the real height on a sparse grid (mountains keep their height for a fraction of the cost)
    Direct,       // the real height of every column, probed one column at a time (about 200 microseconds each)
    Chunk,        // the real height of every column, from the density of whole chunks (about 16 microseconds a column when the cells are blocks: the nearest, finest ones)
};

// The columns of one square of the world, sampled every `cell` blocks, with one extra column all round (for the slope of the edge columns).
struct LodGrid {
    int side = 0;  // columns a side: cells + 3 (the cells, the ring round them, and one more)
    std::vector<LodColumn> columns;
    // i and j run -1 .. cells + 1: the square itself is 0 .. cells.
    [[nodiscard]] const LodColumn& at(int i, int j) const { return columns[static_cast<std::size_t>((j + 1) * side + (i + 1))]; }
};

class LodSurface {
public:
    static constexpr int kSeaLevel = 63;

    // `trees` is what each biome grows, by biome id (empty: no forests).
    explicit LodSurface(std::shared_ptr<const OverworldGenerator> generator, std::vector<LodTrees> trees = {}, LodColourMaps maps = {});

    // Thread-safe (the generator's queries are).
    [[nodiscard]] LodColumn column(int x, int z, LodPredictor how) const;
    // The columns of the square of cells x cells cells of `cell` blocks that starts at (originX, originZ), each read in the middle of its cell, plus the ring of columns round them.
    [[nodiscard]] LodGrid grid(int originX, int originZ, int cell, int cells, LodMode mode) const;

private:
    using Kind = LodGround;
    struct Look {
        Kind kind = Kind::Grass;
        float temperature = 0.5f;
        std::uint8_t r = 0, g = 0, b = 0;           // the tint of the top of the ground (the grass colour; white for the rest)
        std::uint8_t waterR = 0, waterG = 0, waterB = 0;
        std::uint8_t leafR = 0, leafG = 0, leafB = 0;  // oak, acacia, jungle and dark oak leaves: the biome's foliage tint (not its grass tint)
        bool frozen = false;                        // its water is ice
    };
    [[nodiscard]] LodColumn paint(int x, int z, int floor, bool varied) const;  // the colour and the sea of a column whose ground is at `floor`; `varied`: trees and shades
    std::shared_ptr<const OverworldGenerator> generator_;
    std::vector<Look> looks_;  // by biome id
    std::vector<LodTrees> trees_;
};

}  // namespace falcon::worldgen
