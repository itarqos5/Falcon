// The levels of detail (render/lod_renderer.hpp): how a grid of columns becomes a mesh. The grids are made by hand, so nothing of the game is needed, and the quads of the result
// are counted: a flat sea is a few quads however big the tile is, a step in the ground is one wall, and the edge of a tile always has a wall that hangs below it. Every quad has the
// picture (layer) of the block it shows and its tint.
#include "falcon/render/lod_renderer.hpp"
#include "falcon/worldgen/lod_surface.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (false)

using falcon::LodRenderer;
using falcon::LodTextures;
using falcon::worldgen::LodColumn;
using falcon::worldgen::LodGrid;

constexpr int kCells = LodRenderer::kCells;

// A grid of the tile's cells and the ring round them, each column made by `make(i, j)` (i and j run -1 .. kCells + 1).
LodGrid makeGrid(const std::function<LodColumn(int, int)>& make) {
    LodGrid grid;
    grid.side = kCells + 3;
    grid.columns.resize(static_cast<std::size_t>(grid.side) * static_cast<std::size_t>(grid.side));
    for (int j = -1; j <= kCells + 1; ++j) {
        for (int i = -1; i <= kCells + 1; ++i) grid.columns[static_cast<std::size_t>((j + 1) * grid.side + (i + 1))] = make(i, j);
    }
    return grid;
}

LodColumn land(int height, std::uint8_t r = 100, std::uint8_t g = 150, std::uint8_t b = 60) {
    LodColumn c;
    c.height = static_cast<std::int16_t>(height);
    c.floor = c.height;
    c.ground = falcon::worldgen::LodGround::Stone;  // bare rock: one colour all the way down, so a wall is one band
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}
// Every picture a different layer, so that a test can tell which one a quad has.
LodTextures textures() {
    LodTextures t;
    t.grassTop = 1, t.grassSide = 2, t.dirt = 3, t.stone = 4, t.deepslate = 5, t.sand = 6, t.sandstone = 7, t.snow = 8, t.snowSide = 9, t.terracotta = 10, t.mycelium = 11,
    t.myceliumSide = 12, t.gravel = 13, t.water = 14, t.ice = 15;
    t.leaves = {20, 21, 22, 23, 24, 25, 26, 27};
    return t;
}

LodColumn sea() {
    LodColumn c;
    c.height = 63;
    c.floor = 40;
    c.r = 30;
    c.g = 60;
    c.b = 200;
    c.water = true;
    return c;
}

std::size_t quadsOf(const std::vector<LodRenderer::Vertex>& v) { return v.size() / 4; }

void testFlatSea() {
    const LodGrid grid = makeGrid([](int, int) { return sea(); });
    float lo = 0, hi = 0;
    const auto vertices = LodRenderer::buildColumns(grid, 8, textures(), lo, hi);
    // One top quad for each row (the whole row is alike), and one wall along each of the four edges.
    CHECK(quadsOf(vertices) == static_cast<std::size_t>(kCells + 4));
    CHECK(vertices.size() % 4 == 0);
    CHECK(hi == 63.0f);
    CHECK(lo == 63.0f - (2 * 8 + 4));  // the edge walls hang 2 cells + 4 blocks under the top
}

void testStepInTheGround() {
    // The west half is sea, the east half is land 17 blocks higher (and another colour).
    const LodGrid grid = makeGrid([](int i, int) { return i < kCells / 2 ? sea() : land(80); });
    float lo = 0, hi = 0;
    const auto vertices = LodRenderer::buildColumns(grid, 8, textures(), lo, hi);
    // Two runs in every row; the wall of the step (one quad: the higher side makes it, along the whole column); the edge walls: west and east one each, north and south one
    // for each half (the two halves differ).
    CHECK(quadsOf(vertices) == static_cast<std::size_t>(2 * kCells + 1 + 1 + 1 + 2 + 2));
    CHECK(hi == 80.0f);
    CHECK(lo == 63.0f - 20.0f);
    // The step's wall is as tall as the step: some vertex of it is at 63 and some at 80, and it is x = kCells / 2 (cells) wide in no way: its two x are equal.
    bool found = false;
    for (std::size_t q = 0; q + 3 < vertices.size(); q += 4) {
        const bool sameX = vertices[q].x == vertices[q + 1].x && vertices[q].x == vertices[q + 2].x && vertices[q].x == vertices[q + 3].x;
        if (sameX && vertices[q].x == kCells / 2 && vertices[q].y == 63 && vertices[q + 1].y == 80 && vertices[q + 2].z - vertices[q].z == kCells) found = true;
    }
    CHECK(found);
}

void testShadesAndColours() {
    const LodGrid grid = makeGrid([](int i, int) { return i < kCells / 2 ? land(70, 200, 100, 50) : land(70, 200, 100, 50); });
    float lo = 0, hi = 0;
    const auto vertices = LodRenderer::buildColumns(grid, 4, textures(), lo, hi);
    // The tops have the tint of the ground as it is (the picture of the stone); the walls (here only the edge ones) are the stone picture, untinted, shaded like a block's sides (0.6
    // east and west, 0.8 north and south), and each says which way it looks.
    int tops = 0, eastWest = 0, northSouth = 0;
    for (std::size_t q = 0; q + 3 < vertices.size(); q += 4) {
        const auto& v = vertices[q];
        CHECK(v.layer == 4);
        for (std::size_t k = 0; k < 4; ++k) CHECK(((vertices[q + k].face >> LodRenderer::kCornerShift) & LodRenderer::kCornerMask) == static_cast<int>(k));  // each corner says which it is (the shader grows quads by it)
        if (v.y == vertices[q + 1].y && v.y == vertices[q + 2].y) {
            ++tops;
            CHECK(v.r == 200 && v.g == 100 && v.b == 50);
            CHECK((v.face & LodRenderer::kFaceMask) == LodRenderer::kFaceTop);
        } else if (v.x == vertices[q + 1].x && v.x == vertices[q + 2].x) {
            ++eastWest;
            CHECK(v.r == 153 && v.g == 153 && v.b == 153);
            CHECK((v.face & LodRenderer::kFaceMask) == LodRenderer::kFaceWest || (v.face & LodRenderer::kFaceMask) == LodRenderer::kFaceEast);
        } else {
            ++northSouth;
            CHECK(v.r == 204 && v.g == 204 && v.b == 204);
            CHECK((v.face & LodRenderer::kFaceMask) == LodRenderer::kFaceNorth || (v.face & LodRenderer::kFaceMask) == LodRenderer::kFaceSouth);
        }
    }
    CHECK(tops == kCells);
    CHECK(eastWest == 2);
    CHECK(northSouth == 2);
}

void testWorstCaseFitsTheIndexList() {
    // Every cell a different height and colour: no merging at all. A tile may be this many quads at the most (the shared index list covers it).
    const LodGrid grid = makeGrid([](int i, int j) { return land(60 + ((i * 7 + j * 13) % 40), static_cast<std::uint8_t>(i * 5 + j), static_cast<std::uint8_t>(j * 5 + i), 90); });
    float lo = 0, hi = 0;
    const auto vertices = LodRenderer::buildColumns(grid, 2, textures(), lo, hi);
    CHECK(quadsOf(vertices) <= static_cast<std::size_t>(kCells * kCells * LodRenderer::kMaxQuadsPerCell));
    CHECK(quadsOf(vertices) >= static_cast<std::size_t>(kCells * kCells));  // at least a top for every cell
}

// The walls of a step that stand at x = kCells / 2: each as (low y, high y, colour), found among the quads of a tile.
struct WallBand {
    int low, high;
    int r, g, b;
    int layer;
};
std::vector<WallBand> wallBandsAtMiddle(const std::vector<LodRenderer::Vertex>& v) {
    std::vector<WallBand> out;
    for (std::size_t q = 0; q + 3 < v.size(); q += 4) {
        const bool sameX = v[q].x == v[q + 1].x && v[q].x == v[q + 2].x && v[q].x == v[q + 3].x;
        if (!sameX || v[q].x != kCells / 2 || v[q + 2].z - v[q].z != kCells) continue;
        out.push_back({std::min(v[q].y, v[q + 1].y), std::max(v[q].y, v[q + 1].y), v[q].r, v[q].g, v[q].b, v[q].layer});
    }
    std::sort(out.begin(), out.end(), [](const WallBand& a, const WallBand& b) { return a.high > b.high; });
    return out;
}

void testGroundLayersOnACliff() {
    // Low rock in the west, grass 20 blocks higher in the east: the cliff between them shows the grass's topmost block (the picture of a grass block's side), three blocks of earth under
    // it, and then stone down to the foot of the cliff, each a quad of its own with its own picture (east and west walls are shaded 0.6, and nothing tints these pictures).
    const LodGrid grid = makeGrid([](int i, int) {
        if (i < kCells / 2) return land(60, 120, 120, 120);
        LodColumn c = land(80, 100, 150, 60);
        c.ground = falcon::worldgen::LodGround::Grass;
        return c;
    });
    float lo = 0, hi = 0;
    const auto bands = wallBandsAtMiddle(LodRenderer::buildColumns(grid, 8, textures(), lo, hi));
    CHECK(bands.size() == 3);
    if (bands.size() == 3) {
        CHECK(bands[0].high == 80 && bands[0].low == 79 && bands[0].layer == 2);  // the topmost block: a grass block's side
        CHECK(bands[1].high == 79 && bands[1].low == 76 && bands[1].layer == 3);  // earth
        CHECK(bands[2].high == 76 && bands[2].low == 60 && bands[2].layer == 4);  // stone
        for (const WallBand& b : bands) CHECK(b.r == 153 && b.g == 153 && b.b == 153);
    }
}

void testCanopyStandsOnTheGround() {
    // A forest cell is as high as its trees; the leaves are the first band of its wall (the leaf picture of its kind, in the tint of the foliage), the ground's own bands under them.
    const LodGrid grid = makeGrid([](int i, int) {
        if (i < kCells / 2) return land(60, 120, 120, 120);
        LodColumn c = land(80, 40, 90, 20);
        c.ground = falcon::worldgen::LodGround::Grass;
        c.tree = 3;    // spruce
        c.canopy = 5;  // the ground is at 75
        return c;
    });
    float lo = 0, hi = 0;
    const auto bands = wallBandsAtMiddle(LodRenderer::buildColumns(grid, 8, textures(), lo, hi));
    CHECK(bands.size() == 4);
    if (bands.size() == 4) {
        CHECK(bands[0].high == 80 && bands[0].low == 75 && bands[0].layer == 23 && bands[0].r == 24 && bands[0].g == 54 && bands[0].b == 12);  // the leaves, tinted (x 0.6)
        CHECK(bands[1].high == 75 && bands[1].low == 74 && bands[1].layer == 2);                                                               // the topmost block of the ground
        CHECK(bands[2].high == 74 && bands[2].low == 71 && bands[2].layer == 3);                                                               // earth
        CHECK(bands[3].high == 71 && bands[3].low == 60 && bands[3].layer == 4);                                                               // stone
    }
}

void testDeepslateUnderZero() {
    // A cliff that reaches under y 4 (the edge walls hang that far): stone down to there, deepslate under it.
    const LodGrid grid = makeGrid([](int, int) {
        LodColumn c = land(30, 100, 150, 60);
        c.ground = falcon::worldgen::LodGround::Grass;
        return c;
    });
    float lo = 0, hi = 0;
    const auto vertices = LodRenderer::buildColumns(grid, 128, textures(), lo, hi);  // a wide cell: the edge walls hang 260 blocks
    bool deepslateBelowFour = false, stoneAboveFour = false;
    for (std::size_t q = 0; q + 3 < vertices.size(); q += 4) {
        const int low = std::min(vertices[q].y, vertices[q + 1].y), high = std::max(vertices[q].y, vertices[q + 1].y);
        if (vertices[q].layer == 5 && high == 4) deepslateBelowFour = true;
        if (vertices[q].layer == 4 && low == 4) stoneAboveFour = true;
    }
    CHECK(deepslateBelowFour);
    CHECK(stoneAboveFour);
}

void testTopPictures() {
    // What a top is drawn with: a grass block's top (tinted by the biome), leaves where there are trees, the sea (or ice on a frozen one), sand.
    const LodGrid grid = makeGrid([](int i, int j) {
        LodColumn c = land(70, 90, 180, 50);
        c.ground = falcon::worldgen::LodGround::Grass;
        if (j == 0 && i >= 0) c.ground = falcon::worldgen::LodGround::Sand, c.r = c.g = c.b = 255;
        if (j == 1) c.tree = 7, c.canopy = 4;
        if (j == 2) c = sea();
        if (j == 3) c = sea(), c.ice = true;
        return c;
    });
    float lo = 0, hi = 0;
    const auto vertices = LodRenderer::buildColumns(grid, 8, textures(), lo, hi);
    int sand = 0, leaves = 0, water = 0, ice = 0, grass = 0;
    for (std::size_t q = 0; q + 3 < vertices.size(); q += 4) {
        if ((vertices[q].face & LodRenderer::kFaceMask) != LodRenderer::kFaceTop) continue;
        switch (vertices[q].layer) {
            case 6: ++sand; break;
            case 27: ++leaves; break;
            case 14: ++water; break;
            case 15: ++ice; break;
            case 1: ++grass; break;
            default: break;
        }
    }
    CHECK(sand == 1 && leaves == 1 && water == 1 && ice == 1 && grass == kCells - 4);  // (a row of the tile each; the others are grass)
}

}  // namespace

int main() {
    std::printf("lod tests\n");
    testFlatSea();
    testStepInTheGround();
    testShadesAndColours();
    testWorstCaseFitsTheIndexList();
    testGroundLayersOnACliff();
    testCanopyStandsOnTheGround();
    testDeepslateUnderZero();
    testTopPictures();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all lod tests passed\n");
    return 0;
}
