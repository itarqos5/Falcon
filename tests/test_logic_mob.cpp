// The mobs' path finder (world/mob_path.cpp) on a flat world of stone with walls and steps built on it, and the species table.
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/mob.hpp"
#include "test_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace {

using namespace falcon;

// Chunks -2..2 of stone below y = 0 (the ground is the blocks at y = -1; a mob stands in the cells at y = 0).
Level flatWorld(Block stone) {
    Level level;
    for (int cz = -2; cz <= 2; ++cz) {
        for (int cx = -2; cx <= 2; ++cx) {
            auto chunk = std::make_shared<Chunk>();
            for (int s = 0; s < 4; ++s) chunk->sections[static_cast<std::size_t>(s)] = uniformSection(stone);  // y -64 to -1
            level.putChunk({cx, cz}, chunk);
        }
    }
    return level;
}

bool pathTouches(const std::vector<std::array<int, 3>>& path, int x, int y, int z) {
    return std::any_of(path.begin(), path.end(), [&](const std::array<int, 3>& c) { return c[0] == x && c[1] == y && c[2] == z; });
}

}  // namespace

void testMobPath() {
    installBlockRegistry(BlockRegistry::makeMinimal());
    const auto stone = blockRegistry().find("stone");
    LCHECK(stone.has_value());
    if (!stone) return;

    // Open ground: straight along.
    {
        Level level = flatWorld(*stone);
        const auto path = findMobPath(level, {0, 0, 0}, {6, 0, 0}, 0.6f, 1.8f, 300, false);
        LCHECK(!path.empty());
        LCHECK(path.size() >= 5);
        LCHECK(std::abs(path.back()[0] - 6) <= 1 && std::abs(path.back()[2]) <= 1);
        for (const auto& c : path) LCHECK(c[1] == 0);
    }
    // A wall two blocks high across the way: it goes round the end of it, and never through.
    {
        Level level = flatWorld(*stone);
        for (int z = -4; z <= 4; ++z) {
            for (int y = 0; y <= 1; ++y) level.setBlock(3, y, z, *stone);
        }
        const auto path = findMobPath(level, {0, 0, 0}, {6, 0, 0}, 0.6f, 1.8f, 800, false);
        LCHECK(!path.empty());
        LCHECK(std::abs(path.back()[0] - 6) <= 1 && std::abs(path.back()[2]) <= 1);
        LCHECK(std::any_of(path.begin(), path.end(), [](const std::array<int, 3>& c) { return std::abs(c[2]) > 4; }));
        for (const auto& c : path) LCHECK(!(c[0] == 3 && std::abs(c[2]) <= 4 && c[1] <= 1));
    }
    // A step of one block: it climbs it.
    {
        Level level = flatWorld(*stone);
        for (int z = -6; z <= 6; ++z) {
            for (int x = 3; x <= 8; ++x) level.setBlock(x, 0, z, *stone);
        }
        const auto path = findMobPath(level, {0, 0, 0}, {6, 1, 0}, 0.6f, 1.8f, 300, false);
        LCHECK(!path.empty());
        LCHECK(pathTouches(path, 3, 1, 0) || std::any_of(path.begin(), path.end(), [](const std::array<int, 3>& c) { return c[1] == 1; }));
        LCHECK(std::abs(path.back()[0] - 6) <= 1 && path.back()[1] == 1);
    }
    // A step of two blocks cannot be climbed: the path stops where it can go no further.
    {
        Level level = flatWorld(*stone);
        for (int z = -32; z <= 47; ++z) {  // (all across: no way round it)
            for (int x = 3; x <= 8; ++x) {
                for (int y = 0; y <= 1; ++y) level.setBlock(x, y, z, *stone);
            }
        }
        const auto path = findMobPath(level, {0, 0, 0}, {6, 2, 0}, 0.6f, 1.8f, 600, false);
        for (const auto& c : path) LCHECK(c[0] < 3);
    }
    // Already there: nothing to walk.
    {
        Level level = flatWorld(*stone);
        const auto path = findMobPath(level, {0, 0, 0}, {0, 0, 0}, 0.6f, 1.8f, 100, false);
        LCHECK(path.empty());
    }
    // The species table: the ones the game spawns have the game's health.
    const MobSpecies* zombie = mobSpecies("zombie");
    LCHECK(zombie != nullptr && zombie->health == 20.0f && zombie->attack == 3.0f);
    LCHECK(mobSpecies("pig") != nullptr && mobSpecies("pig")->kind == MobKind::Passive);
    LCHECK(mobSpecies("not_a_mob") == nullptr);
}
