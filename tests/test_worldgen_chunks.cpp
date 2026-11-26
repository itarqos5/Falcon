// D:/Projects/Falcon/tests/worldgen_chunks tests (the others are in D:/Projects/Falcon/tests/test_worldgen_chunks_*.cpp).
#include "test_worldgen_chunks_support.hpp"

#include "falcon/world/decoration.hpp"
#include "falcon/worldgen/lod_surface.hpp"

// The surface the levels of detail are drawn from (worldgen/lod_surface.hpp), against the chunks the generator really makes: the ground read from whole chunks is the
// chunk's own to a block, and the cheaper ways of getting a height are closer to it the more they are told (a corrected surface beats the bare preliminary one).
void testLodSurface(const GameData& gd, const falcon::BlockRegistry& registry) {
    std::string error;
    std::shared_ptr<const OverworldGenerator> gen = OverworldGenerator::create(gd.root, registry, 12345, error);
    CHECK(gen != nullptr);
    if (!gen) return;
    const falcon::worldgen::LodSurface lod(gen);
    const auto spawn = gen->findSpawn();
    const int baseX = static_cast<int>(std::floor(spawn.x)) >> 4, baseZ = static_cast<int>(std::floor(spawn.z)) >> 4;

    long total = 0, within4 = 0;
    double absolute = 0.0;
    for (int cz = baseZ - 2; cz <= baseZ + 2; ++cz) {
        for (int cx = baseX - 2; cx <= baseX + 2; ++cx) {
            falcon::worldgen::ChunkExtras extras;
            (void)gen->generate(cx, cz, extras);
            const falcon::worldgen::LodGrid grid = lod.grid(cx * 16, cz * 16, 1, 16, falcon::worldgen::LodMode::Chunk);
            for (int j = 0; j < 16; ++j) {
                for (int i = 0; i < 16; ++i) {
                    const int real = extras.heightmaps.worldSurface[static_cast<std::size_t>(i + 16 * j)] - 1;  // the top block (the sea's surface over water)
                    const falcon::worldgen::LodColumn& c = grid.at(i, j);
                    const int ground = std::max<int>(c.floor, falcon::worldgen::LodSurface::kSeaLevel);
                    const int off = std::abs(ground - real);
                    ++total;
                    absolute += off;
                    within4 += off <= 4;
                }
            }
        }
    }
    std::printf("  lod surface: %ld columns read from whole chunks, mean error %.2f blocks, %.1f%% within 4\n", total, absolute / static_cast<double>(total),
                100.0 * static_cast<double>(within4) / static_cast<double>(total));
    CHECK(absolute / static_cast<double>(total) < 1.0);
    CHECK(static_cast<double>(within4) > 0.98 * static_cast<double>(total));

    // The cheaper modes against that, on cells of 8 blocks.
    const auto error8 = [&](falcon::worldgen::LodMode mode) {
        const int originX = (baseX - 2) * 16, originZ = (baseZ - 2) * 16;
        const falcon::worldgen::LodGrid truth = lod.grid(originX, originZ, 8, 8, falcon::worldgen::LodMode::Chunk);
        const falcon::worldgen::LodGrid other = lod.grid(originX, originZ, 8, 8, mode);
        double sum = 0.0;
        for (int j = 0; j < 8; ++j) {
            for (int i = 0; i < 8; ++i) sum += std::abs(other.at(i, j).floor - truth.at(i, j).floor);
        }
        return sum / 64.0;
    };
    const double preliminary = error8(falcon::worldgen::LodMode::Preliminary), residual = error8(falcon::worldgen::LodMode::Residual);
    const double direct = error8(falcon::worldgen::LodMode::Direct);
    std::printf("  lod surface, cells of 8: preliminary %.2f blocks off, corrected %.2f, probed %.2f\n", preliminary, residual, direct);
    CHECK(residual <= preliminary);
    CHECK(direct <= residual);  // the probes (from a guess) read the real height of every column: what the far LODs are made of
}

// The carvers and the ores (worldgen/underground.hpp): caves cut into the rock and join across chunk borders, lava lies at the bottom of them, the ores appear in their
// stone and deepslate variants, and a chunk is the same however often it is made.
void testUnderground(const GameData& gd, const falcon::BlockRegistry& registry, const OverworldGenerator& plain) {
    using falcon::Block;
    std::string error;
    auto gen = OverworldGenerator::create(gd.root, registry, 12345, error);
    CHECK(gen != nullptr);
    if (!gen) return;
    const falcon::worldgen::Underground underground(registry, 12345);
    const OverworldGenerator* self = gen.get();
    gen->setChunkStage([&underground, self](std::vector<Block>& blocks, int cx, int cz) { underground.apply(blocks, cx, cz, *self); });

    const auto name = [&](Block b) { return registry.type(b).name; };
    long carvedAir = 0, lavaDeep = 0;
    std::map<std::string, long> found;
    long airNextToAir = 0, airAtBorder = 0;
    const auto spawn = gen->findSpawn();  // dry land: a region round it has caves under land, not only under the sea
    const int baseX = static_cast<int>(std::floor(spawn.x)) >> 4, baseZ = static_cast<int>(std::floor(spawn.z)) >> 4;
    double withStage = 0.0, without = 0.0;
    for (int cz = baseZ - 2; cz <= baseZ + 2; ++cz) {
        for (int cx = baseX - 2; cx <= baseX + 2; ++cx) {
            const auto s0 = std::chrono::steady_clock::now();
            const auto a = gen->generate(cx, cz);
            const auto s1 = std::chrono::steady_clock::now();
            const auto b = plain.generate(cx, cz);
            const auto s2 = std::chrono::steady_clock::now();
            withStage += std::chrono::duration<double, std::milli>(s1 - s0).count();
            without += std::chrono::duration<double, std::milli>(s2 - s1).count();
            const auto next = gen->generate(cx + 1, cz);  // for the seam
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    for (int y = falcon::kMinY; y < 150; ++y) {
                        const Block blockA = a->at(x, y, z), blockB = b->at(x, y, z);
                        if (registry.isAir(blockA) && !registry.isAir(blockB)) ++carvedAir;
                        if (blockA != blockB && name(blockA).find("lava") != std::string::npos && y <= -56) ++lavaDeep;
                    }
                    for (int y = falcon::kMinY; y < 320; ++y) {
                        const std::string n = name(a->at(x, y, z));
                        if (n.size() > 4 && n.compare(n.size() - 4, 4, "_ore") == 0) ++found[n];
                        if (n == "tuff" || n == "granite" || n == "gravel") ++found[n];
                    }
                }
                for (int y = -50; y < 40; ++y) {  // the seam between this chunk and the next one to the east
                    if (registry.isAir(a->at(15, y, z)) && !registry.isAir(b->at(15, y, z))) {
                        ++airAtBorder;
                        if (registry.isAir(next->at(0, y, z))) ++airNextToAir;
                    }
                }
            }
        }
    }
    std::printf("  underground: %ld cave blocks cut in 25 chunks, %ld lava blocks; a chunk takes %.1f ms with the carvers and ores, %.1f ms without\n", carvedAir, lavaDeep, withStage / 25.0, without / 25.0);
    CHECK(carvedAir > 5000);                       // caves are a few percent of the rock
    CHECK(lavaDeep > 0);                           // the deep ones are lava-lit
    for (const char* ore : {"coal_ore", "iron_ore", "copper_ore", "deepslate_iron_ore", "deepslate_redstone_ore"}) CHECK(found[ore] > 20);
    CHECK(found["tuff"] > 100 && found["gravel"] > 100);
    CHECK(airAtBorder > 50 && static_cast<double>(airNextToAir) > 0.5 * static_cast<double>(airAtBorder));  // a cave goes on over the border
    // The same chunk twice is the same, and made from other threads' point of view as well.
    const auto one = gen->generate(3, -4);
    const auto two = gen->generate(3, -4);
    bool same = true;
    for (int y = falcon::kMinY; y < falcon::kMaxY && same; ++y) {
        for (int i = 0; i < 256 && same; ++i) same = one->at(i & 15, y, i >> 4) == two->at(i & 15, y, i >> 4);
    }
    CHECK(same);
}

void testFluidTracking(const GameData& gd, const falcon::BlockRegistry& registry, const OverworldGenerator& gen) {
    GeneratorOptions opts;
    opts.trackFluidUpdates = true;
    std::string error;
    auto tracked = OverworldGenerator::create(gd.root, registry, 12345, error, opts);
    CHECK(tracked != nullptr);
    if (!tracked) return;
    long updates = 0, notFluid = 0, differing = 0;
    for (const auto& [cx, cz] : {std::pair{0, 0}, std::pair{-1, -1}, std::pair{20, -15}, std::pair{-8, -8}}) {
        ChunkExtras extras, plainExtras;
        const auto a = tracked->generate(cx, cz, extras);
        const auto b = gen.generate(cx, cz, plainExtras);
        CHECK(plainExtras.fluidUpdates.empty());
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                for (int y = falcon::kMinY; y < falcon::kMaxY; ++y) {
                    if (a->at(x, y, z) != b->at(x, y, z)) ++differing;
                }
            }
        }
        for (const std::uint32_t u : extras.fluidUpdates) {
            ++updates;
            const int x = static_cast<int>(u & 15), z = static_cast<int>((u >> 4) & 15), y = static_cast<int>(u >> 8) + falcon::kMinY;
            // the position was a fluid when the fill placed it (the surface stage may have replaced it later, e.g. by a frozen ocean)
            const falcon::Block fill = tracked->generate(cx, cz)->at(x, y, z);
            if (!tracked->blockFacts().isFluid(fill) && !tracked->blockFacts().isAir(fill) && y > 40) ++notFluid;
        }
    }
    std::printf("  fluid update tracking: %ld positions in 4 chunks\n", updates);
    checkInt("fluid tracking does not change the blocks", differing, 0);
    CHECK(updates > 0);
    (void)notFluid;
}

// Jungle trees (world/decoration.cpp): the great 2 x 2 kind with five layers of canopy and vines hanging down the trunk's sides. Seed 0 is the world Falcon starts a player in;
// a jungle chunk of it near chunk 0,48 (found through the generator's biome map) is generated, decorated, and read back block by block.
void testJungleTrees(const GameData& gd, const falcon::BlockRegistry& registry) {
    std::string error;
    const std::shared_ptr<const OverworldGenerator> gen = OverworldGenerator::create(gd.root, registry, 0, error);
    if (!gen) {
        std::fprintf(stderr, "jungle generator: %s\n", error.c_str());
        ++g_failures;
        return;
    }
    for (int cz = 44; cz <= 52; ++cz) {
        for (int cx = -4; cx <= 4; ++cx) {
            falcon::worldgen::ChunkExtras extras;
            auto chunk = gen->generate(cx, cz, extras);
            const int surface = extras.heightmaps.worldSurface[8 + 16 * 8] - 1;
            if (gen->biomes().name(gen->biomeAt(cx * 16 + 8, surface, cz * 16 + 8)) != "minecraft:jungle") continue;
            const falcon::Decorator decorator(*gen, registry, 0);
            decorator.decorate(*chunk, cx, cz, extras);
            const auto isLog = [&](int x, int y, int z) {
                return x >= 0 && x < 16 && z >= 0 && z < 16 && registry.type(chunk->at(x, y, z)).name == "jungle_log";
            };
            int logs = 0, vines = 0, greatTrunks = 0;
            for (int y = falcon::kMinY; y < falcon::kMaxY; ++y) {
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        const std::string& name = registry.type(chunk->at(x, y, z)).name;
                        if (name == "jungle_log") ++logs;
                        else if (name == "vine") ++vines;
                    }
                }
            }
            for (int y = falcon::kMinY + 1; y + 1 < falcon::kMaxY && greatTrunks == 0; ++y) {  // a 2 x 2 of logs, two blocks tall
                for (int z = 0; z + 1 < 16 && greatTrunks == 0; ++z) {
                    for (int x = 0; x + 1 < 16 && greatTrunks == 0; ++x) {
                        if (isLog(x, y, z) && isLog(x + 1, y, z) && isLog(x, y, z + 1) && isLog(x + 1, y, z + 1) && isLog(x, y + 1, z) && isLog(x + 1, y + 1, z) &&
                            isLog(x, y + 1, z + 1) && isLog(x + 1, y + 1, z + 1)) {
                            ++greatTrunks;
                        }
                    }
                }
            }
            std::printf("  jungle chunk %d,%d: %d jungle logs, %d vines, %d great trunk%s\n", cx, cz, logs, vines, greatTrunks, greatTrunks == 1 ? "" : "s");
            CHECK(logs > 0);
            CHECK(vines > 0);  // (a jungle tree that has none is fine; a whole jungle chunk of them is the bug)
            return;
        }
    }
    std::printf("  (no jungle chunk near chunk 0,48 of seed 0: the jungle's trees and vines were not checked)\n");
}

void performanceReport(const GameData& gd, const falcon::BlockRegistry& registry) {
    const std::size_t before = falcon::processWorkingSetBytes();
    std::string error;
    auto gen = OverworldGenerator::create(gd.root, registry, 777, error);
    if (!gen) {
        std::fprintf(stderr, "perf generator: %s\n", error.c_str());
        ++g_failures;
        return;
    }
    const std::size_t afterCreate = falcon::processWorkingSetBytes();
    // single thread, chunks that were not generated before
    const int n = 36;
    std::vector<std::shared_ptr<falcon::Chunk>> keep;
    (void)gen->generate(500, 500);  // warm-up: thread scratch, caches
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) keep.push_back(gen->generate(600 + i % 6, 700 + i / 6));
    const double single = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
    std::size_t chunkBytes = 0;
    for (const auto& c : keep) chunkBytes += c->memoryBytes();
    keep.clear();

    // three worker threads at once, fresh area
    const int perThread = 24;
    const auto t1 = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < perThread; ++i) (void)gen->generate(900 + t * 20 + i % 6, 900 + i / 6);
        });
    }
    for (std::thread& th : threads) th.join();
    const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    const std::size_t afterThreads = falcon::processWorkingSetBytes();
    std::printf("  performance: %.1f ms per full chunk on one thread (36 adjacent chunks); 3 threads: %.1f ms wall per chunk (%.2fx speed-up), %zu KB per chunk in memory\n", single,
                wall / (3.0 * perThread), single * 3.0 * perThread / wall, chunkBytes / n / 1024);
    std::printf("  memory: shared tables %.1f MB, per-thread scratch %.1f MB; working set grew %.1f MB when the generator was created and %.1f MB more with 3 threads running\n",
                static_cast<double>(gen->sharedMemoryBytes()) / 1048576.0, static_cast<double>(gen->perThreadScratchBytes()) / 1048576.0,
                static_cast<double>(afterCreate - std::min(afterCreate, before)) / 1048576.0, static_cast<double>(afterThreads - std::min(afterThreads, afterCreate)) / 1048576.0);
}


int main() {
    std::printf("worldgen chunk tests\n");
    testSeedHash();
    testClimateBasics();

    const falcon::AssetSearchResult assets = falcon::locateAssets(std::nullopt);
    if (!assets.location) {
        std::printf("  (tests that need the game's data skipped: game assets not found)\n");
    } else {
        GameData gd;
        gd.root = assets.location->root.parent_path();
        gd.worldgen = worldgenDirFromAssetsRoot(assets.location->root);
        if (!std::filesystem::exists(gd.worldgen / "noise_settings" / "overworld.json") || !std::filesystem::exists(gd.root / "biome-parameters" / "overworld.json")) {
            std::printf("  (tests that need the game's data skipped: %s lacks worldgen JSON or biome-parameters)\n", gd.root.string().c_str());
        } else {
            testBiomes(gd);

            // Everything below needs the game's block registry (state ids) as well.
            std::string error;
            const std::filesystem::path cache = std::filesystem::temp_directory_path() / "falcon_worldgen_chunk_test";
            const auto registry = falcon::BlockRegistry::loadFromGameData(gd.root, cache, error);
            if (!registry) {
                std::printf("  (chunk tests skipped: block registry unavailable: %s)\n", error.c_str());
            } else {
                const auto t0 = std::chrono::steady_clock::now();
                auto gen = OverworldGenerator::create(gd.root, *registry, 12345, error);
                if (!gen) {
                    std::fprintf(stderr, "generator: %s\n", error.c_str());
                    ++g_failures;
                } else {
                    std::printf("  generator created in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                    testPrimitives(*gen, *registry);
                    testJungleTrees(gd, *registry);
                    Truth truth;
                    if (!std::filesystem::exists(testDataPath("chunks_seed12345.bin"))) {
                        // (The 40 real chunks are block data the game itself made, so the repository does not carry them: this check
                        // only runs where the file has been put in tests/data. `Falcon --compare-vanilla` is the way to check
                        // against the real game from scratch.)
                        std::printf("  (the whole-chunk comparison is skipped: tests/data/chunks_seed12345.bin is not here)\n");
                    } else if (!loadTruth(testDataPath("chunks_seed12345.bin"), truth)) {
                        std::fprintf(stderr, "cannot read %s\n", testDataPath("chunks_seed12345.bin").string().c_str());
                        ++g_failures;
                    } else {
                        testChunks(*gen, *registry, truth);
                    }
                    testTerrainChecksum(*gen);
                    testChunkObject(*gen);
                    testThreads(*gen);
                    testPresetsAndSpawn(gd, *registry, *gen);
                    testErrorsAndFacts(gd, *registry, *gen);
                    testFluidTracking(gd, *registry, *gen);
                    testUnderground(gd, *registry, *gen);
                    testLodSurface(gd, *registry);
                    performanceReport(gd, *registry);
                }
            }
        }
    }
    std::printf("%ld checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
