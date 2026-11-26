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
    const MobSpecies* dragon = mobSpecies("ender_dragon");
    LCHECK(zombie != nullptr && zombie->health == 20.0f && zombie->attack == 3.0f);
    LCHECK(dragon != nullptr && dragon->health == 200.0f);
    LCHECK(mobSpecies("pig") != nullptr && mobSpecies("pig")->kind == MobKind::Passive);
    LCHECK(mobSpecies("not_a_mob") == nullptr);
}

// How fast a mob walks and how far a blow throws it, measured on flat stone tick by tick against what the game does. A walking mob speeds itself up by its speed attribute times its own
// forward input (Mob.setSpeed makes that the attribute too, times 0.98): a zombie (0.23) gets 0.23 * 0.23 * 0.98 = 0.052 a tick, and the ground's 0.546 a tick holds it back, which
// ends at 0.114 blocks a tick: 2.3 blocks a second. A blow throws a mob 0.4 a tick from rest (LivingEntity.knockback), which the air (0.91 a tick) takes down slowly.
void testMobMovement() {
    installBlockRegistry(BlockRegistry::makeMinimal());
    const auto stone = blockRegistry().find("stone");
    LCHECK(stone.has_value());
    if (!stone) return;
    const auto makeTick = [&](const Level& level) {
        MobTick in;
        in.level = &level;
        in.player = {20.5, 0.0, 0.5};
        in.playerEyeY = 1.62;
        in.playerTargetable = true;
        in.difficulty = 2;
        in.dayTime = 18000;
        in.daylightBurns = false;
        in.spawnMobs = false;
        in.spawnMonsters = false;
        return in;
    };
    // A zombie after the player: its speed once it has got going.
    {
        Level level = flatWorld(*stone);
        MobsHost host;
        Mobs mobs(host, 7);
        Mob* zombie = mobs.spawn("zombie", 0.5, 0.0, 0.5, true);
        LCHECK(zombie != nullptr);
        if (zombie == nullptr) return;
        const int id = zombie->id;
        const MobTick in = makeTick(level);
        double at40 = 0.0, at100 = 0.0;
        for (int t = 1; t <= 100; ++t) {
            mobs.tick(in);
            if (t == 20) at40 = mobs.find(id)->x;
            if (t == 80) at100 = mobs.find(id)->x;
        }
        const double perSecond = (at100 - at40) / 3.0;  // (60 ticks)
        std::printf("  a zombie chasing: %.2f blocks a second\n", perSecond);
        LCHECK(perSecond > 2.0 && perSecond < 2.6);
    }
    // A blow on a pig standing still, from the west: it goes east, a metre or so in the first half second.
    {
        Level level = flatWorld(*stone);
        MobsHost host;
        Mobs mobs(host, 7);
        Mob* pig = mobs.spawn("pig", 0.5, 0.0, 0.5, true);
        LCHECK(pig != nullptr);
        if (pig == nullptr) return;
        const int id = pig->id;
        MobTick in = makeTick(level);
        in.player = {-3.0, 0.0, 0.5};
        for (int t = 0; t < 5; ++t) mobs.tick(in);  // (it lands and settles)
        const double before = mobs.find(id)->x;
        LCHECK(mobs.hurt(id, 1.0f, -3.0, 0.5, 0.4f, true));
        double at10 = 0.0, at30 = 0.0, fastest = 0.0;
        for (int t = 1; t <= 30; ++t) {
            mobs.tick(in);
            fastest = std::max(fastest, std::abs(mobs.find(id)->x - mobs.find(id)->xo));
            if (t == 10) at10 = mobs.find(id)->x - before;
            if (t == 30) at30 = mobs.find(id)->x - before;
        }
        std::printf("  a pig hit: %.2f blocks away after 10 ticks, %.2f after 30 (fastest tick %.2f blocks)\n", at10, at30, fastest);
        LCHECK(at10 > 1.0 && at10 < 3.0);  // (after that it runs off the way it chooses, in panic: no more to say about where it is)
        LCHECK(fastest < 0.6);
    }
}

// A land animal whose eyes are under the surface paddles up (FloatGoal) and comes to rest at the top of the water, bobbing there, instead of walking about the bottom of
// the pool: the cow the user found strolling the sea floor.
void testMobFloats() {
    installBlockRegistry(BlockRegistry::makeMinimal());
    const auto stone = blockRegistry().find("stone");
    const auto water = blockRegistry().find("water");
    LCHECK(stone.has_value() && water.has_value());
    if (!stone || !water) return;
    Level level = flatWorld(*stone);
    for (int x = -6; x <= 6; ++x) {  // a tank three blocks deep over the ground at y = -1: its surface is y = 3, and its stone rim is a block higher
        for (int z = -6; z <= 6; ++z) {
            for (int y = 0; y <= 2; ++y) level.setBlock(x, y, z, *water);
        }
    }
    for (int side = -7; side <= 7; ++side) {
        for (int y = 0; y <= 3; ++y) {
            level.setBlock(side, y, -7, *stone);
            level.setBlock(side, y, 7, *stone);
            level.setBlock(-7, y, side, *stone);
            level.setBlock(7, y, side, *stone);
        }
    }
    MobsHost host;
    Mobs mobs(host, 11);
    Mob* cow = mobs.spawn("cow", 0.5, 0.0, 0.5, true);
    LCHECK(cow != nullptr);
    if (cow == nullptr) return;
    const int id = cow->id;
    MobTick in;
    in.level = &level;
    in.player = {60.0, 0.0, 0.5};  // (far off: there is nothing to walk to)
    in.playerTargetable = false;
    in.difficulty = 0;
    in.dayTime = 6000;
    in.spawnMobs = false;
    in.spawnMonsters = false;
    for (int t = 0; t < 200; ++t) mobs.tick(in);
    const double after = mobs.find(id)->y;
    // FloatGoal paddles until the eyes are out of the water, not until the whole animal is: a cow is 1.4 tall, its eyes ~1.2 up, so it settles just under 1.8 of a pool that
    // is three deep. On the bottom it would still be at 0.
    std::printf("  a cow in a pool three deep: y = %.2f after 10 s\n", after);
    LCHECK(after > 1.6);
    for (int t = 0; t < 100; ++t) mobs.tick(in);
    std::printf("  and after another 5 s: y = %.2f\n", mobs.find(id)->y);
    LCHECK(mobs.find(id)->y > 1.6);  // (it stays up there, bobbing)
}
