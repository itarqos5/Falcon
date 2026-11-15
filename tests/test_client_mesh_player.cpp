// Client tests: mesh, player.
#include "test_support.hpp"

void test_mesher() {
    using falcon::Block;
    using falcon::kMeshScale;
    const falcon::CommonBlocks& cb = falcon::commonBlocks();
    FakeModels models;
    const auto around = [](falcon::SectionPtr middle) {
        falcon::SectionNeighborhood n;
        n.sections[static_cast<std::size_t>(falcon::SectionNeighborhood::slot(0, 0, 0))] = std::move(middle);
        return n;
    };
    const auto mesh = [&](const falcon::SectionNeighborhood& n) { return falcon::meshSection(n, models, 0, 0, 0); };
    // Nothing to draw in air, and a lone block has six quads (24 vertices), none merged.
    CHECK(mesh(around(nullptr)).empty());

    // How the faces of a section connect (what the renderer's occlusion culling walks through). Faces: west 0, east 1, down 2, up 3, north 4, south 5;
    // bit (a * 6 + b) says that face a can see face b through the section.
    {
        const auto bit = [](int a, int b) { return 1ull << (a * 6 + b); };
        const auto connected = [&](const falcon::SectionMesh& m, int a, int b) { return (m.visibility & bit(a, b)) != 0; };
        enum { West, East, Down, Up, North, South };
        CHECK(mesh(around(nullptr)).visibility == falcon::kVisibilityOpen);  // air: everything is connected
        falcon::SectionBuilder rock;
        rock.fill(cb.stone);
        CHECK(mesh(around(rock.build())).visibility == 0);  // solid: nothing is
        falcon::SectionBuilder lone;
        lone.set(5, 6, 7, cb.stone);
        CHECK(mesh(around(lone.build())).visibility == falcon::kVisibilityOpen);  // one block in the way does not close anything
        // A floor across the whole section: the faces above it and the faces below it are not connected, the sides above are connected to each other.
        falcon::SectionBuilder slab;
        slab.fillLayers(8, 9, cb.stone);
        const auto split = mesh(around(slab.build()));
        CHECK(!connected(split, Up, Down) && !connected(split, Down, Up));
        CHECK(connected(split, West, East) && connected(split, Up, North) && connected(split, Down, South));
        // A wall across it from top to bottom: west and east are cut off from each other, up and down are not.
        falcon::SectionBuilder wallAcross;
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) wallAcross.set(8, y, z, cb.stone);
        }
        const auto cut = mesh(around(wallAcross.build()));
        CHECK(!connected(cut, West, East) && !connected(cut, East, West));
        CHECK(connected(cut, Up, Down) && connected(cut, West, Up) && connected(cut, East, Down));
        // A tunnel through rock, west to east: only those two faces see each other (it never touches the others).
        falcon::SectionBuilder tunnel;
        tunnel.fill(cb.stone);
        for (int x = 0; x < 16; ++x) tunnel.set(x, 5, 5, Block::Air);
        const auto bore = mesh(around(tunnel.build()));
        CHECK(connected(bore, West, East) && connected(bore, East, West));
        CHECK(!connected(bore, West, Up) && !connected(bore, West, Down) && !connected(bore, West, North) && !connected(bore, East, South));
        // A cave with one opening to the north and one to the up side: those two see each other.
        falcon::SectionBuilder cave;
        cave.fill(cb.stone);
        for (int z = 0; z <= 8; ++z) cave.set(8, 8, z, Block::Air);  // from the north face to the middle
        for (int y = 8; y < 16; ++y) cave.set(8, y, 8, Block::Air);  // from the middle to the up face
        const auto bend = mesh(around(cave.build()));
        CHECK(connected(bend, North, Up) && connected(bend, Up, North) && !connected(bend, North, East) && !connected(bend, Up, Down));
    }
    falcon::SectionBuilder one;
    one.set(5, 6, 7, cb.stone);
    const auto single = mesh(around(one.build()));
    CHECK(single.opaque.size() == 24 && single.cutout.empty() && single.translucent.empty());
    // The block's quads touch only its own cube.
    for (const auto& v : single.opaque) CHECK(v.x >= 5 * kMeshScale && v.x <= 6 * kMeshScale && v.y >= 6 * kMeshScale && v.y <= 7 * kMeshScale && v.z >= 7 * kMeshScale && v.z <= 8 * kMeshScale);

    // A whole layer merges into one top quad that repeats the texture 16x16 times; the sides are 16x1 strips.
    falcon::SectionBuilder layer;
    layer.fillLayers(0, 1, cb.grassBlock);
    const auto flat = mesh(around(layer.build()));
    CHECK(falcon::quadCount(flat.opaque.size()) == 1 + 1 + 4);  // top, bottom, four edges (nothing is beyond them)
    int tops = 0, sides = 0, bottoms = 0;
    for (std::size_t q = 0; q < flat.opaque.size(); q += 4) {
        const auto& v = flat.opaque[q];
        if (v.layer == FakeModels::kGrassTop) {
            ++tops;
            for (std::size_t i = q; i < q + 4; ++i) {
                CHECK(flat.opaque[i].y == kMeshScale && (flat.opaque[i].u == 0 || flat.opaque[i].u == 16 * kMeshScale) && (flat.opaque[i].v == 0 || flat.opaque[i].v == 16 * kMeshScale));
            }
        } else if (v.layer == FakeModels::kGrassSide) {
            ++sides;
            for (std::size_t i = q; i < q + 4; ++i) CHECK(flat.opaque[i].v == 15 * kMeshScale || flat.opaque[i].v == 16 * kMeshScale);  // one block tall, from the top down
        } else if (v.layer == FakeModels::kGrassBottom) {
            ++bottoms;
        }
        CHECK(v.shade > 0 && v.sky == 255 && v.r == 255 && v.g == 255 && v.b == 255);
    }
    CHECK(tops == 1 && sides == 4 && bottoms == 1);

    // Faces between two solid blocks are not made, and a neighbouring section hides its border faces too.
    falcon::SectionBuilder pair;
    pair.set(0, 0, 0, cb.stone);
    pair.set(1, 0, 0, cb.stone);
    CHECK(falcon::quadCount(mesh(around(pair.build())).opaque.size()) == 6);  // a 2x1x1 box: still six sides (merged), not ten
    falcon::SectionBuilder edge;
    edge.set(0, 3, 3, cb.stone);
    falcon::SectionNeighborhood withWall = around(edge.build());
    CHECK(falcon::quadCount(mesh(withWall).opaque.size()) == 6);
    falcon::SectionBuilder wall;
    wall.fill(cb.stone);
    withWall.sections[static_cast<std::size_t>(falcon::SectionNeighborhood::slot(-1, 0, 0))] = wall.build();  // solid next door on -X
    CHECK(falcon::quadCount(mesh(withWall).opaque.size()) == 5);                                              // the -X face is hidden

    // Different pictures do not merge.
    falcon::SectionBuilder mixed;
    mixed.set(0, 0, 0, cb.stone);
    mixed.set(1, 0, 0, cb.dirt);
    CHECK(falcon::quadCount(mesh(around(mixed.build())).opaque.size()) == 10);  // five visible faces each, none merged

    // Water goes to the translucent list, one top face per block, and only where it meets air.
    falcon::SectionBuilder lake;
    lake.fillLayers(0, 4, cb.stone);
    for (int x = 4; x < 12; ++x) {
        for (int z = 4; z < 12; ++z) lake.set(x, 3, z, cb.water);
    }
    const auto pond = mesh(around(lake.build()));
    CHECK(falcon::quadCount(pond.translucent.size()) == 128);  // the surface, front and reversed (seen from under the water); the sides touch stone, the bottom too
    int fluidTops = 0;
    for (std::size_t q = 0; q < pond.translucent.size(); q += 4) {
        CHECK(pond.translucent[q].layer == FakeModels::kWaterStill);
        fluidTops += pond.translucent[q].y > 3 * kMeshScale;
    }
    CHECK(fluidTops == 128);
    // An interior corner of the sea is 8/9 of a block high (a source block).
    bool foundInterior = false;
    for (const auto& v : pond.translucent) {
        if (v.x == 8 * kMeshScale && v.z == 8 * kMeshScale) {
            foundInterior = true;
            CHECK(std::abs(v.y - (3.0 + 8.0 / 9.0) * kMeshScale) < 3);
        }
    }
    CHECK(foundInterior);
    // Water next to water shows no side; water against air does (down to the floor of the block).
    falcon::SectionBuilder bath;
    bath.fillLayers(0, 1, cb.stone);
    bath.set(5, 1, 5, cb.water);
    const auto drop = mesh(around(bath.build()));
    CHECK(falcon::quadCount(drop.translucent.size()) == 2 + 4);  // top (front and reversed) and four sides, stone below

    // Every cube quad is wound counter-clockwise seen from outside (so back-face culling keeps the right side).
    falcon::SectionBuilder cube;
    cube.set(8, 8, 8, cb.stone);
    const auto cm = mesh(around(cube.build()));
    bool windingOk = !cm.opaque.empty();
    const double centre = 8.5 * kMeshScale;
    for (std::size_t q = 0; q < cm.opaque.size(); q += 4) {
        const auto& p0 = cm.opaque[q];
        const auto& p1 = cm.opaque[q + 1];
        const auto& p2 = cm.opaque[q + 2];
        const double e1[3] = {double(p1.x) - p0.x, double(p1.y) - p0.y, double(p1.z) - p0.z};
        const double e2[3] = {double(p2.x) - p0.x, double(p2.y) - p0.y, double(p2.z) - p0.z};
        const double c[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const double out[3] = {p0.x - centre, p0.y - centre, p0.z - centre};  // from the block's middle to the corner
        windingOk = windingOk && (c[0] * out[0] + c[1] * out[1] + c[2] * out[2]) > 0.0;
    }
    CHECK(windingOk);

    // snapshotNeighborhood: bedrock under the world, the level's own sections elsewhere.
    falcon::Level level;
    level.putChunk({0, 0}, std::make_shared<falcon::Chunk>(*falcon::TerrainGenerator(7, falcon::TerrainStyle::Flat).generate(0, 0)));
    const auto snap = falcon::snapshotNeighborhood(level, 0, 0, 0);
    CHECK(snap.sections[static_cast<std::size_t>(falcon::SectionNeighborhood::slot(0, -1, 0))]->uniform == cb.bedrock);
    CHECK(snap.sections[static_cast<std::size_t>(falcon::SectionNeighborhood::slot(0, 0, 0))] != nullptr);
    CHECK(snap.sections[static_cast<std::size_t>(falcon::SectionNeighborhood::slot(1, 0, 1))] == nullptr);  // chunk not loaded
}

// A small Superflat level around the origin: grass at y -61, so a player standing on it has feet at y -60.
falcon::Level makeFlatLevel() {
    falcon::Level level;
    const falcon::TerrainGenerator flat(1, falcon::TerrainStyle::Flat);
    for (int cz = -2; cz <= 2; ++cz) {
        for (int cx = -2; cx <= 2; ++cx) level.putChunk({cx, cz}, flat.generate(cx, cz));
    }
    return level;
}

// ---- player physics --------------------------------------------------------------------------------------------
// The player moves in 20 ticks a second with the game's own float / double formulas. Every expected number below is
// tagged: [DERIVED] = the research notes' section 12 (computed by a float32 replica of the bytecode formulas; no game
// code was ever run, so these are "expected from the code", not measurements); [closed form] = worked out here from the
// same formulas (a fixed point or a sum), with the game's constants.

// A level around the origin with a stone floor whose top face is at y = -60 (feet y -60 stands on it).
falcon::Level makeFloorLevel(int radius) {
    falcon::Level level;
    falcon::SectionBuilder builder;
    builder.fillLayers(0, 4, falcon::commonBlocks().stone);  // y -64 .. -61
    const falcon::SectionPtr floor = builder.build();
    for (int cz = -radius; cz <= radius; ++cz) {
        for (int cx = -radius; cx <= radius; ++cx) {
            auto chunk = std::make_shared<falcon::Chunk>();
            chunk->sections[0] = floor;
            level.putChunk({cx, cz}, chunk);
        }
    }
    return level;
}

falcon::PlayerInput keysForward() {
    falcon::PlayerInput in;
    in.forward = true;
    return in;
}

void test_player_input_and_walking() {
    using falcon::PlayerInput;
    falcon::Level level = makeFloorLevel();
    PlayerInput fwd = keysForward();

    // Standing: it settles on the floor and stays put, exactly.
    {
        PhysicsRun r(level, 0.5, -60.0, 0.5);
        r.settle();
        CHECK(r.p.onGround && r.p.y == -60.0 && r.p.pose() == falcon::PlayerPose::Standing);
        const double y = r.p.y;
        r.ticks(40);
        CHECK(r.p.onGround && r.p.y == y && r.p.vx == 0.0 && r.p.vz == 0.0);
        CHECK(r.p.xo == r.p.x && r.p.zo == r.p.z);
    }

    // Walking forward (W at yaw 0 is +Z): the first tick moves a = 0.1f * 0.98f, then the displacement approaches
    // a / (1 - 0.6f*0.91f). [closed form] tick k moves a * (1 - drag^k) / (1 - drag); [DERIVED] ticks 1, 2, 5 and the steady
    // 0.215859 blocks a tick = 4.3172 blocks a second.
    {
        PhysicsRun r(level);
        r.settle();
        const double a = kInputF * kWalkSpeedF;
        const double first = r.lastStep(fwd);
        const double second = r.lastStep(fwd);
        CHECK(closeTo(first, 0.098, 1e-6) && closeTo(first, a, 1e-9));
        CHECK(closeTo(second, 0.151508, 1e-6) && closeTo(second, a * (1.0 + kGroundDragF), 1e-9));
        r.ticks(2, fwd);
        CHECK(closeTo(r.lastStep(fwd), 0.205385, 1e-6));  // tick 5
        r.ticks(60, fwd);
        const double m = r.lastStep(fwd);
        CHECK(closeTo(m, steady(a, kGroundDragF), 1e-9) && closeTo(m, 0.215859, 2e-6) && closeTo(m * 20.0, 4.3172, 5e-5));
        CHECK(r.p.onGround && r.p.y == -60.0 && !r.p.sprinting && closeTo(r.p.x, 0.5, 1e-12));
    }

    // Sprinting (the sprint key): speed 0.1 * 1.3, steady 0.280617 blocks a tick = 5.6123 blocks a second [DERIVED].
    {
        PhysicsRun r(level);
        r.settle();
        PlayerInput run = fwd;
        run.sprint = true;
        const double first = r.lastStep(run);
        CHECK(r.p.sprinting && closeTo(first, kInputF * kSprintSpeedF, 1e-9) && closeTo(first, 0.1274, 2e-6));
        r.ticks(60, run);
        const double m = r.lastStep(run);
        CHECK(closeTo(m, steady(kInputF * kSprintSpeedF, kGroundDragF), 1e-9) && closeTo(m, 0.280617, 2e-6) && closeTo(m * 20.0, 5.6123, 5e-5));
    }

    // Sneaking: the pose is crouching, the push is 0.98 * 0.3, steady 0.064758 = 1.2952 blocks a second [DERIVED]. The
    // slowdown follows the shift key one tick late (the first tick still walks at full push).
    {
        PhysicsRun r(level);
        r.settle();
        PlayerInput sneak = fwd;
        sneak.shift = true;
        const double first = r.lastStep(sneak);
        const double second = r.lastStep(sneak);
        CHECK(closeTo(first, 0.098, 1e-6));
        CHECK(second < first && r.p.sneaking);
        r.ticks(60, sneak);
        const double m = r.lastStep(sneak);
        CHECK(closeTo(m, 0.064758, 2e-6) && closeTo(m * 20.0, 1.2952, 5e-5));
        CHECK(r.p.pose() == falcon::PlayerPose::Crouching && closeTo(static_cast<double>(r.p.eyeHeight()), 1.27, 1e-6));
        CHECK(closeTo(r.p.boundingBox().maxY - r.p.boundingBox().minY, 1.5, 1e-6));
    }

    // Diagonal walking: the unit circle is stretched to the unit square, so W+A is exactly one push (1.0), not 0.98
    // [DERIVED] steady 0.220264 = 4.4053 b/s; sprinting 0.286344 (5.7269 b/s); sneaking 0.091581 (1.8316 b/s).
    {
        PlayerInput diag;
        diag.forward = diag.left = true;
        PhysicsRun r(level);
        r.settle();
        r.ticks(60, diag);
        CHECK(closeTo(r.lastStep(diag), 0.220264, 2e-6));
        CHECK(r.p.x > 0.5 && r.p.z > 0.5);  // forward is +Z, left is +X at yaw 0
        PlayerInput diagRun = diag;
        diagRun.sprint = true;
        PhysicsRun s(level);
        s.settle();
        s.ticks(60, diagRun);
        CHECK(closeTo(s.lastStep(diagRun), 0.286344, 2e-6));
        PlayerInput diagSneak = diag;
        diagSneak.shift = true;
        PhysicsRun k(level);
        k.settle();
        k.ticks(60, diagSneak);
        CHECK(closeTo(k.lastStep(diagSneak), 0.091581, 2e-6));
        // Straight is 0.98, diagonal 1.0: about 2% faster.
        PhysicsRun st(level);
        st.settle();
        st.ticks(60, fwd);
        CHECK(closeTo(r.lastStep(diag) / st.lastStep(fwd), 1.0 / 0.98, 1e-4));
        // Backwards and sideways walk at the same speed as forwards.
        PlayerInput back;
        back.backward = true;
        PhysicsRun b(level);
        b.settle();
        b.ticks(60, back);
        CHECK(closeTo(b.lastStep(back), 0.215859, 2e-6) && b.p.z < 0.5);
        // W and S together cancel.
        PlayerInput both;
        both.forward = both.backward = true;
        PhysicsRun c(level);
        c.settle();
        c.ticks(10, both);
        CHECK(c.p.x == 0.5 && c.p.z == 0.5);
    }

    // Using an item (default): the push is 0.98 * 0.2; steady 0.043172 [DERIVED]; sprint cannot start.
    {
        PhysicsRun r(level);
        r.settle();
        r.p.usingItem = true;
        PlayerInput run = fwd;
        run.sprint = true;
        r.ticks(60, run);
        CHECK(!r.p.sprinting && closeTo(r.lastStep(run), 0.043172, 2e-6));
    }

    // Starting a sprint: the sprint key, or a second tap of forward within 7 ticks of the first. Stopping: letting go of
    // forward (the tick after), or hitting a wall head-on. Not with too little food.
    {
        PhysicsRun r(level);
        r.settle();
        r.tick(fwd);
        CHECK(!r.p.sprinting);
        r.tick();          // release
        r.tick(fwd);       // second tap inside the window
        CHECK(r.p.sprinting);
        r.tick();
        CHECK(!r.p.sprinting);  // no forward impulse: the sprint stops
        PhysicsRun slow(level);
        slow.settle();
        slow.tick(fwd);
        slow.ticks(8);     // the window is over
        slow.tick(fwd);
        CHECK(!slow.p.sprinting);
        PhysicsRun hungry(level);
        hungry.settle();
        hungry.p.foodLevel = 6;
        PlayerInput run = fwd;
        run.sprint = true;
        hungry.ticks(5, run);
        CHECK(!hungry.p.sprinting);
        hungry.p.foodLevel = 7;
        hungry.tick(run);
        CHECK(hungry.p.sprinting);
        // Sneaking forbids starting a sprint, and a sprint ends against a wall.
        PhysicsRun wall(level, 0.5, -60.0, 0.5);
        level.setBlock(0, -60, 3, falcon::commonBlocks().stone);
        level.setBlock(0, -59, 3, falcon::commonBlocks().stone);
        wall.settle();
        wall.ticks(30, run);
        CHECK(!wall.p.sprinting && closeTo(wall.p.z, 3.0 - 0.3, 1e-4));  // flush against the block, 0.3 in front of it
        level.setBlock(0, -60, 3, falcon::Block::Air);
        level.setBlock(0, -59, 3, falcon::Block::Air);
    }

    // A wall stops the walk flush: the body ends 0.3 from the block face; walking diagonally into it slides along it.
    {
        for (int z = -8; z <= 8; ++z) {
            for (int y = -60; y <= -58; ++y) level.setBlock(4, y, z, falcon::commonBlocks().stone);
        }
        PhysicsRun r(level, 0.5, -60.0, 0.5, -90.0f);  // yaw -90 looks +X
        r.settle();
        r.ticks(80, fwd);
        CHECK(closeTo(r.p.x, 4.0 - static_cast<double>(0.3f), 1e-9) && r.p.vx == 0.0 && r.p.horizontalCollision);
        PlayerInput slide;
        slide.forward = slide.left = true;  // +X and -Z at yaw -90
        r.p.z = 0.5;
        r.ticks(40, slide);
        CHECK(closeTo(r.p.x, 4.0 - static_cast<double>(0.3f), 1e-9) && r.p.z < -3.0);
        for (int z = -8; z <= 8; ++z) {
            for (int y = -60; y <= -58; ++y) level.setBlock(4, y, z, falcon::Block::Air);
        }
    }
}

void test_player_jump_and_fall() {
    using falcon::PlayerInput;
    falcon::Level level = makeFloorLevel();

    // A standing jump with the key held: the y moves of the 12 ticks and the heights reached [DERIVED]; apex 1.252203 on
    // tick 5, the 12th tick lands (0.6 s in the air).
    {
        PhysicsRun r(level);
        r.settle();
        PlayerInput jump;
        jump.jump = true;
        static constexpr double kMoves[11] = {0.420000, 0.333200, 0.248136, 0.164773, 0.083078, 0.003016, -0.075444, -0.152335, -0.227688, -0.301535, -0.373904};
        double height = 0.0;
        double top = 0.0;
        int topTick = -1;
        bool exact = true;
        for (int t = 0; t < 11; ++t) {
            const double before = r.p.y;
            r.tick(jump);
            exact = exact && closeTo(r.p.y - before, kMoves[t], 1.5e-6) && !r.p.onGround;
            height = r.p.y + 60.0;
            if (height > top) {
                top = height;
                topTick = t;
            }
        }
        CHECK(exact);
        CHECK(topTick == 5 && closeTo(top, 1.252203, 2e-6));
        CHECK(!r.p.onGround);
        r.tick(jump);  // the 12th tick: the move is cut off by the floor
        CHECK(r.p.onGround && r.p.y == -60.0 && closeTo(r.p.vy, -0.0784, 1e-6));
        // The jump delay (10 ticks) only matters for hops shorter than a jump: holding the key jumps again at once.
        r.tick(jump);
        CHECK(!r.p.onGround && closeTo(r.p.y + 60.0, 0.42, 1e-6));
    }

    // Letting go in mid-air does not change the arc, and a jump only works from the ground.
    {
        PhysicsRun r(level);
        r.settle();
        PlayerInput jump;
        jump.jump = true;
        r.tick(jump);
        r.ticks(3);
        const double y = r.p.y;
        r.tick(jump);  // pressing again in the air does nothing
        CHECK(r.p.y < y + 0.2);
    }

    // The sprint jump adds 0.2 blocks a tick along the facing direction on the jump tick.
    {
        PhysicsRun r(level);
        r.settle();
        PlayerInput run = keysForward();
        run.sprint = true;
        r.ticks(40, run);
        const double z0 = r.p.z;
        PlayerInput hop = run;
        hop.jump = true;
        r.tick(hop);
        // On the jump tick the velocity (steady speed after drag) gets +0.2 along the facing direction, then the usual push:
        // the tick moves the steady distance plus 0.2 [closed form]; the climb is the usual 0.42 (then 0.3332 after gravity).
        const double a = kInputF * kSprintSpeedF;
        CHECK(closeTo(r.p.z - z0, steady(a, kGroundDragF) + 0.2, 1e-6) && closeTo(r.p.x, 0.5, 1e-6));
        CHECK(closeTo(r.p.y + 60.0, 0.42, 1e-6) && closeTo(r.p.vy, 0.3332, 1e-5));
    }

    // Falling from rest: the first tick does not move (the velocity is 0 when it starts), then the drop after n moves is
    // 3.92 n - 196 (1 - 0.98^n) [closed form with 0.98 as a double]. From 10 blocks up the player lands on the 18th tick
    // at 22.79 blocks a second, and a 10-block fall does floor(10 + 1e-6 - 3) = 7 damage [DERIVED].
    {
        PhysicsRun r(level, 0.5, -50.0, 0.5);
        int ticks = 0;
        double impactVelocity = 0.0;
        while (!r.p.onGround && ticks < 200) {
            impactVelocity = r.p.vy;
            r.tick();
            ++ticks;
        }
        CHECK(ticks == 18 && r.p.y == -60.0);
        CHECK(closeTo(impactVelocity, -1.13946, 2e-5) && closeTo(impactVelocity * 20.0, -22.79, 1e-3));
        CHECK(r.p.lastFallDamage == 7 && r.p.takeFallDamage() == 7 && r.p.lastFallDamage == 0 && r.p.fallDistance == 0.0);
    }

    // The table of falls [DERIVED]: height -> ticks to land, impact speed, damage.
    {
        struct Fall {
            double height;
            int ticks;
            double speed;  // blocks per tick at the move that lands, 0: not checked
            int damage;
        };
        static constexpr Fall kFalls[] = {{3.0, 10, 0.0, 0}, {4.0, 11, 0.7171, 1}, {5.0, 13, 0.0, 2}, {20.0, 25, 1.5061, 17}, {100.0, 61, 2.7536, 0}};
        for (const Fall& f : kFalls) {
            PhysicsRun r(level, 0.5, -60.0 + f.height, 0.5);
            int ticks = 0;
            double v = 0.0;
            while (!r.p.onGround && ticks < 400) {
                v = r.p.vy;
                r.tick();
                ++ticks;
            }
            CHECK(ticks == f.ticks);
            if (f.speed != 0.0) CHECK(closeTo(-v, f.speed, 2e-4));
            if (f.height != 100.0) CHECK(r.p.takeFallDamage() == f.damage);
            if (f.height == 100.0) CHECK(r.p.takeFallDamage() == 97);  // floor(100 + 1e-6 - 3)
        }
    }

    // Terminal velocity: v' = (v - 0.08) * 0.98f has the fixed point -0.08 * d / (1 - d) = -3.92 blocks a tick (78.4 a second);
    // from rest it follows -3.92 (1 - d^k) (d = the float 0.98f) [closed form]. A fall of 379 blocks reaches 73.7 a second.
    {
        const double d = static_cast<double>(0.98f);
        const double fixedPoint = -0.08 * d / (1.0 - d);
        CHECK(closeTo(fixedPoint, -3.92, 1e-5) && closeTo(fixedPoint * 20.0, -78.4, 2e-4));
        PhysicsRun r(level, 0.5, 315.0, 0.5);
        for (int i = 0; i < 100; ++i) r.tick();
        CHECK(closeTo(r.p.vy, fixedPoint * (1.0 - std::pow(d, 100)), 1e-9) && !r.p.onGround);
        // Starting at the fixed point it stays there.
        PhysicsRun s(level, 0.5, 315.0, 0.5);
        s.p.vy = fixedPoint;
        s.ticks(20);
        CHECK(closeTo(s.p.vy, fixedPoint, 1e-12));
        // Falling over the edge of a ledge starts at the grounded steady velocity -0.0784, not at 0.
        PhysicsRun e(level, 0.5, -60.0, 0.5);
        e.settle();
        CHECK(closeTo(e.p.vy, -0.0784, 1e-6));
    }

    // Landing in water: the fall is forgiven, however far.
    {
        for (int y = -60; y <= -57; ++y) {
            for (int x = 8; x <= 12; ++x) {
                for (int z = 8; z <= 12; ++z) level.setBlock(x, y, z, falcon::commonBlocks().water);
            }
        }
        PhysicsRun r(level, 10.5, -30.0, 10.5);
        r.ticks(80);
        CHECK(r.p.takeFallDamage() == 0 && r.p.inWater && r.p.fallDistance == 0.0);
        for (int y = -60; y <= -57; ++y) {
            for (int x = 8; x <= 12; ++x) {
                for (int z = 8; z <= 12; ++z) level.setBlock(x, y, z, falcon::Block::Air);
            }
        }
    }

    // Creative players take no fall damage.
    {
        PhysicsRun r(level, 0.5, -30.0, 0.5);
        r.p.canFly = true;
        while (!r.p.onGround) r.tick();
        CHECK(r.p.takeFallDamage() == 0);
    }
}

void test_player_flying() {
    using falcon::PlayerInput;
    falcon::Level level = makeFloorLevel(16);

    // Two taps of jump within the window turn flight on (creative only): the second rising edge must come 2..6 ticks after
    // the first; after 7 ticks it only opens a new window.
    {
        PhysicsRun r(level, 0.5, -50.0, 0.5);
        PlayerInput jump;
        jump.jump = true;
        r.tick(jump);
        r.tick();
        r.tick(jump);
        CHECK(!r.p.flying);  // survival cannot
        r.p.canFly = true;
        r.ticks(10);
        r.tick(jump);
        r.tick();
        r.tick(jump);
        CHECK(r.p.flying);
        r.tick();
        r.tick(jump);
        r.tick();
        r.tick(jump);
        CHECK(!r.p.flying);  // the same double tap while flying switches flight off again
    }
    {
        PhysicsRun r(level, 0.5, -50.0, 0.5);
        r.p.canFly = true;
        PlayerInput jump;
        jump.jump = true;
        r.tick(jump);
        r.ticks(6);        // 7 ticks after the first tap
        r.tick(jump);
        CHECK(!r.p.flying);
        PhysicsRun q(level, 0.5, -50.0, 0.5);
        q.p.canFly = true;
        q.tick(jump);
        q.ticks(5);        // 6 ticks later: still inside
        q.tick(jump);
        CHECK(q.p.flying);
    }

    // Flying: horizontal push 0.05f * 0.98 with air drag 0.91: steady 0.544445 blocks a tick = 10.889 a second; sprinting
    // doubles the flying speed: 1.088889 = 21.778 a second; vertical +-0.15 a tick decaying by 0.6: 0.375 = 7.5 a second,
    // not doubled by sprinting [DERIVED].
    {
        PhysicsRun r(level, 0.5, -40.0, 0.5);
        r.p.canFly = true;
        r.p.flying = true;
        r.ticks(5);
        CHECK(r.p.flying && closeTo(r.p.y, -40.0, 1e-12));  // hovers: no gravity
        PlayerInput fwd = keysForward();
        r.ticks(200, fwd);
        const double m = r.lastStep(fwd);
        const double a = kInputF * static_cast<double>(0.05f);
        CHECK(closeTo(m, steady(a, static_cast<double>(1.0f * 0.91f)), 1e-8) && closeTo(m, 0.544445, 2e-6) && closeTo(m * 20.0, 10.889, 2e-4));
        PlayerInput run = fwd;
        run.sprint = true;
        PhysicsRun s(level, 0.5, -40.0, 0.5);
        s.p.canFly = true;
        s.p.flying = true;
        s.ticks(200, run);
        CHECK(s.p.sprinting && closeTo(s.lastStep(run), 1.088889, 2e-6) && closeTo(s.lastStep(run) * 20.0, 21.778, 1e-3));
        // Up with jump, down with shift.
        PhysicsRun v(level, 0.5, -40.0, 0.5);
        v.p.canFly = true;
        v.p.flying = true;
        PlayerInput up;
        up.jump = true;
        up.sprint = true;
        v.ticks(80, up);
        const double y0 = v.p.y;
        v.tick(up);
        CHECK(closeTo(v.p.y - y0, 0.375, 1e-6) && closeTo((v.p.y - y0) * 20.0, 7.5, 1e-4));
        PlayerInput down;
        down.shift = true;
        PhysicsRun w(level, 0.5, -30.0, 0.5);
        w.p.canFly = true;
        w.p.flying = true;
        w.ticks(40, down);
        const double y1 = w.p.y;
        w.tick(down);
        CHECK(closeTo(y1 - w.p.y, 0.375, 1e-6));
        // Touching the ground ends the flight.
        w.ticks(200, down);
        CHECK(w.p.onGround && !w.p.flying && closeTo(w.p.y, -60.0, 1e-12));
    }

    // Spectators fly through blocks: no collision at all.
    {
        PhysicsRun r(level, 0.5, -59.0, 0.5);
        r.p.spectator = true;
        PlayerInput down;
        down.shift = true;
        r.ticks(40, down);
        CHECK(r.p.flying && r.p.y < -61.0 && !r.p.onGround);
    }
}

// A level with a hole of a few blocks in the floor (a ledge at x = 8), for the sneaking tests.
void test_player_sneak_edge_and_poses() {
    using falcon::PlayerInput;
    falcon::Level level = makeFloorLevel();
    for (int x = 8; x < 20; ++x) {
        for (int y = -64; y <= -61; ++y) level.setBlock(x, y, 0, falcon::Block::Air);
    }
    PlayerInput fwd = keysForward();

    // Sneaking stops at an edge; walking does not. The sneaking body hangs over the edge by up to its half width.
    {
        PhysicsRun s(level, 6.5, -60.0, 0.5, -90.0f);
        PlayerInput sneak = fwd;
        sneak.shift = true;
        s.settle();
        s.ticks(120, sneak);
        CHECK(s.p.onGround && s.p.y == -60.0 && s.p.x > 8.3 - 0.06 && s.p.x <= 8.3 + 1e-6);
        PhysicsRun f(level, 6.5, -60.0, 0.5, -90.0f);
        f.settle();
        f.ticks(60, fwd);
        CHECK(f.p.y < -62.0 && f.p.x > 8.0);  // it walked off the edge and fell
    }

    // A sneaking player in mid-air is not held (the clamp only works on or just above the ground).
    {
        PhysicsRun s(level, 12.5, -40.0, 0.5, -90.0f);
        PlayerInput sneak = fwd;
        sneak.shift = true;
        s.ticks(10, sneak);
        CHECK(s.p.x > 12.5);
    }
    falcon::Level poseLevel = makeFloorLevel();

    // Eye heights and the camera: the pose changes at the end of the tick, the camera eases halfway each tick:
    // 1.27 + 0.35 * 0.5^k [DERIVED]; standing up eases back the same way.
    {
        PhysicsRun r(poseLevel);
        r.settle();
        PlayerInput sneak;
        sneak.shift = true;
        r.tick(sneak);
        CHECK(r.p.pose() == falcon::PlayerPose::Crouching && closeTo(static_cast<double>(r.p.eyeHeight()), 1.27, 1e-6) && closeTo(r.p.eyeY(), -60.0 + 1.27, 1e-6));
        CHECK(closeTo(r.p.cameraPosition(1.0)[1] + 60.0, 1.62, 1e-6));  // the camera has not started to move yet
        for (int k = 1; k <= 5; ++k) {
            r.tick(sneak);
            CHECK(closeTo(r.p.cameraPosition(1.0)[1] + 60.0, 1.27 + 0.35 * std::pow(0.5, k), 1e-5));
        }
        CHECK(closeTo(r.p.cameraPosition(0.0)[1] + 60.0, 1.27 + 0.35 * std::pow(0.5, 4), 1e-5));  // one tick earlier
        r.ticks(10, sneak);
        r.tick();
        CHECK(r.p.pose() == falcon::PlayerPose::Standing && closeTo(static_cast<double>(r.p.eyeHeight()), 1.62, 1e-6));
        CHECK(closeTo(r.p.cameraPosition(1.0)[1] + 60.0, 1.27, 1e-3));  // the camera follows the body one tick late
        r.tick();
        CHECK(closeTo(r.p.cameraPosition(1.0)[1] + 60.0, 1.62 - 0.35 * 0.5, 2e-3));
    }

    // Interpolation: the position between ticks is the lerp of the last two.
    {
        PhysicsRun r(poseLevel);
        r.settle();
        r.ticks(5, fwd);
        const double x0 = r.p.z;
        r.tick(fwd);
        const auto half = r.p.position(0.5);
        CHECK(closeTo(half[2], (x0 + r.p.z) / 2.0, 1e-12) && r.p.position(1.0)[2] == r.p.z && r.p.position(0.0)[2] == x0);
    }

    // Under a low ceiling: with the 1.5 block high gap a crouching player cannot stand up and stays crouched after
    // releasing shift (and keeps the slow speed); with a 1 block gap the pose is crawling (0.6 x 0.6, eye 0.4).
    {
        PhysicsRun r(poseLevel, 0.5, -60.0, 0.5);
        r.settle();
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) poseLevel.setBlock(dx, -59, dz, falcon::commonBlocks().stone);  // ceiling 1 block above the floor
        }
        r.ticks(3);
        CHECK(r.p.pose() == falcon::PlayerPose::Swimming && closeTo(static_cast<double>(r.p.eyeHeight()), 0.4, 1e-6));
        CHECK(closeTo(r.p.boundingBox().maxY - r.p.boundingBox().minY, 0.6, 1e-6) && r.p.isMovingSlowly());
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) poseLevel.setBlock(dx, -59, dz, falcon::Block::Air);
        }
        r.ticks(3);
        CHECK(r.p.pose() == falcon::PlayerPose::Standing && !r.p.isMovingSlowly());
    }
}
