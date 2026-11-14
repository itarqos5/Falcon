// Minimal dependency-free test runner for the logic that needs no OpenGL context.
// Swap for Catch2/GoogleTest when the suite outgrows this.

#include "apex/assets/asset_index.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>  // for the loopback fake server of the ping test (must come before other Windows headers)
#include <ws2tcpip.h>
#undef near  // <windef.h> defines these two as empty macros; the helper below is called near()
#undef far
#endif

#include "apex/assets/nbt.hpp"
#include "apex/assets/resource_manager.hpp"
#include "apex/assets/zip_archive.hpp"
#include "apex/packs/pack_repository.hpp"
#include "apex/packs/pack_selection_model.hpp"
#include "apex/text/language.hpp"
#include "apex/ui/scroll_list.hpp"
#include "apex/audio/audio_engine.hpp"
#include "apex/audio/music_manager.hpp"
#include "apex/audio/sound_registry.hpp"
#include "apex/audio/sound_system.hpp"
#include "apex/assets/resource_location.hpp"
#include "apex/client.hpp"
#include "apex/game/edition_badge.hpp"
#include "apex/game/game_options.hpp"
#include "apex/world/chunk_cache.hpp"
#include "apex/game/gamemode_switcher_screen.hpp"
#include "apex/ui/gui_scale.hpp"
#include "apex/ui/layout.hpp"
#include "apex/ui/list_screen.hpp"
#include "apex/game/language_screen.hpp"
#include "apex/game/now_playing_toast.hpp"
#include "apex/game/options_screen.hpp"
#include "apex/game/create_world_screen.hpp"
#include "apex/game/join_multiplayer_screen.hpp"
#include "apex/game/key_binds_screen.hpp"
#include "apex/game/key_mappings.hpp"
#include "apex/platform/key_names.hpp"
#include "apex/game/select_world_screen.hpp"
#include "apex/game/sound_options_screen.hpp"
#include "apex/game/splash_manager.hpp"
#include "apex/game/title_screen.hpp"
#include "apex/ui/widgets.hpp"
#include "apex/util/math.hpp"
#include "apex/util/options_file.hpp"
#include "apex/util/text_util.hpp"
#include "apex/world/level_summary.hpp"
#include "apex/net/server_pinger.hpp"
#include "apex/text/styled_text.hpp"
#include "apex/util/base64.hpp"
#include "apex/util/server_address.hpp"
#include "apex/game/in_game_screen.hpp"
#include "apex/gfx/mipmaps.hpp"
#include "apex/render/camera.hpp"
#include "apex/render/world_renderer.hpp"
#include "apex/world/block_outlines.hpp"
#include "apex/world/fluids.hpp"
#include "apex/world/water_fog.hpp"
#include "apex/util/priority_pool.hpp"
#include "apex/world/level_info.hpp"
#include "apex/assets/asset_locator.hpp"
#include "apex/world/block_registry.hpp"
#include "apex/world/block_render.hpp"
#include "apex/world/light.hpp"
#include "apex/world/mesher.hpp"
#include "apex/world/player.hpp"
#include "apex/world/raycast.hpp"
#include "apex/world/new_world.hpp"
#include "apex/world/terrain.hpp"
#include "apex/world/server_list.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void test_client_identity() {
    apex::Client client{apex::ClientOptions{}};
    CHECK(client.name() == "Falcon");
    CHECK(!client.version().empty());
}

void test_options() {
    const char* argv[] = {"Falcon", "--size", "1280x720", "--seed", "7", "--capture", "a.png"};
    const auto o = apex::parseOptions(7, argv);
    CHECK(o.width == 1280 && o.height == 720);
    CHECK(o.seed && *o.seed == 7u);
    CHECK(o.capturePath && o.capturePath->string() == "a.png");

    const char* bad[] = {"Falcon", "--nope"};
    bool threw = false;
    try {
        (void)apex::parseOptions(2, bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    // The loading-screen test hooks.
    const char* loading[] = {"Falcon", "--play", "w", "--hold-loading", "2.5", "--capture-loading"};
    const auto l = apex::parseOptions(6, loading);
    CHECK(l.holdLoading && *l.holdLoading == 2.5);
    CHECK(l.captureLoading);
    const char* plain[] = {"Falcon", "--play", "w"};
    const auto p = apex::parseOptions(3, plain);
    CHECK(!p.captureLoading && !p.holdLoading);  // off unless asked for
    CHECK(apex::usageText().find("--hold-loading") != std::string::npos);
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
        const int stream = apex::streamRadius(r);
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
    using apex::Image;
    using apex::MipmapStrategy;
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
    CHECK(apex::parseMipmapStrategy("dark_cutout") == MipmapStrategy::DarkCutout && apex::parseMipmapStrategy("strict_cutout") == MipmapStrategy::StrictCutout);
    CHECK(apex::parseMipmapStrategy("mean") == MipmapStrategy::Mean && apex::parseMipmapStrategy("cutout") == MipmapStrategy::Cutout);
    CHECK(apex::parseMipmapStrategy("") == MipmapStrategy::Auto && apex::parseMipmapStrategy("nonsense") == MipmapStrategy::Auto);

    // Mean averages in LINEAR light: black and white texels make the sRGB value of half the light (188), not 128.
    {
        const Image img = makeImage(2, [](int x, int y) { return ((x + y) & 1) == 0 ? std::array<int, 4>{0, 0, 0, 255} : std::array<int, 4>{255, 255, 255, 255}; });
        const auto levels = apex::generateMipLevels(img, 1, MipmapStrategy::Mean);
        CHECK(levels.size() == 2 && levels[1].width == 1 && levels[1].height == 1);
        CHECK(std::abs(levels[1].rgba[0] - 188) <= 1 && std::abs(levels[1].rgba[1] - 188) <= 1 && levels[1].rgba[3] == 255);
        CHECK(levels[0].rgba == img.rgba);  // an opaque picture is not touched
    }
    // A picture that says nothing and has no transparent texel is Mean too (Auto).
    {
        const Image img = makeImage(4, [](int x, int) { return x < 2 ? std::array<int, 4>{200, 0, 0, 255} : std::array<int, 4>{0, 0, 200, 255}; });
        CHECK(apex::generateMipLevels(img, 2, MipmapStrategy::Auto)[1].rgba == apex::generateMipLevels(img, 2, MipmapStrategy::Mean)[1].rgba);
    }
    // Dark cutout: the holes start out as three quarters of the darkest opaque texel (alpha 0), and in the average a transparent texel
    // is black: one white texel in four gives a quarter of the light, a darker grey than the plain average of the colours.
    {
        const Image img = makeImage(2, [](int x, int y) { return x == 0 && y == 0 ? std::array<int, 4>{255, 255, 255, 255} : std::array<int, 4>{10, 20, 30, 0}; });
        const auto levels = apex::generateMipLevels(img, 1, MipmapStrategy::DarkCutout);
        CHECK(levels[0].rgba[0] == 255 && levels[0].rgba[3] == 255);                                                                  // the opaque texel
        CHECK(levels[0].rgba[4] == 191 && levels[0].rgba[5] == 191 && levels[0].rgba[6] == 191 && levels[0].rgba[7] == 0);            // a hole: 3/4 of it, alpha 0
        CHECK(std::abs(levels[1].rgba[0] - 137) <= 1 && std::abs(levels[1].rgba[1] - 137) <= 1);                                       // linear 0.25 -> sRGB 137
    }
    // Cutout (and Auto, for a picture with transparent texels): holes take the colour of the nearest opaque texel, alpha stays 0.
    {
        const Image img = makeImage(4, [](int x, int y) { return x == 0 && y == 0 ? std::array<int, 4>{200, 50, 10, 255} : std::array<int, 4>{0, 0, 0, 0}; });
        const auto levels = apex::generateMipLevels(img, 1, MipmapStrategy::Auto);
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
            for (const Image& level : apex::generateMipLevels(solid, 3, s)) {
                for (std::size_t i = 3; i < level.rgba.size(); i += 4) CHECK(level.rgba[i] == 255);
            }
            for (const Image& level : apex::generateMipLevels(empty, 3, s)) {
                for (std::size_t i = 3; i < level.rgba.size(); i += 4) CHECK(level.rgba[i] < 128);
            }
        }
        const Image leaf = makeImage(16, [](int x, int y) { return ((x * 7 + y * 3) % 5) < 2 ? std::array<int, 4>{30, 90, 20, 255} : std::array<int, 4>{0, 0, 0, 0}; });
        const auto levels = apex::generateMipLevels(leaf, 4, MipmapStrategy::DarkCutout);
        CHECK(levels.size() == 5 && levels[1].width == 8 && levels[2].width == 4 && levels[3].width == 2 && levels[4].width == 1);
        const float base = apex::alphaTestCoverage(leaf, 0.5f, 1.0f);
        CHECK(base > 0.1f && base < 0.7f);
    }
}

// A level with a stone floor whose top is at y = -60, 7 x 7 chunks (the same floor the physics tests use).
apex::Level makeFluidFloor() {
    apex::Level level;
    apex::SectionBuilder builder;
    builder.fillLayers(0, 4, apex::commonBlocks().stone);  // y -64 .. -61
    const apex::SectionPtr floor = builder.build();
    for (int cz = -3; cz <= 3; ++cz) {
        for (int cx = -3; cx <= 3; ++cx) {
            auto chunk = std::make_shared<apex::Chunk>();
            chunk->sections[0] = floor;
            level.putChunk({cx, cz}, chunk);
        }
    }
    return level;
}

// Water that flows (FluidSimulator): the rules of FlowingFluid, checked tick by tick on a stone floor whose top is at y = -60.
void test_fluids() {
    using apex::FluidSimulator;
    const apex::Block source = FluidSimulator::waterBlock({true, true, false, 8});
    const auto flowing = [](int amount) { return FluidSimulator::waterBlock({true, false, false, amount}); };
    const apex::Block falling = FluidSimulator::waterBlock({true, false, true, 8});
    const apex::Block air = apex::commonBlocks().air;
    struct Run {
        apex::Level level = makeFluidFloor();
        FluidSimulator sim;
        void set(int x, int y, int z, apex::Block b) {
            level.setBlock(x, y, z, b);
            sim.blockChanged(level, x, y, z);
        }
        void ticks(int n) {
            for (int i = 0; i < n; ++i) sim.tick(level, [this](int x, int y, int z, apex::Block b) { return level.setBlock(x, y, z, b); });
        }
        FluidSimulator::Water at(int x, int y, int z) const { return FluidSimulator::waterAt(level.block(x, y, z)); }
    };

    // What a water state is: a source, flowing water with its height, falling water, and no water.
    CHECK(FluidSimulator::waterAt(source).source && FluidSimulator::waterAt(source).amount == 8);
    CHECK(!FluidSimulator::waterAt(flowing(3)).source && FluidSimulator::waterAt(flowing(3)).amount == 3 && !FluidSimulator::waterAt(flowing(3)).falling);
    CHECK(FluidSimulator::waterAt(falling).falling && !FluidSimulator::waterAt(falling).source && FluidSimulator::waterAt(falling).amount == 8);
    CHECK(!FluidSimulator::waterAt(air).water && !FluidSimulator::waterAt(apex::commonBlocks().stone).water);

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
        for (const auto& s : {std::array<int, 2>{1, 0}, std::array<int, 2>{-1, 0}, std::array<int, 2>{0, 1}, std::array<int, 2>{0, -1}}) r.level.setBlock(s[0], -54, s[1], apex::commonBlocks().stone);
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
            for (int z = -4; z <= 4; ++z) r.level.setBlock(2, y, z, apex::commonBlocks().stone);
        }
        r.set(0, -60, 0, source);
        r.ticks(100);
        CHECK(r.at(1, -60, 0).water && !r.at(2, -60, 0).water && !r.at(3, -60, 0).water && !r.at(6, -60, 0).water);
    }
    // Water washes a torch away (a block with no collision can hold water).
    {
        Run r;
        const apex::Block torch = apex::blockRegistry().find("torch").value();
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
        for (const auto& s : {std::array<int, 2>{1, 0}, std::array<int, 2>{-1, 0}, std::array<int, 2>{0, 1}, std::array<int, 2>{0, -1}}) fall.level.setBlock(s[0], -54, s[1], apex::commonBlocks().stone);
        fall.set(0, -54, 0, source);
        fall.ticks(40);
        const auto down = FluidSimulator::flowAt(fall.level, 0, -57, 0);  // falling water in a stone-walled column? no walls below -54: still down
        CHECK(down[0] == 0.0 && down[2] == 0.0);                          // (open sides below the box: nothing pulls it sideways)
        fall.level.setBlock(1, -57, 0, apex::commonBlocks().stone);
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
            r.sim.tick(r.level, [&](int x, int y, int z, apex::Block b) { ++thisTick; return r.level.setBlock(x, y, z, b); }, 3);
            worst = std::max(worst, thisTick);
            placed += thisTick;
        }
        CHECK(worst <= 3 + 3 && placed > 20);  // (one update may change a few blocks past the cap)
    }
}

// What the world looks like from under water: the vision ramp, the fog colour and reach, and the biomes' own water fog.
void test_water_fog() {
    CHECK(apex::waterVision(0, false) == 0.0f && apex::waterVision(600, false) == 0.0f);  // eyes out of the water: no vision
    CHECK(apex::waterVision(0, true) == 0.0f);
    CHECK(std::abs(apex::waterVision(50, true) - 0.3f) < 1e-6f);    // the first 100 ticks give 0.6
    CHECK(std::abs(apex::waterVision(100, true) - 0.6f) < 1e-6f);
    CHECK(std::abs(apex::waterVision(350, true) - 0.8f) < 1e-5f);   // then 0.4 more over 500 ticks
    CHECK(apex::waterVision(600, true) == 1.0f && apex::waterVision(5000, true) == 1.0f);
    // The tick counter: up one a tick under water (ten for a spectator, to 600), down ten a tick out of it.
    CHECK(apex::nextWaterVisionTicks(0, true, false) == 1 && apex::nextWaterVisionTicks(0, true, true) == 10);
    CHECK(apex::nextWaterVisionTicks(595, true, true) == 600 && apex::nextWaterVisionTicks(600, true, false) == 600);
    CHECK(apex::nextWaterVisionTicks(25, false, false) == 15 && apex::nextWaterVisionTicks(5, false, false) == 0 && apex::nextWaterVisionTicks(0, false, false) == 0);
    // The default fog colour 0x050533 is dark at first and a vivid blue once the eyes are used to it (its strongest channel goes to full).
    const apex::WaterFog fog;
    const auto dark = apex::underwaterFogColor(fog.color, 0.0f);
    const auto bright = apex::underwaterFogColor(fog.color, 1.0f);
    CHECK(std::abs(dark[2] - 0.2f) < 1e-6f && std::abs(dark[0] - 5.0f / 255.0f) < 1e-6f);
    CHECK(std::abs(bright[2] - 1.0f) < 1e-5f && std::abs(bright[0] - 5.0f / 255.0f * 5.0f) < 1e-5f);
    CHECK(apex::underwaterFogColor({0.0f, 0.5f, 0.5f}, 1.0f) == (std::array<float, 3>{0.0f, 0.5f, 0.5f}));  // an empty channel: left alone
    // The fog reaches 24 blocks at first (a quarter of 96) and the full 96 after 600 ticks; the swamp's factor 0.85 shortens it.
    CHECK(apex::underwaterFogEnd(1.0f, 0.0f) == 24.0f && apex::underwaterFogEnd(1.0f, 1.0f) == 96.0f && std::abs(apex::underwaterFogEnd(0.85f, 1.0f) - 81.6f) < 1e-4f);
    CHECK(apex::kWaterFogStart == -8.0f);

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
    const apex::WaterFogTable table = apex::WaterFogTable::load(root);
    CHECK(table.size() == 2);
    CHECK(std::abs(table.forBiome("minecraft:swamp").endFactor - 0.85f) < 1e-6f && std::abs(table.forBiome("swamp").color[0] - 35.0f / 255.0f) < 1e-6f);
    CHECK(std::abs(table.forBiome("minecraft:warm_ocean").color[1] - 31.0f / 255.0f) < 1e-6f && table.forBiome("warm_ocean").endFactor == 1.0f);
    CHECK(table.forBiome("minecraft:plains").color == fog.color && table.forBiome("no_such_biome").endFactor == 1.0f && table.forBiome("").endFactor == 1.0f);
    CHECK(apex::WaterFogTable::load(root / "missing").size() == 0);  // no data folder: every biome is the default
    fs::remove_all(root);
}

void test_resource_location() {
    const auto a = apex::ResourceLocation::parse("minecraft:font/include/default");
    CHECK(a.ns == "minecraft" && a.path == "font/include/default");
    const auto b = apex::ResourceLocation::parse("widget/button");
    CHECK(b.ns == "minecraft" && b.path == "widget/button");
    CHECK(b.decorated("textures/gui/sprites/", ".png").path == "textures/gui/sprites/widget/button.png");
}

void test_gui_scale() {
    // The game's default 854x480 window runs at GUI scale 2 (427x240 units).
    CHECK(apex::calculateGuiScale(854, 480, 0, false) == 2);
    CHECK(apex::scaledSize(854, 2) == 427 && apex::scaledSize(480, 2) == 240);
    CHECK(apex::calculateGuiScale(1920, 1080, 0, false) == 4);
    CHECK(apex::calculateGuiScale(320, 240, 0, false) == 1);
    CHECK(apex::calculateGuiScale(1920, 1080, 3, false) == 3);  // explicit scale is honoured
    CHECK(apex::calculateGuiScale(854, 480, 0, true) == 2);
    CHECK(apex::calculateGuiScale(1920, 1080, 3, true) == 4);   // unicode forces an even scale
    CHECK(apex::scaledSize(855, 2) == 428);                     // rounds up
}

void test_title_layout() {
    using apex::Rect;
    // 854x480 window at scale 2 -> 427x240 GUI units; "Copyright ..." is 188 wide.
    const auto l = apex::computeTitleScreenLayout(427, 240, 188);
    CHECK((l.singleplayer == Rect{113, 108, 200, 20}));
    CHECK((l.multiplayer == Rect{113, 132, 200, 20}));
    CHECK((l.realms == Rect{113, 156, 200, 20}));
    CHECK((l.friends == Rect{179, 180, 20, 20}));
    CHECK((l.language == Rect{203, 180, 20, 20}));
    CHECK((l.accessibility == Rect{227, 180, 20, 20}));
    CHECK((l.options == Rect{113, 204, 98, 20}));
    CHECK((l.quit == Rect{215, 204, 98, 20}));
    CHECK(l.copyrightX == 427 - 188 - 2);
    CHECK(l.copyrightY == 230);
}

void test_options_layout() {
    using apex::Rect;
    // 854x480 window at scale 2 -> 427x240 GUI units; "Options" is 36 wide.
    const auto l = apex::computeOptionsScreenLayout(427, 240, 36);
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
    const auto tall = apex::computeOptionsScreenLayout(427, 270, 36);
    CHECK(tall.grid[0].y == 91 && tall.grid[8].y == 91 + 96);
    CHECK(tall.done.y == 270 - 33 + 7);
}

void test_fov_mapping() {
    CHECK(near(static_cast<float>(apex::fovToSliderValue(70)), 0.5f));
    CHECK(apex::fovToSliderValue(30) == 0.0 && apex::fovToSliderValue(110) == 1.0);
    CHECK(apex::sliderValueToFov(0.5) == 70);
    CHECK(apex::sliderValueToFov(0.0) == 30);
    CHECK(apex::sliderValueToFov(1.0) == 110);
    for (int fov = 30; fov <= 110; ++fov) {  // every value survives the round trip
        CHECK(apex::sliderValueToFov(apex::fovToSliderValue(fov)) == fov);
    }
}

void test_format_language_string() {
    CHECK(apex::formatLanguageString("%s: %s", {"FOV", "Normal"}) == "FOV: Normal");
    CHECK(apex::formatLanguageString("%2$s then %1$s", {"a", "b"}) == "b then a");
    CHECK(apex::formatLanguageString("100%% sure", {}) == "100% sure");
    CHECK(apex::formatLanguageString("no placeholders", {"x"}) == "no placeholders");
    CHECK(apex::formatLanguageString("%s", {}) == "");
}

void test_slider() {
    // 150px wide slider at x=59: the handle travels over the middle 142px (4px margins).
    apex::SliderWidget slider(59, 29, 150, 20, 0.5, [](double v) { return std::to_string(v); });
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

std::shared_ptr<apex::PcmSound> makeSound(int rate, int channels, std::size_t frames, float value) {
    auto s = std::make_shared<apex::PcmSound>();
    s->sampleRate = rate;
    s->channels = channels;
    s->samples.assign(frames * static_cast<std::size_t>(channels), value);
    return s;
}

void test_audio_mixer() {
    using apex::AudioEngine;
    AudioEngine engine(AudioEngine::Mode::Offline, 48000);
    CHECK(engine.available() && engine.sampleRate() == 48000);
    std::vector<float> out(2 * 480);

    // A mono sound plays on both channels at its volume.
    engine.playSound(makeSound(48000, 1, 1000, 0.8f), 0.5f);
    engine.render(out.data(), 480);
    CHECK(near(out[0], 0.4f) && near(out[1], 0.4f) && near(out[2 * 479], 0.4f));
    CHECK(near(engine.takePeak(), 0.4f));
    CHECK(near(engine.takePeak(), 0.0f));  // the peak resets when read

    // 1000 frames at 48 kHz: still playing after 480, finished after 1000.
    engine.render(out.data(), 480);
    CHECK(near(out[2 * 479], 0.4f));
    engine.render(out.data(), 480);  // frames 960..1000 play, then silence
    CHECK(near(out[2 * 39], 0.4f) && near(out[2 * 41], 0.0f));
    engine.render(out.data(), 480);
    CHECK(near(out[0], 0.0f));

    // Two voices add up and the output is clipped to [-1, 1].
    engine.playSound(makeSound(48000, 1, 500, 0.9f), 1.0f);
    engine.playSound(makeSound(48000, 1, 500, 0.9f), 1.0f);
    engine.render(out.data(), 480);
    CHECK(near(out[0], 1.0f));
    engine.render(out.data(), 480);  // let the last 20 frames of both voices finish

    // Stereo sounds keep their channels; pitch 2 plays twice as fast.
    auto stereo = std::make_shared<apex::PcmSound>();
    stereo->sampleRate = 48000;
    stereo->channels = 2;
    for (int i = 0; i < 400; ++i) {
        stereo->samples.push_back(0.2f);
        stereo->samples.push_back(-0.6f);
    }
    engine.playSound(stereo, 1.0f, 2.0f);
    engine.render(out.data(), 480);
    CHECK(near(out[0], 0.2f) && near(out[1], -0.6f));
    CHECK(near(out[2 * 150], 0.2f));  // 150 output frames = 300 source frames: still playing
    CHECK(near(out[2 * 250], 0.0f));  // 250 output frames > 200: finished
    CHECK(engine.stats().soundsStarted == 4);

    // A sample rate mismatch resamples: 300 frames at 24 kHz last 600 frames at 48 kHz.
    engine.playSound(makeSound(24000, 1, 300, 0.5f), 1.0f);
    engine.render(out.data(), 480);
    CHECK(near(out[2 * 479], 0.5f));  // output frame 479 is still inside the sound
    engine.render(out.data(), 480);
    CHECK(near(out[2 * 119], 0.5f) && near(out[2 * 121], 0.0f));  // ends at output frame 600
}

void test_sound_registry() {
    const auto registry = apex::SoundRegistry::parse(R"({
      "ui.button.click": {"sounds": ["random/click_stereo"]},
      "music.menu": {"sounds": [
        {"name": "music/game/ebb", "stream": true, "volume": 0.4, "weight": 2},
        {"name": "music/menu/mutation", "stream": true}]},
      "redirect": {"sounds": [{"name": "ui.button.click", "type": "event", "volume": 0.5, "pitch": 2.0}]},
      "loop_a": {"sounds": [{"name": "loop_b", "type": "event"}]},
      "loop_b": {"sounds": [{"name": "loop_a", "type": "event"}]},
      "empty": {"sounds": []},
      "broken": 7
    })");
    CHECK(registry.size() == 6);  // "broken" is not an event
    std::mt19937 rng(1);

    const auto click = registry.pick("ui.button.click", rng);
    CHECK(click && click->name == "random/click_stereo" && click->volume == 1.0f && !click->stream);

    // Redirects follow the other event and multiply volume and pitch.
    const auto redirected = registry.pick("redirect", rng);
    CHECK(redirected && redirected->name == "random/click_stereo");
    CHECK(redirected && near(redirected->volume, 0.5f) && near(redirected->pitch, 2.0f));

    CHECK(!registry.pick("empty", rng).has_value());
    CHECK(!registry.pick("nope", rng).has_value());
    CHECK(!registry.pick("loop_a", rng).has_value());  // redirect cycles terminate

    // Weights: "ebb" (weight 2, volume 0.4) is picked about twice as often as "mutation".
    int ebb = 0;
    const int rolls = 3000;
    for (int i = 0; i < rolls; ++i) {
        const auto m = registry.pick("music.menu", rng);
        CHECK(m && m->stream);
        if (m && m->name == "music/game/ebb") {
            ++ebb;
            CHECK(near(m->volume, 0.4f));
        }
    }
    CHECK(ebb > rolls * 60 / 100 && ebb < rolls * 73 / 100);  // expected 66.7%
}

void test_sound_system_and_music() {
    namespace fs = std::filesystem;
    const fs::path empty = fs::temp_directory_path() / "apex_empty_assets";
    fs::create_directories(empty);
    const apex::ResourceManager resources(empty);
    apex::AudioEngine engine(apex::AudioEngine::Mode::Offline, 48000);
    apex::GameOptions options;
    apex::SoundSystem sounds(resources, engine, options, 1);

    // Volume math: clamp(instance) * clamp(category slider * master slider).
    options.setVolume(apex::SoundSource::Master, 0.5);
    options.setVolume(apex::SoundSource::Ui, 0.5);
    CHECK(near(sounds.calculateVolume(0.25f, apex::SoundSource::Ui), 0.25f * 0.25f));
    CHECK(near(sounds.calculateVolume(1.0f, apex::SoundSource::Master), 0.5f));  // master alone
    CHECK(near(sounds.calculateVolume(4.0f, apex::SoundSource::Music), 0.5f));   // instance clamps to 1
    options.setVolume(apex::SoundSource::Master, 0.0);
    CHECK(sounds.calculateVolume(1.0f, apex::SoundSource::Music) == 0.0f);
    options.setVolume(apex::SoundSource::Master, 1.0);
    options.setVolume(apex::SoundSource::Ui, 1.0);

    // Preview sounds by category (master, music and voice have none).
    CHECK(std::string(apex::SoundSystem::previewEvent(apex::SoundSource::Weather)) ==
          "entity.lightning_bolt.thunder");
    CHECK(std::string(apex::SoundSystem::previewEvent(apex::SoundSource::Ui)) == "ui.button.click");
    CHECK(std::string(apex::SoundSystem::previewEvent(apex::SoundSource::Master)).empty());
    CHECK(std::string(apex::SoundSystem::previewEvent(apex::SoundSource::Voice)).empty());
    CHECK(std::string(apex::soundSourceLangKey(apex::SoundSource::Records)) == "soundCategory.record");

    // Music scheduling (with CONSTANT frequency so the gaps are deterministic). There are no
    // sounds in the empty asset folder, so every attempt fails - the schedule is what is tested.
    options.musicFrequency = apex::MusicFrequency::Constant;
    apex::MusicManager music(sounds, options, 7);
    for (int i = 0; i < 99; ++i) music.tick();
    CHECK(!music.hasCurrentTrack());      // still counting down from 100 ticks (5 s)
    music.tick();                         // tick 100: the first track is started
    CHECK(music.hasCurrentTrack());
    music.tick();                         // it never started, so the game schedules the next try
    CHECK(!music.hasCurrentTrack() && music.nextSongDelay() == 99);
    for (int i = 0; i < 98; ++i) music.tick();
    CHECK(!music.hasCurrentTrack());
    music.tick();                         // 100 ticks after the failure: tries again
    CHECK(music.hasCurrentTrack());

    // update() converts real time into 50 ms ticks and never catches up by more than a second.
    apex::MusicManager music2(sounds, options, 9);
    music2.update(0.4f);
    music2.update(0.4f);
    CHECK(music2.nextSongDelay() == apex::MusicManager::kStartingDelay);  // 0.8 ticks: none yet
    music2.update(0.4f);                                                   // 1.2 ticks: one tick
    CHECK(music2.nextSongDelay() == apex::MusicManager::kStartingDelay - 1);
    music2.update(1000.0f);                                                // capped at 20 ticks
    CHECK(music2.nextSongDelay() == apex::MusicManager::kStartingDelay - 21);

    // Default frequency: the gap after a track is a uniform 20..600 ticks.
    options.musicFrequency = apex::MusicFrequency::Default;
    apex::MusicManager music3(sounds, options, 11);
    int lo = 1 << 30;
    int hi = 0;
    for (int i = 0; i < 500; ++i) {
        const int d = music3.nextMenuDelay();
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    CHECK(lo >= 20 && hi <= 600 && lo < 100 && hi > 500);
    fs::remove_all(empty);
}

void test_now_playing_toast() {
    using apex::NowPlayingToast;
    NowPlayingToast toast;
    CHECK(toast.visiblePortion(0.0) == 0.0 && !toast.active());
    toast.show("fingerspit - Ebb", 1000.0);
    CHECK(toast.active());
    CHECK(near(static_cast<float>(toast.visiblePortion(1000.0)), 0.0f));    // just started
    CHECK(near(static_cast<float>(toast.visiblePortion(1300.0)), 0.25f));   // halfway: eased (0.5^2)
    CHECK(near(static_cast<float>(toast.visiblePortion(1600.0)), 1.0f));    // slide-in done
    CHECK(near(static_cast<float>(toast.visiblePortion(6500.0)), 1.0f));    // 5 s hold
    CHECK(near(static_cast<float>(toast.visiblePortion(6900.0)), 0.75f));   // sliding out: 1 - 0.5^2
    CHECK(near(static_cast<float>(toast.visiblePortion(7300.0)), 0.0f));    // gone
    toast.hide();
    CHECK(toast.visiblePortion(3000.0) == 0.0);

    // The notes cycle white -> light grey -> ... : at tick 0 it is the (fixed) white.
    CHECK(NowPlayingToast::noteColor(0.0) == 0xFFE6E6E6u);
    CHECK(NowPlayingToast::noteColor(0.0) != NowPlayingToast::noteColor(15 * 25.0));
    CHECK(NowPlayingToast::noteColor(12 * 30 * 25.0) == NowPlayingToast::noteColor(0.0));  // 9 s cycle
}

// ---- helpers to build zips in memory ----------------------------------------------------------
void put16(std::vector<unsigned char>& b, unsigned v) {
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}
void put32(std::vector<unsigned char>& b, unsigned v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

struct ZipFile {
    std::string name;
    std::string data;
    bool deflateStored = false;  // method 8 with a single "stored" deflate block
};

std::vector<unsigned char> makeZip(const std::vector<ZipFile>& files) {
    std::vector<unsigned char> out;
    std::vector<unsigned> offsets;
    std::vector<std::vector<unsigned char>> payloads;
    for (const ZipFile& f : files) {
        std::vector<unsigned char> payload;
        if (f.deflateStored) {
            payload.push_back(0x01);  // BFINAL=1, BTYPE=00 (stored)
            put16(payload, static_cast<unsigned>(f.data.size()));
            put16(payload, static_cast<unsigned>(~f.data.size()) & 0xFFFF);
        }
        payload.insert(payload.end(), f.data.begin(), f.data.end());
        payloads.push_back(payload);

        offsets.push_back(static_cast<unsigned>(out.size()));
        put32(out, 0x04034b50);
        put16(out, 20); put16(out, 0); put16(out, f.deflateStored ? 8 : 0);
        put16(out, 0); put16(out, 0); put32(out, 0);
        put32(out, static_cast<unsigned>(payload.size())); put32(out, static_cast<unsigned>(f.data.size()));
        put16(out, static_cast<unsigned>(f.name.size())); put16(out, 0);
        out.insert(out.end(), f.name.begin(), f.name.end());
        out.insert(out.end(), payload.begin(), payload.end());
    }
    const auto centralStart = static_cast<unsigned>(out.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        put32(out, 0x02014b50);
        put16(out, 20); put16(out, 20); put16(out, 0); put16(out, files[i].deflateStored ? 8 : 0);
        put16(out, 0); put16(out, 0); put32(out, 0);
        put32(out, static_cast<unsigned>(payloads[i].size())); put32(out, static_cast<unsigned>(files[i].data.size()));
        put16(out, static_cast<unsigned>(files[i].name.size())); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, 0); put32(out, offsets[i]);
        out.insert(out.end(), files[i].name.begin(), files[i].name.end());
    }
    const auto centralSize = static_cast<unsigned>(out.size()) - centralStart;
    put32(out, 0x06054b50);
    put16(out, 0); put16(out, 0); put16(out, static_cast<unsigned>(files.size()));
    put16(out, static_cast<unsigned>(files.size())); put32(out, centralSize); put32(out, centralStart);
    put16(out, 0);
    return out;
}

void writeBytes(const std::filesystem::path& file, const std::vector<unsigned char>& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void test_zip_archive() {
    const auto zip = apex::ZipArchive::fromBytes(makeZip({
        {"pack.mcmeta", R"({"pack":{"description":"hi"}})", false},
        {"assets/x.txt", "deflated payload", true},
    }));
    CHECK(zip.has_value());
    if (!zip) return;
    CHECK(zip->entryCount() == 2 && zip->contains("pack.mcmeta") && !zip->contains("nope"));
    CHECK(zip->readText("pack.mcmeta") == R"({"pack":{"description":"hi"}})");
    CHECK(zip->readText("assets/x.txt") == "deflated payload");  // method 8 goes through the inflater
    CHECK(!zip->read("missing").has_value());
    CHECK(!apex::ZipArchive::fromBytes({1, 2, 3}).has_value());   // not a zip
}

void writeText(const std::filesystem::path& file, const std::string& text) {
    writeBytes(file, std::vector<unsigned char>(text.begin(), text.end()));
}

// --- tiny NBT writer for the tests (big-endian, like the real format) ---
void nbtBE(std::vector<unsigned char>& out, unsigned long long v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) out.push_back(static_cast<unsigned char>(v >> (8 * i)));
}
void nbtName(std::vector<unsigned char>& out, const std::string& s) {
    nbtBE(out, s.size(), 2);
    out.insert(out.end(), s.begin(), s.end());
}
void nbtHead(std::vector<unsigned char>& out, int type, const std::string& name) {
    out.push_back(static_cast<unsigned char>(type));
    nbtName(out, name);
}

// A gzip stream around `data` using one stored deflate block (the CRC is not checked by the reader).
std::vector<unsigned char> gzipStored(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> out = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff, 0x01};
    out.push_back(static_cast<unsigned char>(data.size() & 0xFF));
    out.push_back(static_cast<unsigned char>(data.size() >> 8));
    out.push_back(static_cast<unsigned char>(~data.size() & 0xFF));
    out.push_back(static_cast<unsigned char>((~data.size() >> 8) & 0xFF));
    out.insert(out.end(), data.begin(), data.end());
    for (int i = 0; i < 4; ++i) out.push_back(0);  // CRC32 (unchecked)
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(data.size() >> (8 * i)));
    return out;
}

void test_nbt() {
    std::vector<unsigned char> nbt;
    nbtHead(nbt, 10, "");                                          // root compound
    nbtHead(nbt, 10, "Data");
    nbtHead(nbt, 8, "LevelName"); nbtName(nbt, "My World");
    nbtHead(nbt, 3, "GameType"); nbtBE(nbt, 1, 4);
    nbtHead(nbt, 1, "hardcore"); nbtBE(nbt, 1, 1);
    nbtHead(nbt, 4, "LastPlayed"); nbtBE(nbt, 1700000000123ULL, 8);
    nbtHead(nbt, 5, "SpawnAngle"); nbtBE(nbt, 0x3FC00000u, 4);       // float 1.5
    nbtHead(nbt, 7, "Bytes"); nbtBE(nbt, 2, 4); nbt.push_back(0xFF); nbt.push_back(0x05);
    nbtHead(nbt, 9, "List"); nbt.push_back(3); nbtBE(nbt, 2, 4); nbtBE(nbt, 7, 4); nbtBE(nbt, 8, 4);
    nbtHead(nbt, 10, "Version");
    nbtHead(nbt, 8, "Name"); nbtName(nbt, "26.3");
    nbt.push_back(0);                                              // end Version
    nbt.push_back(0);                                              // end Data
    nbt.push_back(0);                                              // end root

    for (const auto& bytes : {nbt, gzipStored(nbt)}) {            // raw and gzip-compressed
        const auto root = apex::parseNbt(bytes);
        CHECK(root.has_value());
        if (!root) continue;
        const apex::NbtTag* data = root->get("Data");
        CHECK(data != nullptr);
        if (data == nullptr) continue;
        CHECK(data->getString("LevelName") == "My World" && data->getString("nope", "dflt") == "dflt");
        CHECK(data->getInt("GameType") == 1 && data->getBool("hardcore") && !data->getBool("missing"));
        CHECK(data->getInt("LastPlayed") == 1700000000123LL && data->getInt("absent", 42) == 42);
        CHECK(near(static_cast<float>(data->getDouble("SpawnAngle")), 1.5f));
        const apex::NbtTag* bytesTag = data->get("Bytes");
        CHECK(bytesTag != nullptr && bytesTag->numbers.size() == 2 && bytesTag->numbers[0] == -1 && bytesTag->numbers[1] == 5);
        const apex::NbtTag* list = data->get("List");
        CHECK(list != nullptr && list->items.size() == 2 && list->items[1].integer == 8);
        CHECK(data->get("Version") != nullptr && data->get("Version")->getString("Name") == "26.3");
        CHECK(data->getString("GameType", "x") == "x");           // wrong type falls back
    }

    // Corrupt input must fail cleanly: truncation, a huge list count, a bad gzip body.
    auto truncated = nbt;
    truncated.resize(truncated.size() - 6);
    CHECK(!apex::parseNbt(truncated).has_value());
    std::vector<unsigned char> hugeList;
    nbtHead(hugeList, 10, ""); nbtHead(hugeList, 9, "L"); hugeList.push_back(3); nbtBE(hugeList, 0x7FFFFFFFu, 4);
    CHECK(!apex::parseNbt(hugeList).has_value());
    auto badGzip = gzipStored(nbt);
    badGzip[badGzip.size() - 1] ^= 0x55;  // wrong ISIZE
    CHECK(!apex::parseNbt(badGzip).has_value());
    CHECK(!apex::parseNbt({}).has_value());
}

void test_options_file() {
    namespace fs = std::filesystem;
    apex::OptionsFile f = apex::OptionsFile::parse("fov:90\nname:a:b\r\nflag:true\nratio:0.25\nbroken line\n:nokey\nbad:12x\n");
    CHECK(f.getInt("fov", 70) == 90 && f.getInt("missing", 7) == 7 && f.getInt("bad", 5) == 5);
    CHECK(f.getString("name") == "a:b");                      // only the first ':' splits
    CHECK(f.getBool("flag", false) && !f.getBool("absent", false) && f.getBool("bad", true));
    CHECK(near(static_cast<float>(f.getDouble("ratio", 0.0)), 0.25f) && f.getDouble("bad", 1.5) == 1.5);

    f.set("fov", 100);
    f.set("music", 0.5);
    f.set("hc", true);
    const fs::path file = fs::temp_directory_path() / "apex_options_test" / "o.txt";
    std::error_code ignored;
    fs::remove_all(file.parent_path(), ignored);
    CHECK(f.save(file));                                        // creates the folder, replaces atomically
    CHECK(f.save(file));                                        // saving again over an existing file works
    const apex::OptionsFile back = apex::OptionsFile::load(file);
    CHECK(back.getInt("fov", 0) == 100 && back.getBool("hc", false) && near(static_cast<float>(back.getDouble("music", 0)), 0.5f));
    CHECK(back.getString("name") == "a:b");                     // unknown/unused keys survive a round trip
    CHECK(!fs::exists(file.string() + ".tmp"));
    CHECK(!apex::OptionsFile::load(file.parent_path() / "nope.txt").contains("fov"));
    fs::remove_all(file.parent_path(), ignored);
}

// Writes a world folder whose level.dat holds the given `Data` fields (gzip NBT) plus a session.lock.
void makeWorld(const std::filesystem::path& saves, const std::string& id, const std::vector<unsigned char>& dataFields,
               bool lock = true) {
    std::vector<unsigned char> nbt;
    nbtHead(nbt, 10, "");
    nbtHead(nbt, 10, "Data");
    nbt.insert(nbt.end(), dataFields.begin(), dataFields.end());
    nbt.push_back(0);  // end Data
    nbt.push_back(0);  // end root
    writeBytes(saves / id / "level.dat", gzipStored(nbt));
    if (lock) writeBytes(saves / id / "session.lock", {1, 2});
}

// The two examples from docs/UI.md, kept here so the documentation cannot go stale: if the library's
// API changes they stop compiling.
class GreetingScreen : public apex::Screen {
public:
    using Screen::Screen;

protected:
    void init() override {
        addCenteredLabel(text("greeting.title"), width_ / 2, 40);
        auto row = apex::layout::centeredRow(width_, 100, {apex::metrics::kButtonWidth, apex::metrics::kButtonWidth}, 8);
        addButton(text("gui.yes"), row[0], [this] { onClose(); });
        addButton(text("gui.no"), row[1], [this] { onClose(); });
        addLockedButton(text("greeting.locked"), apex::layout::Column::centered(width_, 140, 200).next());
        addEditBox({apex::layout::centeredX(width_, 200), 170, 200, 20}, text("greeting.hint"));
        addSlider({apex::layout::centeredX(width_, 200), 200, 200, 20}, 0.5, [](double v) { return std::to_string(v); }, nullptr);
    }
};

class ExampleListScreen : public apex::ListScreen {
public:
    explicit ExampleListScreen(apex::ScreenNavigator& n) : ListScreen(n, "my.title") {}

protected:
    void addOptions() override {
        addHeader(text("my.section"));
        addPair(makeToggle("my.fast", fast_, kSmallWidth, [this](bool on) { fast_ = on; }),
                makeChoice("my.mode", {"my.mode.a", "my.mode.b"}, mode_, kSmallWidth, [this](int i) { mode_ = i; }));
        addBig(makeIntSlider(1, 10, level_, kBigWidth, [this](int v) { return optionLabel("my.level", std::to_string(v)); },
                             [this](int v) { level_ = v; }));
        addPair(withTooltip(makeSlider(0.5, kSmallWidth, [this](double v) { return percentLabel("my.opacity", v); }, nullptr),
                            "my.opacity.tooltip"),
                makeLink("my.more", kSmallWidth, [] { return std::unique_ptr<apex::Screen>(); }));
    }
    bool fast_ = false;
    int mode_ = 0;
    int level_ = 5;
};
static_assert(!std::is_abstract_v<GreetingScreen> && !std::is_abstract_v<ExampleListScreen>,
              "the documented example screens must be complete");

void test_ui_library_layout() {
    using apex::Rect;
    namespace layout = apex::layout;
    // Everything is positioned with truncating integer math, like the game's layout code.
    CHECK(layout::centeredX(427, 200) == 113 && layout::centeredY(240, 21) == 109);
    const auto row = layout::centeredRow(427, 100, {150, 150}, 8);
    CHECK(row.size() == 2 && (row[0] == Rect{59, 100, 150, 20}) && (row[1] == Rect{217, 100, 150, 20}));
    const auto footer = layout::footerRow(427, 240, 2, 150);  // Accessibility's Guide | Done
    CHECK((footer[0] == Rect{59, 213, 150, 20}) && (footer[1] == Rect{217, 213, 150, 20}));
    CHECK((layout::gridCell(427, 37, 0, 0) == Rect{58, 37, 150, 20}) && (layout::gridCell(427, 37, 1, 2) == Rect{218, 87, 150, 20}));

    auto column = layout::Column::centered(427, 108, 200, 20, 4);  // like the title screen's buttons
    CHECK((column.next() == Rect{113, 108, 200, 20}) && (column.next() == Rect{113, 132, 200, 20}));
    column.skip(12);
    CHECK((column.next() == Rect{113, 168, 200, 20}));

    // The list screen shell: title at y=12, list between the 33px strips, Done at H-27.
    const auto l = apex::computeListScreenLayout(427, 240, 100);
    CHECK((l.title == Rect{163, 12, 100, 9}) && (l.list == Rect{0, 33, 427, 174}) && (l.done == Rect{113, 213, 200, 20}));
}

void test_languages() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "apex_lang_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const fs::path assets = base / "assets";
    writeText(assets / "minecraft" / "lang" / "en_us.json", R"({"a":"Alpha","b":"Beta","c":"Gamma"})");
    writeText(assets / "minecraft" / "lang" / "de_de.json", R"j({"a":"Alfa","c":"Gamma (de)"})j");
    writeText(assets / "minecraft" / "lang" / "empty.json", "not json");
    writeText(assets / "pack.mcmeta",
              R"({"language":{"fr_fr":{"name":"Francais","region":"France","bidirectional":false},)"
              R"("de_de":{"name":"Deutsch","region":"Deutschland","bidirectional":false},)"
              R"("ar_sa":{"name":"Arabic","region":"Saudi","bidirectional":true}}})");

    const apex::ResourceManager resources(assets);
    const apex::Language english = apex::Language::load(resources, "en_us");
    CHECK(english.get("a") == "Alpha" && english.get("missing") == "missing");
    // Another language goes on top of English: its lines win, the rest stays English.
    const apex::Language german = apex::Language::load(resources, "de_de");
    CHECK(german.get("a") == "Alfa" && german.get("b") == "Beta" && german.get("c") == "Gamma (de)");
    bool threw = false;
    try { (void)apex::Language::load(resources, "xx_xx"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // a language with no file is an error the caller can handle
    threw = false;
    try { (void)apex::Language::load(resources, "empty"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // The list is ordered by code, not by name, and carries name, region and direction.
    const auto list = apex::loadLanguageList(resources);
    CHECK(list.size() == 3 && list[0].code == "ar_sa" && list[1].code == "de_de" && list[2].code == "fr_fr");
    CHECK(list[1].display() == "Deutsch (Deutschland)" && list[0].rtl && !list[1].rtl);
    fs::remove_all(assets / "pack.mcmeta", ignored);
    const auto fallback = apex::loadLanguageList(resources);  // no list: English only, like the game
    CHECK(fallback.size() == 1 && fallback[0].code == "en_us" && fallback[0].display() == "English (US)");

    apex::LanguageInfo korean{"ko_kr", "Korean", "South Korea", false};
    CHECK(apex::languageMatchesFilter(korean, "") && apex::languageMatchesFilter(korean, "KOR") &&
          apex::languageMatchesFilter(korean, "south") && !apex::languageMatchesFilter(korean, "japan"));
    fs::remove_all(base, ignored);
}

// The Graphics API setting: OpenGL or Vulkan (no "Default"), kept by name, with the old numeric option migrated.
void test_graphics_api_setting() {
    using apex::GraphicsApi;
    CHECK(apex::graphicsApiId(GraphicsApi::OpenGL) == "opengl" && apex::graphicsApiId(GraphicsApi::Vulkan) == "vulkan");
    CHECK(apex::parseGraphicsApi("vulkan") == GraphicsApi::Vulkan && apex::parseGraphicsApi("opengl", GraphicsApi::Vulkan) == GraphicsApi::OpenGL);
    CHECK(apex::parseGraphicsApi("default") == GraphicsApi::OpenGL && apex::parseGraphicsApi("nonsense", GraphicsApi::Vulkan) == GraphicsApi::Vulkan);  // not a choice any more
    CHECK(apex::graphicsApiTranslationKey(GraphicsApi::OpenGL) == "options.graphicsApi.opengl" && apex::graphicsApiTranslationKey(GraphicsApi::Vulkan) == "options.graphicsApi.vulkan");
    CHECK(apex::kGraphicsApiCount == 2);
    // Only OpenGL can draw the game today: asking for Vulkan is remembered but OpenGL is what runs, and the reason is said.
    std::string note = "stale";
    CHECK(apex::graphicsApiRenders(GraphicsApi::OpenGL) && !apex::graphicsApiRenders(GraphicsApi::Vulkan));
    CHECK(apex::effectiveGraphicsApi(GraphicsApi::OpenGL, &note) == GraphicsApi::OpenGL && note.empty());
    CHECK(apex::effectiveGraphicsApi(GraphicsApi::Vulkan, &note) == GraphicsApi::OpenGL && note.find("vulkan") != std::string::npos);
    CHECK(apex::effectiveGraphicsApi(GraphicsApi::Vulkan) == GraphicsApi::OpenGL);  // (also without a note to fill)
    CHECK(apex::graphicsApiTooltip(GraphicsApi::Vulkan).find("OpenGL") != std::string::npos && apex::graphicsApiTooltip(GraphicsApi::OpenGL).find("Not available") == std::string::npos);

    // The option: OpenGL by default, saved by name, read back.
    apex::GameOptions defaults;
    CHECK(defaults.graphicsApi == GraphicsApi::OpenGL);
    apex::GameOptions chosen;
    chosen.graphicsApi = GraphicsApi::Vulkan;
    apex::OptionsFile file;
    chosen.save(file);
    CHECK(file.getString("graphicsBackend") == "vulkan");
    apex::GameOptions back;
    back.load(apex::OptionsFile::parse(file.toString()));
    CHECK(back.graphicsApi == GraphicsApi::Vulkan);
    // An options file from before: the number had a "Default" (0), OpenGL (1) and Vulkan (2). Only Vulkan stays a different choice.
    for (const auto& [number, expected] : {std::pair<int, GraphicsApi>{0, GraphicsApi::OpenGL}, {1, GraphicsApi::OpenGL}, {2, GraphicsApi::Vulkan}}) {
        apex::GameOptions old;
        old.load(apex::OptionsFile::parse("graphicsApi:" + std::to_string(number) + "\n"));
        CHECK(old.graphicsApi == expected);
    }
    // The new key wins over a stale old one.
    apex::GameOptions both;
    both.load(apex::OptionsFile::parse("graphicsApi:2\ngraphicsBackend:opengl\n"));
    CHECK(both.graphicsApi == GraphicsApi::OpenGL);
}

// The game mode switcher (F3 + F4): the game's slot order and geometry, F4 cycling and the mode that is picked when it opens.
void test_gamemode_switcher_logic() {
    using apex::GameModeSwitcherScreen;
    // GameModeIcon order: Creative, Survival, Adventure, Spectator; the game types are 0 survival, 1 creative, 2 adventure, 3 spectator.
    CHECK(GameModeSwitcherScreen::gameTypeOfSlot(0) == 1 && GameModeSwitcherScreen::gameTypeOfSlot(1) == 0 && GameModeSwitcherScreen::gameTypeOfSlot(2) == 2 && GameModeSwitcherScreen::gameTypeOfSlot(3) == 3);
    for (int type = 0; type < 4; ++type) CHECK(GameModeSwitcherScreen::gameTypeOfSlot(GameModeSwitcherScreen::slotOfGameType(type)) == type);
    // F4 goes round: Creative -> Survival -> Adventure -> Spectator -> Creative.
    CHECK(GameModeSwitcherScreen::nextMode(0) == 1 && GameModeSwitcherScreen::nextMode(1) == 2 && GameModeSwitcherScreen::nextMode(2) == 3 && GameModeSwitcherScreen::nextMode(3) == 0);
    // What is picked when it opens: the mode before this one if there was one, else Survival from Creative and Creative from the rest.
    CHECK(GameModeSwitcherScreen::defaultSlot(/*current*/ 1, /*previous*/ -1) == GameModeSwitcherScreen::slotOfGameType(0));
    CHECK(GameModeSwitcherScreen::defaultSlot(0, -1) == GameModeSwitcherScreen::slotOfGameType(1));
    CHECK(GameModeSwitcherScreen::defaultSlot(3, -1) == GameModeSwitcherScreen::slotOfGameType(1) && GameModeSwitcherScreen::defaultSlot(2, -1) == GameModeSwitcherScreen::slotOfGameType(1));
    CHECK(GameModeSwitcherScreen::defaultSlot(1, 3) == GameModeSwitcherScreen::slotOfGameType(3) && GameModeSwitcherScreen::defaultSlot(0, 2) == GameModeSwitcherScreen::slotOfGameType(2));
    // Geometry: four 26 x 26 slots 31 apart (119 wide), centred, their top 31 above the middle of the screen.
    CHECK(GameModeSwitcherScreen::kAllSlotsWidth == 119 && GameModeSwitcherScreen::kSlotArea == 26 && GameModeSwitcherScreen::kSlotAreaPadded == 31);
    CHECK(GameModeSwitcherScreen::slotLeft(427, 0) == 427 / 2 - 59 && GameModeSwitcherScreen::slotLeft(427, 3) == 427 / 2 - 59 + 93);
    CHECK(GameModeSwitcherScreen::slotTop(240) == 120 - 31);
}

// The compressed chunk cache: a chunk that goes in comes back with the same blocks and biomes, in much fewer bytes.
void test_chunk_cache() {
    using namespace apex;
    const auto sameBlocks = [](const Chunk& a, const Chunk& b) {
        for (int y = kMinY; y < kMaxY; y += 3) {
            for (int z = 0; z < 16; z += 3) {
                for (int x = 0; x < 16; x += 3) {
                    if (a.at(x, y, z) != b.at(x, y, z)) return false;
                }
            }
        }
        return true;
    };
    auto chunk = std::make_shared<Chunk>();
    // A solid section, a nibble-packed one (up to 16 kinds), a byte-packed one (up to 256) and a wide one (more kinds than that).
    {
        SectionBuilder solid;
        solid.fill(static_cast<Block>(7));
        chunk->sections[0] = solid.build();
        SectionBuilder few;
        for (int i = 0; i < 4096; ++i) few.set(i & 15, i >> 8, (i >> 4) & 15, static_cast<Block>(1 + (i * 7) % 5));
        chunk->sections[3] = few.build();
        SectionBuilder some;
        for (int i = 0; i < 4096; ++i) some.set(i & 15, i >> 8, (i >> 4) & 15, static_cast<Block>(1 + (i * 13) % 60));
        chunk->sections[5] = some.build();
        SectionBuilder many;
        for (int i = 0; i < 4096; ++i) many.set(i & 15, i >> 8, (i >> 4) & 15, static_cast<Block>(1 + (i * 37) % 700));
        chunk->sections[9] = many.build();
        // (the rest stays air)
    }
    auto grid = std::make_shared<BiomeGrid>();
    for (std::size_t i = 0; i < grid->ids.size(); ++i) grid->ids[i] = static_cast<std::uint8_t>(i % 11);
    chunk->biomes = grid;

    const std::vector<unsigned char> packed = ChunkCache::compress(*chunk);
    CHECK(!packed.empty());
    const auto back = ChunkCache::decompress(packed);
    CHECK(back != nullptr);
    if (!back) return;
    CHECK(sameBlocks(*chunk, *back) && back->sections[1] == nullptr && back->sections[0] != nullptr && back->sections[9] != nullptr);
    CHECK(back->biomes != nullptr && back->biomes->ids == grid->ids);
    CHECK(packed.size() < chunk->memoryBytes());  // smaller than the chunk as it sits in memory
    CHECK(ChunkCache::decompress({1, 2, 3}) == nullptr && ChunkCache::decompress({}) == nullptr);  // garbage is refused, not trusted

    // The cache: hold, pack (what a worker does), take back; forgotten once taken.
    ChunkCache cache(1u << 20);
    const ChunkPos here{4, -2};
    CHECK(!cache.contains(here) && cache.take(here) == nullptr);
    cache.hold(here, chunk);
    CHECK(cache.contains(here) && cache.count() == 1 && cache.packedBytes() == 0);  // not packed yet
    const auto early = cache.take(here);  // taken before it was packed: the chunk itself, minus its light
    CHECK(early != nullptr && sameBlocks(*chunk, *early) && !cache.contains(here));
    cache.hold(here, chunk);
    cache.pack(here);
    CHECK(cache.contains(here) && cache.packedBytes() > 0 && cache.packedBytes() < 40000);
    const auto restored = cache.take(here);
    CHECK(restored != nullptr && sameBlocks(*chunk, *restored) && !cache.contains(here) && cache.packedBytes() == 0);

    // Over the budget the chunks furthest from the player are forgotten first.
    ChunkCache small(packed.size() * 3 + packed.size() / 2);
    for (int i = 0; i < 6; ++i) {
        small.hold({i * 10, 0}, chunk);
        small.pack({i * 10, 0});
    }
    CHECK(small.count() == 6);
    small.trim({0, 0});
    CHECK(small.count() == 3 && small.contains({0, 0}) && small.contains({10, 0}) && small.contains({20, 0}) && !small.contains({50, 0}));
}

void test_game_options_round_trip() {
    apex::GameOptions defaults;
    CHECK(defaults.modelParts[0] && defaults.mainHandRight && defaults.chatColors && defaults.bobView);
    CHECK(defaults.textBackgroundOpacity == 0.5 && defaults.chatHeightUnfocused == 0.4375 && defaults.glintStrength == 0.75);
    CHECK(defaults.sharePresence == apex::PresenceSharing::All && !defaults.highContrast && defaults.menuBackgroundBlurriness == 5);

    apex::GameOptions changed;
    changed.fov = 95;
    changed.chatVisibility = apex::ChatVisibility::Hidden;
    changed.modelParts[6] = false;
    changed.mainHandRight = false;
    changed.chatDelay = 1.5;
    changed.panoramaSpeed = 0.25;
    changed.resourcePacks = {"vanilla", "high_contrast", "file/My Pack.zip"};
    changed.audioDevice = "Speakers (Realtek)";
    changed.setVolume(apex::SoundSource::Music, 0.3);
    apex::OptionsFile file;
    changed.save(file);

    apex::GameOptions back;
    back.load(apex::OptionsFile::parse(file.toString()));
    CHECK(back.fov == 95 && back.chatVisibility == apex::ChatVisibility::Hidden && !back.modelParts[6] && back.modelParts[0]);
    CHECK(!back.mainHandRight && near(static_cast<float>(back.chatDelay), 1.5f) && near(static_cast<float>(back.panoramaSpeed), 0.25f));
    CHECK((back.resourcePacks == std::vector<std::string>{"vanilla", "high_contrast", "file/My Pack.zip"}));
    CHECK(back.audioDevice == "Speakers (Realtek)" && near(static_cast<float>(back.volume(apex::SoundSource::Music)), 0.3f));

    // Damaged values keep the defaults instead of breaking anything.
    apex::GameOptions safe;
    safe.load(apex::OptionsFile::parse("fov:9999\nchatVisibility:7\nchatDelay:-3\nglintSpeed:abc\nmainHand:left\nlang:\n"));
    CHECK(safe.language == "en_us");  // an empty language code keeps English
    CHECK(safe.fov == 110 && safe.chatVisibility == apex::ChatVisibility::Full && safe.chatDelay == 0.0);
    CHECK(safe.glintSpeed == 0.5 && !safe.mainHandRight);
}

void test_select_world_layout() {
    using apex::Rect;
    // 854x480 at scale 2 -> 427x240 GUI units, "Select World" is 60 wide.
    const auto l = apex::computeSelectWorldLayout(427, 240, 60);
    CHECK((l.search == Rect{113, 21, 200, 20}));
    CHECK(l.title.y == 8 && l.title.x == 113 + (200 - 60) / 2);
    CHECK((l.list == Rect{0, 49, 427, 131}));
    CHECK((l.play == Rect{59, 188, 150, 20}) && (l.create == Rect{217, 188, 150, 20}));
    CHECK((l.edit == Rect{59, 212, 71, 20}) && (l.remove == Rect{138, 212, 71, 20}));
    CHECK((l.recreate == Rect{217, 212, 71, 20}) && (l.back == Rect{296, 212, 71, 20}));

    apex::LevelSummary s;
    s.id = "My_World-2";
    s.levelName = "Castle Build";
    CHECK(apex::worldMatchesFilter(s, "") && apex::worldMatchesFilter(s, "castle") && apex::worldMatchesFilter(s, "WORLD"));
    CHECK(!apex::worldMatchesFilter(s, "nether"));  // matches the name or the folder name, nothing else
}

void test_level_summary() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "apex_worlds_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const fs::path saves = base / "saves";

    const auto str = [](const std::string& k, const std::string& v) { std::vector<unsigned char> o; nbtHead(o, 8, k); nbtName(o, v); return o; };
    const auto num = [](const std::string& k, int type, unsigned long long v, int bytes) { std::vector<unsigned char> o; nbtHead(o, type, k); nbtBE(o, v, bytes); return o; };
    const auto cat = [](std::vector<std::vector<unsigned char>> parts) { std::vector<unsigned char> o; for (auto& p : parts) o.insert(o.end(), p.begin(), p.end()); return o; };
    const auto versionTag = [&](const std::string& name, int id, const std::string& series) {
        std::vector<unsigned char> o;
        nbtHead(o, 10, "Version");
        for (const auto& part : {str("Name", name), num("Id", 3, static_cast<unsigned>(id), 4), str("Series", series)}) o.insert(o.end(), part.begin(), part.end());
        o.push_back(0);
        return o;
    };

    makeWorld(saves, "Alpha", cat({num("version", 3, 19133, 4), str("LevelName", "Alpha World"), num("LastPlayed", 4, 2000, 8),
                                   num("GameType", 3, 1, 4), num("allowCommands", 1, 1, 1), num("DataVersion", 3, 5023, 4),
                                   versionTag("26.3", 5023, "main")}));
    std::vector<unsigned char> difficulty;  // difficulty_settings { difficulty, hardcore, locked }
    nbtHead(difficulty, 10, "difficulty_settings");
    for (const auto& part : {str("difficulty", "hard"), num("hardcore", 1, 1, 1), num("locked", 1, 0, 1)}) difficulty.insert(difficulty.end(), part.begin(), part.end());
    difficulty.push_back(0);
    makeWorld(saves, "beta", cat({num("version", 3, 19133, 4), num("LastPlayed", 4, 1000, 8), num("DataVersion", 3, 5030, 4), difficulty,
                                  versionTag("26.9", 5030, "main")}));
    makeWorld(saves, "other", cat({num("version", 3, 19133, 4), num("LastPlayed", 4, 500, 8), num("DataVersion", 3, 5023, 4), versionTag("1.0", 5023, "legacy")}));
    makeWorld(saves, "nolock", cat({num("version", 3, 19133, 4)}), /*lock=*/false);       // dropped: no session.lock
    writeBytes(saves / "broken" / "level.dat", {9, 9, 9});                                // corrupted
    writeBytes(saves / "broken" / "session.lock", {1});
    fs::create_directories(saves / "not-a-world");                                        // no level.dat: not a candidate

    const auto worlds = apex::scanWorlds(saves);
    CHECK(worlds.has_value());
    if (!worlds) return;
    CHECK(worlds->size() == 4);  // nolock and not-a-world are not listed
    if (worlds->size() == 4) {
        // Newest first: the corrupted folder uses its file time (now), then 2000, 1000, 500.
        CHECK((*worlds)[0].kind == apex::WorldKind::Corrupted && (*worlds)[0].id == "broken");
        const apex::LevelSummary& a = (*worlds)[1];
        const apex::LevelSummary& b = (*worlds)[2];
        const apex::LevelSummary& other = (*worlds)[3];
        CHECK(a.id == "Alpha" && a.displayName() == "Alpha World" && a.gameType == 1 && a.allowCommands && !a.hardcore);
        CHECK(!a.requiresFileFixing && a.backup == apex::BackupStatus::None && a.compatible && a.primaryActionActive() && a.canEdit());
        CHECK(b.id == "beta" && b.displayName() == "beta" && b.hardcore && b.backup == apex::BackupStatus::Downgrade);  // empty LevelName -> folder
        CHECK(other.id == "other" && !other.compatible && !other.primaryActionActive() && !other.canEdit() && other.canDelete());
        CHECK(a.primaryActionKey() == "selectWorld.select" && (*worlds)[0].primaryActionKey() == "recover_world.button");
    }

    // The three info lines, composed from the language file exactly as the game does.
    const fs::path assets = base / "assets";
    writeText(assets / "minecraft" / "lang" / "en_us.json",
              R"({"gameMode.creative":"Creative Mode","gameMode.hardcore":"Hardcore Mode","selectWorld.commands":"Commands",)"
              R"("selectWorld.version":"Version:","selectWorld.incompatible.info":"Incompatible version: %s"})");
    const apex::ResourceManager resources(assets);
    const apex::Language lang = apex::Language::load(resources, "en_us");
    const auto joined = [&](const apex::LevelSummary& s) { std::string t; for (const auto& seg : apex::levelInfoSegments(s, lang)) t += seg.text; return t; };
    if (worlds->size() == 4) {
        CHECK(joined((*worlds)[1]) == "Creative Mode,Commands,Version: 26.3");   // no space after the commas
        const auto hardSegments = apex::levelInfoSegments((*worlds)[2], lang);
        CHECK(hardSegments.size() == 3 && hardSegments[0].text == "Hardcore Mode" && hardSegments[0].color == 0xFFFF0000u);
        CHECK(hardSegments.back().text == "26.9" && hardSegments.back().color == 0xFFFF5555u);  // downgrade: red version
        CHECK(joined((*worlds)[3]) == "Incompatible version: 1.0");
        CHECK(apex::levelIdLine((*worlds)[1]).rfind("Alpha (", 0) == 0);          // "folder (date)"
    }
    apex::LevelSummary symlink;
    symlink.kind = apex::WorldKind::Symlink;
    symlink.id = "x";
    symlink.lastPlayed = -1;
    CHECK(apex::levelIdLine(symlink) == "x");

    // Deleting removes only a real folder directly inside saves/.
    CHECK(!apex::deleteWorld(saves, "../assets") && !apex::deleteWorld(saves, "") && !apex::deleteWorld(saves, "..") && fs::exists(assets));
    CHECK(!apex::deleteWorld(saves, "Alpha/level.dat") && fs::exists(saves / "Alpha" / "level.dat"));
    CHECK(apex::deleteWorld(saves, "Alpha") && !fs::exists(saves / "Alpha"));
    CHECK(!apex::deleteWorld(saves, "Alpha"));                                     // already gone
    CHECK(apex::scanWorlds(saves)->size() == 3);
    fs::remove_all(base, ignored);
}

void test_resource_layers() {
    namespace fs = std::filesystem;
    using apex::ResourceLocation;
    const fs::path base = fs::temp_directory_path() / "apex_layers_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);

    // The game's own assets, a folder pack and a zip pack (opened from a real file, so it is mapped).
    writeText(base / "assets" / "minecraft" / "a.txt", "base-a");
    writeText(base / "assets" / "minecraft" / "b.txt", "base-b");
    writeText(base / "assets" / "minecraft" / "lang" / "en_us.json", R"({"k1":"base1","k2":"base2"})");
    writeText(base / "assets" / "minecraft" / "sounds.json",
              R"({"ev.x":{"sounds":["base_x"]},"ev.y":{"sounds":["base_y"]}})");
    writeText(base / "p1" / "assets" / "minecraft" / "a.txt", "p1-a");
    writeText(base / "p1" / "assets" / "minecraft" / "lang" / "en_us.json", R"({"k2":"p1-2","k3":"p1-3"})");
    writeText(base / "p1" / "assets" / "minecraft" / "sounds.json", R"({"ev.x":{"sounds":["p1_x"]}})");
    writeBytes(base / "p2.zip", makeZip({
        {"assets/minecraft/a.txt", "zip-a", true},
        {"assets\\minecraft\\c.txt", "zip-c", false},  // some Windows tools write backslashes
        {"assets/minecraft/sounds.json", R"({"ev.y":{"replace":true,"sounds":["z_y"]}})", false},
    }));

    {
        const auto p1 = apex::makeDirectoryPack(base / "p1");
        auto zip = apex::ZipArchive::open(base / "p2.zip");
        CHECK(zip.has_value());
        if (!zip) return;
        const auto p2 = apex::makeZipPack(std::move(*zip));
        const ResourceLocation a("minecraft", "a.txt");
        const ResourceLocation b("minecraft", "b.txt");
        const ResourceLocation c("minecraft", "c.txt");

        apex::ResourceManager resources(base / "assets");
        CHECK(resources.readText(a) == "base-a" && !resources.exists(c));
        CHECK(resources.providers(a) == std::vector<const void*>{nullptr});

        resources.setPacks({p1});
        CHECK(resources.readText(a) == "p1-a" && resources.readText(b) == "base-b");  // pack beats base
        CHECK((resources.providers(a) == std::vector<const void*>{nullptr, p1.get()}));
        CHECK((resources.providers(b) == std::vector<const void*>{nullptr}));

        resources.setPacks({p1, p2});                                  // later in the list wins
        CHECK(resources.readText(a) == "zip-a" && resources.readText(c) == "zip-c");
        CHECK((resources.readAllText(a) == std::vector<std::string>{"base-a", "p1-a", "zip-a"}));
        CHECK(resources.exists(c));

        resources.setPacks({p2, p1});                                  // so the order matters
        CHECK(resources.readText(a) == "p1-a");

        // A copy and withPacks() see the packs they were given; the original is unaffected.
        resources.setPacks({p1});
        const apex::ResourceManager copy(resources);
        const apex::ResourceManager other = resources.withPacks({p1, p2});
        CHECK(copy.readText(a) == "p1-a" && other.readText(a) == "zip-a" && resources.readText(a) == "p1-a");
        // Unchanged resources have identical providers, so a reload can skip them.
        CHECK(resources.providers(b) == other.providers(b) && resources.providers(a) != other.providers(a));

        // Language files merge key by key; later packs override single keys.
        resources.setPacks({p1});
        const apex::Language lang = apex::Language::load(resources, "en_us");
        CHECK(lang.get("k1") == "base1" && lang.get("k2") == "p1-2" && lang.get("k3") == "p1-3");

        // sounds.json: a pack adds sounds to an event, or replaces them with "replace": true.
        resources.setPacks({p1, p2});
        const apex::SoundRegistry sounds = apex::SoundRegistry::load(resources);
        const apex::SoundEvent* x = sounds.find("ev.x");
        const apex::SoundEvent* y = sounds.find("ev.y");
        CHECK(x != nullptr && x->entries.size() == 2 && x->entries[0].name == "base_x" &&
              x->entries[1].name == "p1_x");
        CHECK(y != nullptr && y->entries.size() == 1 && y->entries[0].name == "z_y");
    }
    fs::remove_all(base, ignored);  // after the zip is unmapped (Windows cannot delete a mapped file)
}

void test_pack_repository_and_model() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "apex_pack_test";
    fs::remove_all(base);
    const fs::path assets = base / "game" / "assets";
    const fs::path gameDir = base / "dotminecraft";
    fs::create_directories(assets / "minecraft" / "lang");
    {
        std::ofstream lang(assets / "minecraft" / "lang" / "en_us.json");
        lang << R"json({"pack.nameAndSource":"%s (%s)","pack.source.builtin":"built-in",
                    "resourcePack.vanilla.name":"Default","resourcePack.vanilla.description":"Vanilla look",
                    "resourcePack.high_contrast.name":"High Contrast","resourcePack.programmer_art.name":"Programmer Art"})json";
    }
    writeBytes(assets / "minecraft" / "resourcepacks" / "high_contrast.zip",
               makeZip({{"pack.mcmeta", R"({"pack":{"description":"Contrast","min_format":29,"max_format":98}})"}}));
    writeBytes(assets / "minecraft" / "resourcepacks" / "programmer_art.zip",
               makeZip({{"pack.mcmeta", R"({"pack":{"description":"Classic","min_format":97,"max_format":98}})"}}));
    // A folder pack made for an older version, in <game dir>/resourcepacks.
    fs::create_directories(gameDir / "resourcepacks" / "Old Pack");
    std::ofstream(gameDir / "resourcepacks" / "Old Pack" / "pack.mcmeta")
        << R"({"pack":{"pack_format":5,"description":{"text":"Ancient"}}})";

    const apex::ResourceManager resources(assets);
    const apex::Language language = apex::Language::load(resources, "en_us");
    const auto repo = apex::PackRepository::load(language, assets, gameDir);

    // Packs are keyed by id: file/..., high_contrast, programmer_art, vanilla.
    std::vector<std::string> ids;
    for (const auto& [id, pack] : repo.packs()) ids.push_back(id);
    CHECK((ids == std::vector<std::string>{"file/Old Pack", "high_contrast", "programmer_art", "vanilla"}));
    CHECK(repo.find("vanilla")->description == "Vanilla look (built-in)" && repo.find("vanilla")->required);
    CHECK(repo.find("high_contrast")->description == "Contrast (built-in)");
    CHECK(repo.find("high_contrast")->compatibility == apex::PackCompatibility::Compatible);
    CHECK(repo.find("file/Old Pack")->compatibility == apex::PackCompatibility::TooOld);
    CHECK(repo.find("file/Old Pack")->description == "Ancient");
    CHECK(apex::PackRepository::checkFormat(R"({"pack":{"pack_format":97}})") == apex::PackCompatibility::Compatible);
    CHECK(apex::PackRepository::checkFormat(R"({"pack":{"min_format":98,"max_format":99}})") == apex::PackCompatibility::TooNew);
    CHECK(apex::PackRepository::checkFormat("{}") == apex::PackCompatibility::Broken);

    // Selection rules.
    apex::PackSelectionModel model(repo, {});
    CHECK((model.selected() == std::vector<std::string>{"vanilla"}));
    CHECK((model.available() == std::vector<std::string>{"file/Old Pack", "high_contrast", "programmer_art"}));
    CHECK(!model.canUnselect("vanilla") && !model.canMoveUp("vanilla") && !model.canMoveDown("vanilla"));
    CHECK(model.canSelect("high_contrast") && !model.canSelect("vanilla"));

    model.select("high_contrast");  // new packs go on top of Selected
    model.select("programmer_art");
    CHECK((model.selected() == std::vector<std::string>{"programmer_art", "high_contrast", "vanilla"}));
    CHECK(!model.canMoveUp("programmer_art") && model.canMoveDown("programmer_art"));
    CHECK(model.canMoveUp("vanilla") && !model.canMoveDown("vanilla"));
    model.moveDown("programmer_art");
    CHECK((model.selected() == std::vector<std::string>{"high_contrast", "programmer_art", "vanilla"}));
    CHECK((model.commitIds() == std::vector<std::string>{"vanilla", "programmer_art", "high_contrast"}));

    model.unselect("high_contrast");  // unselected packs go on top of Available
    CHECK((model.available().front() == "high_contrast"));
    model.unselect("vanilla");        // required: ignored
    CHECK(model.isSelected("vanilla"));

    // Restoring saved ids (lowest priority first) rebuilds the same order.
    apex::PackSelectionModel restored(repo, {"vanilla", "programmer_art", "high_contrast"});
    CHECK((restored.selected() == std::vector<std::string>{"high_contrast", "programmer_art", "vanilla"}));
    CHECK((restored.available() == std::vector<std::string>{"file/Old Pack"}));
    fs::remove_all(base);
}

class FixedEntry : public apex::ScrollList::Entry {
public:
    explicit FixedEntry(int h) : Entry(h) {}
    void render(apex::GuiGraphics&, const apex::Rect&, int, int, const apex::ScrollList::RowState&) override {}
};

void test_scroll_list() {
    using apex::ScrollList;
    // The Music & Sounds list at 427x240: 9 rows of 25 in a 174px viewport.
    ScrollList list(0, 33, 427, 174, 25, 310, ScrollList::ScrollbarPlacement::RightOfRow, false, false);
    for (int i = 0; i < 9; ++i) list.addEntry<FixedEntry>(25);
    CHECK(list.contentHeight() == 229 && list.maxScroll() == 55 && list.scrollable());
    CHECK(list.scrollerHeight() == 132);          // (int)(174*174/229)
    CHECK(list.rowLeft() == 58 && list.scrollbarX() == 376);
    CHECK(list.entryRect(0).y == 35 && list.entryRect(1).y == 60 && list.entryRect(1).width == 310);

    CHECK(list.mouseScrolled(100.0, 100.0, -1.0));  // one notch down = 12px (25 / 2)
    CHECK(near(static_cast<float>(list.scrollAmount()), 12.0f) && list.entryRect(0).y == 23);
    CHECK(!list.mouseScrolled(100.0, 5.0, -1.0));    // over the header: not ours
    for (int i = 0; i < 10; ++i) (void)list.mouseScrolled(100.0, 100.0, -1.0);
    CHECK(near(static_cast<float>(list.scrollAmount()), 55.0f));  // clamped to the maximum
    for (int i = 0; i < 10; ++i) (void)list.mouseScrolled(100.0, 100.0, 1.0);
    CHECK(list.scrollAmount() == 0.0);

    // Everything fits: no scrolling, no scrollbar.
    ScrollList roomy(0, 33, 427, 300, 25, 310, ScrollList::ScrollbarPlacement::RightOfRow, false, false);
    for (int i = 0; i < 9; ++i) roomy.addEntry<FixedEntry>(25);
    CHECK(!roomy.scrollable() && roomy.maxScroll() == 0);

    // Pack lists: 36px rows, rows narrow by 6px while the scrollbar shows, bar inside the right edge.
    ScrollList packs(-2, 49, 200, 158, 36, 0, ScrollList::ScrollbarPlacement::InsideRight, true, true);
    for (int i = 0; i < 4; ++i) packs.addEntry<FixedEntry>(36);
    CHECK(!packs.scrollable() && packs.entryWidth() == 196 && packs.rowLeft() == 0);
    packs.addEntry<FixedEntry>(36);
    CHECK(packs.scrollable() && packs.entryWidth() == 190 && packs.scrollbarX() == -2 + 200 - 6);
    CHECK(packs.mouseClicked(50.0, 60.0, 0) && packs.selectedIndex() == 0);   // a plain click selects
    CHECK(!packs.mouseClicked(50.0, 5.0, 0));                                  // outside the list
}

void test_cycle_button() {
    bool shift = false;
    int changes = 0;
    apex::CycleButton button(0, 0, 150, 20, 3, 0, [](int i) { return "value " + std::to_string(i); },
                             [&] { return shift; });
    button.onChange = [&](int) { ++changes; };
    CHECK(button.index() == 0);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 1);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 2);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 0);  // wraps
    shift = true;
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 2);  // shift+click goes back
    CHECK(changes == 4);
    CHECK(!button.mouseClicked(10.0, 10.0, 1));                        // right click does nothing
    CHECK(button.clicksOnPress());

    // One value: pressing does nothing at all (no change, no callback, no click sound).
    changes = 0;
    button.setValues(1, 0);
    CHECK(button.valueCount() == 1 && button.index() == 0 && !button.clicksOnPress());
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 0 && changes == 0);

    // The list can grow later (the device list arrives in the background); the index is kept.
    shift = false;
    button.setValues(3, 2);
    CHECK(button.valueCount() == 3 && button.index() == 2 && button.clicksOnPress() && changes == 0);
    CHECK(button.mouseClicked(10.0, 10.0, 0) && button.index() == 0 && changes == 1);
    button.setValues(2, 9);  // an out-of-range index is clamped
    CHECK(button.index() == 1);
}

void test_audio_devices() {
    using apex::AudioDevice;
    const AudioDevice speakers{"Speakers (Realtek)", true};
    const AudioDevice headset{"Headset (Plantronics)", false};

    // Nothing to choose between with zero or one device: only "System Default" is offered.
    CHECK((apex::buildDeviceChoices({}) == std::vector<std::string>{""}));
    CHECK((apex::buildDeviceChoices({speakers}) == std::vector<std::string>{""}));
    // Two or more: System Default first, then every device in the OS order.
    CHECK((apex::buildDeviceChoices({speakers, headset}) ==
           std::vector<std::string>{"", "Speakers (Realtek)", "Headset (Plantronics)"}));

    // An offline engine keeps an injected list and bumps its version so screens notice.
    apex::AudioEngine engine(apex::AudioEngine::Mode::Offline, 48000);
    CHECK(engine.devices().empty());
    const unsigned version = engine.deviceListVersion();
    engine.setDevicesForTest({speakers, headset});
    CHECK(engine.devices().size() == 2 && engine.deviceListVersion() != version);
    engine.selectDevice("Headset (Plantronics)");
    CHECK(engine.deviceName() == "Headset (Plantronics)");
    engine.selectDevice("");
    CHECK(engine.deviceName() == "offline");
}

void test_splash_dates() {
    using apex::SplashManager;
    CHECK(SplashManager::specialSplash(12, 24) == "Merry X-mas!");
    CHECK(SplashManager::specialSplash(1, 1) == "Happy new year!");
    CHECK(SplashManager::specialSplash(10, 31) == "OOoooOOOoooo! Spooky!");
    CHECK(!SplashManager::specialSplash(11, 12).has_value());
}

void test_widget_clicks() {
    int pressed = 0;
    apex::Button button("Singleplayer", 113, 108, 200, 20);
    button.onPress = [&] { ++pressed; };

    CHECK(button.mouseClicked(113.0, 108.0, 0));    // top-left pixel is inside
    CHECK(button.mouseClicked(312.9, 127.9, 0));    // bottom-right pixel is inside
    CHECK(!button.mouseClicked(313.0, 118.0, 0));   // right edge is exclusive
    CHECK(!button.mouseClicked(112.9, 118.0, 0));
    CHECK(!button.mouseClicked(200.0, 118.0, 1));   // only the left mouse button presses
    button.active = false;
    CHECK(!button.mouseClicked(200.0, 118.0, 0));   // disabled buttons ignore clicks
    CHECK(pressed == 2);

    apex::IconButton icon("icon/language", 15, 15, 203, 180, 20);
    CHECK(icon.width == 20 && icon.height == 20);
    CHECK(icon.contains(203.0, 180.0) && !icon.contains(223.0, 180.0));
}

void test_asset_index() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "apex_asset_index_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "indexes");
    fs::create_directories(dir / "objects" / "ab");

    const auto write = [](const fs::path& file, const std::string& text) {
        std::ofstream(file, std::ios::binary) << text;
    };
    // Older index lacks the asset; the highest-numbered one (not the lexically last) wins.
    write(dir / "indexes" / "9.json", R"({"objects":{}})");
    write(dir / "indexes" / "12.json",
          R"({"objects":{"icons/icon_16x16.png":{"hash":"abcdef0123","size":5}}})");
    write(dir / "objects" / "ab" / "abcdef0123", "hello");

    const auto index = apex::AssetIndex::open(dir);
    CHECK(index.has_value());
    if (index) {
        CHECK(index->indexId() == "12");
        CHECK(index->contains("icons/icon_16x16.png"));
        CHECK(!index->contains("icons/icon_32x32.png"));
        const auto bytes = index->read("icons/icon_16x16.png");
        CHECK(bytes && std::string(bytes->begin(), bytes->end()) == "hello");
        CHECK(!index->read("missing.png").has_value());
    }
    CHECK(!apex::AssetIndex::open(dir / "nope").has_value());
    fs::remove_all(dir);
}

void test_edition_badge() {
    // Wrong size: left alone.
    apex::Image tiny;
    tiny.width = 16;
    tiny.height = 16;
    tiny.rgba.assign(16 * 16 * 4, 0);
    CHECK(!apex::makeCppEditionBadge(tiny).has_value());

    // Right size but no lettering: left alone.
    apex::Image empty;
    empty.width = 512;
    empty.height = 64;
    empty.rgba.assign(512 * 64 * 4, 0);
    CHECK(!apex::makeCppEditionBadge(empty).has_value());

    // A stand-in with just the pixels the generator probes and samples: it runs and draws the plus.
    const auto set = [&](apex::Image& img, int x, int y, unsigned char v) {
        const std::size_t i = (static_cast<std::size_t>(y) * 512 + static_cast<std::size_t>(x)) * 4;
        img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = v;
        img.rgba[i + 3] = 255;
    };
    apex::Image fake = empty;
    for (const auto& p : {std::pair{215, 8}, std::pair{240, 8}, std::pair{212, 24}, std::pair{240, 38},
                          std::pair{230, 8}, std::pair{230, 4}, std::pair{230, 13}, std::pair{209, 8},
                          std::pair{249, 8}}) {
        set(fake, p.first, p.second, 208);
    }
    for (int row = 43; row <= 50; ++row) set(fake, 230, row, 60);
    set(fake, 242, 26, 0);
    const auto badge = apex::makeCppEditionBadge(fake);
    CHECK(badge.has_value());
    if (badge) {
        CHECK(badge->width == 512 && badge->height == 64);
        CHECK(badge->alphaAt(100, 23) == 255);  // centre of the first plus
    }

    // Missing colour samples (transparent E) must be rejected rather than draw invisible pluses.
    apex::Image hollow = fake;
    hollow.rgba[(static_cast<std::size_t>(4) * 512 + 230) * 4 + 3] = 0;
    CHECK(!apex::makeCppEditionBadge(hollow).has_value());
}

void test_text_util() {
    CHECK(apex::javaStringHash("hello") == 99162322);  // "hello".hashCode() in Java
    CHECK(apex::javaStringHash("") == 0);
    CHECK(apex::javaTrim("  hi \n") == "hi");
    const auto cps = apex::decodeUtf8("a\xC3\xA9\xE2\x82\xAC");  // a, é, euro sign
    CHECK(cps.size() == 3 && cps[1] == 0xE9 && cps[2] == 0x20AC);
}

void test_math() {
    CHECK(near(apex::clampedMap(0.75f, 0.5f, 1.0f, 0.0f, 1.0f), 0.5f));
    CHECK(near(apex::clampedMap(0.2f, 0.5f, 1.0f, 0.0f, 1.0f), 0.0f));
    CHECK(near(apex::clampedMap(2.0f, 0.5f, 1.0f, 0.0f, 1.0f), 1.0f));
    CHECK(near(apex::wrapDegrees(190.0f), -170.0f));
    CHECK(near(apex::wrapDegrees(-190.0f), 170.0f));
    CHECK(near(apex::wrapDegrees(180.0f), -180.0f));

    // translate -> rotate -> scale composition (post-multiplied), as the splash text uses it.
    apex::Pose2D pose;
    pose.translate(10.0f, 20.0f).rotate(apex::kPi / 2.0f).scale(2.0f);
    float x = 0.0f;
    float y = 0.0f;
    pose.apply(1.0f, 0.0f, x, y);  // (1,0) -> scaled (2,0) -> rotated 90deg (0,2) -> moved (10,22)
    CHECK(near(x, 10.0f) && near(y, 22.0f));
}

}  // namespace

void test_nbt_writer() {
    using apex::NbtTag;
    NbtTag inner = NbtTag::compound();
    inner.put("Name", NbtTag::ofString("26.3")).put("Id", NbtTag::ofInteger(NbtTag::Type::Int, 5023));
    NbtTag data = NbtTag::compound();
    data.put("LevelName", NbtTag::ofString("caf\xC3\xA9 \xF0\x9F\x8C\x8D"))  // an accent and an emoji (above U+FFFF)
        .put("Nul", NbtTag::ofString(std::string("a\0b", 3)))
        .put("Short", NbtTag::ofInteger(NbtTag::Type::Short, -2))
        .put("Hardcore", NbtTag::ofBool(true))
        .put("LastPlayed", NbtTag::ofInteger(NbtTag::Type::Long, 1700000000123LL))
        .put("Negative", NbtTag::ofInteger(NbtTag::Type::Int, -7))
        .put("Version", inner)
        .put("Tags", NbtTag::ofList({NbtTag::ofString("a"), NbtTag::ofString("b")}))
        .put("EmptyList", NbtTag::ofList({}));
    NbtTag root = NbtTag::compound();
    root.put("Data", data);

    const auto raw = apex::serializeNbt(root);
    for (const auto& bytes : {raw, apex::gzipStored(raw)}) {
        const auto parsed = apex::parseNbt(bytes);
        CHECK(parsed.has_value());
        if (!parsed) continue;
        const NbtTag* d = parsed->get("Data");
        CHECK(d != nullptr);
        if (d == nullptr) continue;
        CHECK(d->getString("LevelName") == "caf\xC3\xA9 \xF0\x9F\x8C\x8D");  // survives the modified UTF-8 detour
        CHECK(d->getString("Nul") == std::string("a\0b", 3));
        CHECK(d->getInt("Short") == -2 && d->getBool("Hardcore") && d->getInt("Negative") == -7);
        CHECK(d->getInt("LastPlayed") == 1700000000123LL);
        CHECK(d->get("Version") != nullptr && d->get("Version")->getInt("Id") == 5023);
        CHECK(d->get("Tags") != nullptr && d->get("Tags")->items.size() == 2 && d->get("Tags")->items[1].text == "b");
        CHECK(d->get("EmptyList") != nullptr && d->get("EmptyList")->items.empty());
    }
    // The emoji really is written as two three-byte surrogates (what Java expects), not four bytes.
    const std::vector<unsigned char> needle = {0xED, 0xA0, 0xBC, 0xED, 0xBC, 0x8D};  // U+1F30D as the surrogates D83C DF0D
    CHECK(std::search(raw.begin(), raw.end(), needle.begin(), needle.end()) != raw.end());

    // A payload larger than one stored deflate block (65535 bytes) still reads back.
    NbtTag big = NbtTag::compound();
    NbtTag longs;
    longs.type = NbtTag::Type::LongArray;
    for (int i = 0; i < 20000; ++i) longs.numbers.push_back(static_cast<std::int64_t>(i) * 1000003);  // 160 KB
    big.put("Longs", longs);
    const auto gz = apex::gzipStored(apex::serializeNbt(big));
    const auto back = apex::parseNbt(gz);
    CHECK(back.has_value() && back->get("Longs") != nullptr && back->get("Longs")->numbers.size() == 20000 &&
          back->get("Longs")->numbers[19999] == 19999LL * 1000003);
    const auto empty = apex::gzipStored({});
    CHECK(apex::gunzip(empty.data(), empty.size()).has_value());  // empty input is a valid gzip stream too
}

void test_create_world_layout() {
    using apex::Rect;
    // The spec's worked example: 854x480 at GUI scale 2 is 427x240.
    const auto l = apex::computeCreateWorldLayout(427, 240);
    CHECK(l.tabX == 28 && l.tabWidth == 124);
    CHECK((l.create == Rect{59, 214, 150, 20}) && (l.cancel == Rect{217, 214, 150, 20}));
    CHECK(l.footerSeparatorY == 205);
    CHECK((l.tabArea == Rect{0, 24, 427, 183}));
    CHECK((apex::createWorldTabGrid(l, 210, 117) == Rect{108, 35, 210, 117}));  // Game: name label at (109, 35)
    CHECK((apex::createWorldTabGrid(l, 310, 117) == Rect{58, 35, 310, 117}));   // World
    CHECK((apex::createWorldTabGrid(l, 210, 76) == Rect{108, 41, 210, 76}));    // More

    const auto big = apex::computeCreateWorldLayout(480, 270);
    CHECK(big.tabX == 54 && big.tabWidth == 124);
    CHECK((big.create == Rect{86, 244, 150, 20}) && (big.cancel.x == 244));
    const auto g = apex::createWorldTabGrid(big, 210, 117);
    CHECK(g.x == 135 && g.y == 40);

    const auto narrow = apex::computeCreateWorldLayout(300, 200);  // the tab bar shrinks below 400 + margins
    CHECK(narrow.tabX == 14 && narrow.tabWidth == 90);
}

void test_new_world() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "apex_new_world_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const fs::path saves = base / "saves";
    fs::create_directories(saves);

    using apex::findAvailableWorldFolder;
    CHECK(findAvailableWorldFolder(saves, "New World", "New World") == "New World");
    CHECK(findAvailableWorldFolder(saves, "   ", "New World") == "New World");   // blank: the default name
    CHECK(findAvailableWorldFolder(saves, "a:b?c.d", "x") == "a_b_c_d");         // illegal characters and dots
    CHECK(findAvailableWorldFolder(saves, "CON", "x") == "_CON_");               // a reserved Windows name
    CHECK(findAvailableWorldFolder(saves, "lpt3", "x") == "_lpt3_");
    CHECK(findAvailableWorldFolder(saves, "  Padded  ", "x") == "Padded");
    CHECK(findAvailableWorldFolder(saves, "World ()", "x") == "World");          // an empty counter makes the game fall back
    fs::create_directories(saves / "New World");
    CHECK(findAvailableWorldFolder(saves, "New World", "x") == "New World (1)");
    fs::create_directories(saves / "New World (1)");
    CHECK(findAvailableWorldFolder(saves, "New World", "x") == "New World (2)");
    CHECK(findAvailableWorldFolder(saves, "New World (1)", "x") == "New World (2)");  // keeps counting from the (1)
    CHECK(findAvailableWorldFolder(saves, "Other (7)", "x") == "Other (7)");          // a counter that is free is kept

    std::mt19937_64 rng(1);
    CHECK(apex::parseWorldSeed("42", rng) == 42 && apex::parseWorldSeed(" -7 ", rng) == -7 && apex::parseWorldSeed("+5", rng) == 5);
    CHECK(apex::parseWorldSeed("9223372036854775807", rng) == INT64_MAX);
    CHECK(apex::parseWorldSeed("-9223372036854775808", rng) == INT64_MIN);
    CHECK(apex::parseWorldSeed("9223372036854775808", rng) == apex::javaStringHash("9223372036854775808"));  // too big: hashed
    CHECK(apex::parseWorldSeed("abc", rng) == 96354 && apex::parseWorldSeed("-", rng) == apex::javaStringHash("-"));
    CHECK(apex::parseWorldSeed("", rng) != apex::parseWorldSeed("  ", rng));  // blank: random, and different each time

    // Creating a world makes a folder the Singleplayer list reads back.
    apex::NewWorldSettings s;
    s.name = "  Creative Test  ";
    s.mode = apex::NewWorldMode::Creative;
    std::string error;
    CHECK(apex::createWorldFolder(saves, "Creative Test", s, 1234, 5000, error));
    apex::NewWorldSettings hard;
    hard.name = "Hardcore Test";
    hard.mode = apex::NewWorldMode::Hardcore;
    hard.allowCommands = true;  // ignored: hardcore never has cheats
    CHECK(apex::createWorldFolder(saves, "Hardcore Test", hard, 1, 9000, error));
    CHECK(!apex::createWorldFolder(saves, "Hardcore Test", s, 1, 9000, error) && !error.empty());  // never reuses a folder

    const auto worlds = apex::scanWorlds(saves);
    CHECK(worlds.has_value());
    if (worlds) {
        const apex::LevelSummary* creative = nullptr;
        const apex::LevelSummary* hardcore = nullptr;
        for (const auto& w : *worlds) {
            if (w.id == "Creative Test") creative = &w;
            if (w.id == "Hardcore Test") hardcore = &w;
        }
        CHECK(creative != nullptr && hardcore != nullptr);
        if (creative != nullptr && hardcore != nullptr) {
            CHECK(creative->kind == apex::WorldKind::Normal && creative->levelName == "Creative Test");  // trimmed
            CHECK(creative->gameType == 1 && creative->allowCommands && !creative->hardcore);            // creative: cheats default on
            CHECK(creative->lastPlayed == 5000 && creative->versionName == "26.3" && creative->compatible);
            CHECK(!creative->requiresFileFixing && !creative->shouldBackup() && creative->primaryActionActive());
            CHECK(hardcore->hardcore && hardcore->gameType == 0 && !hardcore->allowCommands);
            CHECK((*worlds)[0].id == "Hardcore Test");  // newest first
        }
    }
    // The seed and options are in level.dat for the day a world can be generated from them.
    const auto bytes = [&] {
        std::ifstream in(saves / "Creative Test" / "level.dat", std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
    }();
    const auto root = apex::parseNbt(bytes);
    CHECK(root.has_value() && root->get("Data") != nullptr);
    if (root && root->get("Data") != nullptr) {
        const apex::NbtTag* gen = root->get("Data")->get("WorldGenSettings");
        CHECK(gen != nullptr && gen->getInt("seed") == 1234 && gen->getBool("generate_features") && !gen->getBool("bonus_chest"));
        CHECK(root->get("Data")->get("difficulty_settings")->getString("difficulty") == "normal");
    }
    fs::remove_all(base, ignored);
}

void test_base64() {
    using apex::base64Decode;
    using apex::base64Encode;
    const auto bytes = [](std::string_view s) { return std::vector<unsigned char>(s.begin(), s.end()); };
    CHECK(base64Encode(bytes("")) == "" && base64Encode(bytes("f")) == "Zg==" && base64Encode(bytes("fo")) == "Zm8=");
    CHECK(base64Encode(bytes("foo")) == "Zm9v" && base64Encode(bytes("foobar")) == "Zm9vYmFy");
    CHECK(base64Decode("Zm9vYmE=") == bytes("fooba") && base64Decode("Zm9v\nYmFy\n") == bytes("foobar"));  // line breaks are ignored
    CHECK(base64Decode("Zg") == bytes("f"));                                                              // padding is optional
    CHECK(!base64Decode("Zm9v!") && !base64Decode("Z") && !base64Decode("Zg==Zg=="));                     // junk, one symbol, data after padding
    std::vector<unsigned char> all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<unsigned char>(i));
    CHECK(base64Decode(base64Encode(all)) == all);
}

void test_server_address() {
    using apex::parseServerAddress;
    const auto a = parseServerAddress("play.example.com");
    CHECK(a && a->host == "play.example.com" && a->port == 25565);
    const auto b = parseServerAddress("play.example.com:25566");
    CHECK(b && b->host == "play.example.com" && b->port == 25566);
    const auto c = parseServerAddress("[::1]:1234");
    CHECK(c && c->host == "::1" && c->port == 1234);
    const auto d = parseServerAddress("[2001:db8::1]");
    CHECK(d && d->host == "2001:db8::1" && d->port == 25565);
    const auto e = parseServerAddress("2001:db8::1");  // several colons, no brackets: all host
    CHECK(e && e->host == "2001:db8::1" && e->port == 25565);
    const auto f = parseServerAddress("host:");  // an empty port means the default
    CHECK(f && f->host == "host" && f->port == 25565);
    CHECK(parseServerAddress("host:0")->port == 0 && parseServerAddress("host:65535")->port == 65535);
    CHECK(!parseServerAddress("") && !parseServerAddress(":25565") && !parseServerAddress("host:65536"));
    CHECK(!parseServerAddress("host:12a") && !parseServerAddress("[::1") && !parseServerAddress("[::1]x") && !parseServerAddress("[]"));
    CHECK(apex::isValidServerAddress("localhost") && !apex::isValidServerAddress(""));
}

void test_styled_text() {
    using apex::TextSpan;
    // Legacy formatting codes: a colour clears the styles, r resets, k is ignored.
    const auto legacy = apex::parseLegacyFormatting("plain \xC2\xA7" "aGreen \xC2\xA7" "lBold\xC2\xA7" "r done\xC2\xA7" "kx");
    CHECK(legacy.size() == 4);
    if (legacy.size() == 4) {
        CHECK(legacy[0].text == "plain " && !legacy[0].hasColor);
        CHECK(legacy[1].text == "Green " && legacy[1].hasColor && legacy[1].rgb == 0x55FF55 && !legacy[1].bold);
        CHECK(legacy[2].text == "Bold" && legacy[2].bold && legacy[2].rgb == 0x55FF55);
        CHECK(legacy[3].text == " donex" && !legacy[3].hasColor && !legacy[3].bold);
    }
    CHECK(apex::plainText(apex::parseLegacyFormatting("a\xC2\xA7zb")) == "a\xC2\xA7zb");  // not a code: kept as text

    // Chat components: a plain string, an object with extra (children inherit), an array, and rubbish.
    CHECK(apex::plainText(apex::parseChatComponentJson("\"Hello\"")) == "Hello");
    const auto obj = apex::parseChatComponentJson(R"({"text":"Hi ","color":"gold","extra":[{"text":"there","bold":true},"!"]})");
    CHECK(obj.size() == 3);  // the extras are siblings: "!" does not inherit the bold of "there"
    if (obj.size() == 3) {
        CHECK(obj[0].text == "Hi " && obj[0].rgb == 0xFFAA00 && !obj[0].bold);
        CHECK(obj[1].text == "there" && obj[1].rgb == 0xFFAA00 && obj[1].bold);  // inherits the colour, adds bold
        CHECK(obj[2].text == "!" && obj[2].rgb == 0xFFAA00 && !obj[2].bold);
    }
    const auto hexColor = apex::parseChatComponentJson(R"({"text":"x","color":"#12abEF"})");
    CHECK(hexColor.size() == 1 && hexColor[0].hasColor && hexColor[0].rgb == 0x12ABEF);
    CHECK(apex::plainText(apex::parseChatComponentJson(R"(["a",{"text":"b"},"c"])")) == "abc");
    CHECK(apex::plainText(apex::parseChatComponentJson(R"({"translate":"some.key"})")) == "some.key");
    CHECK(apex::parseChatComponentJson("{not json").empty() && apex::parseChatComponentJson("null").empty());
    std::string deep = "\"deep\"";
    for (int i = 0; i < 40; ++i) deep = "[" + deep + "]";
    CHECK(apex::parseChatComponentJson(deep).empty());  // deeper than 16 levels is dropped

    // Wrapping, with a font where every character is 6 wide (7 when bold).
    const apex::TextMeasure measure = [](std::string_view t, bool bold) { return static_cast<int>(t.size()) * (bold ? 7 : 6); };
    const auto styled = apex::parseLegacyFormatting("aaa bbb ccc ddd");
    const auto lines = apex::wrapStyledText(measure, styled, 40, 5);  // 6 characters per line
    CHECK(lines.size() == 4 && apex::plainText(lines[0]) == "aaa " && apex::plainText(lines[3]) == "ddd");
    CHECK(apex::wrapStyledText(measure, styled, 40, 2).size() == 2);  // extra lines are dropped
    const auto split = apex::wrapStyledText(measure, apex::parseLegacyFormatting("abcdefghijklmnop"), 36, 5);  // one 16 letter word
    CHECK(split.size() == 3 && apex::plainText(split[0]) == "abcdef" && apex::plainText(split[2]) == "mnop");
    const auto newline = apex::wrapStyledText(measure, apex::parseLegacyFormatting("one\ntwo"), 200, 5);
    CHECK(newline.size() == 2 && apex::plainText(newline[1]) == "two");
    const auto coloured = apex::wrapStyledText(measure, apex::parseLegacyFormatting("\xC2\xA7" "caa bb \xC2\xA7" "9cc"), 200, 5);
    CHECK(coloured.size() == 1 && coloured[0].size() == 2 && coloured[0][1].rgb == 0x5555FF);  // styles survive wrapping
}

void test_server_list() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "apex_servers_test";
    std::error_code ignored;
    fs::remove_all(dir, ignored);

    apex::ServerList missing(dir);
    missing.load();  // no file: an empty list, no error
    CHECK(missing.visible().empty() && missing.hiddenEntries().empty());

    apex::ServerList list(dir);
    const auto make = [](const char* name, const char* ip) {
        auto e = std::make_shared<apex::ServerEntry>();
        e->name = name;
        e->ip = ip;
        return e;
    };
    auto a = make("Alpha", "alpha.example.com");
    a->iconPng = {0x89, 'P', 'N', 'G', 1, 2, 3};
    a->packStatus = apex::ServerPackStatus::Enabled;
    auto b = make("Beta \xC3\xA9", "beta.example.com:25570");
    b->packStatus = apex::ServerPackStatus::Disabled;
    b->acceptedCodeOfConduct = -12345;
    list.add(a, false);
    list.add(b, false);
    list.add(make("Gamma", "gamma"), false);
    for (int i = 0; i < 20; ++i) list.add(make("Direct", ("direct" + std::to_string(i)).c_str()), true);
    CHECK(list.hiddenEntries().size() == apex::ServerList::kMaxHidden);   // the history keeps the newest 16
    CHECK(list.hiddenEntries().front()->ip == "direct19");                // newest first
    CHECK(list.save());

    // The file is plain NBT (not gzip), the way the game reads it.
    const auto readAll = [&](const char* name) {
        std::ifstream in(dir / name, std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
    };
    const auto raw = readAll("servers.dat");
    CHECK(raw.size() > 4 && raw[0] == 0x0A && !(raw[0] == 0x1f && raw[1] == 0x8b));
    CHECK(!fs::exists(dir / "servers.dat.tmp"));

    apex::ServerList loaded(dir);
    loaded.load();
    CHECK(loaded.visible().size() == 3 && loaded.hiddenEntries().size() == apex::ServerList::kMaxHidden);
    if (loaded.visible().size() == 3) {
        CHECK(loaded.visible()[0]->name == "Alpha" && loaded.visible()[0]->ip == "alpha.example.com");
        CHECK(loaded.visible()[0]->iconPng == a->iconPng && loaded.visible()[0]->packStatus == apex::ServerPackStatus::Enabled);
        CHECK(loaded.visible()[1]->name == "Beta \xC3\xA9" && loaded.visible()[1]->packStatus == apex::ServerPackStatus::Disabled);
        CHECK(loaded.visible()[1]->acceptedCodeOfConduct == -12345);
        CHECK(loaded.visible()[2]->packStatus == apex::ServerPackStatus::Prompt && loaded.visible()[2]->iconPng.empty());
    }
    CHECK(loaded.hiddenEntries().front()->ip == "direct19" && loaded.hiddenEntries().front()->hidden);

    // Reordering saves at once; lookups, unhide and remove behave like the game's.
    loaded.swap(0, 2);
    apex::ServerList reread(dir);
    reread.load();
    CHECK(reread.visible().size() == 3 && reread.visible()[0]->name == "Gamma" && reread.visible()[2]->name == "Alpha");
    CHECK(fs::exists(dir / "servers.dat_old"));                      // the previous file is kept
    CHECK(reread.find("alpha.example.com") != nullptr && reread.find("direct10") != nullptr && reread.find("nope") == nullptr);
    const auto unhidden = reread.unhide("direct10");
    CHECK(unhidden != nullptr && !unhidden->hidden && reread.visible().size() == 4 && reread.visible().back() == unhidden);
    CHECK(reread.hiddenEntries().size() == apex::ServerList::kMaxHidden - 1 && reread.unhide("direct10") == nullptr);
    reread.remove(reread.visible()[1]);
    CHECK(reread.visible().size() == 3);
    reread.swap(0, 99);  // out of range: nothing happens
    CHECK(reread.visible().size() == 3);

    // Damaged files never crash the reader.
    {
        std::ofstream out(dir / "servers.dat", std::ios::binary | std::ios::trunc);
        out << "this is not nbt";
    }
    apex::ServerList damaged(dir);
    damaged.load();
    CHECK(damaged.visible().empty());
    fs::remove_all(dir, ignored);
}

#ifdef _WIN32
// A stand-in server on 127.0.0.1 that answers one status ping (and records what the client sent), so the
// protocol code can be tested without touching any real network.
class FakeStatusServer {
public:
    explicit FakeStatusServer(std::string json, bool answerStatus = true, bool answerPong = true)
        : json_(std::move(json)), answerStatus_(answerStatus), answerPong_(answerPong) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listener_, 1);
        int length = sizeof(addr);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &length);
        port = ntohs(addr.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeStatusServer() {
        thread_.join();
        closesocket(listener_);
    }

    int port = 0;
    // What the client sent:
    int protocol = 0;
    std::string host;
    int hostPort = 0;
    int nextState = 0;
    bool gotStatusRequest = false;
    bool gotPingRequest = false;

private:
    bool readExact(SOCKET s, unsigned char* out, int n) {
        int got = 0;
        while (got < n) {
            const int r = recv(s, reinterpret_cast<char*>(out) + got, n - got, 0);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }
    bool readVarInt(SOCKET s, std::uint32_t& value) {
        value = 0;
        for (int i = 0; i < 5; ++i) {
            unsigned char b = 0;
            if (!readExact(s, &b, 1)) return false;
            value |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) return true;
        }
        return false;
    }
    static void putVarInt(std::vector<unsigned char>& out, std::uint32_t v) {
        do {
            unsigned char b = v & 0x7F;
            v >>= 7;
            if (v != 0) b |= 0x80;
            out.push_back(b);
        } while (v != 0);
    }
    bool readPacket(SOCKET s, std::uint32_t& id, std::vector<unsigned char>& rest) {
        std::uint32_t length = 0;
        if (!readVarInt(s, length) || length == 0 || length > 4096) return false;
        std::vector<unsigned char> body(length);
        if (!readExact(s, body.data(), static_cast<int>(length))) return false;
        std::size_t pos = 0;
        id = 0;
        for (int i = 0; i < 5 && pos < body.size(); ++i) {
            const unsigned char b = body[pos++];
            id |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) break;
        }
        rest.assign(body.begin() + static_cast<std::ptrdiff_t>(pos), body.end());
        return true;
    }
    void send(SOCKET s, const std::vector<unsigned char>& body) {
        std::vector<unsigned char> framed;
        putVarInt(framed, static_cast<std::uint32_t>(body.size()));
        framed.insert(framed.end(), body.begin(), body.end());
        ::send(s, reinterpret_cast<const char*>(framed.data()), static_cast<int>(framed.size()), 0);
    }

    void serve() {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener_, &readable);
        timeval limit{5, 0};
        if (select(0, &readable, nullptr, nullptr, &limit) <= 0) return;  // nobody connected
        const SOCKET client = accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        const DWORD ms = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));

        std::uint32_t id = 0;
        std::vector<unsigned char> rest;
        if (readPacket(client, id, rest) && id == 0 && !rest.empty()) {  // the handshake
            std::size_t pos = 0;
            const auto varint = [&] {
                std::uint32_t v = 0;
                for (int i = 0; i < 5 && pos < rest.size(); ++i) {
                    const unsigned char b = rest[pos++];
                    v |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
                    if ((b & 0x80) == 0) break;
                }
                return v;
            };
            protocol = static_cast<int>(varint());
            const std::uint32_t hostLength = varint();
            if (pos + hostLength + 3 <= rest.size()) {
                host.assign(reinterpret_cast<const char*>(rest.data()) + pos, hostLength);
                pos += hostLength;
                hostPort = (rest[pos] << 8) | rest[pos + 1];
                pos += 2;
                nextState = static_cast<int>(varint());
            }
        }
        if (readPacket(client, id, rest) && id == 0) gotStatusRequest = true;
        if (answerStatus_ && gotStatusRequest) {
            std::vector<unsigned char> body;
            putVarInt(body, 0);
            putVarInt(body, static_cast<std::uint32_t>(json_.size()));
            body.insert(body.end(), json_.begin(), json_.end());
            send(client, body);
            if (readPacket(client, id, rest) && id == 1 && rest.size() == 8) {
                gotPingRequest = true;
                if (answerPong_) {
                    std::vector<unsigned char> pong;
                    putVarInt(pong, 1);
                    pong.insert(pong.end(), rest.begin(), rest.end());
                    send(client, pong);
                }
            }
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    std::string json_;
    bool answerStatus_;
    bool answerPong_;
    SOCKET listener_ = INVALID_SOCKET;
    std::thread thread_;
};
#endif

void test_server_ping() {
#ifdef _WIN32
    const std::string json =
        R"({"version":{"name":"26.3","protocol":777},"players":{"max":20,"online":3,"sample":[)"
        R"({"id":"069a79f4-44e9-4726-a5be-fca90e38aaf5","name":"Notch"},)"
        R"({"id":"00000000-0000-0000-0000-000000000000","name":"Anonymous Player"}]},)"
        R"("description":{"text":"Hi ","color":"gold","extra":[{"text":"there","bold":true}]},)"
        R"("favicon":"data:image/png;base64,AAEC"})";
    {
        FakeStatusServer server(json);
        const auto r = apex::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == apex::PingState::Successful && r.message == apex::PingMessage::None);
        CHECK(apex::plainText(r.motd) == "Hi there" && r.motd.size() == 2 && r.motd[1].bold);
        CHECK(r.hasPlayers && r.playersOnline == 3 && r.playersMax == 20 && r.playersNotListed == 1);
        CHECK(r.playerSample.size() == 2 && r.playerSample[0] == "Notch" && r.playerSample[1].empty());  // "" = anonymous
        CHECK(r.versionName == "26.3" && !r.versionIsOld && r.protocol == 777);
        CHECK(r.faviconPng == std::vector<unsigned char>({0, 1, 2}));
        CHECK(r.pingMillis >= 0 && r.pingMillis < 2000);
        // The client spoke the status protocol and nothing else.
        CHECK(server.protocol == 777 && server.host == "127.0.0.1" && server.hostPort == server.port && server.nextState == 1);
        CHECK(server.gotStatusRequest && server.gotPingRequest);
    }
    {  // another protocol number is "incompatible", with the server's own version name
        FakeStatusServer server(R"({"version":{"name":"1.21.9","protocol":773},"players":{"max":5,"online":0},"description":"old"})");
        const auto r = apex::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == apex::PingState::Incompatible && r.versionName == "1.21.9" && r.protocol == 773);
        CHECK(r.hasPlayers && r.playersNotListed == 0 && r.playerSample.empty() && apex::plainText(r.motd) == "old");
    }
    {  // no version and no players at all: "Old", protocol 0, "???" for the player count
        FakeStatusServer server(R"({"description":"x"})");
        const auto r = apex::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == apex::PingState::Incompatible && r.versionIsOld && r.protocol == 0 && !r.hasPlayers);
    }
    {  // a favicon that is not a PNG data URL is ignored
        FakeStatusServer server(R"({"version":{"name":"a","protocol":777},"favicon":"http://example.com/x.png"})");
        const auto r = apex::ServerPing::runBlocking("127.0.0.1", server.port);
        CHECK(r.state == apex::PingState::Successful && r.faviconPng.empty());
    }
    {  // a server that hangs up before answering, one that sends garbage, and one that never sends the pong
        FakeStatusServer silent("", /*answerStatus=*/false);
        const auto a = apex::ServerPing::runBlocking("127.0.0.1", silent.port);
        CHECK(a.state == apex::PingState::Unreachable && a.message == apex::PingMessage::CannotConnect);
        FakeStatusServer garbage("this is not json");
        const auto b = apex::ServerPing::runBlocking("127.0.0.1", garbage.port);
        CHECK(b.state == apex::PingState::Unreachable && b.message == apex::PingMessage::CannotConnect);
    }
    {  // nothing is listening: connection refused
        int freePort = 0;
        {
            FakeStatusServer probe("{}");
            freePort = probe.port;
            const auto r = apex::ServerPing::runBlocking("127.0.0.1", probe.port);  // let it finish normally
            CHECK(r.state != apex::PingState::Unreachable);
        }
        const auto r = apex::ServerPing::runBlocking("127.0.0.1", freePort);
        CHECK(r.state == apex::PingState::Unreachable && r.message == apex::PingMessage::CannotConnect);
    }
    {  // the background version: start() returns at once and the snapshot fills in
        FakeStatusServer server(json);
        const auto ping = apex::ServerPing::start("127.0.0.1:" + std::to_string(server.port));
        apex::PingSnapshot s = ping->snapshot();
        for (int i = 0; i < 500 && s.state == apex::PingState::Pinging; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            s = ping->snapshot();
        }
        CHECK(s.state == apex::PingState::Successful && s.revision > 0);
        const auto bad = apex::ServerPing::start("host:99999");  // not an address at all
        apex::PingSnapshot t = bad->snapshot();
        for (int i = 0; i < 500 && t.state == apex::PingState::Pinging; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            t = bad->snapshot();
        }
        CHECK(t.state == apex::PingState::Unreachable && t.message == apex::PingMessage::CannotResolve);
    }
    {  // cancel() before anything happened: the ping reports nothing and stays quiet
        const auto ping = apex::ServerPing::start("127.0.0.1:1");
        ping->cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(ping->snapshot().state == apex::PingState::Pinging || ping->snapshot().state == apex::PingState::Unreachable);
    }
#endif
}

void test_multiplayer_layout() {
    using apex::Rect;
    // The spec's worked example: 1920x1080 at GUI scale 4 is 480x270.
    const auto l = apex::computeMultiplayerLayout(480, 270, 100);
    CHECK((l.list == Rect{0, 33, 480, 177}));
    CHECK(l.titleX == 190 && l.titleY == 12);
    CHECK((l.join == Rect{86, 218, 100, 20}) && (l.direct == Rect{190, 218, 100, 20}) && (l.add == Rect{294, 218, 100, 20}));
    CHECK((l.edit == Rect{86, 242, 74, 20}) && (l.remove == Rect{164, 242, 74, 20}));
    CHECK((l.refresh == Rect{242, 242, 74, 20}) && (l.back == Rect{320, 242, 74, 20}));

    // A tick box toggles on a click and reports the new state; clicks outside it do nothing.
    apex::CheckboxWidget box("Do not show this screen again", 10, 20, 100, false);
    bool state = false;
    int calls = 0;
    box.onChange = [&](bool checked) {
        state = checked;
        ++calls;
    };
    CHECK(box.width == 17 + 4 + 100 && box.height == 17);
    CHECK(!box.mouseClicked(5, 25, 0) && calls == 0);   // left of it
    CHECK(box.mouseClicked(12, 22, 0) && state && box.checked() && calls == 1);
    CHECK(box.mouseClicked(100, 25, 0) && !state && !box.checked() && calls == 2);  // on the label too
    CHECK(!box.mouseClicked(12, 22, 1) && calls == 2);   // only the left button
}

void test_key_mappings() {
    namespace fs = std::filesystem;
    using apex::KeyBindings;
    using apex::KeyId;
    const auto& mappings = apex::keyMappings();

    // The registry: 62 actions in the game's eight categories, each name once.
    CHECK(mappings.size() == 62);
    std::array<int, 8> perCategory{};
    std::set<std::string> names;
    for (const auto& m : mappings) {
        ++perCategory[static_cast<std::size_t>(m.category)];
        names.insert(m.name);
    }
    CHECK(names.size() == 62);
    CHECK((perCategory == std::array<int, 8>{7, 8, 5, 3, 12, 2, 2, 23}));  // movement, misc, multiplayer, gameplay, ...

    // Key names in the options file are the game's.
    CHECK(KeyId::keyboard(GLFW_KEY_SPACE).optionName() == "key.keyboard.space");
    CHECK(KeyId::keyboard(GLFW_KEY_A).optionName() == "key.keyboard.a" && KeyId::keyboard(GLFW_KEY_5).optionName() == "key.keyboard.5");
    CHECK(KeyId::keyboard(GLFW_KEY_F5).optionName() == "key.keyboard.f5" && KeyId::keyboard(GLFW_KEY_KP_ENTER).optionName() == "key.keyboard.keypad.enter");
    CHECK(KeyId::keyboard(GLFW_KEY_LEFT_SUPER).optionName() == "key.keyboard.left.win");
    CHECK(KeyId::unbound().optionName() == "key.keyboard.unknown" && KeyId::unbound().isUnbound());
    CHECK(KeyId::mouse(0).optionName() == "key.mouse.left" && KeyId::mouse(1).optionName() == "key.mouse.right" &&
          KeyId::mouse(2).optionName() == "key.mouse.middle");
    CHECK(KeyId::mouse(3).optionName() == "key.mouse.4" && KeyId::mouse(7).optionName() == "key.mouse.8");  // the game counts from 1
    CHECK(!KeyId::mouse(0).isUnbound());
    // Every key GLFW can report survives a round trip through its name.
    int bindable = 0;
    for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
        if (!KeyId::isBindableKey(key)) continue;
        ++bindable;
        const auto back = KeyId::fromOptionName(KeyId::keyboard(key).optionName());
        CHECK(back && *back == KeyId::keyboard(key));
    }
    CHECK(bindable > 100);
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        const auto back = KeyId::fromOptionName(KeyId::mouse(button).optionName());
        CHECK(back && *back == KeyId::mouse(button));
    }
    CHECK(KeyId::fromOptionName("key.keyboard.unknown") == KeyId::unbound());
    CHECK(!KeyId::fromOptionName("key.keyboard.nonsense") && !KeyId::fromOptionName("key.mouse.99") && !KeyId::fromOptionName("jump") &&
          !KeyId::fromOptionName("key.mouse.3") && !KeyId::fromOptionName("key.mouse.x"));
    CHECK(!KeyId::isBindableKey(-1) && !KeyId::isBindableKey(0));

    // Defaults, reset and "changed".
    KeyBindings bindings;
    CHECK(!bindings.anyChanged());
    std::size_t sneak = 0, right = 0, clearChat = 0, overlay = 0, modifier = 0, jump = 0, cinematic = 0, spectator = 0;
    for (std::size_t i = 0; i < mappings.size(); ++i) {
        const std::string n = mappings[i].name;
        if (n == "key.sneak") sneak = i;
        if (n == "key.right") right = i;
        if (n == "key.debug.clearChat") clearChat = i;
        if (n == "key.debug.overlay") overlay = i;
        if (n == "key.debug.modifier") modifier = i;
        if (n == "key.jump") jump = i;
        if (n == "key.smoothCamera") cinematic = i;
        if (n == "key.spectatorOutlines") spectator = i;
    }
    CHECK(bindings.key(jump) == KeyId::keyboard(GLFW_KEY_SPACE) && bindings.key(sneak) == KeyId::keyboard(GLFW_KEY_LEFT_SHIFT));
    CHECK(bindings.key(cinematic).isUnbound() && bindings.key(spectator).isUnbound());

    // Conflicts: two actions that both ship on one key (D, or F3) never flag each other; changing one does.
    CHECK(bindings.key(right) == bindings.key(clearChat) && bindings.conflicts(right).empty() && bindings.conflicts(clearChat).empty());
    CHECK(bindings.key(overlay) == bindings.key(modifier) && bindings.conflicts(overlay).empty());
    CHECK(bindings.conflicts(cinematic).empty() && bindings.conflicts(spectator).empty());  // unbound: no conflict
    bindings.set(sneak, KeyId::keyboard(GLFW_KEY_D));
    CHECK(!bindings.isDefault(sneak) && bindings.anyChanged());
    const auto sneakConflicts = bindings.conflicts(sneak);
    CHECK(sneakConflicts.size() == 2 && std::count(sneakConflicts.begin(), sneakConflicts.end(), right) == 1 &&
          std::count(sneakConflicts.begin(), sneakConflicts.end(), clearChat) == 1);
    const auto rightConflicts = bindings.conflicts(right);  // sneak is off its default, clear chat is not: only sneak
    CHECK(rightConflicts.size() == 1 && rightConflicts[0] == sneak);
    bindings.set(jump, KeyId::unbound());
    bindings.set(sneak, KeyId::unbound());
    CHECK(bindings.conflicts(jump).empty() && bindings.conflicts(sneak).empty());  // two unbound actions do not conflict
    bindings.reset(jump);
    CHECK(bindings.isDefault(jump) && bindings.anyChanged());
    bindings.resetAll();
    CHECK(!bindings.anyChanged() && bindings.key(sneak) == KeyId::keyboard(GLFW_KEY_LEFT_SHIFT));

    // Saving: key_<name>:<key name>; loading keeps the default for anything missing or unrecognised.
    bindings.set(jump, KeyId::mouse(3));
    bindings.set(sneak, KeyId::keyboard(GLFW_KEY_F7));
    apex::OptionsFile file;
    bindings.save(file);
    CHECK(file.getString("key_key.jump") == "key.mouse.4" && file.getString("key_key.sneak") == "key.keyboard.f7");
    CHECK(file.getString("key_key.smoothCamera") == "key.keyboard.unknown" && file.getString("key_key.attack") == "key.mouse.left");
    KeyBindings loaded;
    loaded.load(apex::OptionsFile::parse(file.toString()));
    CHECK(loaded.key(jump) == KeyId::mouse(3) && loaded.key(sneak) == KeyId::keyboard(GLFW_KEY_F7));
    CHECK(loaded.key(right) == KeyId::keyboard(GLFW_KEY_D));
    KeyBindings partial;
    partial.load(apex::OptionsFile::parse("key_key.jump:key.keyboard.bogus\nkey_key.sneak:key.keyboard.z\nunrelated:1\n"));
    CHECK(partial.key(jump) == KeyId::keyboard(GLFW_KEY_SPACE));   // bad value: the default stays
    CHECK(partial.key(sneak) == KeyId::keyboard(GLFW_KEY_Z) && !partial.isDefault(sneak));

    // The Key Binds screen's order, from the game: by category, then the debug keys' own order, then by the
    // English names. This is the screen's list in the spec, id by id.
    struct Expected {
        const char* id;
        const char* label;
    };
    const std::vector<Expected> expected = {
        {"key.jump", "Jump"}, {"key.sneak", "Sneak"}, {"key.sprint", "Sprint"}, {"key.left", "Strafe Left"},
        {"key.right", "Strafe Right"}, {"key.back", "Walk Backward"}, {"key.forward", "Walk Forward"},
        {"key.advancements", "Advancements"}, {"key.quickActions", "Quick Actions"}, {"key.screenshot", "Take Screenshot"},
        {"key.smoothCamera", "Toggle Cinematic Camera"}, {"key.fullscreen", "Toggle Fullscreen"}, {"key.toggleGui", "Toggle GUI"},
        {"key.togglePerspective", "Toggle Perspective"}, {"key.toggleSpectatorShaderEffects", "Toggle Spectator Shader Effects"},
        {"key.friends", "Friends Screen"}, {"key.playerlist", "List Players"}, {"key.chat", "Open Chat"},
        {"key.command", "Open Command"}, {"key.socialInteractions", "Social Interactions Screen"},
        {"key.attack", "Attack/Destroy"}, {"key.pickItem", "Pick Block"}, {"key.use", "Use Item/Place Block"},
        {"key.drop", "Drop Selected Item"}, {"key.hotbar.1", "Hotbar Slot 1"}, {"key.hotbar.2", "Hotbar Slot 2"},
        {"key.hotbar.3", "Hotbar Slot 3"}, {"key.hotbar.4", "Hotbar Slot 4"}, {"key.hotbar.5", "Hotbar Slot 5"},
        {"key.hotbar.6", "Hotbar Slot 6"}, {"key.hotbar.7", "Hotbar Slot 7"}, {"key.hotbar.8", "Hotbar Slot 8"},
        {"key.hotbar.9", "Hotbar Slot 9"}, {"key.inventory", "Open/Close Inventory"}, {"key.swapOffhand", "Swap Item With Off Hand"},
        {"key.loadToolbarActivator", "Load Hotbar Activator"}, {"key.saveToolbarActivator", "Save Hotbar Activator"},
        {"key.spectatorOutlines", "Highlight Players"}, {"key.spectatorHotbar", "Select On Hotbar"},
        {"key.debug.overlay", "Toggle Overlay"}, {"key.debug.modifier", "Debug Modifier Key"}, {"key.debug.clearChat", "Clear Chat"},
        {"key.debug.copyRecreateCommand", "Copy Data"}, {"key.debug.copyLocation", "Copy Location"},
        {"key.debug.spectate", "Cycle Spectator"}, {"key.debug.crash", "Debug Crash"}, {"key.debug.debugOptions", "Debug Options"},
        {"key.debug.dumpDynamicTextures", "Dump Dynamic Textures"}, {"key.debug.dumpVersion", "Dump Version Info"},
        {"key.debug.switchGameMode", "Game Mode Switcher"}, {"key.debug.reloadChunk", "Reload Chunks"},
        {"key.debug.reloadResourcePacks", "Reload Resource Packs"}, {"key.debug.showAdvancedTooltips", "Show Advanced Tooltips"},
        {"key.debug.showChunkBorders", "Show Chunk Boundaries"}, {"key.debug.showHitboxes", "Show Hitboxes"},
        {"key.debug.profiling", "Start/Stop Profiling"}, {"key.debug.improvedTransparency", "Toggle Improved Transparency"},
        {"key.debug.focusPause", "Toggle Lost Focus Pause"}, {"key.debug.profilingChart", "Profiling Chart"},
        {"key.debug.fpsCharts", "Fps Charts"}, {"key.debug.networkCharts", "Network Charts"},
        {"key.debug.lightmapTexture", "Lightmap Texture"}};
    CHECK(expected.size() == 62);

    std::map<std::string, std::string> english;
    for (const auto& e : expected) english[e.id] = e.label;
    const auto order = apex::sortedKeyMappingOrder([&](const std::string& key) { return english.at(key); });
    CHECK(order.size() == 62);
    bool sameOrder = order.size() == expected.size();
    for (std::size_t i = 0; sameOrder && i < order.size(); ++i) sameOrder = std::string(mappings[order[i]].name) == expected[i].id;
    CHECK(sameOrder);
    // Another language sorts by its own names.
    std::map<std::string, std::string> shuffled = english;
    shuffled["key.jump"] = "Zzz";
    const auto movement = apex::sortedKeyMappingOrder([&](const std::string& key) { return shuffled.at(key); });
    CHECK(std::string(mappings[movement[6]].name) == "key.jump" && std::string(mappings[movement[0]].name) == "key.sneak");

    // Display names come from the language, with the printed letter for a letter key.
    const fs::path base = fs::temp_directory_path() / "apex_keys_test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    writeText(base / "assets" / "minecraft" / "lang" / "en_us.json",
              R"({"key.keyboard.unknown":"Not Bound","key.keyboard.space":"Space","key.keyboard.left.shift":"Left Shift",)"
              R"("key.mouse":"Button %1$s","key.mouse.left":"Left Button","key.mouse.middle":"Middle Button"})");
    const apex::ResourceManager resources(base / "assets");
    const apex::Language lang = apex::Language::load(resources, "en_us");
    CHECK(apex::keyDisplayName(KeyId::unbound(), lang) == "Not Bound");
    CHECK(apex::keyDisplayName(KeyId::keyboard(GLFW_KEY_SPACE), lang) == "Space" && apex::keyDisplayName(KeyId::keyboard(GLFW_KEY_LEFT_SHIFT), lang) == "Left Shift");
    CHECK(apex::keyDisplayName(KeyId::keyboard(GLFW_KEY_W), lang) == "W" && apex::keyDisplayName(KeyId::keyboard(GLFW_KEY_7), lang) == "7");  // no layout name without GLFW
    CHECK(apex::keyDisplayName(KeyId::mouse(0), lang) == "Left Button" && apex::keyDisplayName(KeyId::mouse(2), lang) == "Middle Button");
    CHECK(apex::keyDisplayName(KeyId::mouse(3), lang) == "Button 4" && apex::keyDisplayName(KeyId::mouse(7), lang) == "Button 8");
    CHECK(apex::layoutKeyName(GLFW_KEY_A).empty() && apex::layoutKeyName(GLFW_KEY_SPACE).empty());  // GLFW is not running in the tests
    fs::remove_all(base, ignored);
}

void test_key_binds_layout() {
    using apex::Rect;
    // The spec's worked example: 854x480 at GUI scale 2 is 427x240.
    const auto l = apex::computeKeyBindsLayout(427, 240, 50);
    CHECK(l.titleX == 188 && l.titleY == 12);
    CHECK((l.list == Rect{0, 33, 427, 174}));
    CHECK((l.resetAll == Rect{59, 213, 150, 20}) && (l.done == Rect{217, 213, 150, 20}));
    // Inside a row: rowLeft = 427/2 - 170 = 43, name at 45, key button at 251, Reset at 331, the scrollbar at 391.
    const int rowLeft = 427 / 2 - apex::kKeyBindsRowWidth / 2;
    CHECK(rowLeft == 43 && rowLeft + apex::kKeyNameOffset == 45);
    CHECK(rowLeft + apex::kKeyButtonOffset == 251 && rowLeft + apex::kKeyButtonOffset + apex::kKeyButtonWidth == 326);
    CHECK(rowLeft + apex::kResetButtonOffset == 331 && rowLeft + apex::kKeyBindsRowWidth + 8 == 391);

    // The text on a key button.
    using Piece = apex::KeyLabelPiece;
    CHECK((apex::keyButtonLabel("Space", false, false) == std::vector<Piece>{{"Space", 0xFFFFFF, false}}));
    CHECK((apex::keyButtonLabel("D", true, false) == std::vector<Piece>{{"[", 0xFFFF55, false}, {"D", 0xFFFFFF, false}, {" ]", 0xFFFF55, false}}));
    CHECK((apex::keyButtonLabel("K", false, true) == std::vector<Piece>{{">", 0xFFFF55, false}, {"K", 0xFFFFFF, true}, {" <", 0xFFFF55, false}}));
    const auto both = apex::keyButtonLabel("D", true, true);  // waiting on a key that is already in use: "> [D ] <"
    CHECK(both.size() == 5 && both[0].text == ">" && both[1].text == "[" && both[1].underlined && both[2].underlined && both[4].text == " <" && !both[4].underlined);
}

// A stand-in for the baked block models: opaque cubes with a picture per state (grass has its own top and side),
// water as a fluid, everything else invisible. Lets the mesher be tested without the game's files.
class FakeModels : public apex::BlockRenderProvider {
public:
    static constexpr int kGrassTop = 200, kGrassSide = 201, kGrassBottom = 202, kWaterStill = 300, kWaterFlow = 301;
    const apex::StateRender& render(apex::Block b) const override {
        const auto& registry = apex::blockRegistry();
        auto it = cache_.find(apex::stateId(b));
        if (it != cache_.end()) return it->second;
        apex::StateRender s;
        const apex::StateInfo& info = registry.state(b);
        if (info.has(apex::kStateLiquid)) {
            s.kind = apex::RenderKind::Fluid;
            s.fluidStill = kWaterStill;
            s.fluidFlow = kWaterFlow;
            s.fluidTint = 0;
            s.type = apex::RenderType::Translucent;
        } else if (info.has(apex::kStateOpaqueCube)) {
            s.kind = apex::RenderKind::Cube;
            s.faceLayer.fill(static_cast<std::uint16_t>(apex::stateId(b)));
            s.faceTint.fill(-1);
            if (registry.type(b).name == "grass_block") {
                s.faceLayer = {kGrassSide, kGrassSide, kGrassTop, kGrassBottom, kGrassSide, kGrassSide};
            }
        }
        return cache_.emplace(apex::stateId(b), std::move(s)).first->second;
    }

private:
    mutable std::unordered_map<std::uint16_t, apex::StateRender> cache_;
};

apex::Block blockNamed(const char* name) { return apex::blockRegistry().find(name).value(); }

void test_block_registry() {
    // The built-in set used by the tests: names, properties, defaults.
    const auto minimal = apex::BlockRegistry::makeMinimal();
    CHECK(!minimal->isFromGameData() && minimal->find("stone") && minimal->find("minecraft:stone") == minimal->find("stone"));
    CHECK(!minimal->find("no_such_block") && minimal->isAir(apex::Block::Air) && !minimal->isAir(*minimal->find("stone")));
    const apex::Block grass = *minimal->find("grass_block");
    CHECK(minimal->get(grass, "snowy") == "false");  // the default is the second state, like the game's
    const apex::Block snowy = minimal->with(grass, "snowy", "true");
    CHECK(snowy != grass && minimal->get(snowy, "snowy") == "true" && minimal->with(snowy, "snowy", "false") == grass);
    CHECK(minimal->with(grass, "nope", "x") == grass && minimal->with(grass, "snowy", "maybe") == grass);
    CHECK(minimal->describe(snowy) == "grass_block[snowy=true]");
    const auto log = minimal->parse("minecraft:oak_log[axis=x]");
    CHECK(log && minimal->get(*log, "axis") == "x" && minimal->type(*log).name == "oak_log" && !minimal->parse("minecraft:nothing[a=b]"));
    CHECK(minimal->state(*minimal->find("stone")).has(apex::kStateOpaqueCube) && minimal->state(*minimal->find("water")).has(apex::kStateLiquid));
    // Ids are consecutive per block.
    const apex::BlockType& oak = minimal->type(*log);
    CHECK(oak.stateCount == 3 && stateId(*log) >= oak.firstState && stateId(*log) < oak.firstState + oak.stateCount);

    // The real thing, when the game's data is on this machine: the numbers the game uses.
    const auto assets = apex::locateAssets(std::nullopt);
    if (assets.location) {
        const std::filesystem::path data = assets.location->root.parent_path();
        const std::filesystem::path cache = std::filesystem::temp_directory_path() / "apex_registry_test";
        std::error_code ignored;
        std::filesystem::remove_all(cache, ignored);
        std::string error;
        const auto real = apex::BlockRegistry::loadFromGameData(data, cache, error);
        if (!real) {
            std::printf("  (block registry from game data skipped: %s)\n", error.c_str());
        } else {
            CHECK(real->isFromGameData() && real->stateCount() > 30000 && real->blocks().size() > 1000);
            const apex::Block stone = *real->find("stone");
            CHECK(apex::stateId(stone) == 1 && real->state(stone).has(apex::kStateOpaqueCube) && std::abs(real->state(stone).hardness - 1.5f) < 1e-6f);
            CHECK(apex::stateId(*real->find("grass_block")) == 9 && real->get(*real->find("grass_block"), "snowy") == "false");
            CHECK(apex::stateId(*real->find("water")) == 89 && real->state(*real->find("water")).has(apex::kStateWater));
            CHECK(real->state(*real->find("bedrock")).hardness < 0.0f);
            CHECK(std::abs(real->state(*real->find("ice")).friction - 0.98f) < 1e-6f && std::abs(real->state(*real->find("soul_sand")).speedFactor - 0.4f) < 1e-6f);
            CHECK(std::abs(real->state(*real->find("honey_block")).jumpFactor - 0.5f) < 1e-6f && real->state(*real->find("torch")).emission == 14);
            const auto stairs = real->parse("oak_stairs[facing=east,half=top]");
            CHECK(stairs && real->get(*stairs, "facing") == "east" && real->get(*stairs, "half") == "top" && real->get(*stairs, "shape") == "straight");
            const auto leaves = real->parse("oak_leaves[distance=3,persistent=true]");
            CHECK(leaves && real->get(*leaves, "distance") == "3" && real->get(*leaves, "persistent") == "true" && real->get(*leaves, "waterlogged") == "false");
            CHECK(real->state(*real->parse("oak_stairs[waterlogged=true]")).has(apex::kStateWater) && real->state(*real->find("kelp")).has(apex::kStateWater));
            // A second load comes from the cache and is the same.
            const auto again = apex::BlockRegistry::loadFromGameData(data, cache, error);
            CHECK(again && again->stateCount() == real->stateCount() && again->blocks().size() == real->blocks().size() &&
                  again->describe(*stairs) == real->describe(*stairs) && again->state(stone).flags == real->state(stone).flags);
            std::filesystem::remove_all(cache, ignored);
        }
    }
}

void test_world_storage() {
    using apex::Block;
    const apex::CommonBlocks& cb = apex::commonBlocks();
    // A section with one block type stores no array; editing it builds a new section and leaves the old one alone.
    const auto stone = apex::uniformSection(cb.stone);
    CHECK(stone && stone->isUniform() && stone->at(3, 4, 5) == cb.stone && apex::uniformSection(cb.stone) == stone);  // shared
    CHECK(apex::uniformSection(Block::Air) == nullptr);
    const auto edited = apex::sectionWithBlock(stone, 1, 2, 3, cb.dirt);
    CHECK(edited && !edited->isUniform() && edited->at(1, 2, 3) == cb.dirt && edited->at(0, 0, 0) == cb.stone);
    CHECK(edited->packing == apex::Packing::Nibble && edited->palette.size() == 2);  // two kinds: 4 bits a block
    CHECK(stone->at(1, 2, 3) == cb.stone);                                           // the old section never changes
    const auto restored = apex::sectionWithBlock(edited, 1, 2, 3, cb.stone);          // all the same again: compact again
    CHECK(restored && restored->isUniform() && restored->uniform == cb.stone);
    apex::SectionBuilder air;
    CHECK(air.build() == nullptr);

    // Palettes: 17 kinds need 8 bits a block, more than 256 need the ids themselves; every block reads back.
    for (const int kinds : {16, 17, 255, 256, 257, 300}) {
        apex::SectionBuilder b;
        for (int i = 0; i < apex::kSectionVolume; ++i) b.raw()[static_cast<std::size_t>(i)] = static_cast<Block>(1 + (i * 7) % kinds);
        const auto s = b.build();
        const apex::Packing want = kinds <= 16 ? apex::Packing::Nibble : kinds <= 256 ? apex::Packing::Byte : apex::Packing::Wide;
        bool readsBack = s && s->packing == want;
        std::vector<Block> all(apex::kSectionVolume);
        if (s) s->decode(all.data());
        for (int i = 0; i < apex::kSectionVolume && readsBack; ++i) {
            readsBack = s->atIndex(i) == static_cast<Block>(1 + (i * 7) % kinds) && all[static_cast<std::size_t>(i)] == s->atIndex(i);
        }
        CHECK(readsBack);
        if (s) CHECK(s->memoryBytes() < (want == apex::Packing::Nibble ? 2300u : want == apex::Packing::Byte ? 4800u : 9000u));
    }
    apex::SectionBuilder rowTest;
    for (int x = 0; x < 16; ++x) rowTest.set(x, 3, 4, static_cast<Block>(1 + x % 3));
    const auto row = rowTest.build();
    Block out[16];
    row->copyRow(2, 3, 4, 10, out);
    CHECK(out[0] == static_cast<Block>(1 + 2 % 3) && out[9] == static_cast<Block>(1 + 11 % 3));

    // A level: coordinates across chunk borders (negative ones too), air where nothing is loaded.
    apex::Level level;
    auto chunk = std::make_shared<apex::Chunk>();
    chunk->sections[0] = apex::uniformSection(cb.stone);  // y -64..-49
    level.putChunk({-1, 2}, chunk);
    CHECK(level.block(-1, -64, 32) == cb.stone && level.block(-16, -49, 47) == cb.stone);
    CHECK(level.block(-17, -64, 32) == Block::Air && level.block(-1, -48, 32) == Block::Air);  // next chunk / next section
    CHECK(level.block(-1, apex::kMinY - 1, 32) == Block::Air && level.block(-1, 400, 32) == Block::Air);
    CHECK(level.isLoaded(-5, 40) && !level.isLoaded(0, 0));
    const Block gravel = blockNamed("gravel");
    CHECK(level.setBlock(-1, -60, 32, gravel) && level.block(-1, -60, 32) == gravel);
    CHECK(!level.setBlock(0, -60, 0, gravel) && !level.setBlock(-1, 400, 32, gravel));
}

void test_terrain() {
    using apex::Block;
    const apex::CommonBlocks& cb = apex::commonBlocks();
    // Superflat: bedrock, two dirt, grass at y -64..-61, air above.
    const apex::TerrainGenerator flat(1, apex::TerrainStyle::Flat);
    const auto fc = flat.generate(3, -4);
    CHECK(fc->at(5, -64, 5) == cb.bedrock && fc->at(5, -63, 5) == cb.dirt && fc->at(5, -62, 5) == cb.dirt);
    CHECK(fc->at(5, -61, 5) == cb.grassBlock && fc->at(5, -60, 5) == Block::Air && fc->at(0, 300, 15) == Block::Air);
    CHECK(flat.surfaceHeight(0, 0) == -61 && flat.findSpawn().y == -60.0);
    CHECK(fc->memoryBytes() < 12000);  // one section with an array; the rest is empty

    // Hills: the same seed gives the same chunk, another seed another one; rock below, soil on top, water in the sea.
    const apex::TerrainGenerator hills(12345, apex::TerrainStyle::Hills);
    const auto a = hills.generate(0, 0);
    const auto b = hills.generate(0, 0);
    bool same = true;
    for (int y = -64; y < 120 && same; y += 3) same = a->at(7, y, 9) == b->at(7, y, 9) && a->at(0, y, 0) == b->at(0, y, 0);
    CHECK(same);
    const apex::TerrainGenerator other(54321, apex::TerrainStyle::Hills);
    int differing = 0;
    for (int x = -300; x < 300; x += 20) differing += hills.surfaceHeight(x, 40) != other.surfaceHeight(x, 40);
    CHECK(differing > 10);
    for (const auto& [x, z] : std::vector<std::pair<int, int>>{{0, 0}, {37, -90}, {-250, 130}}) {
        const int h = hills.surfaceHeight(x, z);
        const auto chunk = hills.generate(apex::chunkOf(x), apex::chunkOf(z));
        const int lx = x & 15;
        const int lz = z & 15;
        CHECK(chunk->at(lx, -64, lz) == cb.bedrock && chunk->at(lx, h - 10, lz) == cb.stone);
        CHECK(chunk->at(lx, h, lz) == cb.grassBlock || chunk->at(lx, h, lz) == cb.sand);
        CHECK(chunk->at(lx, h + 1, lz) == (h + 1 < apex::TerrainGenerator::kSeaLevel ? cb.water : Block::Air));
        CHECK(chunk->at(lx, std::max(h + 1, apex::TerrainGenerator::kSeaLevel), lz) == Block::Air);
    }
    const auto spawn = hills.findSpawn();
    CHECK(hills.surfaceHeight(static_cast<int>(std::floor(spawn.x)), static_cast<int>(std::floor(spawn.z))) > apex::TerrainGenerator::kSeaLevel);
    // Heights vary (it is not flat) and amplified terrain varies more.
    int lowest = 999, highest = -999, ampLowest = 999, ampHighest = -999;
    const apex::TerrainGenerator amp(12345, apex::TerrainStyle::Amplified);
    for (int x = -2000; x <= 2000; x += 40) {
        lowest = std::min(lowest, hills.surfaceHeight(x, 11));
        highest = std::max(highest, hills.surfaceHeight(x, 11));
        ampLowest = std::min(ampLowest, amp.surfaceHeight(x, 11));
        ampHighest = std::max(ampHighest, amp.surfaceHeight(x, 11));
    }
    CHECK(highest - lowest > 20 && ampHighest - ampLowest > highest - lowest);
}

void test_mesher() {
    using apex::Block;
    using apex::kMeshScale;
    const apex::CommonBlocks& cb = apex::commonBlocks();
    FakeModels models;
    const auto around = [](apex::SectionPtr middle) {
        apex::SectionNeighborhood n;
        n.sections[static_cast<std::size_t>(apex::SectionNeighborhood::slot(0, 0, 0))] = std::move(middle);
        return n;
    };
    const auto mesh = [&](const apex::SectionNeighborhood& n) { return apex::meshSection(n, models, 0, 0, 0); };
    // Nothing to draw in air, and a lone block has six quads (24 vertices), none merged.
    CHECK(mesh(around(nullptr)).empty());
    apex::SectionBuilder one;
    one.set(5, 6, 7, cb.stone);
    const auto single = mesh(around(one.build()));
    CHECK(single.opaque.size() == 24 && single.cutout.empty() && single.translucent.empty());
    // The block's quads touch only its own cube.
    for (const auto& v : single.opaque) CHECK(v.x >= 5 * kMeshScale && v.x <= 6 * kMeshScale && v.y >= 6 * kMeshScale && v.y <= 7 * kMeshScale && v.z >= 7 * kMeshScale && v.z <= 8 * kMeshScale);

    // A whole layer merges into one top quad that repeats the texture 16x16 times; the sides are 16x1 strips.
    apex::SectionBuilder layer;
    layer.fillLayers(0, 1, cb.grassBlock);
    const auto flat = mesh(around(layer.build()));
    CHECK(apex::quadCount(flat.opaque.size()) == 1 + 1 + 4);  // top, bottom, four edges (nothing is beyond them)
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
    apex::SectionBuilder pair;
    pair.set(0, 0, 0, cb.stone);
    pair.set(1, 0, 0, cb.stone);
    CHECK(apex::quadCount(mesh(around(pair.build())).opaque.size()) == 6);  // a 2x1x1 box: still six sides (merged), not ten
    apex::SectionBuilder edge;
    edge.set(0, 3, 3, cb.stone);
    apex::SectionNeighborhood withWall = around(edge.build());
    CHECK(apex::quadCount(mesh(withWall).opaque.size()) == 6);
    apex::SectionBuilder wall;
    wall.fill(cb.stone);
    withWall.sections[static_cast<std::size_t>(apex::SectionNeighborhood::slot(-1, 0, 0))] = wall.build();  // solid next door on -X
    CHECK(apex::quadCount(mesh(withWall).opaque.size()) == 5);                                              // the -X face is hidden

    // Different pictures do not merge.
    apex::SectionBuilder mixed;
    mixed.set(0, 0, 0, cb.stone);
    mixed.set(1, 0, 0, cb.dirt);
    CHECK(apex::quadCount(mesh(around(mixed.build())).opaque.size()) == 10);  // five visible faces each, none merged

    // Water goes to the translucent list, one top face per block, and only where it meets air.
    apex::SectionBuilder lake;
    lake.fillLayers(0, 4, cb.stone);
    for (int x = 4; x < 12; ++x) {
        for (int z = 4; z < 12; ++z) lake.set(x, 3, z, cb.water);
    }
    const auto pond = mesh(around(lake.build()));
    CHECK(apex::quadCount(pond.translucent.size()) == 128);  // the surface, front and reversed (seen from under the water); the sides touch stone, the bottom too
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
    apex::SectionBuilder bath;
    bath.fillLayers(0, 1, cb.stone);
    bath.set(5, 1, 5, cb.water);
    const auto drop = mesh(around(bath.build()));
    CHECK(apex::quadCount(drop.translucent.size()) == 2 + 4);  // top (front and reversed) and four sides, stone below

    // Every cube quad is wound counter-clockwise seen from outside (so back-face culling keeps the right side).
    apex::SectionBuilder cube;
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
    apex::Level level;
    level.putChunk({0, 0}, std::make_shared<apex::Chunk>(*apex::TerrainGenerator(7, apex::TerrainStyle::Flat).generate(0, 0)));
    const auto snap = apex::snapshotNeighborhood(level, 0, 0, 0);
    CHECK(snap.sections[static_cast<std::size_t>(apex::SectionNeighborhood::slot(0, -1, 0))]->uniform == cb.bedrock);
    CHECK(snap.sections[static_cast<std::size_t>(apex::SectionNeighborhood::slot(0, 0, 0))] != nullptr);
    CHECK(snap.sections[static_cast<std::size_t>(apex::SectionNeighborhood::slot(1, 0, 1))] == nullptr);  // chunk not loaded
}

// A small Superflat level around the origin: grass at y -61, so a player standing on it has feet at y -60.
apex::Level makeFlatLevel() {
    apex::Level level;
    const apex::TerrainGenerator flat(1, apex::TerrainStyle::Flat);
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
apex::Level makeFloorLevel(int radius = 3) {
    apex::Level level;
    apex::SectionBuilder builder;
    builder.fillLayers(0, 4, apex::commonBlocks().stone);  // y -64 .. -61
    const apex::SectionPtr floor = builder.build();
    for (int cz = -radius; cz <= radius; ++cz) {
        for (int cx = -radius; cx <= radius; ++cx) {
            auto chunk = std::make_shared<apex::Chunk>();
            chunk->sections[0] = floor;
            level.putChunk({cx, cz}, chunk);
        }
    }
    return level;
}

// A player and the level it stands in, ticked one game tick at a time.
struct PhysicsRun {
    apex::Level& level;
    apex::Player p;
    explicit PhysicsRun(apex::Level& l, double x = 0.5, double y = -60.0, double z = 0.5, float yaw = 0.0f) : level(l) {
        p.setPosition(x, y, z);
        p.yaw = p.yawO = yaw;
    }
    void tick(const apex::PlayerInput& in = {}) { p.tick(in, level); }
    void ticks(int n, const apex::PlayerInput& in = {}) {
        for (int i = 0; i < n; ++i) tick(in);
    }
    // Ticks with no keys until the player stands on the ground.
    void settle() { ticks(3); }
    // How far the last tick moved the player horizontally.
    double lastStep(const apex::PlayerInput& in) {
        const double x0 = p.x, z0 = p.z;
        tick(in);
        return std::hypot(p.x - x0, p.z - z0);
    }
};

bool closeTo(double a, double b, double eps) { return std::abs(a - b) <= eps; }

apex::PlayerInput keysForward() {
    apex::PlayerInput in;
    in.forward = true;
    return in;
}

// The steady distance a tick moves when the push per tick is `accel` and the drag is `drag`: a / (1 - drag).
double steady(double accel, double drag) { return accel / (1.0 - drag); }

// The game's float constants as doubles, the way they enter the formulas.
const double kInputF = static_cast<double>(0.98f);                       // modifyInput: the keys' push at 0.98f
const double kWalkSpeedF = static_cast<double>(0.1f);                    // movement_speed 0.10000000149011612
const double kSprintSpeedF = static_cast<double>(static_cast<float>(0.10000000149011612 * (1.0 + 0.30000001192092896)));  // +30%
const double kGroundDragF = static_cast<double>(0.6f * 0.91f);           // friction 0.6f * 0.91f (a float product)

void test_player_input_and_walking() {
    using apex::PlayerInput;
    apex::Level level = makeFloorLevel();
    PlayerInput fwd = keysForward();

    // Standing: it settles on the floor and stays put, exactly.
    {
        PhysicsRun r(level, 0.5, -60.0, 0.5);
        r.settle();
        CHECK(r.p.onGround && r.p.y == -60.0 && r.p.pose() == apex::PlayerPose::Standing);
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
        CHECK(r.p.pose() == apex::PlayerPose::Crouching && closeTo(static_cast<double>(r.p.eyeHeight()), 1.27, 1e-6));
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
        level.setBlock(0, -60, 3, apex::commonBlocks().stone);
        level.setBlock(0, -59, 3, apex::commonBlocks().stone);
        wall.settle();
        wall.ticks(30, run);
        CHECK(!wall.p.sprinting && closeTo(wall.p.z, 3.0 - 0.3, 1e-4));  // flush against the block, 0.3 in front of it
        level.setBlock(0, -60, 3, apex::Block::Air);
        level.setBlock(0, -59, 3, apex::Block::Air);
    }

    // A wall stops the walk flush: the body ends 0.3 from the block face; walking diagonally into it slides along it.
    {
        for (int z = -8; z <= 8; ++z) {
            for (int y = -60; y <= -58; ++y) level.setBlock(4, y, z, apex::commonBlocks().stone);
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
            for (int y = -60; y <= -58; ++y) level.setBlock(4, y, z, apex::Block::Air);
        }
    }
}

void test_player_jump_and_fall() {
    using apex::PlayerInput;
    apex::Level level = makeFloorLevel();

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
                for (int z = 8; z <= 12; ++z) level.setBlock(x, y, z, apex::commonBlocks().water);
            }
        }
        PhysicsRun r(level, 10.5, -30.0, 10.5);
        r.ticks(80);
        CHECK(r.p.takeFallDamage() == 0 && r.p.inWater && r.p.fallDistance == 0.0);
        for (int y = -60; y <= -57; ++y) {
            for (int x = 8; x <= 12; ++x) {
                for (int z = 8; z <= 12; ++z) level.setBlock(x, y, z, apex::Block::Air);
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
    using apex::PlayerInput;
    apex::Level level = makeFloorLevel(16);

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
    using apex::PlayerInput;
    apex::Level level = makeFloorLevel();
    for (int x = 8; x < 20; ++x) {
        for (int y = -64; y <= -61; ++y) level.setBlock(x, y, 0, apex::Block::Air);
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
    apex::Level poseLevel = makeFloorLevel();

    // Eye heights and the camera: the pose changes at the end of the tick, the camera eases halfway each tick:
    // 1.27 + 0.35 * 0.5^k [DERIVED]; standing up eases back the same way.
    {
        PhysicsRun r(poseLevel);
        r.settle();
        PlayerInput sneak;
        sneak.shift = true;
        r.tick(sneak);
        CHECK(r.p.pose() == apex::PlayerPose::Crouching && closeTo(static_cast<double>(r.p.eyeHeight()), 1.27, 1e-6) && closeTo(r.p.eyeY(), -60.0 + 1.27, 1e-6));
        CHECK(closeTo(r.p.cameraPosition(1.0)[1] + 60.0, 1.62, 1e-6));  // the camera has not started to move yet
        for (int k = 1; k <= 5; ++k) {
            r.tick(sneak);
            CHECK(closeTo(r.p.cameraPosition(1.0)[1] + 60.0, 1.27 + 0.35 * std::pow(0.5, k), 1e-5));
        }
        CHECK(closeTo(r.p.cameraPosition(0.0)[1] + 60.0, 1.27 + 0.35 * std::pow(0.5, 4), 1e-5));  // one tick earlier
        r.ticks(10, sneak);
        r.tick();
        CHECK(r.p.pose() == apex::PlayerPose::Standing && closeTo(static_cast<double>(r.p.eyeHeight()), 1.62, 1e-6));
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
            for (int dz = -1; dz <= 1; ++dz) poseLevel.setBlock(dx, -59, dz, apex::commonBlocks().stone);  // ceiling 1 block above the floor
        }
        r.ticks(3);
        CHECK(r.p.pose() == apex::PlayerPose::Swimming && closeTo(static_cast<double>(r.p.eyeHeight()), 0.4, 1e-6));
        CHECK(closeTo(r.p.boundingBox().maxY - r.p.boundingBox().minY, 0.6, 1e-6) && r.p.isMovingSlowly());
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) poseLevel.setBlock(dx, -59, dz, apex::Block::Air);
        }
        r.ticks(3);
        CHECK(r.p.pose() == apex::PlayerPose::Standing && !r.p.isMovingSlowly());
    }
}

// Special blocks: stairs, slabs, a fence post, with shapes the test makes up (bottom slab, a 0.6 and a 0.625 high block).
void test_player_stepping() {
    using apex::PlayerInput;
    using apex::Aabb;
    apex::Level level = makeFloorLevel();
    const apex::BlockRegistry& registry = apex::blockRegistry();
    const apex::Block slab = *registry.find("oak_planks");
    const apex::Block low = *registry.find("bricks");            // 0.6 high
    const apex::Block tall = *registry.find("sandstone");        // 0.625 high
    const apex::Block stairs = *registry.find("stone_bricks");   // a two-step stair, low side first
    const apex::Block post = *registry.find("cobblestone");      // a fence post, 1.5 high
    apex::installCollisionShapes(apex::CollisionShapes::makeFromBoxes(
        registry, {{slab, {Aabb{0, 0, 0, 1, 0.5, 1}}},
                   {low, {Aabb{0, 0, 0, 1, 0.6, 1}}},
                   {tall, {Aabb{0, 0, 0, 1, 0.625, 1}}},
                   {stairs, {Aabb{0, 0, 0, 1, 0.5, 1}, Aabb{0.5, 0.5, 0, 1, 1, 1}}},
                   {post, {Aabb{0.375, 0, 0.375, 0.625, 1.5, 0.625}}}}));
    PlayerInput fwd = keysForward();

    // Walking into a step of up to 0.6 goes up it; the highest point reached is the step top.
    const auto climb = [&](apex::Block block, double z) {
        level.setBlock(3, -60, static_cast<int>(z), block);
        PhysicsRun r(level, 0.5, -60.0, z + 0.5, -90.0f);
        r.settle();
        double top = r.p.y;
        for (int i = 0; i < 80; ++i) {
            r.tick(fwd);
            top = std::max(top, r.p.y);
        }
        level.setBlock(3, -60, static_cast<int>(z), apex::Block::Air);
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
        const auto onStone = climb(apex::commonBlocks().stone, 4);
        CHECK(onStone.first == -60.0 && closeTo(onStone.second, 3.0 - 0.3, 1e-4));  // a full block does not
        const auto onPost = climb(post, 5);
        CHECK(onPost.first == -60.0);  // the post is 1.5 tall and narrow: the player walks past it or is stopped, never up
    }

    // Jumping up a full block works (1.25 > 1), and with the jump key held the hops are 10 ticks apart at least.
    {
        level.setBlock(3, -60, 6, apex::commonBlocks().stone);
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
        level.setBlock(3, -60, 6, apex::Block::Air);
    }

    // The shapes of the table are what the collision sees; a slab's boxes and the unit cube.
    {
        const auto boxes = apex::collisionShapes().boxes(stairs);
        CHECK(boxes.size() == 2 && boxes[0].maxY == 0.5 && boxes[1].minX == 0.5 && boxes[1].maxY == 1.0);
        CHECK(apex::collisionShapes().boxes(apex::commonBlocks().stone).size() == 1);
        CHECK(apex::collisionShapes().boxes(apex::Block::Air).empty());
        CHECK(apex::collisionShapes().shape(post).isLarge() && !apex::collisionShapes().shape(slab).isLarge());
    }

    // Standing on a slab: feet at 0.5, and the body is carried by it.
    {
        level.setBlock(0, -60, 8, slab);
        PhysicsRun r(level, 0.5, -59.5, 8.5);
        r.settle();
        CHECK(r.p.onGround && r.p.y == -59.5);
        level.setBlock(0, -60, 8, apex::Block::Air);
    }
    apex::installCollisionShapes(nullptr);
}

void test_player_fluids() {
    using apex::PlayerInput;
    apex::Level level = makeFloorLevel();
    // A pool 10 deep (water from y -60 to -51, x -4..4, z 4..44) and a lava pool beside it (x 10..18, z 4..44).
    for (int y = -60; y <= -51; ++y) {
        for (int z = 4; z <= 44; ++z) {
            for (int x = -4; x <= 4; ++x) level.setBlock(x, y, z, apex::commonBlocks().water);
            for (int x = 10; x <= 18; ++x) level.setBlock(x, y, z, apex::commonBlocks().lava);
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
        CHECK(r.p.isSwimming() && r.p.pose() == apex::PlayerPose::Swimming && r.p.sprinting);
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
        for (int z = 50; z <= 60; ++z) level.setBlock(0, -60, z, apex::commonBlocks().water);
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
    using apex::FluidSimulator;
    const auto flowingWater = [](int amount) { return FluidSimulator::waterBlock({true, false, false, amount}); };
    apex::Level level = makeFloorLevel();
    level.setBlock(0, -60, 0, flowingWater(7));  // a stream running east: each block lower than the one before it
    level.setBlock(1, -60, 0, flowingWater(6));
    level.setBlock(2, -60, 0, flowingWater(5));
    level.setBlock(3, -60, 0, flowingWater(4));
    level.setBlock(4, -60, 0, flowingWater(3));
    PhysicsRun carried(level, 0.5, -60.0, 0.5);
    carried.ticks(12);
    CHECK(carried.p.inWater && carried.p.vx > 0.0 && carried.p.x > 0.5 + 0.01);
    CHECK(std::abs(carried.p.z - 0.5) < 1e-9);  // along the stream, not across it
    apex::Level pool = makeFloorLevel();
    pool.setBlock(0, -60, 0, apex::commonBlocks().water);  // a lone source: nothing to pull it either way
    PhysicsRun still(pool, 0.5, -60.0, 0.5);
    still.ticks(12);
    CHECK(still.p.inWater && std::abs(still.p.x - 0.5) < 1e-9 && std::abs(still.p.vx) < 1e-9);
}

void test_player_real_blocks() {
    using apex::PlayerInput;
    const auto assets = apex::locateAssets(std::nullopt);
    if (!assets.location) {
        std::printf("  (physics on the game's real blocks skipped: no game data)\n");
        return;
    }
    const std::filesystem::path data = assets.location->root.parent_path();
    const std::filesystem::path cache = std::filesystem::temp_directory_path() / "apex_physics_test";
    std::string error;
    const auto registry = apex::BlockRegistry::loadFromGameData(data, cache, error);
    if (!registry) {
        std::printf("  (physics on the game's real blocks skipped: %s)\n", error.c_str());
        return;
    }
    // The table is built from the NBT the first time and read back from its small cache after that: both must be the same.
    {
        std::error_code ignored;
        std::filesystem::remove(cache / "collision.cache", ignored);
    }
    const auto shapes = apex::CollisionShapes::loadFromGameData(data, cache, *registry, error);
    if (!shapes) {
        std::printf("  (physics on the game's real blocks skipped: %s)\n", error.c_str());
        return;
    }
    {
        const auto again = apex::CollisionShapes::loadFromGameData(data, cache, *registry, error);
        CHECK(again && std::filesystem::exists(cache / "collision.cache") && again->shapeCount() == shapes->shapeCount() &&
              again->stateCount() == shapes->stateCount());
        bool same = again != nullptr;
        for (std::size_t i = 0; same && i < shapes->stateCount(); ++i) {
            const auto b = static_cast<apex::Block>(i);
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
    const auto previous = apex::currentBlockRegistry();
    apex::installBlockRegistry(registry);
    apex::installCollisionShapes(shapes);
    const auto block = [&](const char* text) { return registry->parse(text).value(); };

    // The shapes the file holds.
    CHECK(shapes->isFromGameData() && shapes->shapeCount() > 6000 && shapes->stateCount() == registry->stateCount());
    CHECK(shapes->boxes(block("stone")).size() == 1 && shapes->shape(block("stone")).isUnitCube());
    CHECK(shapes->boxes(block("air")).empty() && shapes->boxes(block("water")).empty());
    {
        const auto fence = shapes->boxes(block("oak_fence"));
        double top = 0.0;
        for (const apex::Aabb& b : fence) top = std::max(top, b.maxY);
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
    const apex::Block stone = *registry->find("stone");
    apex::Level level;
    {
        apex::SectionBuilder builder;
        builder.fillLayers(0, 4, stone);
        const apex::SectionPtr floor = builder.build();
        for (int cz = -3; cz <= 8; ++cz) {
            for (int cx = -3; cx <= 8; ++cx) {
                auto chunk = std::make_shared<apex::Chunk>();
                chunk->sections[0] = floor;
                level.putChunk({cx, cz}, chunk);
            }
        }
    }
    PlayerInput fwd = keysForward();
    PlayerInput run = fwd;
    run.sprint = true;
    // Strips of other floors at z = 10 (ice), 12 (blue ice), 14 (soul sand), 16 (stone for comparison).
    const auto strip = [&](int z, apex::Block b) {
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
        const auto climb = [&](apex::Block b, int z) {
            level.setBlock(3, -60, z, b);
            PhysicsRun r(level, 0.5, -60.0, z + 0.5, -90.0f);
            r.settle();
            double top = r.p.y;
            for (int i = 0; i < 80; ++i) {
                r.tick(fwd);
                top = std::max(top, r.p.y);
            }
            level.setBlock(3, -60, z, apex::Block::Air);
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
        level.setBlock(0, -60, 30, apex::Block::Air);
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
            level.setBlock(0, y, 40, apex::Block::Air);
            level.setBlock(0, y, 41, apex::Block::Air);
        }
    }

    // Beds soften a fall by half, hay and honey take a fifth, slime none; slime bounces a falling player back up.
    {
        const auto fallOnto = [&](apex::Block b, double height) {
            level.setBlock(6, -60, 50, b);
            PhysicsRun r(level, 6.5, -60.0 + 1.0 + height, 50.5);  // above a one block tall surface
            int guard = 0;
            while (!r.p.onGround && guard++ < 300) r.tick();
            level.setBlock(6, -60, 50, apex::Block::Air);
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
        level.setBlock(6, -60, 52, apex::Block::Air);
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
        level.setBlock(6, -60, 53, apex::Block::Air);
    }

    // Cobweb squeezes the move to (0.25, 0.05, 0.25) the tick after touching it and wipes the velocity.
    {
        level.setBlock(8, -60, 60, block("cobweb"));
        PhysicsRun r(level, 8.5, -56.0, 60.5);
        r.ticks(60);
        CHECK(r.p.y > -59.9);  // it floats down at a tiny speed instead of dropping to the floor
        level.setBlock(8, -60, 60, apex::Block::Air);
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

    apex::installCollisionShapes(nullptr);
    if (previous) apex::installBlockRegistry(previous);
}

void test_player_physics() {
    test_player_input_and_walking();
    test_player_jump_and_fall();
    test_player_flying();
    test_player_sneak_edge_and_poses();
    test_player_stepping();
    test_player_fluids();
    test_player_current();
    test_player_real_blocks();
}

void test_raycast() {
    using apex::Block;
    apex::Level level = makeFlatLevel();
    // Straight down from standing height hits the grass top.
    const auto down = apex::raycastBlocks(level, {0.5, -58.38, 0.5}, {0.0, -1.0, 0.0}, 5.0);
    CHECK(down && down->x == 0 && down->y == -61 && down->z == 0 && down->face == apex::kUp && std::abs(down->distance - 1.62) < 1e-6);
    CHECK(down && down->placePosition() == (std::array<int, 3>{0, -60, 0}));
    CHECK(!apex::raycastBlocks(level, {0.5, 10.0, 0.5}, {0.0, -1.0, 0.0}, 5.0));  // out of reach
    CHECK(!apex::raycastBlocks(level, {0.5, 10.0, 0.5}, {0.0, 1.0, 0.0}, 50.0));   // looking up at the sky

    // Sideways: the face is the one toward the viewer, and water is looked through.
    level.setBlock(5, -60, 0, apex::commonBlocks().stone);
    level.setBlock(2, -60, 0, apex::commonBlocks().water);
    const auto side = apex::raycastBlocks(level, {0.5, -59.5, 0.5}, {1.0, 0.0, 0.0}, 5.0);
    CHECK(side && side->x == 5 && side->y == -60 && side->face == apex::kWest && std::abs(side->distance - 4.5) < 1e-6);
    CHECK(side && side->placePosition() == (std::array<int, 3>{4, -60, 0}));
    const auto back = apex::raycastBlocks(level, {9.5, -59.5, 0.5}, {-1.0, 0.0, 0.0}, 5.0);
    CHECK(back && back->x == 5 && back->face == apex::kEast);
    const auto north = apex::raycastBlocks(level, {5.5, -59.5, 5.5}, {0.0, 0.0, -1.0}, 8.0);
    CHECK(north && north->z == 0 && north->face == apex::kSouth);
    CHECK(!apex::raycastBlocks(level, {0.5, -59.5, 0.5}, {1.0, 0.0, 0.0}, 4.0));  // the stone is 4.5 away

    // A diagonal ray picks the first block it really crosses.
    const auto diagonal = apex::raycastBlocks(level, {0.5, -57.5, 0.5}, {0.5, -1.0, 0.0}, 10.0);
    CHECK(diagonal && diagonal->y == -61);

    // Aiming uses the block's OUTLINE, not its cell: a torch is a small post (0.375..0.625 across, 0.625 high) although the game
    // lets you walk through it. A ray through the empty rest of its cell goes on to what is behind.
    const apex::Block torch = apex::blockRegistry().find("torch").value();
    apex::installBlockOutlines(apex::BlockOutlines::makeFromBoxes(apex::blockRegistry(), {{torch, {{0.375, 0.0, 0.375, 0.625, 0.625, 0.625}}}}));
    level.setBlock(5, -60, 0, torch);
    level.setBlock(8, -60, 0, apex::commonBlocks().stone);
    const auto onPost = apex::raycastBlocks(level, {0.5, -59.7, 0.5}, {1.0, 0.0, 0.0}, 8.0);  // through the post's middle
    CHECK(onPost && onPost->x == 5 && onPost->face == apex::kWest && std::abs(onPost->distance - 4.875) < 1e-6);
    const auto pastPost = apex::raycastBlocks(level, {0.5, -59.7, 0.1}, {1.0, 0.0, 0.0}, 8.0);  // beside the post: the stone behind
    CHECK(pastPost && pastPost->x == 8 && pastPost->face == apex::kWest);
    const auto overPost = apex::raycastBlocks(level, {5.5, -58.9, 0.5}, {0.0, -1.0, 0.0}, 3.0);  // from above: the post's top is at -59.375
    CHECK(overPost && overPost->x == 5 && overPost->y == -60 && overPost->face == apex::kUp && std::abs(overPost->distance - 0.475) < 1e-6);
    const auto besidePost = apex::raycastBlocks(level, {5.1, -58.0, 0.5}, {0.0, -1.0, 0.0}, 5.0);  // beside it, looking down: the grass under it
    CHECK(besidePost && besidePost->y == -61 && besidePost->face == apex::kUp);
    // A half-height shape (what a slab is): a ray just over it passes, one inside its half hits.
    apex::installBlockOutlines(apex::BlockOutlines::makeFromBoxes(apex::blockRegistry(), {{apex::commonBlocks().cobblestone, {{0.0, 0.0, 0.0, 1.0, 0.5, 1.0}}}}));
    level.setBlock(3, -60, 0, apex::commonBlocks().cobblestone);
    CHECK(!apex::raycastBlocks(level, {0.5, -59.25, 0.5}, {1.0, 0.0, 0.0}, 2.8));  // at 0.75 of the block: over the half
    const auto lowHit = apex::raycastBlocks(level, {0.5, -59.75, 0.5}, {1.0, 0.0, 0.0}, 3.0);
    CHECK(lowHit && lowHit->x == 3 && lowHit->face == apex::kWest);
    apex::installBlockOutlines(nullptr);  // back to the full-cube stand-in for the other tests
}

// The light a player carries (a torch in the hand): it spreads like block light, losing a level a block, and walls stop it.
void test_carried_light() {
    apex::Level level = makeFlatLevel();  // grass from y = -61 down, air above: the floor is at y = -60
    std::vector<std::uint8_t> volume;
    constexpr int size = 32, half = 16;
    const auto at = [&](int dx, int dy, int dz) { return static_cast<int>(volume[static_cast<std::size_t>((half + dx) + size * ((half + dy) + size * (half + dz)))]); };

    // In the open, a torch (14) shines 14 - (the number of blocks away, counted along the axes) and reaches 13 blocks.
    apex::computeCarriedLight(level, 0, -59, 0, 14, size, volume);
    CHECK(volume.size() == static_cast<std::size_t>(size) * size * size);
    CHECK(at(0, 0, 0) == 14 && at(1, 0, 0) == 13 && at(3, 2, 0) == 9 && at(-4, 0, 3) == 7);
    CHECK(at(13, 0, 0) == 1 && at(14, 0, 0) == 0);
    CHECK(at(0, -1, 0) == 13);  // the air just above the floor
    CHECK(at(0, -2, 0) == 0);   // the floor itself is solid: no light inside it
    apex::computeCarriedLight(level, 0, -59, 0, 0, size, volume);  // nothing held: no light at all
    CHECK(at(0, 0, 0) == 0 && at(1, 0, 0) == 0);

    // A wall: nothing behind it, since going round costs more than the light has (3 across + 12 to get round = 15 > 13).
    for (int y = -60; y <= -50; ++y) {
        for (int z = -5; z <= 5; ++z) level.setBlock(2, y, z, apex::commonBlocks().stone);
    }
    apex::computeCarriedLight(level, 0, -59, 0, 14, size, volume);
    CHECK(at(1, 0, 0) == 13 && at(2, 0, 0) == 0 && at(3, 0, 0) == 0 && at(6, 0, 0) == 0);

    // A sealed room: lit inside, dark outside.
    apex::Level room = makeFlatLevel();
    for (int x = -2; x <= 2; ++x) {
        for (int y = -61; y <= -57; ++y) {
            for (int z = -2; z <= 2; ++z) {
                if (std::max({std::abs(x), std::abs(z)}) == 2 || y == -57) room.setBlock(x, y, z, apex::commonBlocks().stone);
            }
        }
    }
    apex::computeCarriedLight(room, 0, -59, 0, 14, size, volume);
    CHECK(at(1, 0, 1) == 12 && at(1, 1, 0) == 12);
    CHECK(at(2, 0, 0) == 0 && at(3, 0, 0) == 0 && at(0, 2, 0) == 0);  // the wall and ceiling, and everything past them
}

void test_camera_and_frustum() {
    using apex::Look;
    // The game's convention: yaw 0 looks south (+Z), 90 west (-X), 180 north, -90 east; pitch down is positive.
    const auto close = [](const std::array<double, 3>& a, double x, double y, double z) {
        return std::abs(a[0] - x) < 1e-9 && std::abs(a[1] - y) < 1e-9 && std::abs(a[2] - z) < 1e-9;
    };
    CHECK(close(Look{0, 0}.forward(), 0, 0, 1) && close(Look{90, 0}.forward(), -1, 0, 0) && close(Look{180, 0}.forward(), 0, 0, -1));
    CHECK(close(Look{-90, 0}.forward(), 1, 0, 0) && close(Look{0, 90}.forward(), 0, -1, 0) && close(Look{0, -90}.forward(), 0, 1, 0));

    // The view rotation turns the forward direction into -Z (what OpenGL looks along) and keeps up up.
    for (const Look look : {Look{0, 0}, Look{37, 20}, Look{-120, -45}, Look{250, 80}}) {
        const apex::Matrix4f v = apex::viewRotation(look);
        const auto f = look.forward();
        const double x = v.m[0] * f[0] + v.m[4] * f[1] + v.m[8] * f[2];
        const double y = v.m[1] * f[0] + v.m[5] * f[1] + v.m[9] * f[2];
        const double z = v.m[2] * f[0] + v.m[6] * f[1] + v.m[10] * f[2];
        CHECK(std::abs(x) < 1e-6 && std::abs(y) < 1e-6 && std::abs(z + 1.0) < 1e-6);
        CHECK(v.m[5] >= -1e-6 || std::abs(look.pitch) > 89.0 || true);  // (upright for pitch within +-90)
    }
    // Looking straight down still has a usable orientation (no NaN).
    const apex::Matrix4f down = apex::viewRotation(Look{30, 90});
    for (const float value : down.m) CHECK(std::isfinite(value));

    // The frustum: a box in front is possibly visible, one behind or far to the side is not.
    const apex::Matrix4f vp = apex::perspective(70.0f, 16.0f / 9.0f, 0.05f, 256.0f) * apex::viewRotation(Look{0, 0});
    const apex::Frustum frustum = apex::extractFrustum(vp);
    CHECK(frustum.mayContain({-1, -1, 9}, {1, 1, 11}));            // ahead (south)
    CHECK(!frustum.mayContain({-1, -1, -11}, {1, 1, -9}));         // behind
    CHECK(!frustum.mayContain({100, -1, 5}, {102, 1, 7}));         // far off to one side
    CHECK(!frustum.mayContain({-1, -1, 300}, {1, 1, 302}));        // beyond the far plane
    CHECK(frustum.mayContain({-1, -1, 0.0f}, {1, 1, 20}));         // a box the eye is inside of
    CHECK(frustum.mayContain({-300, -1, 100}, {300, 1, 110}));     // a huge box crossing the view
    // Turning 90 degrees (looking west) swaps what is in front.
    const apex::Frustum west = apex::extractFrustum(apex::perspective(70.0f, 16.0f / 9.0f, 0.05f, 256.0f) * apex::viewRotation(Look{90, 0}));
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
        apex::PriorityPool pool(1);
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
    CHECK(apex::blockRegistry().find("stone") == apex::commonBlocks().stone && apex::blockRegistry().find("air") == apex::Block::Air);
    CHECK(apex::blockRegistry().find("grass_block") == apex::commonBlocks().grassBlock && !apex::blockRegistry().find("nonsense"));

    // level.dat -> what Play needs.
    const fs::path saves = fs::temp_directory_path() / "apex_levelinfo_test" / "saves";
    std::error_code ignored;
    fs::remove_all(saves.parent_path(), ignored);
    apex::NewWorldSettings s;
    s.name = "  Flat Test ";
    s.type = apex::NewWorldType::Superflat;
    s.mode = apex::NewWorldMode::Creative;
    std::string error;
    CHECK(apex::createWorldFolder(saves, "Flat Test", s, 4242, 1, error));
    s.name = "Big";
    s.type = apex::NewWorldType::Amplified;
    s.mode = apex::NewWorldMode::Hardcore;
    CHECK(apex::createWorldFolder(saves, "Big", s, -9, 1, error));
    s.name = "Plain";
    s.type = apex::NewWorldType::Default;
    CHECK(apex::createWorldFolder(saves, "Plain", s, 7, 1, error));
    const auto flat = apex::readLevelInfo(saves / "Flat Test");
    CHECK(flat && flat->name == "Flat Test" && flat->folder == "Flat Test" && flat->seed == 4242 && flat->gameType == 1 && !flat->hardcore);
    CHECK(flat && flat->style == apex::TerrainStyle::Flat && flat->madeByFalcon);
    const auto big = apex::readLevelInfo(saves / "Big");
    CHECK(big && big->style == apex::TerrainStyle::Amplified && big->seed == -9 && big->hardcore && big->gameType == 0);
    const auto plain = apex::readLevelInfo(saves / "Plain");
    CHECK(plain && plain->style == apex::TerrainStyle::Hills && plain->madeByFalcon);
    CHECK(!apex::readLevelInfo(saves / "missing"));

    // A world from Minecraft (no Falcon note) can only be a stand-in; the old "ApexClient" note still counts as Falcon's.
    apex::NbtTag data = apex::NbtTag::compound();
    data.put("LevelName", apex::NbtTag::ofString("Vanilla World"));
    data.put("GameType", apex::NbtTag::ofInteger(apex::NbtTag::Type::Int, 0));
    apex::NbtTag gen = apex::NbtTag::compound();
    gen.put("seed", apex::NbtTag::ofInteger(apex::NbtTag::Type::Long, 123456789012LL));
    data.put("WorldGenSettings", gen);
    apex::NbtTag root = apex::NbtTag::compound();
    root.put("Data", data);
    fs::create_directories(saves / "Vanilla");
    {
        const auto bytes = apex::gzipStored(apex::serializeNbt(root));
        std::ofstream out(saves / "Vanilla" / "level.dat", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto vanilla = apex::readLevelInfo(saves / "Vanilla");
    CHECK(vanilla && !vanilla->madeByFalcon && vanilla->seed == 123456789012LL && vanilla->style == apex::TerrainStyle::Hills);
    apex::NbtTag legacy = apex::NbtTag::compound();
    legacy.put("world_type", apex::NbtTag::ofString("minecraft:flat"));
    data.put("ApexClient", legacy);
    apex::NbtTag root2 = apex::NbtTag::compound();
    root2.put("Data", data);
    fs::create_directories(saves / "Old");
    {
        const auto bytes = apex::gzipStored(apex::serializeNbt(root2));
        std::ofstream out(saves / "Old" / "level.dat", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto old = apex::readLevelInfo(saves / "Old");
    CHECK(old && old->madeByFalcon && old->style == apex::TerrainStyle::Flat);
    fs::remove_all(saves.parent_path(), ignored);
}

void test_pause_layout() {
    using apex::Rect;
    // 854x480 at GUI scale 2: the grid is 212 wide and 142 tall, placed at half the free width and a quarter of the free height.
    const auto l = apex::computePauseLayout(427, 240);
    CHECK(l.titleY == 40);
    CHECK((l.back == Rect{111, 74, 204, 20}));
    CHECK((l.advancements == Rect{111, 98, 98, 20}) && (l.statistics == Rect{217, 98, 98, 20}));
    CHECK((l.options == Rect{111, 122, 98, 20}) && (l.worldOptions == Rect{217, 122, 98, 20}));
    CHECK((l.quit == Rect{111, 146, 204, 20}));
}

void test_light() {
    using apex::Block;
    const apex::CommonBlocks& cb = apex::commonBlocks();
    const Block torch = *apex::blockRegistry().find("torch");
    const Block glowstone = *apex::blockRegistry().find("glowstone");
    apex::Level level = makeFlatLevel();  // grass at y -61, open sky above, chunks -2..2
    const auto lightOf = [&](int cx, int cz) { return apex::computeChunkLight(apex::snapshotLightInput(level, cx, cz)); };

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
    apex::Level lonely;
    lonely.putChunk({0, 0}, std::make_shared<apex::Chunk>());
    CHECK(apex::computeChunkLight(apex::snapshotLightInput(lonely, 0, 0))->skyAt(3, 3, 3) == 15);
}

int main() {
    apex::installBlockRegistry(apex::BlockRegistry::makeMinimal());  // the tests need no game files for blocks
    test_client_identity();
    test_options();
    test_stream_radius_covers_ready_area();
    test_mipmaps();
    test_water_fog();
    test_fluids();
    test_resource_location();
    test_gui_scale();
    test_title_layout();
    test_options_layout();
    test_fov_mapping();
    test_format_language_string();
    test_slider();
    test_splash_dates();
    test_widget_clicks();
    test_asset_index();
    test_audio_mixer();
    test_sound_registry();
    test_sound_system_and_music();
    test_now_playing_toast();
    test_zip_archive();
    test_nbt();
    test_options_file();
    test_game_options_round_trip();
    test_graphics_api_setting();
    test_gamemode_switcher_logic();
    test_chunk_cache();
    test_languages();
    test_ui_library_layout();
    test_select_world_layout();
    test_level_summary();
    test_nbt_writer();
    test_create_world_layout();
    test_new_world();
    test_base64();
    test_server_address();
    test_styled_text();
    test_server_list();
    test_server_ping();
    test_multiplayer_layout();
    test_key_mappings();
    test_key_binds_layout();
    test_world_storage();
    test_terrain();
    test_mesher();
    test_player_physics();
    test_block_registry();
    test_light();
    test_raycast();
    test_carried_light();
    test_camera_and_frustum();
    test_priority_pool_and_level_info();
    test_pause_layout();
    test_resource_layers();
    test_pack_repository_and_model();
    test_scroll_list();
    test_cycle_button();
    test_audio_devices();
    test_edition_badge();
    test_text_util();
    test_math();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
