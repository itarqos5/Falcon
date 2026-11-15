// The levels of detail (render/lod_renderer.hpp): how a grid of columns becomes a mesh. The grids are made by hand, so nothing of the game is needed, and the quads of the result
// are counted: a flat sea is a few quads however big the tile is, a step in the ground is one wall, and the edge of a tile always has a wall that hangs below it.
#include "falcon/render/lod_renderer.hpp"
#include "falcon/worldgen/lod_surface.hpp"

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
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
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
    const auto vertices = LodRenderer::buildColumns(grid, 8, lo, hi);
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
    const auto vertices = LodRenderer::buildColumns(grid, 8, lo, hi);
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
    const auto vertices = LodRenderer::buildColumns(grid, 4, lo, hi);
    // The tops have the colour of the ground as it is; the walls (here only the edge ones) are shaded like a block's sides (0.6 east and west, 0.8 north and south).
    int tops = 0, eastWest = 0, northSouth = 0;
    for (std::size_t q = 0; q + 3 < vertices.size(); q += 4) {
        const auto& v = vertices[q];
        if (v.y == vertices[q + 1].y && v.y == vertices[q + 2].y) {
            ++tops;
            CHECK(v.r == 200 && v.g == 100 && v.b == 50);
        } else if (v.x == vertices[q + 1].x && v.x == vertices[q + 2].x) {
            ++eastWest;
            CHECK(v.r == 120 && v.g == 60 && v.b == 30);
        } else {
            ++northSouth;
            CHECK(v.r == 160 && v.g == 80 && v.b == 40);
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
    const auto vertices = LodRenderer::buildColumns(grid, 2, lo, hi);
    CHECK(quadsOf(vertices) <= static_cast<std::size_t>(kCells * kCells * 5));
    CHECK(quadsOf(vertices) >= static_cast<std::size_t>(kCells * kCells));  // at least a top for every cell
}

}  // namespace

int main() {
    std::printf("lod tests\n");
    testFlatSea();
    testStepInTheGround();
    testShadesAndColours();
    testWorstCaseFitsTheIndexList();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all lod tests passed\n");
    return 0;
}
