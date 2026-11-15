// D:/Projects/Falcon/tests/worldgen_chunks tests (the others are in D:/Projects/Falcon/tests/test_worldgen_chunks_*.cpp).
#include "test_worldgen_chunks_support.hpp"

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
                    performanceReport(gd, *registry);
                }
            }
        }
    }
    std::printf("%ld checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
