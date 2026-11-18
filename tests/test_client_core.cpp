// Client tests: core.
#include "test_support.hpp"

void test_client_identity() {
    falcon::Client client{falcon::ClientOptions{}};
    CHECK(client.name() == "Falcon");
    CHECK(!client.version().empty());
}

void test_options() {
    const char* argv[] = {"Falcon", "--size", "1280x720", "--seed", "7", "--capture", "a.png"};
    const auto o = falcon::parseOptions(7, argv);
    CHECK(o.width == 1280 && o.height == 720);
    CHECK(o.seed && *o.seed == 7u);
    CHECK(o.capturePath && o.capturePath->string() == "a.png");

    const char* bad[] = {"Falcon", "--nope"};
    bool threw = false;
    try {
        (void)falcon::parseOptions(2, bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    // The loading-screen test hooks.
    const char* loading[] = {"Falcon", "--play", "w", "--hold-loading", "2.5", "--capture-loading"};
    const auto l = falcon::parseOptions(6, loading);
    CHECK(l.holdLoading && *l.holdLoading == 2.5);
    CHECK(l.captureLoading);
    const char* plain[] = {"Falcon", "--play", "w"};
    const auto p = falcon::parseOptions(3, plain);
    CHECK(!p.captureLoading && !p.holdLoading);  // off unless asked for
    CHECK(falcon::usageText().find("--hold-loading") != std::string::npos);
}

// Regression test for "the loading screen never goes away" at a small render distance. A chunk is ready once it and its 8
// neighbours are lit, and a chunk is lit once ITS 8 neighbours exist (Chebyshev distance 2 from the chunk). Every chunk the
// ready area needs must therefore be inside the disk the streaming code generates - for the loading area (radius min(R, 4)) and
// for the whole drawn disk (radius R, whose rim chunks must be meshable too).
void test_stream_radius_covers_ready_area() {
    const auto farthestNeed = [](int radius) {  // the largest squared distance of a chunk needed by a chunk of the disk
        int worst = 0;
        for (int px = -radius; px <= radius; ++px) {
            for (int pz = -radius; pz <= radius; ++pz) {
                if (px * px + pz * pz > radius * radius) continue;
                for (int sx = -2; sx <= 2; ++sx) {
                    for (int sz = -2; sz <= 2; ++sz) worst = std::max(worst, (px + sx) * (px + sx) + (pz + sz) * (pz + sz));
                }
            }
        }
        return worst;
    };
    bool oldMarginFailed = false;
    for (int r = 1; r <= 32; ++r) {
        const int stream = falcon::streamRadius(r);
        CHECK(farthestNeed(r) <= stream * stream);                // the drawn disk
        CHECK(farthestNeed(std::min(r, 4)) <= stream * stream);   // the loading area
        if (farthestNeed(std::min(r, 4)) > (r + 2) * (r + 2)) oldMarginFailed = true;
    }
    CHECK(oldMarginFailed);  // the old R + 2 margin really was too small for some render distance (that was the bug)
    CHECK(farthestNeed(2) > 4 * 4);  // render distance 2 (the user's): the chunk (4, 2) is needed but R + 2 = 4 does not reach it
}

// The game's mip level generator (MipmapGenerator), ported: averaging in linear light, the dark and plain cutout strategies, and the
// alpha that keeps the share of a picture passing the alpha test the same at every level.
void test_mipmaps() {
    using falcon::Image;
    using falcon::MipmapStrategy;
    const auto makeImage = [](int size, auto&& pixel) {  // pixel(x, y) -> {r, g, b, a}
        Image image;
        image.width = image.height = size;
        image.rgba.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const std::array<int, 4> p = pixel(x, y);
                for (int c = 0; c < 4; ++c) image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)] = static_cast<unsigned char>(p[static_cast<std::size_t>(c)]);
            }
        }
        return image;
    };
    CHECK(falcon::parseMipmapStrategy("dark_cutout") == MipmapStrategy::DarkCutout && falcon::parseMipmapStrategy("strict_cutout") == MipmapStrategy::StrictCutout);
    CHECK(falcon::parseMipmapStrategy("mean") == MipmapStrategy::Mean && falcon::parseMipmapStrategy("cutout") == MipmapStrategy::Cutout);
    CHECK(falcon::parseMipmapStrategy("") == MipmapStrategy::Auto && falcon::parseMipmapStrategy("nonsense") == MipmapStrategy::Auto);

    // Mean averages in LINEAR light: black and white texels make the sRGB value of half the light (188), not 128.
    {
        const Image img = makeImage(2, [](int x, int y) { return ((x + y) & 1) == 0 ? std::array<int, 4>{0, 0, 0, 255} : std::array<int, 4>{255, 255, 255, 255}; });
        const auto levels = falcon::generateMipLevels(img, 1, MipmapStrategy::Mean);
        CHECK(levels.size() == 2 && levels[1].width == 1 && levels[1].height == 1);
        CHECK(std::abs(levels[1].rgba[0] - 188) <= 1 && std::abs(levels[1].rgba[1] - 188) <= 1 && levels[1].rgba[3] == 255);
        CHECK(levels[0].rgba == img.rgba);  // an opaque picture is not touched
    }
    // A picture that says nothing and has no transparent texel is Mean too (Auto).
    {
        const Image img = makeImage(4, [](int x, int) { return x < 2 ? std::array<int, 4>{200, 0, 0, 255} : std::array<int, 4>{0, 0, 200, 255}; });
        CHECK(falcon::generateMipLevels(img, 2, MipmapStrategy::Auto)[1].rgba == falcon::generateMipLevels(img, 2, MipmapStrategy::Mean)[1].rgba);
    }
    // Dark cutout: the holes start out as three quarters of the darkest opaque texel (alpha 0), and in the average a transparent texel
    // is black: one white texel in four gives a quarter of the light, a darker grey than the plain average of the colours.
    {
        const Image img = makeImage(2, [](int x, int y) { return x == 0 && y == 0 ? std::array<int, 4>{255, 255, 255, 255} : std::array<int, 4>{10, 20, 30, 0}; });
        const auto levels = falcon::generateMipLevels(img, 1, MipmapStrategy::DarkCutout);
        CHECK(levels[0].rgba[0] == 255 && levels[0].rgba[3] == 255);                                                                  // the opaque texel
        CHECK(levels[0].rgba[4] == 191 && levels[0].rgba[5] == 191 && levels[0].rgba[6] == 191 && levels[0].rgba[7] == 0);            // a hole: 3/4 of it, alpha 0
        CHECK(std::abs(levels[1].rgba[0] - 137) <= 1 && std::abs(levels[1].rgba[1] - 137) <= 1);                                       // linear 0.25 -> sRGB 137
    }
    // Cutout (and Auto, for a picture with transparent texels): holes take the colour of the nearest opaque texel, alpha stays 0.
    {
        const Image img = makeImage(4, [](int x, int y) { return x == 0 && y == 0 ? std::array<int, 4>{200, 50, 10, 255} : std::array<int, 4>{0, 0, 0, 0}; });
        const auto levels = falcon::generateMipLevels(img, 1, MipmapStrategy::Auto);
        CHECK(levels[0].rgba[0] == 200 && levels[0].rgba[1] == 50 && levels[0].rgba[2] == 10 && levels[0].rgba[3] == 255);
        const std::size_t far = (3 * 4 + 3) * 4;  // the far corner takes the only colour there is
        CHECK(levels[0].rgba[far] == 200 && levels[0].rgba[far + 1] == 50 && levels[0].rgba[far + 2] == 10 && levels[0].rgba[far + 3] == 0);
    }
    // The alpha of every cutout level is scaled to keep the share of the picture that passes the alpha test (the game does it in five
    // bisection steps, so it is a good match for a typical texture but not exact): a solid picture stays solid and an empty one stays
    // below the test, whatever the strategy; a leaf-like picture makes the full chain.
    {
        const Image solid = makeImage(8, [](int, int) { return std::array<int, 4>{30, 90, 20, 255}; });
        const Image empty = makeImage(8, [](int, int) { return std::array<int, 4>{0, 0, 0, 0}; });
        for (const MipmapStrategy s : {MipmapStrategy::Cutout, MipmapStrategy::StrictCutout, MipmapStrategy::DarkCutout}) {
            for (const Image& level : falcon::generateMipLevels(solid, 3, s)) {
                for (std::size_t i = 3; i < level.rgba.size(); i += 4) CHECK(level.rgba[i] == 255);
            }
            for (const Image& level : falcon::generateMipLevels(empty, 3, s)) {
                for (std::size_t i = 3; i < level.rgba.size(); i += 4) CHECK(level.rgba[i] < 128);
            }
        }
        const Image leaf = makeImage(16, [](int x, int y) { return ((x * 7 + y * 3) % 5) < 2 ? std::array<int, 4>{30, 90, 20, 255} : std::array<int, 4>{0, 0, 0, 0}; });
        const auto levels = falcon::generateMipLevels(leaf, 4, MipmapStrategy::DarkCutout);
        CHECK(levels.size() == 5 && levels[1].width == 8 && levels[2].width == 4 && levels[3].width == 2 && levels[4].width == 1);
        const float base = falcon::alphaTestCoverage(leaf, 0.5f, 1.0f);
        CHECK(base > 0.1f && base < 0.7f);
    }
}

// A level with a stone floor whose top is at y = -60, 7 x 7 chunks (the same floor the physics tests use).
falcon::Level makeFluidFloor() {
    falcon::Level level;
    falcon::SectionBuilder builder;
    builder.fillLayers(0, 4, falcon::commonBlocks().stone);  // y -64 .. -61
    const falcon::SectionPtr floor = builder.build();
    for (int cz = -3; cz <= 3; ++cz) {
        for (int cx = -3; cx <= 3; ++cx) {
            auto chunk = std::make_shared<falcon::Chunk>();
            chunk->sections[0] = floor;
            level.putChunk({cx, cz}, chunk);
        }
    }
    return level;
}

// Water that flows (FluidSimulator): the rules of FlowingFluid, checked tick by tick on a stone floor whose top is at y = -60.
void test_fluids() {
    using falcon::FluidSimulator;
    const falcon::Block source = FluidSimulator::waterBlock({true, true, false, 8});
    const auto flowing = [](int amount) { return FluidSimulator::waterBlock({true, false, false, amount}); };
    const falcon::Block falling = FluidSimulator::waterBlock({true, false, true, 8});
    const falcon::Block air = falcon::commonBlocks().air;
    struct Run {
        falcon::Level level = makeFluidFloor();
        FluidSimulator sim;
        void set(int x, int y, int z, falcon::Block b) {
            level.setBlock(x, y, z, b);
            sim.blockChanged(level, x, y, z);
        }
        void ticks(int n) {
            for (int i = 0; i < n; ++i) sim.tick(level, [this](int x, int y, int z, falcon::Block b) { return level.setBlock(x, y, z, b); });
        }
        FluidSimulator::Water at(int x, int y, int z) const { return FluidSimulator::waterAt(level.block(x, y, z)); }
    };

    // What a water state is: a source, flowing water with its height, falling water, and no water.
    CHECK(FluidSimulator::waterAt(source).source && FluidSimulator::waterAt(source).amount == 8);
    CHECK(!FluidSimulator::waterAt(flowing(3)).source && FluidSimulator::waterAt(flowing(3)).amount == 3 && !FluidSimulator::waterAt(flowing(3)).falling);
    CHECK(FluidSimulator::waterAt(falling).falling && !FluidSimulator::waterAt(falling).source && FluidSimulator::waterAt(falling).amount == 8);
    CHECK(!FluidSimulator::waterAt(air).water && !FluidSimulator::waterAt(falcon::commonBlocks().stone).water);

    // A source on the floor spreads one ring every 5 ticks, a block of height less each: a diamond that reaches 7 blocks.
    {
        Run r;
        r.set(0, -60, 0, source);
        r.ticks(4);
        CHECK(!r.at(1, -60, 0).water);                                                    // nothing yet: the delay is 5 ticks
        r.ticks(1);
        CHECK(r.at(1, -60, 0).water && r.at(1, -60, 0).amount == 7 && !r.at(2, -60, 0).water);
        r.ticks(45);
        CHECK(r.at(3, -60, 0).amount == 5 && r.at(1, -60, 1).amount == 6 && r.at(-2, -60, -3).amount == 3);  // 8 minus the blocks away
        CHECK(r.at(7, -60, 0).amount == 1 && !r.at(8, -60, 0).water && !r.at(4, -60, 4).water);            // the edge of the diamond
        CHECK(r.at(0, -60, 0).source);
        CHECK(r.sim.pending() == 0);  // it has settled: nothing left to do
    }
    // Water falls down through air, then spreads at the bottom at 7.
    {
        Run r;
        r.set(0, -54, 0, source);
        r.ticks(5);
        CHECK(r.at(0, -55, 0).falling && !r.at(0, -56, 0).water);
        r.ticks(60);
        CHECK(r.at(0, -57, 0).falling && r.at(0, -60, 0).falling);  // a column of falling water to the floor
        // A source in mid-air also fans out at its own level once there is water under it (as pouring a bucket in the air does), and
        // those side blocks fall too: the waterfall is a plus-shaped bundle of falling columns.
        CHECK(r.at(1, -54, 0).water && r.at(1, -54, 0).amount == 7 && r.at(1, -58, 0).falling && r.at(0, -58, 1).falling);
    }
    // Boxed in on its sides, a source only falls: a single column, which spreads along the floor at 7 when it lands.
    {
        Run r;
        for (const auto& s : {std::array<int, 2>{1, 0}, std::array<int, 2>{-1, 0}, std::array<int, 2>{0, 1}, std::array<int, 2>{0, -1}}) r.level.setBlock(s[0], -54, s[1], falcon::commonBlocks().stone);
        r.set(0, -54, 0, source);
        r.ticks(60);
        CHECK(r.at(0, -57, 0).falling && r.at(0, -60, 0).falling && !r.at(1, -58, 0).water);
        CHECK(r.at(1, -60, 0).water && r.at(1, -60, 0).amount == 7 && !r.at(1, -59, 0).water);
    }
    // Two sources make a third between them (the infinite pool); one source does not.
    {
        Run r;
        r.set(-1, -60, 0, source);
        r.set(1, -60, 0, source);
        r.ticks(20);
        CHECK(r.at(0, -60, 0).source);
        Run one;
        one.set(-1, -60, 0, source);
        one.ticks(20);
        CHECK(one.at(0, -60, 0).water && !one.at(0, -60, 0).source);
    }
    // Take the source away and the flow dries up.
    {
        Run r;
        r.set(0, -60, 0, source);
        r.ticks(60);
        CHECK(r.at(4, -60, 0).water);
        r.set(0, -60, 0, air);
        r.ticks(200);
        bool anyWater = false;
        for (int x = -8; x <= 8; ++x) {
            for (int z = -8; z <= 8; ++z) anyWater = anyWater || r.at(x, -60, z).water;
        }
        CHECK(!anyWater);
        CHECK(r.sim.pending() == 0);
    }
    // Solid blocks stop it: no water behind a wall it cannot get round in 7 blocks.
    {
        Run r;
        for (int y = -60; y <= -50; ++y) {
            for (int z = -4; z <= 4; ++z) r.level.setBlock(2, y, z, falcon::commonBlocks().stone);
        }
        r.set(0, -60, 0, source);
        r.ticks(100);
        CHECK(r.at(1, -60, 0).water && !r.at(2, -60, 0).water && !r.at(3, -60, 0).water && !r.at(6, -60, 0).water);
    }
    // Water washes a torch away (a block with no collision can hold water).
    {
        Run r;
        const falcon::Block torch = falcon::blockRegistry().find("torch").value();
        r.level.setBlock(1, -60, 0, torch);
        r.set(0, -60, 0, source);
        r.ticks(5);
        CHECK(r.at(1, -60, 0).water && r.level.block(1, -60, 0) != torch);
    }
    // It runs for the nearest drop: with a hole in the floor three blocks east, the water goes that way and no other.
    {
        Run r;
        r.level.setBlock(3, -61, 0, air);
        r.set(0, -60, 0, source);
        r.ticks(5);
        CHECK(r.at(1, -60, 0).water && !r.at(-1, -60, 0).water && !r.at(0, -60, 1).water && !r.at(0, -60, -1).water);
        r.ticks(40);
        CHECK(r.at(3, -61, 0).water);  // and it pours into the hole
    }
    // The current (FlowingFluid.getFlow): water pulls towards where the surface is lower - outwards from a source - and not at all where
    // it is level; falling water against a wall is pulled straight down.
    {
        Run r;
        r.set(0, -60, 0, source);
        r.ticks(50);
        const auto east = FluidSimulator::flowAt(r.level, 2, -60, 0);
        const auto west = FluidSimulator::flowAt(r.level, -2, -60, 0);
        const auto diag = FluidSimulator::flowAt(r.level, 2, -60, 2);
        CHECK(std::abs(east[0] - 1.0) < 1e-9 && std::abs(east[1]) < 1e-9 && std::abs(east[2]) < 1e-9);  // downhill, away from the source
        CHECK(std::abs(west[0] + 1.0) < 1e-9 && std::abs(west[2]) < 1e-9);
        CHECK(diag[0] > 0.7 && diag[2] > 0.7 && std::abs(diag[0] - diag[2]) < 1e-9);                   // away at 45 degrees
        const auto middle = FluidSimulator::flowAt(r.level, 0, -60, 0);
        CHECK(middle[0] == 0.0 && middle[1] == 0.0 && middle[2] == 0.0);                               // the source: pulled every way equally
        CHECK(FluidSimulator::flowAt(r.level, 9, -60, 0) == (std::array<double, 3>{0.0, 0.0, 0.0}));   // no water there
        Run fall;
        for (const auto& s : {std::array<int, 2>{1, 0}, std::array<int, 2>{-1, 0}, std::array<int, 2>{0, 1}, std::array<int, 2>{0, -1}}) fall.level.setBlock(s[0], -54, s[1], falcon::commonBlocks().stone);
        fall.set(0, -54, 0, source);
        fall.ticks(40);
        const auto down = FluidSimulator::flowAt(fall.level, 0, -57, 0);  // falling water in a stone-walled column? no walls below -54: still down
        CHECK(down[0] == 0.0 && down[2] == 0.0);                          // (open sides below the box: nothing pulls it sideways)
        fall.level.setBlock(1, -57, 0, falcon::commonBlocks().stone);
        const auto pinned = FluidSimulator::flowAt(fall.level, 0, -57, 0);  // a stone beside it: pulled down
        CHECK(pinned[1] < -0.99 && std::abs(pinned[0]) < 0.01);
    }
    // The cap on changes a tick: a flood is spread over several ticks, never lost.
    {
        Run r;
        r.set(0, -60, 0, source);
        int placed = 0, worst = 0;
        for (int i = 0; i < 80; ++i) {
            int thisTick = 0;
            r.sim.tick(r.level, [&](int x, int y, int z, falcon::Block b) { ++thisTick; return r.level.setBlock(x, y, z, b); }, 3);
            worst = std::max(worst, thisTick);
            placed += thisTick;
        }
        CHECK(worst <= 3 + 3 && placed > 20);  // (one update may change a few blocks past the cap)
    }
}

// What the world looks like from under water: the vision ramp, the fog colour and reach, and the biomes' own water fog.
void test_water_fog() {
    CHECK(falcon::waterVision(0, false) == 0.0f && falcon::waterVision(600, false) == 0.0f);  // eyes out of the water: no vision
    CHECK(falcon::waterVision(0, true) == 0.0f);
    CHECK(std::abs(falcon::waterVision(50, true) - 0.3f) < 1e-6f);    // the first 100 ticks give 0.6
    CHECK(std::abs(falcon::waterVision(100, true) - 0.6f) < 1e-6f);
    CHECK(std::abs(falcon::waterVision(350, true) - 0.8f) < 1e-5f);   // then 0.4 more over 500 ticks
    CHECK(falcon::waterVision(600, true) == 1.0f && falcon::waterVision(5000, true) == 1.0f);
    // The tick counter: up one a tick under water (ten for a spectator, to 600), down ten a tick out of it.
    CHECK(falcon::nextWaterVisionTicks(0, true, false) == 1 && falcon::nextWaterVisionTicks(0, true, true) == 10);
    CHECK(falcon::nextWaterVisionTicks(595, true, true) == 600 && falcon::nextWaterVisionTicks(600, true, false) == 600);
    CHECK(falcon::nextWaterVisionTicks(25, false, false) == 15 && falcon::nextWaterVisionTicks(5, false, false) == 0 && falcon::nextWaterVisionTicks(0, false, false) == 0);
    // The default fog colour 0x050533 is dark at first and a vivid blue once the eyes are used to it (its strongest channel goes to full).
    const falcon::WaterFog fog;
    const auto dark = falcon::underwaterFogColor(fog.color, 0.0f);
    const auto bright = falcon::underwaterFogColor(fog.color, 1.0f);
    CHECK(std::abs(dark[2] - 0.2f) < 1e-6f && std::abs(dark[0] - 5.0f / 255.0f) < 1e-6f);
    CHECK(std::abs(bright[2] - 1.0f) < 1e-5f && std::abs(bright[0] - 5.0f / 255.0f * 5.0f) < 1e-5f);
    CHECK(falcon::underwaterFogColor({0.0f, 0.5f, 0.5f}, 1.0f) == (std::array<float, 3>{0.0f, 0.5f, 0.5f}));  // an empty channel: left alone
    // The fog reaches 24 blocks at first (a quarter of 96) and the full 96 after 600 ticks; the swamp's factor 0.85 shortens it.
    CHECK(falcon::underwaterFogEnd(1.0f, 0.0f) == 24.0f && falcon::underwaterFogEnd(1.0f, 1.0f) == 96.0f && std::abs(falcon::underwaterFogEnd(0.85f, 1.0f) - 81.6f) < 1e-4f);
    CHECK(falcon::kWaterFogStart == -8.0f);

    // The table reads each biome's attributes from the game's data files.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "falcon_water_fog_test";
    fs::remove_all(root);
    fs::create_directories(root / "data" / "minecraft" / "worldgen" / "biome");
    const auto write = [&](const char* name, const char* json) { std::ofstream(root / "data" / "minecraft" / "worldgen" / "biome" / name) << json; };
    write("swamp.json", R"({"attributes":{"minecraft:visual/water_fog_color":"#232317","minecraft:visual/water_fog_end_distance":{"argument":0.85,"modifier":"multiply"}}})");
    write("warm_ocean.json", R"({"attributes":{"minecraft:visual/water_fog_color":"#041f33"}})");
    write("plains.json", R"({"attributes":{"minecraft:visual/sky_color":"#78a7ff"}})");
    write("broken.json", "{ this is not json");
    const falcon::WaterFogTable table = falcon::WaterFogTable::load(root);
    CHECK(table.size() == 2);
    CHECK(std::abs(table.forBiome("minecraft:swamp").endFactor - 0.85f) < 1e-6f && std::abs(table.forBiome("swamp").color[0] - 35.0f / 255.0f) < 1e-6f);
    CHECK(std::abs(table.forBiome("minecraft:warm_ocean").color[1] - 31.0f / 255.0f) < 1e-6f && table.forBiome("warm_ocean").endFactor == 1.0f);
    CHECK(table.forBiome("minecraft:plains").color == fog.color && table.forBiome("no_such_biome").endFactor == 1.0f && table.forBiome("").endFactor == 1.0f);
    CHECK(falcon::WaterFogTable::load(root / "missing").size() == 0);  // no data folder: every biome is the default
    fs::remove_all(root);
}

void test_resource_location() {
    const auto a = falcon::ResourceLocation::parse("minecraft:font/include/default");
    CHECK(a.ns == "minecraft" && a.path == "font/include/default");
    const auto b = falcon::ResourceLocation::parse("widget/button");
    CHECK(b.ns == "minecraft" && b.path == "widget/button");
    CHECK(b.decorated("textures/gui/sprites/", ".png").path == "textures/gui/sprites/widget/button.png");
}

void test_gui_scale() {
    // The game's default 854x480 window runs at GUI scale 2 (427x240 units).
    CHECK(falcon::calculateGuiScale(854, 480, 0, false) == 2);
    CHECK(falcon::scaledSize(854, 2) == 427 && falcon::scaledSize(480, 2) == 240);
    CHECK(falcon::calculateGuiScale(1920, 1080, 0, false) == 4);
    CHECK(falcon::calculateGuiScale(320, 240, 0, false) == 1);
    CHECK(falcon::calculateGuiScale(1920, 1080, 3, false) == 3);  // explicit scale is honoured
    CHECK(falcon::calculateGuiScale(854, 480, 0, true) == 2);
    CHECK(falcon::calculateGuiScale(1920, 1080, 3, true) == 4);   // unicode forces an even scale
    CHECK(falcon::scaledSize(855, 2) == 428);                     // rounds up
}

void test_title_layout() {
    using falcon::Rect;
    // 854x480 window at scale 2 -> 427x240 GUI units; "Copyright ..." is 188 wide.
    const auto l = falcon::computeTitleScreenLayout(427, 240, 188);
    // (the game's own rows start at 108; this client's Mods button under Realms is a row more, so they start at 90 and the last one still ends above the footer line)
    CHECK((l.singleplayer == Rect{113, 90, 200, 20}));
    CHECK((l.multiplayer == Rect{113, 114, 200, 20}));
    CHECK((l.realms == Rect{113, 138, 200, 20}));
    CHECK((l.mods == Rect{113, 162, 200, 20}));
    CHECK((l.friends == Rect{179, 186, 20, 20}));
    CHECK((l.language == Rect{203, 186, 20, 20}));
    CHECK((l.accessibility == Rect{227, 186, 20, 20}));
    CHECK((l.options == Rect{113, 210, 98, 20}));
    CHECK((l.quit == Rect{215, 210, 98, 20}));
    CHECK(l.options.y + l.options.height <= 230);  // above the line at the bottom
    // A taller window keeps the game's own start (a quarter of the height and 48).
    CHECK(falcon::computeTitleScreenLayout(480, 270, 188).singleplayer.y == 270 / 4 + 48);
    CHECK(l.copyrightX == 427 - 188 - 2);
    CHECK(l.copyrightY == 230);
}

// The survival bars: positions as Hud.extractPlayerHealth and ContextualBar place them (checked against the 26.3 bytecode), and which icon is full.
void test_survival_hud() {
    // 854x480 at GUI scale 2: 427x240 units.
    const auto l = falcon::computeSurvivalLayout(427, 240);
    CHECK(l.left == 122 && l.right == 304);  // 427 / 2 = 213: 213 - 91 and 213 + 91, the edges of the 182-wide hotbar
    CHECK(l.rowY == 201);                    // 39 above the bottom
    CHECK(l.heartX(0) == 122 && l.heartX(9) == 194 && l.heartX(10) == 122);  // 8 apart, ten to a row
    CHECK(l.heartY(0) == 201 && l.heartY(10) == 190);                        // a second row is rowHeight (11 with one row's worth of health) higher
    CHECK(l.foodX(0) == 295 && l.foodX(9) == 223);                           // from the right edge, 9 wide icons 8 apart
    CHECK(l.xpLeft == 122 && l.xpTop == 211);                                // (427 - 182) / 2, and 24 + 5 above the bottom
    CHECK(l.levelBaselineY == 205);                                          // 24 + 9 + 2 above the bottom
    CHECK(falcon::computeSurvivalLayout(427, 240, 60.0f).rowHeight == 9);    // three rows of hearts squeeze together: max(10 - (3 - 2), 3)
    CHECK(falcon::computeSurvivalLayout(427, 240, 20.0f).rowHeight == 11);

    // Full health is ten full hearts; 13 points is six full, one half; 0 is none.
    for (int i = 0; i < 10; ++i) CHECK(falcon::heartFill(i, 20.0f) == falcon::BarFill::Full);
    CHECK(falcon::heartFill(5, 13.0f) == falcon::BarFill::Full);
    CHECK(falcon::heartFill(6, 13.0f) == falcon::BarFill::Half);
    CHECK(falcon::heartFill(7, 13.0f) == falcon::BarFill::Empty);
    CHECK(falcon::heartFill(0, 0.5f) == falcon::BarFill::Half);  // the health is rounded up: half a point still shows a half heart
    CHECK(falcon::heartFill(0, 0.0f) == falcon::BarFill::Empty);
    CHECK(falcon::foodFill(9, 20) == falcon::BarFill::Full && falcon::foodFill(3, 7) == falcon::BarFill::Half && falcon::foodFill(4, 7) == falcon::BarFill::Empty);
    CHECK(falcon::experienceFillWidth(0.0f) == 0 && falcon::experienceFillWidth(0.5f) == 91 && falcon::experienceFillWidth(0.999f) == 182 && falcon::experienceFillWidth(1.0f) == 182);

    // The shakes only happen at 2 hearts or less, and with no saturation left, and the jitter stays inside what it is asked for.
    CHECK(falcon::heartsShake(4.0f) && !falcon::heartsShake(5.0f));
    CHECK(falcon::foodShakes(0.0f, 7, 22) && !falcon::foodShakes(0.0f, 7, 23) && !falcon::foodShakes(5.0f, 7, 22));
    bool sawZero = false, sawOne = false;
    for (long long tick = 0; tick < 200; ++tick) {
        const int j = falcon::tickJitter(tick, 3, 2);
        CHECK(j == 0 || j == 1);
        (j == 0 ? sawZero : sawOne) = true;
    }
    CHECK(sawZero && sawOne);
}

void test_system_info() {
    // The CPU name without the marketing.
    CHECK(falcon::cleanCpuName("AMD Ryzen 5 5600G with Radeon Graphics") == "AMD Ryzen 5 5600G");
    CHECK(falcon::cleanCpuName("Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz") == "Intel Core i7-9700K");
    CHECK(falcon::cleanCpuName("AMD Ryzen 5 3600 6-Core Processor        ") == "AMD Ryzen 5 3600");
    CHECK(falcon::cleanCpuName("") == "");
    CHECK(falcon::formatByteSize(34'359'738'368ull) == "32.0 GB");
    CHECK(falcon::formatByteSize(512ull * 1024 * 1024) == "512 MB");
    // The PC this runs on has a processor, an operating system and some memory; and what is free cannot be more than what there is.
    const falcon::SystemInfo& pc = falcon::systemInfo();
    CHECK(pc.logicalProcessors >= 1);
    CHECK(!pc.os.empty());
    CHECK(pc.totalMemory > 256ull * 1024 * 1024);
    const falcon::SystemMemory m = falcon::systemMemory();
    CHECK(m.total > 0 && m.available > 0 && m.available <= m.total);
    CHECK(!falcon::findGpuAdapter("").has_value());
}

// What makes a step sound, and how the world's sounds are mixed (checked against Entity.applyMovementEmissionAndPlaySound, SoundType and
// LivingEntity.getFallDamageSound in the 26.3 bytecode).
void test_world_sounds() {
    falcon::StepClock clock;
    CHECK(!clock.due() && clock.nextStep() == 1.0f);
    for (int i = 0; i < 16; ++i) clock.add(0.1);  // 1.6 blocks: 0.96 of the clock
    CHECK(!clock.due());
    clock.add(0.1);  // 1.7 blocks: 1.02, past the first mark
    CHECK(clock.due());
    clock.played();
    CHECK(clock.nextStep() == 2.0f && !clock.due());
    // A jump: the distance flown while there was nothing to land on is added without a sound, and the landing makes the sound at once.
    for (int i = 0; i < 12; ++i) clock.add(0.4);  // 4.8 blocks: 2.88 + 1.02 = 3.9
    CHECK(clock.due());
    clock.played();
    CHECK(clock.nextStep() == 4.0f);

    CHECK(falcon::blockSoundEvent("stone", "break") == "block.stone.break");
    CHECK(falcon::breakOrPlaceMix(1.0f, 1.0f).volume == 1.0f && std::abs(falcon::breakOrPlaceMix(1.0f, 1.0f).pitch - 0.8f) < 1e-6f);
    CHECK(std::abs(falcon::breakOrPlaceMix(0.3f, 1.0f).volume - 0.65f) < 1e-6f);  // an anvil's own volume is 0.3
    CHECK(std::abs(falcon::stepMix(1.0f, 1.2f).volume - 0.15f) < 1e-6f && falcon::stepMix(1.0f, 1.2f).pitch == 1.2f);
    CHECK(std::abs(falcon::mutedStepMix(1.0f, 1.0f).volume - 0.05f) < 1e-6f && std::abs(falcon::mutedStepMix(1.0f, 1.0f).pitch - 0.8f) < 1e-6f);
    CHECK(std::abs(falcon::fallMix(1.0f, 1.0f).volume - 0.5f) < 1e-6f && std::abs(falcon::fallMix(1.0f, 1.0f).pitch - 0.75f) < 1e-6f);
    CHECK(std::string(falcon::fallDamageEvent(4)) == "entity.player.small_fall" && std::string(falcon::fallDamageEvent(5)) == "entity.player.big_fall");
}

// Items lying in the world: they fall with the game's gravity and drag, settle on the ground, count their pickup delay down, join a neighbour of the
// same kind, and are gone after five minutes.
void test_item_entities() {
    falcon::Level level = makeFlatLevel();  // the ground's surface is at y = -60
    const falcon::Item stone = falcon::itemRegistry().find("stone").value();
    falcon::ItemEntities items;
    falcon::ItemEntity& a = items.spawn(falcon::ItemStack(stone, 1), 0.5, -57.0, 0.5, 0.0, 0.0, 0.0, 10, 0.0f);
    CHECK(!a.onGround);
    items.tick(level);
    CHECK(std::abs(items.items()[0].vy - (-0.04 * 0.98)) < 1e-9 || std::abs(items.items()[0].vy - (-0.04)) < 1e-9);  // gravity 0.04, then the drag of 0.98
    CHECK(items.items()[0].pickupDelay == 9);
    for (int i = 0; i < 40; ++i) items.tick(level);
    CHECK(items.items().size() == 1);
    const falcon::ItemEntity& landed = items.items()[0];
    CHECK(landed.onGround && std::abs(landed.y - (-60.0)) < 1e-6);   // on top of the grass
    CHECK(std::abs(landed.vy) < 0.2 && landed.pickupDelay == 0);  // (resting, it only moves every fourth tick, so gravity builds up a little in between)
    CHECK(landed.age == 41);

    // Two stacks of the same kind side by side become one (the bigger stays), and a different kind stays apart.
    falcon::ItemEntities pile;
    pile.spawn(falcon::ItemStack(stone, 5), 3.5, -60.0, 3.5, 0.0, 0.0, 0.0, 0, 0.0f);
    pile.spawn(falcon::ItemStack(stone, 3), 3.7, -60.0, 3.6, 0.0, 0.0, 0.0, 0, 0.0f);
    pile.spawn(falcon::ItemStack(falcon::itemRegistry().find("dirt").value(), 2), 3.6, -60.0, 3.5, 0.0, 0.0, 0.0, 0, 0.0f);
    for (int i = 0; i < 80; ++i) pile.tick(level);
    int stoneCount = 0, stacks = 0;
    for (const falcon::ItemEntity& e : pile.items()) {
        ++stacks;
        if (e.stack.item == stone) stoneCount += e.stack.count;
    }
    CHECK(stacks == 2 && stoneCount == 8);

    // Five minutes (6000 ticks) and it is gone; what has not been picked up by then vanishes.
    falcon::ItemEntities old;
    old.spawn(falcon::ItemStack(stone, 1), 5.5, -60.0, 5.5, 0.0, 0.0, 0.0, 0, 0.0f).age = falcon::ItemEntity::kLifetimeTicks - 1;
    old.tick(level);
    CHECK(old.items().empty());
    CHECK(falcon::ItemEntities::renderedAmount(1) == 1 && falcon::ItemEntities::renderedAmount(16) == 2 && falcon::ItemEntities::renderedAmount(17) == 3 &&
          falcon::ItemEntities::renderedAmount(33) == 4 && falcon::ItemEntities::renderedAmount(49) == 5);
}

// A world's save: the block edits go into chunks as they are made, and the player, the inventory and the items lying about survive a write and a read.
void test_world_save() {
    const falcon::BlockRegistry& registry = falcon::blockRegistry();
    const falcon::Block stone = registry.find("stone").value();
    const falcon::Block dirt = registry.find("dirt").value();
    falcon::WorldEdits edits;
    edits.set(5, 70, -3, stone);
    edits.set(5, 71, -3, dirt);
    edits.set(100, falcon::kMinY, 100, stone);
    edits.set(1, falcon::kMaxY, 1, stone);  // outside the world: ignored
    CHECK(edits.size() == 3 && edits.chunkCount() == 2 && edits.changed());
    falcon::Chunk chunk;
    edits.applyTo({0, -1}, chunk);  // x 5, z -3 is local x 5, z 13 in chunk (0, -1)
    CHECK(chunk.at(5, 70, 13) == stone && chunk.at(5, 71, 13) == dirt && chunk.at(5, 72, 13) == falcon::Block::Air);
    falcon::Chunk untouched;
    edits.applyTo({3, 3}, untouched);
    CHECK(untouched.at(5, 70, 13) == falcon::Block::Air);

    // Through NBT and back.
    falcon::WorldEdits again;
    CHECK(again.fromNbt(edits.toNbt(registry), registry) && !edits.changed());
    CHECK(again.size() == 3 && again.chunkCount() == 2);
    falcon::Chunk restored;
    again.applyTo({0, -1}, restored);
    CHECK(restored.at(5, 70, 13) == stone && restored.at(5, 71, 13) == dirt);
    // An edit that was overwritten counts once, with the last block.
    edits.set(5, 70, -3, dirt);
    falcon::Chunk last;
    edits.applyTo({0, -1}, last);
    CHECK(edits.size() == 3 && last.at(5, 70, 13) == dirt);

    // The whole file.
    falcon::WorldSave save;
    save.hasPlayer = true;
    save.player.x = 12.5;
    save.player.y = 70.0;
    save.player.z = -3.25;
    save.player.yaw = 135.0f;
    save.player.pitch = -20.0f;
    save.player.health = 13.0f;
    save.player.food = 17;
    save.player.selectedSlot = 4;
    save.player.gameType = 0;
    falcon::ItemStack pick(falcon::itemRegistry().find("stone").value(), 42);
    pick.customName = "Rocky";
    save.player.inventory.emplace_back(4, pick);
    falcon::ItemEntity lying;
    lying.stack = falcon::ItemStack(falcon::itemRegistry().find("dirt").value(), 3);
    lying.x = 1.5;
    lying.y = 64.0;
    lying.z = 2.5;
    lying.age = 77;
    lying.pickupDelay = 9;
    save.items.push_back(lying);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "falcon-save-test";
    std::filesystem::remove_all(dir);
    const std::filesystem::path file = dir / "falcon_save.dat";
    CHECK(falcon::writeWorldSave(file, save, edits, registry).empty() && std::filesystem::exists(file));
    CHECK(falcon::writeWorldSave(file, save, edits, registry).empty() && std::filesystem::exists(dir / "falcon_save.dat_old"));  // the previous save is kept
    falcon::WorldSave loaded;
    falcon::WorldEdits loadedEdits;
    std::string error;
    CHECK(falcon::readWorldSave(file, loaded, loadedEdits, registry, error) && error.empty());
    CHECK(loaded.hasPlayer && loaded.player.x == 12.5 && loaded.player.z == -3.25 && loaded.player.yaw == 135.0f && loaded.player.health == 13.0f && loaded.player.food == 17);
    CHECK(loaded.player.selectedSlot == 4 && loaded.player.inventory.size() == 1 && loaded.player.inventory[0].first == 4);
    CHECK(loaded.player.inventory[0].second.count == 42 && loaded.player.inventory[0].second.customName == "Rocky");
    CHECK(loaded.items.size() == 1 && loaded.items[0].stack.count == 3 && loaded.items[0].age == 77 && loaded.items[0].pickupDelay == 9 && loaded.items[0].y == 64.0);
    CHECK(loadedEdits.size() == 3);
    // A world that was never saved says nothing; a damaged file falls back on the spare, and with no spare says so.
    falcon::WorldSave none;
    falcon::WorldEdits noneEdits;
    CHECK(!falcon::readWorldSave(dir / "missing.dat", none, noneEdits, registry, error) && error.empty());
    {
        std::ofstream damaged(file, std::ios::binary | std::ios::trunc);
        damaged << "not nbt";
    }
    falcon::WorldSave fromSpare;
    CHECK(falcon::readWorldSave(file, fromSpare, noneEdits, registry, error) && fromSpare.hasPlayer);
    std::filesystem::remove(dir / "falcon_save.dat_old");
    CHECK(!falcon::readWorldSave(file, none, noneEdits, registry, error) && !error.empty());
    std::filesystem::remove_all(dir);
}

// The part of the ownership check that needs no network: reading Mojang's two answers.
void test_ownership_answers() {
    CHECK(falcon::entitlementsListGame(R"({"items":[{"name":"product_minecraft","signature":"x"},{"name":"game_minecraft","signature":"y"}],"signature":"z","keyId":"1"})"));
    CHECK(falcon::entitlementsListGame(R"({"items":[{"name":"game_minecraft"}]})"));
    CHECK(!falcon::entitlementsListGame(R"({"items":[]})"));  // an account that owns nothing
    CHECK(!falcon::entitlementsListGame(R"({"items":[{"name":"game_minecraft_bedrock"}]})"));
    CHECK(!falcon::entitlementsListGame("not json") && !falcon::entitlementsListGame("{}") && !falcon::entitlementsListGame(""));
    std::string why;
    const std::string profile = R"({"id":"069a79f444e94726a5befca90e38aaf5","name":"Notch","skins":[]})";
    CHECK(falcon::profileMatches(profile, "069a79f444e94726a5befca90e38aaf5", "Notch", why));
    CHECK(falcon::profileMatches(profile, "069A79F4-44E9-4726-A5BE-FCA90E38AAF5", "Notch", why));  // dashes and case do not matter
    CHECK(falcon::profileMatches(profile, "", "", why));                                           // nothing to compare
    CHECK(!falcon::profileMatches(profile, "11111111111111111111111111111111", "Notch", why) && !why.empty());
    CHECK(!falcon::profileMatches(profile, "069a79f444e94726a5befca90e38aaf5", "Jeb_", why) && !why.empty());
    CHECK(!falcon::profileMatches(R"({"error":"NOT_FOUND"})", "", "", why) && !why.empty());
    // With no token there is nothing to ask: the check says what to do and does not go to the network.
    const falcon::OwnershipCheck none = falcon::verifyMinecraftOwnership("", "", "");
    CHECK(!none.ok && none.message.find("Launcher") != std::string::npos);
}

void test_options_layout() {
    using falcon::Rect;
    // 854x480 window at scale 2 -> 427x240 GUI units; "Options" is 36 wide.
    const auto l = falcon::computeOptionsScreenLayout(427, 240, 36);
    CHECK((l.title == Rect{195, 12, 36, 9}));
    CHECK((l.fov == Rect{59, 29, 150, 20}));
    CHECK((l.online == Rect{217, 29, 150, 20}));
    const int rowY[5] = {87, 111, 135, 159, 183};
    for (int i = 0; i < 10; ++i) {
        CHECK((l.grid[static_cast<std::size_t>(i)] ==
               Rect{i % 2 == 0 ? 59 : 217, rowY[i / 2], 150, 20}));
    }
    CHECK((l.done == Rect{113, 214, 200, 20}));  // (33 - 20) / 2 = 6.5 rounds up to 7

    // A taller window pins the grid 30px under the header instead (y = 91).
    const auto tall = falcon::computeOptionsScreenLayout(427, 270, 36);
    CHECK(tall.grid[0].y == 91 && tall.grid[8].y == 91 + 96);
    CHECK(tall.done.y == 270 - 33 + 7);
}

void test_fov_mapping() {
    CHECK(near(static_cast<float>(falcon::fovToSliderValue(70)), 0.5f));
    CHECK(falcon::fovToSliderValue(30) == 0.0 && falcon::fovToSliderValue(110) == 1.0);
    CHECK(falcon::sliderValueToFov(0.5) == 70);
    CHECK(falcon::sliderValueToFov(0.0) == 30);
    CHECK(falcon::sliderValueToFov(1.0) == 110);
    for (int fov = 30; fov <= 110; ++fov) {  // every value survives the round trip
        CHECK(falcon::sliderValueToFov(falcon::fovToSliderValue(fov)) == fov);
    }
}

void test_format_language_string() {
    CHECK(falcon::formatLanguageString("%s: %s", {"FOV", "Normal"}) == "FOV: Normal");
    CHECK(falcon::formatLanguageString("%2$s then %1$s", {"a", "b"}) == "b then a");
    CHECK(falcon::formatLanguageString("100%% sure", {}) == "100% sure");
    CHECK(falcon::formatLanguageString("no placeholders", {"x"}) == "no placeholders");
    CHECK(falcon::formatLanguageString("%s", {}) == "");
}

void test_slider() {
    // 150px wide slider at x=59: the handle travels over the middle 142px (4px margins).
    falcon::SliderWidget slider(59, 29, 150, 20, 0.5, [](double v) { return std::to_string(v); });
    double last = -1.0;
    int changes = 0;
    slider.onValueChanged = [&](double v) {
        last = v;
        ++changes;
    };

    CHECK(slider.mouseClicked(59.0 + 4.0 + 142.0, 35.0, 0));  // far right of the track
    CHECK(slider.dragging() && near(static_cast<float>(last), 1.0f));
    slider.mouseDragged(0.0, 35.0);                            // dragged past the left end clamps
    CHECK(slider.value() == 0.0);
    slider.mouseDragged(59.0 + 4.0 + 71.0, 35.0);
    CHECK(near(static_cast<float>(slider.value()), 0.5f));
    const int before = changes;
    slider.mouseDragged(59.0 + 4.0 + 71.0, 35.0);              // same position: no change event
    CHECK(changes == before);
    slider.mouseReleased(0.0, 0.0, 0);
    CHECK(!slider.dragging());
    slider.mouseDragged(100.0, 35.0);                          // not dragging any more
    CHECK(near(static_cast<float>(slider.value()), 0.5f));
    CHECK(!slider.mouseClicked(59.0, 60.0, 0));                // below the slider
    slider.active = false;
    CHECK(!slider.mouseClicked(100.0, 35.0, 0));               // inactive sliders ignore clicks
}

std::shared_ptr<falcon::PcmSound> makeSound(int rate, int channels, std::size_t frames, float value) {
    auto s = std::make_shared<falcon::PcmSound>();
    s->sampleRate = rate;
    s->channels = channels;
    s->samples.assign(frames * static_cast<std::size_t>(channels), value);
    return s;
}
