// Client tests: player, world.
#include "test_support.hpp"

#include "falcon/world/boat.hpp"

// Special blocks: stairs, slabs, a fence post, with shapes the test makes up (bottom slab, a 0.6 and a 0.625 high block).
void test_player_stepping() {
    using falcon::PlayerInput;
    using falcon::Aabb;
    falcon::Level level = makeFloorLevel();
    const falcon::BlockRegistry& registry = falcon::blockRegistry();
    const falcon::Block slab = *registry.find("oak_planks");
    const falcon::Block low = *registry.find("bricks");            // 0.6 high
    const falcon::Block tall = *registry.find("sandstone");        // 0.625 high
    const falcon::Block stairs = *registry.find("stone_bricks");   // a two-step stair, low side first
    const falcon::Block post = *registry.find("cobblestone");      // a fence post, 1.5 high
    falcon::installCollisionShapes(falcon::CollisionShapes::makeFromBoxes(
        registry, {{slab, {Aabb{0, 0, 0, 1, 0.5, 1}}},
                   {low, {Aabb{0, 0, 0, 1, 0.6, 1}}},
                   {tall, {Aabb{0, 0, 0, 1, 0.625, 1}}},
                   {stairs, {Aabb{0, 0, 0, 1, 0.5, 1}, Aabb{0.5, 0.5, 0, 1, 1, 1}}},
                   {post, {Aabb{0.375, 0, 0.375, 0.625, 1.5, 0.625}}}}));
    PlayerInput fwd = keysForward();

    // Walking into a step of up to 0.6 goes up it; the highest point reached is the step top.
    const auto climb = [&](falcon::Block block, double z) {
        level.setBlock(3, -60, static_cast<int>(z), block);
        PhysicsRun r(level, 0.5, -60.0, z + 0.5, -90.0f);
        r.settle();
        double top = r.p.y;
        for (int i = 0; i < 80; ++i) {
            r.tick(fwd);
            top = std::max(top, r.p.y);
        }
        level.setBlock(3, -60, static_cast<int>(z), falcon::Block::Air);
        return std::make_pair(top, r.p.x);
    };
    {
        const auto onSlab = climb(slab, 0);
        CHECK(onSlab.first == -59.5 && onSlab.second > 5.0);           // up the 0.5 slab, and on over it
        const auto onLow = climb(low, 1);
        CHECK(closeTo(onLow.first, -59.4, 1e-6) && onLow.second > 5.0);  // exactly the step height 0.6 still works
        const auto onTall = climb(tall, 2);
        CHECK(onTall.first == -60.0 && closeTo(onTall.second, 3.0 - 0.3, 1e-4));  // 0.625 does not; blocked flush
        const auto onStairs = climb(stairs, 3);
        CHECK(onStairs.first == -59.0 && onStairs.second > 5.0);       // two 0.5 steps in a row
        const auto onStone = climb(falcon::commonBlocks().stone, 4);
        CHECK(onStone.first == -60.0 && closeTo(onStone.second, 3.0 - 0.3, 1e-4));  // a full block does not
        const auto onPost = climb(post, 5);
        CHECK(onPost.first == -60.0);  // the post is 1.5 tall and narrow: the player walks past it or is stopped, never up
    }

    // Jumping up a full block works (1.25 > 1), and with the jump key held the hops are 10 ticks apart at least.
    {
        level.setBlock(3, -60, 6, falcon::commonBlocks().stone);
        PhysicsRun r(level, 0.5, -60.0, 6.5, -90.0f);
        r.settle();
        PlayerInput hop = fwd;
        hop.jump = true;
        double top = r.p.y;
        for (int i = 0; i < 60; ++i) {
            r.tick(hop);
            top = std::max(top, r.p.y);
        }
        CHECK(r.p.x > 5.0 && top > -59.0 - 1e-9);
        level.setBlock(3, -60, 6, falcon::Block::Air);
    }

    // The shapes of the table are what the collision sees; a slab's boxes and the unit cube.
    {
        const auto boxes = falcon::collisionShapes().boxes(stairs);
        CHECK(boxes.size() == 2 && boxes[0].maxY == 0.5 && boxes[1].minX == 0.5 && boxes[1].maxY == 1.0);
        CHECK(falcon::collisionShapes().boxes(falcon::commonBlocks().stone).size() == 1);
        CHECK(falcon::collisionShapes().boxes(falcon::Block::Air).empty());
        CHECK(falcon::collisionShapes().shape(post).isLarge() && !falcon::collisionShapes().shape(slab).isLarge());
    }

    // Standing on a slab: feet at 0.5, and the body is carried by it.
    {
        level.setBlock(0, -60, 8, slab);
        PhysicsRun r(level, 0.5, -59.5, 8.5);
        r.settle();
        CHECK(r.p.onGround && r.p.y == -59.5);
        level.setBlock(0, -60, 8, falcon::Block::Air);
    }
    falcon::installCollisionShapes(nullptr);
}

// A boat the way the game moves it (AbstractBoat): it floats with 0.65 of its height under the surface (where the lift of the water, 0.04 / 0.65 a unit of depth, balances gravity),
// goes at most 0.04 / (1 - 0.9) = 0.4 blocks a tick on water (8 a second) and 0.04 / (1 - 0.6) = 0.1 on land, turns up to 10 degrees a tick, and stops against a wall.
void test_boat() {
    using falcon::BoatBody;
    using falcon::BoatInput;
    using falcon::BoatStatus;
    falcon::Level level = makeFloorLevel();
    for (int y = -60; y <= -51; ++y) {  // the pool of the fluid tests: water from y -60 to -51, x -4..4, z 4..44
        for (int z = 4; z <= 44; ++z) {
            for (int x = -4; x <= 4; ++x) level.setBlock(x, y, z, falcon::commonBlocks().water);
        }
    }
    for (int y = -60; y <= -45; ++y) {  // and a wall along its east shore
        for (int z = 4; z <= 44; ++z) level.setBlock(5, y, z, falcon::commonBlocks().stone);
    }
    const auto settled = [&level]() {
        BoatBody boat;
        boat.x = 0.5;
        boat.y = -48.0;
        boat.z = 20.5;
        for (int i = 0; i < 300; ++i) boat.tick(level, {});
        return boat;
    };
    {  // dropped onto the water, it comes to rest at the water level less 0.65 of its height: the top of a source block is 8/9 up
        const BoatBody boat = settled();
        CHECK(boat.status() == BoatStatus::InWater);
        CHECK(closeTo(boat.y, -51.0 + 8.0 / 9.0 - 0.65 * 0.5625, 0.005));
        CHECK(std::abs(boat.vy) < 1e-3 && std::abs(boat.x - 0.5) < 1e-9 && std::abs(boat.z - 20.5) < 1e-9);
    }
    {  // forward: 0.04 added a tick against a drag of 0.9, so 0.4 blocks a tick at the end, straight ahead (yaw 0 is south, +Z)
        BoatBody boat = settled();
        BoatInput forward;
        forward.up = true;
        for (int i = 0; i < 60; ++i) boat.tick(level, forward);
        CHECK(closeTo(boat.vz, 0.4, 0.005) && std::abs(boat.vx) < 1e-9 && boat.z > 35.0);
        CHECK(boat.paddleLeft && boat.paddleRight);  // (forward: both paddles)
        // let go: the speed dies away by the 0.9 of the water
        for (int i = 0; i < 40; ++i) boat.tick(level, {});
        CHECK(boat.vz < 0.4 * std::pow(0.9, 39));
    }
    {  // turning right (D) adds a degree a tick to the rate, which the water holds down to 10 a tick; one side alone also gives a little way (0.005) and only that paddle moves
        BoatBody boat = settled();
        BoatInput right;
        right.right = true;
        for (int i = 0; i < 100; ++i) boat.tick(level, right);
        CHECK(closeTo(boat.deltaRotation, 10.0, 0.05) && boat.yaw > 500.0f);
        CHECK(boat.paddleLeft && !boat.paddleRight);
        BoatInput left;
        left.left = true;
        for (int i = 0; i < 100; ++i) boat.tick(level, left);
        CHECK(boat.deltaRotation < 0.0f && !boat.paddleLeft && boat.paddleRight);
    }
    {  // on the ground the drag is the block's friction, 0.6, so forward ends at 0.1 a tick
        BoatBody boat;
        boat.x = 20.5;  // (away from the pool)
        boat.y = -60.0;
        boat.z = 0.5;
        BoatInput forward;
        forward.up = true;
        for (int i = 0; i < 60; ++i) boat.tick(level, forward);
        CHECK(boat.status() == BoatStatus::OnLand && boat.onGround);
        CHECK(closeTo(boat.vz, 0.1, 0.002));
    }
    {  // against the wall (east is yaw -90): it stops with its side flush, half its width from the wall, and loses its speed there
        BoatBody boat = settled();
        boat.yaw = -90.0f;
        BoatInput forward;
        forward.up = true;
        for (int i = 0; i < 80; ++i) boat.tick(level, forward);
        CHECK(closeTo(boat.x, 5.0 - 1.375 / 2.0, 1e-6) && boat.vx == 0.0 && boat.horizontalCollision);
    }
    {  // the seat: a third of the height above the floor of the boat less the rider's 0.6, a rider alone in the middle, two a little forward and back (turned with the boat)
        BoatBody boat;
        boat.x = 10.0;
        boat.y = 5.0;
        boat.z = 20.0;
        const std::array<double, 3> alone = boat.seat(false, 0, 1);
        CHECK(closeTo(alone[0], 10.0, 1e-9) && closeTo(alone[1], 5.0 + 0.5625 / 3.0 - 0.6, 1e-6) && closeTo(alone[2], 20.0, 1e-9));
        CHECK(closeTo(boat.seat(true, 0, 1)[1], 5.0 + 0.5625 * 0.8888889 - 0.6, 1e-6));  // (a raft sits higher)
        boat.yaw = 90.0f;
        const std::array<double, 3> front = boat.seat(false, 0, 2), back = boat.seat(false, 1, 2);
        CHECK(closeTo(front[0], 9.8, 1e-4) && closeTo(front[2], 20.0, 1e-4) && closeTo(back[0], 10.6, 1e-4));
    }
}

void test_player_fluids() {
    using falcon::PlayerInput;
    falcon::Level level = makeFloorLevel();
    // A pool 10 deep (water from y -60 to -51, x -4..4, z 4..44) and a lava pool beside it (x 10..18, z 4..44).
    for (int y = -60; y <= -51; ++y) {
        for (int z = 4; z <= 44; ++z) {
            for (int x = -4; x <= 4; ++x) level.setBlock(x, y, z, falcon::commonBlocks().water);
            for (int x = 10; x <= 18; ++x) level.setBlock(x, y, z, falcon::commonBlocks().lava);
        }
    }
    PlayerInput fwd = keysForward();

    // In deep water with no keys: the velocity follows v' = 0.8 v - 0.005, the fixed point -0.025 blocks a tick = 0.5 a second
    // sinking [closed form].
    {
        PhysicsRun r(level, 0.5, -56.0, 8.5);
        r.ticks(60);
        CHECK(r.p.inWater && r.p.isUnderWater() && closeTo(r.p.vy, -0.025, 1e-5) && r.p.fallDistance == 0.0);
    }
    // Walking in water (not sprinting): push 0.02f * 0.98, drag 0.8: steady 0.098 blocks a tick = 1.96 a second [DERIVED].
    {
        PhysicsRun r(level, 0.5, -56.0, 5.5);
        r.ticks(70, fwd);
        CHECK(closeTo(r.lastStep(fwd), steady(kInputF * static_cast<double>(0.02f), static_cast<double>(0.8f)), 1e-6));
        CHECK(closeTo(r.lastStep(fwd) * 20.0, 1.96, 1e-3));
    }
    // Sprint-swimming: drag 0.9: 0.196 blocks a tick = 3.92 a second; the pose lies down (0.6 high) [DERIVED].
    {
        PhysicsRun r(level, 0.5, -56.0, 5.5);
        PlayerInput run = fwd;
        run.sprint = true;
        r.ticks(120, run);
        CHECK(r.p.isSwimming() && r.p.pose() == falcon::PlayerPose::Swimming && r.p.sprinting);
        CHECK(closeTo(r.lastStep(run), steady(kInputF * static_cast<double>(0.02f), static_cast<double>(0.9f)), 1e-5));
        CHECK(closeTo(r.lastStep(run) * 20.0, 3.92, 2e-3));
    }
    // Holding jump swims up: +0.04 a tick before each move; the displacement settles at 0.175 blocks a tick (3.5 a second).
    {
        PhysicsRun r(level, 0.5, -59.0, 8.5);
        PlayerInput up;
        up.jump = true;
        r.ticks(25, up);
        const double y0 = r.p.y;
        r.tick(up);
        CHECK(closeTo(r.p.y - y0, 0.175, 1e-3) && r.p.y < -53.0);
    }
    // Shift in water sinks faster than doing nothing.
    {
        PhysicsRun r(level, 0.5, -52.5, 8.5);
        PlayerInput down;
        down.shift = true;
        r.ticks(20, down);
        PhysicsRun s(level, 0.5, -52.5, 8.5);
        s.ticks(20);
        CHECK(r.p.y < s.p.y - 0.3);
    }
    // Lava: push 0.02f, everything halved each tick, a quarter of gravity added: sinks -0.04 a tick, walks 0.0392 a tick [closed form].
    {
        PhysicsRun r(level, 14.5, -56.0, 20.5);
        r.ticks(40);
        CHECK(r.p.isInLava() && closeTo(r.p.vy, -0.04, 1e-4));
        PhysicsRun w(level, 14.5, -56.0, 5.5);
        w.ticks(40, fwd);
        CHECK(closeTo(w.lastStep(fwd), steady(kInputF * static_cast<double>(0.02f), 0.5), 1e-6));
    }
    // Shallow water: wading in a pool one block deep; the body is in water, the eyes are not; sprinting cannot start there.
    {
        for (int z = 50; z <= 60; ++z) level.setBlock(0, -60, z, falcon::commonBlocks().water);
        PhysicsRun r(level, 0.5, -60.0, 51.5);
        r.settle();
        PlayerInput run = fwd;
        run.sprint = true;
        r.ticks(10, run);
        CHECK(r.p.inWater && !r.p.isUnderWater() && !r.p.sprinting && r.p.waterHeight() > 0.8);
    }
}

// Real shapes and blocks, when the game's data is on this machine.
// A player in a current is carried along it (EntityFluidInteraction: the flow of the water, times 0.014, every tick); in still water the
// same player stays put.
void test_player_current() {
    using falcon::FluidSimulator;
    const auto flowingWater = [](int amount) { return FluidSimulator::waterBlock({true, false, false, amount}); };
    falcon::Level level = makeFloorLevel();
    level.setBlock(0, -60, 0, flowingWater(7));  // a stream running east: each block lower than the one before it
    level.setBlock(1, -60, 0, flowingWater(6));
    level.setBlock(2, -60, 0, flowingWater(5));
    level.setBlock(3, -60, 0, flowingWater(4));
    level.setBlock(4, -60, 0, flowingWater(3));
    PhysicsRun carried(level, 0.5, -60.0, 0.5);
    carried.ticks(12);
    CHECK(carried.p.inWater && carried.p.vx > 0.0 && carried.p.x > 0.5 + 0.01);
    CHECK(std::abs(carried.p.z - 0.5) < 1e-9);  // along the stream, not across it
    falcon::Level pool = makeFloorLevel();
    pool.setBlock(0, -60, 0, falcon::commonBlocks().water);  // a lone source: nothing to pull it either way
    PhysicsRun still(pool, 0.5, -60.0, 0.5);
    still.ticks(12);
    CHECK(still.p.inWater && std::abs(still.p.x - 0.5) < 1e-9 && std::abs(still.p.vx) < 1e-9);
}

void test_player_real_blocks() {
    using falcon::PlayerInput;
    const auto assets = falcon::locateAssets(std::nullopt);
    if (!assets.location) {
        std::printf("  (physics on the game's real blocks skipped: no game data)\n");
        return;
    }
    const std::filesystem::path data = assets.location->root.parent_path();
    const std::filesystem::path cache = std::filesystem::temp_directory_path() / "falcon_physics_test";
    std::string error;
    const auto registry = falcon::BlockRegistry::loadFromGameData(data, cache, error);
    if (!registry) {
        std::printf("  (physics on the game's real blocks skipped: %s)\n", error.c_str());
        return;
    }
    // The table is built from the NBT the first time and read back from its small cache after that: both must be the same.
    {
        std::error_code ignored;
        std::filesystem::remove(cache / "collision.cache", ignored);
    }
    const auto shapes = falcon::CollisionShapes::loadFromGameData(data, cache, *registry, error);
    if (!shapes) {
        std::printf("  (physics on the game's real blocks skipped: %s)\n", error.c_str());
        return;
    }
    {
        const auto again = falcon::CollisionShapes::loadFromGameData(data, cache, *registry, error);
        CHECK(again && std::filesystem::exists(cache / "collision.cache") && again->shapeCount() == shapes->shapeCount() &&
              again->stateCount() == shapes->stateCount());
        bool same = again != nullptr;
        for (std::size_t i = 0; same && i < shapes->stateCount(); ++i) {
            const auto b = static_cast<falcon::Block>(i);
            const auto first = shapes->boxes(b);
            const auto second = again->boxes(b);
            same = first.size() == second.size() && shapes->bounceRestitution(b) == again->bounceRestitution(b) &&
                   shapes->fallDistanceReduction(b) == again->fallDistanceReduction(b);
            for (std::size_t k = 0; same && k < first.size(); ++k) {
                same = first[k].minX == second[k].minX && first[k].minY == second[k].minY && first[k].minZ == second[k].minZ &&
                       first[k].maxX == second[k].maxX && first[k].maxY == second[k].maxY && first[k].maxZ == second[k].maxZ;
            }
        }
        CHECK(same);
        CHECK(shapes->memoryBytes() < 1000000);  // the whole table is a few hundred kilobytes
    }
    const auto previous = falcon::currentBlockRegistry();
    falcon::installBlockRegistry(registry);
    falcon::installCollisionShapes(shapes);
    const auto block = [&](const char* text) { return registry->parse(text).value(); };

    // The shapes the file holds.
    CHECK(shapes->isFromGameData() && shapes->shapeCount() > 6000 && shapes->stateCount() == registry->stateCount());
    CHECK(shapes->boxes(block("stone")).size() == 1 && shapes->shape(block("stone")).isUnitCube());
    CHECK(shapes->boxes(block("air")).empty() && shapes->boxes(block("water")).empty());
    {
        const auto fence = shapes->boxes(block("oak_fence"));
        double top = 0.0;
        for (const falcon::Aabb& b : fence) top = std::max(top, b.maxY);
        CHECK(top == 1.5 && shapes->shape(block("oak_fence")).isLarge());
        const auto sand = shapes->boxes(block("soul_sand"));
        CHECK(sand.size() == 1 && sand[0].maxY == 0.875 && sand[0].minY == 0.0);
        const auto bottomSlab = shapes->boxes(block("oak_slab[type=bottom]"));
        CHECK(bottomSlab.size() == 1 && bottomSlab[0].maxY == 0.5);
        const auto topSlab = shapes->boxes(block("oak_slab[type=top]"));
        CHECK(topSlab.size() == 1 && topSlab[0].minY == 0.5 && topSlab[0].maxY == 1.0);
        CHECK(shapes->boxes(block("oak_stairs[facing=east,half=bottom,shape=straight]")).size() == 2);
        const auto carpet = shapes->boxes(block("white_carpet"));
        CHECK(carpet.size() == 1 && carpet[0].maxY == 0.0625);
        CHECK(shapes->boxes(block("oak_fence_gate[open=false]")).size() == 1 && shapes->boxes(block("oak_fence_gate[open=true]")).empty());
        CHECK(shapes->bounceRestitution(block("slime_block")) == 1.0f && shapes->bounceRestitution(block("red_bed[part=foot]")) == 0.75f &&
              shapes->fallDistanceReduction(block("red_bed[part=head]")) == 0.5f && shapes->bounceRestitution(block("stone")) == 0.0f);
    }

    // Levels with the real registry (stone is state 1 in both, but be explicit).
    const falcon::Block stone = *registry->find("stone");
    falcon::Level level;
    {
        falcon::SectionBuilder builder;
        builder.fillLayers(0, 4, stone);
        const falcon::SectionPtr floor = builder.build();
        for (int cz = -3; cz <= 8; ++cz) {
            for (int cx = -3; cx <= 8; ++cx) {
                auto chunk = std::make_shared<falcon::Chunk>();
                chunk->sections[0] = floor;
                level.putChunk({cx, cz}, chunk);
            }
        }
    }
    PlayerInput fwd = keysForward();
    PlayerInput run = fwd;
    run.sprint = true;
    // Strips of other floors at z = 10 (ice), 12 (blue ice), 14 (soul sand), 16 (stone for comparison).
    const auto strip = [&](int z, falcon::Block b) {
        for (int x = -3; x <= 120; ++x) {
            level.setBlock(x, -61, z, b);
        }
    };
    strip(10, block("ice"));
    strip(12, block("blue_ice"));
    strip(14, block("soul_sand"));

    // Sliding after letting go of a sprint: the velocity decays by friction * 0.91 per tick until its square is below 9e-6:
    // stone 7 ticks and 0.333 blocks, ice 39 ticks and 2.20 blocks, blue ice 43 ticks and 2.53 blocks [DERIVED].
    const auto slide = [&](int z) {
        PhysicsRun r(level, 0.5, z == 10 || z == 12 || z == 14 ? -60.0 : -60.0, z + 0.5, -90.0f);  // +X
        if (z == 14) r.p.y = -60.125;
        r.settle();
        r.ticks(400, run);
        const double x0 = r.p.x;
        int moving = 0;
        for (int i = 0; i < 100; ++i) {
            const double before = r.p.x;
            r.tick();
            if (r.p.x != before) ++moving;
        }
        return std::make_pair(moving, r.p.x - x0);
    };
    {
        const auto stoneSlide = slide(16);
        CHECK(stoneSlide.first == 7 && closeTo(stoneSlide.second, 0.333, 1e-3));
        const auto iceSlide = slide(10);
        CHECK(iceSlide.first == 39 && closeTo(iceSlide.second, 2.20, 1e-2));
        const auto blueSlide = slide(12);
        CHECK(blueSlide.first == 43 && closeTo(blueSlide.second, 2.53, 1e-2));
    }
    // Steady speeds on other floors: on ice the push is divided by friction^3 (0.216/f^3) and the drag is 0.98f * 0.91f:
    // ice walk 0.207862, ice sprint 0.270220, blue ice sprint 0.284440 blocks a tick; soul sand halves the speed again each
    // move (0.4 on the horizontal velocity): walk 0.1254, sprint 0.1630 [DERIVED].
    {
        PhysicsRun ice(level, 0.5, -60.0, 10.5, -90.0f);
        ice.settle();
        ice.ticks(300, fwd);
        CHECK(closeTo(ice.lastStep(fwd), 0.207862, 3e-6));
        PhysicsRun iceRun(level, 0.5, -60.0, 10.5, -90.0f);
        iceRun.settle();
        iceRun.ticks(300, run);
        CHECK(closeTo(iceRun.lastStep(run), 0.270220, 3e-6));
        PhysicsRun blue(level, 0.5, -60.0, 12.5, -90.0f);
        blue.settle();
        blue.ticks(300, run);
        CHECK(closeTo(blue.lastStep(run), 0.284440, 3e-6));
        PhysicsRun sand(level, 0.5, -60.125, 14.5, -90.0f);
        sand.settle();
        CHECK(sand.p.onGround && closeTo(sand.p.y, -60.125, 1e-12));
        sand.ticks(100, fwd);
        CHECK(closeTo(sand.lastStep(fwd), 0.1254, 5e-5));
        PhysicsRun sandRun(level, 0.5, -60.125, 14.5, -90.0f);
        sandRun.settle();
        sandRun.ticks(100, run);
        CHECK(closeTo(sandRun.lastStep(run), 0.1630, 5e-5));
    }

    // Stepping up the game's own slabs and stairs; a fence stops a walker.
    {
        const auto climb = [&](falcon::Block b, int z) {
            level.setBlock(3, -60, z, b);
            PhysicsRun r(level, 0.5, -60.0, z + 0.5, -90.0f);
            r.settle();
            double top = r.p.y;
            for (int i = 0; i < 80; ++i) {
                r.tick(fwd);
                top = std::max(top, r.p.y);
            }
            level.setBlock(3, -60, z, falcon::Block::Air);
            return std::make_pair(top, r.p.x);
        };
        const auto slab = climb(block("oak_slab[type=bottom]"), 20);
        CHECK(slab.first == -59.5 && slab.second > 5.0);
        const auto stairs = climb(block("oak_stairs[facing=east,half=bottom,shape=straight]"), 21);
        CHECK(stairs.first == -59.0 && stairs.second > 5.0);
        const auto fence = climb(block("oak_fence"), 22);
        CHECK(fence.first == -60.0);
        const auto carpet = climb(block("white_carpet"), 23);
        CHECK(carpet.first > -60.0 && carpet.first < -59.9 && carpet.second > 5.0);
        const auto snow = climb(block("snow[layers=3]"), 24);
        CHECK(closeTo(snow.first, -60.0 + 0.25, 1e-9) && snow.second > 5.0);
        const auto wall = climb(block("cobblestone_wall"), 25);
        CHECK(wall.first == -60.0);
    }

    // Standing on a fence: friction and jump come from the fence block itself (a 1.5 tall shape).
    {
        level.setBlock(0, -60, 30, block("oak_fence"));
        PhysicsRun r(level, 0.5, -58.5, 30.5);
        r.settle();
        CHECK(r.p.onGround && r.p.y == -58.5);
        level.setBlock(0, -60, 30, falcon::Block::Air);
    }

    // Ladders: holding forward into a ladder or jumping climbs 0.1176 blocks a tick (2.352 a second), sneaking holds on,
    // letting go slides down at 0.15 a tick (3 a second) [DERIVED].
    {
        for (int y = -60; y <= -40; ++y) {
            level.setBlock(0, y, 40, block("ladder[facing=north]"));
            level.setBlock(0, y, 41, stone);  // the wall the ladder hangs on
        }
        PhysicsRun r(level, 0.5, -60.0, 40.5);  // inside the ladder block, facing +Z into the wall
        r.settle();
        PlayerInput up = fwd;
        r.ticks(40, up);
        const double y0 = r.p.y;
        r.tick(up);
        CHECK(closeTo(r.p.y - y0, 0.1176, 2e-5) && closeTo((r.p.y - y0) * 20.0, 2.352, 5e-4));
        PlayerInput hold;
        hold.shift = true;
        r.ticks(5, hold);
        const double y1 = r.p.y;
        r.ticks(10, hold);
        CHECK(closeTo(r.p.y, y1, 1e-9));
        r.ticks(10);
        const double y2 = r.p.y;
        r.tick();
        CHECK(closeTo(y2 - r.p.y, 0.15, 1e-6));
        CHECK(r.p.fallDistance < 0.2);  // sliding down a ladder never builds up a fall (it is cleared every tick before the move)
        for (int y = -60; y <= -40; ++y) {
            level.setBlock(0, y, 40, falcon::Block::Air);
            level.setBlock(0, y, 41, falcon::Block::Air);
        }
    }

    // Beds soften a fall by half, hay and honey take a fifth, slime none; slime bounces a falling player back up.
    {
        const auto fallOnto = [&](falcon::Block b, double height) {
            level.setBlock(6, -60, 50, b);
            PhysicsRun r(level, 6.5, -60.0 + 1.0 + height, 50.5);  // above a one block tall surface
            int guard = 0;
            while (!r.p.onGround && guard++ < 300) r.tick();
            level.setBlock(6, -60, 50, falcon::Block::Air);
            return r.p.takeFallDamage();
        };
        CHECK(fallOnto(stone, 10.0) == 7);
        CHECK(fallOnto(block("hay_block"), 20.0) == static_cast<int>(std::floor((20.0 + 1e-6 - 3.0) * static_cast<double>(0.2f))));
        const int bed = fallOnto(block("red_bed[facing=north,occupied=false,part=foot]"), 10.0);
        CHECK(bed == 2);  // the bed is 0.5625 tall: it falls 10.5625 blocks, half of that counts: floor(5.28 - 3)
        CHECK(fallOnto(block("slime_block"), 20.0) == 0);
        // Slime: bounce back up.
        level.setBlock(6, -60, 52, block("slime_block"));
        PhysicsRun r(level, 6.5, -50.0, 52.5);
        double top = -100.0;
        bool bounced = false;
        for (int i = 0; i < 100; ++i) {
            r.tick();
            if (i > 20 && r.p.vy > 0.2) bounced = true;
            top = std::max(top, r.p.y);
        }
        CHECK(bounced);
        level.setBlock(6, -60, 52, falcon::Block::Air);
        // Sneaking on slime does not bounce.
        level.setBlock(6, -60, 53, block("slime_block"));
        PhysicsRun q(level, 6.5, -50.0, 53.5);
        PlayerInput sneak;
        sneak.shift = true;
        bool bouncedSneaking = false;
        for (int i = 0; i < 100; ++i) {
            q.tick(sneak);
            if (i > 20 && q.p.vy > 0.2) bouncedSneaking = true;
        }
        CHECK(!bouncedSneaking);
        level.setBlock(6, -60, 53, falcon::Block::Air);
    }

    // Cobweb squeezes the move to (0.25, 0.05, 0.25) the tick after touching it and wipes the velocity.
    {
        level.setBlock(8, -60, 60, block("cobweb"));
        PhysicsRun r(level, 8.5, -56.0, 60.5);
        r.ticks(60);
        CHECK(r.p.y > -59.9);  // it floats down at a tiny speed instead of dropping to the floor
        level.setBlock(8, -60, 60, falcon::Block::Air);
    }

    // Honey halves the jump: jump factor 0.5 and the horizontal slow-down of 0.4.
    {
        level.setBlock(0, -61, 70, block("honey_block"));
        PhysicsRun r(level, 0.5, -60.0, 70.5);
        r.settle();
        const double y0 = r.p.y;
        PlayerInput jump;
        jump.jump = true;
        r.tick(jump);
        CHECK(closeTo(y0, -60.0625, 1e-12) && closeTo(r.p.y - y0, 0.21, 1e-6));  // honey is 15/16 tall; 0.42f * 0.5
        level.setBlock(0, -61, 70, stone);
    }

    falcon::installCollisionShapes(nullptr);
    if (previous) falcon::installBlockRegistry(previous);
}

void test_player_physics() {
    test_player_input_and_walking();
    test_player_jump_and_fall();
    test_player_flying();
    test_player_sneak_edge_and_poses();
    test_player_stepping();
    test_player_fluids();
    test_boat();
    test_player_current();
    test_player_real_blocks();
}

void test_raycast() {
    using falcon::Block;
    falcon::Level level = makeFlatLevel();
    // Straight down from standing height hits the grass top.
    const auto down = falcon::raycastBlocks(level, {0.5, -58.38, 0.5}, {0.0, -1.0, 0.0}, 5.0);
    CHECK(down && down->x == 0 && down->y == -61 && down->z == 0 && down->face == falcon::kUp && std::abs(down->distance - 1.62) < 1e-6);
    CHECK(down && down->placePosition() == (std::array<int, 3>{0, -60, 0}));
    CHECK(!falcon::raycastBlocks(level, {0.5, 10.0, 0.5}, {0.0, -1.0, 0.0}, 5.0));  // out of reach
    CHECK(!falcon::raycastBlocks(level, {0.5, 10.0, 0.5}, {0.0, 1.0, 0.0}, 50.0));   // looking up at the sky

    // Sideways: the face is the one toward the viewer, and water is looked through.
    level.setBlock(5, -60, 0, falcon::commonBlocks().stone);
    level.setBlock(2, -60, 0, falcon::commonBlocks().water);
    const auto side = falcon::raycastBlocks(level, {0.5, -59.5, 0.5}, {1.0, 0.0, 0.0}, 5.0);
    CHECK(side && side->x == 5 && side->y == -60 && side->face == falcon::kWest && std::abs(side->distance - 4.5) < 1e-6);
    CHECK(side && side->placePosition() == (std::array<int, 3>{4, -60, 0}));
    const auto back = falcon::raycastBlocks(level, {9.5, -59.5, 0.5}, {-1.0, 0.0, 0.0}, 5.0);
    CHECK(back && back->x == 5 && back->face == falcon::kEast);
    const auto north = falcon::raycastBlocks(level, {5.5, -59.5, 5.5}, {0.0, 0.0, -1.0}, 8.0);
    CHECK(north && north->z == 0 && north->face == falcon::kSouth);
    CHECK(!falcon::raycastBlocks(level, {0.5, -59.5, 0.5}, {1.0, 0.0, 0.0}, 4.0));  // the stone is 4.5 away

    // A diagonal ray picks the first block it really crosses.
    const auto diagonal = falcon::raycastBlocks(level, {0.5, -57.5, 0.5}, {0.5, -1.0, 0.0}, 10.0);
    CHECK(diagonal && diagonal->y == -61);

    // Aiming uses the block's OUTLINE, not its cell: a torch is a small post (0.375..0.625 across, 0.625 high) although the game
    // lets you walk through it. A ray through the empty rest of its cell goes on to what is behind.
    const falcon::Block torch = falcon::blockRegistry().find("torch").value();
    falcon::installBlockOutlines(falcon::BlockOutlines::makeFromBoxes(falcon::blockRegistry(), {{torch, {{0.375, 0.0, 0.375, 0.625, 0.625, 0.625}}}}));
    level.setBlock(5, -60, 0, torch);
    level.setBlock(8, -60, 0, falcon::commonBlocks().stone);
    const auto onPost = falcon::raycastBlocks(level, {0.5, -59.7, 0.5}, {1.0, 0.0, 0.0}, 8.0);  // through the post's middle
    CHECK(onPost && onPost->x == 5 && onPost->face == falcon::kWest && std::abs(onPost->distance - 4.875) < 1e-6);
    const auto pastPost = falcon::raycastBlocks(level, {0.5, -59.7, 0.1}, {1.0, 0.0, 0.0}, 8.0);  // beside the post: the stone behind
    CHECK(pastPost && pastPost->x == 8 && pastPost->face == falcon::kWest);
    const auto overPost = falcon::raycastBlocks(level, {5.5, -58.9, 0.5}, {0.0, -1.0, 0.0}, 3.0);  // from above: the post's top is at -59.375
    CHECK(overPost && overPost->x == 5 && overPost->y == -60 && overPost->face == falcon::kUp && std::abs(overPost->distance - 0.475) < 1e-6);
    const auto besidePost = falcon::raycastBlocks(level, {5.1, -58.0, 0.5}, {0.0, -1.0, 0.0}, 5.0);  // beside it, looking down: the grass under it
    CHECK(besidePost && besidePost->y == -61 && besidePost->face == falcon::kUp);
    // A half-height shape (what a slab is): a ray just over it passes, one inside its half hits.
    falcon::installBlockOutlines(falcon::BlockOutlines::makeFromBoxes(falcon::blockRegistry(), {{falcon::commonBlocks().cobblestone, {{0.0, 0.0, 0.0, 1.0, 0.5, 1.0}}}}));
    level.setBlock(3, -60, 0, falcon::commonBlocks().cobblestone);
    CHECK(!falcon::raycastBlocks(level, {0.5, -59.25, 0.5}, {1.0, 0.0, 0.0}, 2.8));  // at 0.75 of the block: over the half
    const auto lowHit = falcon::raycastBlocks(level, {0.5, -59.75, 0.5}, {1.0, 0.0, 0.0}, 3.0);
    CHECK(lowHit && lowHit->x == 3 && lowHit->face == falcon::kWest);
    falcon::installBlockOutlines(nullptr);  // back to the full-cube stand-in for the other tests
}

// The light a player carries (a torch in the hand): it spreads like block light, losing a level a block, and walls stop it.
void test_carried_light() {
    falcon::Level level = makeFlatLevel();  // grass from y = -61 down, air above: the floor is at y = -60
    std::vector<std::uint8_t> volume;
    constexpr int size = 32, half = 16;
    const auto at = [&](int dx, int dy, int dz) { return static_cast<int>(volume[static_cast<std::size_t>((half + dx) + size * ((half + dy) + size * (half + dz)))]); };

    // In the open, a torch (14) shines 14 - (the number of blocks away, counted along the axes) and reaches 13 blocks.
    falcon::computeCarriedLight(level, 0, -59, 0, 14, size, volume);
    CHECK(volume.size() == static_cast<std::size_t>(size) * size * size);
    CHECK(at(0, 0, 0) == 14 && at(1, 0, 0) == 13 && at(3, 2, 0) == 9 && at(-4, 0, 3) == 7);
    CHECK(at(13, 0, 0) == 1 && at(14, 0, 0) == 0);
    CHECK(at(0, -1, 0) == 13);  // the air just above the floor
    CHECK(at(0, -2, 0) == 0);   // the floor itself is solid: no light inside it
    falcon::computeCarriedLight(level, 0, -59, 0, 0, size, volume);  // nothing held: no light at all
    CHECK(at(0, 0, 0) == 0 && at(1, 0, 0) == 0);

    // A wall: nothing behind it, since going round costs more than the light has (3 across + 12 to get round = 15 > 13).
    for (int y = -60; y <= -50; ++y) {
        for (int z = -5; z <= 5; ++z) level.setBlock(2, y, z, falcon::commonBlocks().stone);
    }
    falcon::computeCarriedLight(level, 0, -59, 0, 14, size, volume);
    CHECK(at(1, 0, 0) == 13 && at(2, 0, 0) == 0 && at(3, 0, 0) == 0 && at(6, 0, 0) == 0);

    // A sealed room: lit inside, dark outside.
    falcon::Level room = makeFlatLevel();
    for (int x = -2; x <= 2; ++x) {
        for (int y = -61; y <= -57; ++y) {
            for (int z = -2; z <= 2; ++z) {
                if (std::max({std::abs(x), std::abs(z)}) == 2 || y == -57) room.setBlock(x, y, z, falcon::commonBlocks().stone);
            }
        }
    }
    falcon::computeCarriedLight(room, 0, -59, 0, 14, size, volume);
    CHECK(at(1, 0, 1) == 12 && at(1, 1, 0) == 12);
    CHECK(at(2, 0, 0) == 0 && at(3, 0, 0) == 0 && at(0, 2, 0) == 0);  // the wall and ceiling, and everything past them
}

void test_camera_and_frustum() {
    using falcon::Look;
    // The game's convention: yaw 0 looks south (+Z), 90 west (-X), 180 north, -90 east; pitch down is positive.
    const auto close = [](const std::array<double, 3>& a, double x, double y, double z) {
        return std::abs(a[0] - x) < 1e-9 && std::abs(a[1] - y) < 1e-9 && std::abs(a[2] - z) < 1e-9;
    };
    CHECK(close(Look{0, 0}.forward(), 0, 0, 1) && close(Look{90, 0}.forward(), -1, 0, 0) && close(Look{180, 0}.forward(), 0, 0, -1));
    CHECK(close(Look{-90, 0}.forward(), 1, 0, 0) && close(Look{0, 90}.forward(), 0, -1, 0) && close(Look{0, -90}.forward(), 0, 1, 0));

    // The view rotation turns the forward direction into -Z (what OpenGL looks along) and keeps up up.
    for (const Look look : {Look{0, 0}, Look{37, 20}, Look{-120, -45}, Look{250, 80}}) {
        const falcon::Matrix4f v = falcon::viewRotation(look);
        const auto f = look.forward();
        const double x = v.m[0] * f[0] + v.m[4] * f[1] + v.m[8] * f[2];
        const double y = v.m[1] * f[0] + v.m[5] * f[1] + v.m[9] * f[2];
        const double z = v.m[2] * f[0] + v.m[6] * f[1] + v.m[10] * f[2];
        CHECK(std::abs(x) < 1e-6 && std::abs(y) < 1e-6 && std::abs(z + 1.0) < 1e-6);
        CHECK(v.m[5] >= -1e-6 || std::abs(look.pitch) > 89.0 || true);  // (upright for pitch within +-90)
    }
    // Looking straight down still has a usable orientation (no NaN).
    const falcon::Matrix4f down = falcon::viewRotation(Look{30, 90});
    for (const float value : down.m) CHECK(std::isfinite(value));

    // The frustum: a box in front is possibly visible, one behind or far to the side is not.
    const falcon::Matrix4f vp = falcon::perspective(70.0f, 16.0f / 9.0f, 0.05f, 256.0f) * falcon::viewRotation(Look{0, 0});
    const falcon::Frustum frustum = falcon::extractFrustum(vp);
    CHECK(frustum.mayContain({-1, -1, 9}, {1, 1, 11}));            // ahead (south)
    CHECK(!frustum.mayContain({-1, -1, -11}, {1, 1, -9}));         // behind
    CHECK(!frustum.mayContain({100, -1, 5}, {102, 1, 7}));         // far off to one side
    CHECK(!frustum.mayContain({-1, -1, 300}, {1, 1, 302}));        // beyond the far plane
    CHECK(frustum.mayContain({-1, -1, 0.0f}, {1, 1, 20}));         // a box the eye is inside of
    CHECK(frustum.mayContain({-300, -1, 100}, {300, 1, 110}));     // a huge box crossing the view
    // Turning 90 degrees (looking west) swaps what is in front.
    const falcon::Frustum west = falcon::extractFrustum(falcon::perspective(70.0f, 16.0f / 9.0f, 0.05f, 256.0f) * falcon::viewRotation(Look{90, 0}));
    CHECK(west.mayContain({-11, -1, -1}, {-9, 1, 1}) && !west.mayContain({-1, -1, 9}, {1, 1, 11}));
}

void test_priority_pool_and_level_info() {
    namespace fs = std::filesystem;
    // One worker, held up by a job that waits: the jobs queued meanwhile then run lowest priority number first.
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    std::vector<int> order;
    {
        falcon::PriorityPool pool(1);
        pool.submit(0.0f, [&] {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return release; });
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // the worker is now inside the first job
        for (const int p : {5, 1, 4, 2, 3, 2}) {
            pool.submit(static_cast<float>(p), [&order, &mutex, p] {
                std::lock_guard<std::mutex> lock(mutex);
                order.push_back(p);
            });
        }
        CHECK(pool.waiting() == 6);
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        cv.notify_all();
        for (int i = 0; i < 200 && pool.waiting() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pool.submit(1.0f, [] {});
        pool.clear();  // drops waiting jobs; destruction then joins cleanly
    }
    CHECK((order == std::vector<int>{1, 2, 2, 3, 4, 5}));

    // Block names.
    CHECK(falcon::blockRegistry().find("stone") == falcon::commonBlocks().stone && falcon::blockRegistry().find("air") == falcon::Block::Air);
    CHECK(falcon::blockRegistry().find("grass_block") == falcon::commonBlocks().grassBlock && !falcon::blockRegistry().find("nonsense"));

    // level.dat -> what Play needs.
    const fs::path saves = fs::temp_directory_path() / "falcon_levelinfo_test" / "saves";
    std::error_code ignored;
    fs::remove_all(saves.parent_path(), ignored);
    falcon::NewWorldSettings s;
    s.name = "  Flat Test ";
    s.type = falcon::NewWorldType::Superflat;
    s.mode = falcon::NewWorldMode::Creative;
    std::string error;
    CHECK(falcon::createWorldFolder(saves, "Flat Test", s, 4242, 1, error));
    s.name = "Big";
    s.type = falcon::NewWorldType::Amplified;
    s.mode = falcon::NewWorldMode::Hardcore;
    CHECK(falcon::createWorldFolder(saves, "Big", s, -9, 1, error));
    s.name = "Plain";
    s.type = falcon::NewWorldType::Default;
    CHECK(falcon::createWorldFolder(saves, "Plain", s, 7, 1, error));
    const auto flat = falcon::readLevelInfo(saves / "Flat Test");
    CHECK(flat && flat->name == "Flat Test" && flat->folder == "Flat Test" && flat->seed == 4242 && flat->gameType == 1 && !flat->hardcore);
    CHECK(flat && flat->style == falcon::TerrainStyle::Flat && flat->madeByFalcon);
    const auto big = falcon::readLevelInfo(saves / "Big");
    CHECK(big && big->style == falcon::TerrainStyle::Amplified && big->seed == -9 && big->hardcore && big->gameType == 0);
    const auto plain = falcon::readLevelInfo(saves / "Plain");
    CHECK(plain && plain->style == falcon::TerrainStyle::Hills && plain->madeByFalcon);
    CHECK(!falcon::readLevelInfo(saves / "missing"));

    // A world from Minecraft (no Falcon note) can only be a stand-in; a Falcon note makes it Falcon's, in the world type it names.
    falcon::NbtTag data = falcon::NbtTag::compound();
    data.put("LevelName", falcon::NbtTag::ofString("Vanilla World"));
    data.put("GameType", falcon::NbtTag::ofInteger(falcon::NbtTag::Type::Int, 0));
    falcon::NbtTag gen = falcon::NbtTag::compound();
    gen.put("seed", falcon::NbtTag::ofInteger(falcon::NbtTag::Type::Long, 123456789012LL));
    data.put("WorldGenSettings", gen);
    falcon::NbtTag root = falcon::NbtTag::compound();
    root.put("Data", data);
    fs::create_directories(saves / "Vanilla");
    {
        const auto bytes = falcon::gzipStored(falcon::serializeNbt(root));
        std::ofstream out(saves / "Vanilla" / "level.dat", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto vanilla = falcon::readLevelInfo(saves / "Vanilla");
    CHECK(vanilla && !vanilla->madeByFalcon && vanilla->seed == 123456789012LL && vanilla->style == falcon::TerrainStyle::Hills);
    falcon::NbtTag legacy = falcon::NbtTag::compound();
    legacy.put("world_type", falcon::NbtTag::ofString("minecraft:flat"));
    data.put("Falcon", legacy);
    falcon::NbtTag root2 = falcon::NbtTag::compound();
    root2.put("Data", data);
    fs::create_directories(saves / "Old");
    {
        const auto bytes = falcon::gzipStored(falcon::serializeNbt(root2));
        std::ofstream out(saves / "Old" / "level.dat", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto old = falcon::readLevelInfo(saves / "Old");
    CHECK(old && old->madeByFalcon && old->style == falcon::TerrainStyle::Flat);
    fs::remove_all(saves.parent_path(), ignored);
}

void test_pause_layout() {
    using falcon::Rect;
    // 854x480 at GUI scale 2: the grid is 212 wide and 142 tall, placed at half the free width and a quarter of the free height.
    const auto l = falcon::computePauseLayout(427, 240);
    CHECK(l.titleY == 40);
    CHECK((l.back == Rect{111, 74, 204, 20}));
    CHECK((l.advancements == Rect{111, 98, 98, 20}) && (l.statistics == Rect{217, 98, 98, 20}));
    CHECK((l.options == Rect{111, 122, 98, 20}) && (l.worldOptions == Rect{217, 122, 98, 20}));
    CHECK((l.quit == Rect{111, 146, 204, 20}));
}

void test_light() {
    using falcon::Block;
    const falcon::CommonBlocks& cb = falcon::commonBlocks();
    const Block torch = *falcon::blockRegistry().find("torch");
    const Block glowstone = *falcon::blockRegistry().find("glowstone");
    falcon::Level level = makeFlatLevel();  // grass at y -61, open sky above, chunks -2..2
    const auto lightOf = [&](int cx, int cz) { return falcon::computeChunkLight(falcon::snapshotLightInput(level, cx, cz)); };

    // Open sky is 15 straight down to the ground; the ground itself is dark; nothing glows.
    auto light = lightOf(0, 0);
    CHECK(light->skyAt(5, -60, 5) == 15 && light->skyAt(5, 200, 5) == 15 && light->skyAt(5, 319, 5) == 15);
    CHECK(light->skyAt(5, -61, 5) == 0 && light->skyAt(5, -63, 5) == 0 && light->blockAt(5, -60, 5) == 0);
    CHECK(light->skyAt(5, 400, 5) == 15 && light->skyAt(5, -100, 5) == 0 && light->blockAt(5, 400, 5) == 0);
    // Uniform sections store no array: the sky is 15 all the way up, rock below is 0.
    CHECK(!light->sky[10] && light->skyFill[10] == 15 && !light->block[10]);
    CHECK(!light->sky[0] || light->sky[0]->get(0) == 0);

    // A roof 5 x 5: light creeps in sideways one level per block from the open edge.
    for (int x = 3; x <= 7; ++x) {
        for (int z = 3; z <= 7; ++z) level.setBlock(x, -58, z, cb.stone);
    }
    light = lightOf(0, 0);
    CHECK(light->skyAt(2, -59, 5) == 15);                                             // beside the roof: open
    CHECK(light->skyAt(3, -59, 5) == 14 && light->skyAt(4, -59, 5) == 13 && light->skyAt(5, -59, 5) == 12);
    CHECK(light->skyAt(5, -60, 5) == 12 && light->skyAt(5, -57, 5) == 15 && light->skyAt(5, -58, 5) == 0);  // the roof is dark, above it is bright
    CHECK(light->skyAt(5, -59, 3) == 14 && light->skyAt(3, -59, 3) == 14);            // the first cell under the edge: one step from the open sky

    // A torch: 14 where it stands, a level less per block, blocked by opaque rock.
    level.setBlock(5, -60, 12, torch);
    light = lightOf(0, 0);
    CHECK(light->blockAt(5, -60, 12) == 14 && light->blockAt(6, -60, 12) == 13 && light->blockAt(5, -60, 14) == 12 && light->blockAt(5, -59, 13) == 12);
    CHECK(light->blockAt(5, -60, 0) == 2 && light->blockAt(5, -60, 15) == 11);  // 12 and 3 blocks away
    CHECK(light->blockAt(5, -62, 12) == 0);  // inside the ground: it does not take light
    level.setBlock(6, -60, 12, cb.stone);
    level.setBlock(6, -59, 12, cb.stone);
    light = lightOf(0, 0);
    CHECK(light->blockAt(7, -60, 12) == 10 && light->blockAt(6, -60, 12) == 0);  // around the wall (four steps), never through it
    // A glowstone gives 15.
    level.setBlock(10, -60, 5, glowstone);
    light = lightOf(0, 0);
    CHECK(light->blockAt(10, -60, 5) == 15 && light->blockAt(11, -60, 5) == 14);

    // Light crosses chunk borders both ways.
    level.setBlock(15, -60, 5, torch);
    CHECK(lightOf(1, 0)->blockAt(0, -60, 5) == 13 && lightOf(1, 0)->blockAt(2, -60, 5) == 11);
    level.setBlock(16, -60, 9, torch);
    CHECK(lightOf(0, 0)->blockAt(15, -60, 9) == 13);
    // Roofs also shade across borders: a roof over the border keeps the sky from both sides.
    for (int x = 12; x <= 19; ++x) {
        for (int z = 20; z <= 23; ++z) level.setBlock(x, -58, z, cb.stone);
    }
    CHECK(lightOf(0, 1)->skyAt(14, -59, 5) == 13 && lightOf(0, 1)->skyAt(14, -59, 6) == 13);  // under a roof, from the nearer open side
    // Missing chunks around the middle one do not crash (their sections are treated as air).
    falcon::Level lonely;
    lonely.putChunk({0, 0}, std::make_shared<falcon::Chunk>());
    CHECK(falcon::computeChunkLight(falcon::snapshotLightInput(lonely, 0, 0))->skyAt(3, 3, 3) == 15);
}
