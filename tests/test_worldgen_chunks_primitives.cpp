// D:/Projects/Falcon/tests/worldgen_chunks tests: primitives.
#include "test_worldgen_chunks_support.hpp"

// =================================================================================================================
// Primitives that the chunk dumps depend on
struct RndVec {
    const char* factory;
    int x, y, z;
    int i10, i9, i10b;
    float f0, f1;
    double d;
    float fresh;
};
const RndVec kRndVecs[] = {
#include "data/rnd_vectors.inc"
};
struct DepthVec {
    int x, z, depth;
    double secondary;
};
const DepthVec kDepthVecs[] = {
#include "data/depth_vectors.inc"
};
struct BandVec {
    int x, y, z;
    const char* block;
};
const BandVec kBandVecs[] = {
#include "data/band_vectors.inc"
};
struct AqVec {
    int x, y, z;
    float barrier, floodedness, spread, lava, exclusion;
};
const AqVec kAqVecs[] = {
#include "data/aquifer_df_vectors.inc"
};

void testPrimitives(const OverworldGenerator& gen, const falcon::BlockRegistry& registry) {
    const NoiseRouter& router = gen.router();
    const RandomState& rs = *router.randomState();

    // positional random factories (aquifer / ore / bedrock_floor / deepslate / clay_bands)
    for (const RndVec& v : kRndVecs) {
        const XoroshiroPositionalRandomFactory f = rs.randomFactory(std::string("minecraft:") + v.factory);
        XoroshiroRandomSource r = f.makeAt(v.x, v.y, v.z);
        const std::string tag = std::string(v.factory) + " at(" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + ")";
        checkInt(tag + " nextInt(10)", r.nextInt(10), v.i10);
        checkInt(tag + " nextInt(9)", r.nextInt(9), v.i9);
        checkInt(tag + " nextInt(10)'", r.nextInt(10), v.i10b);
        CHECK(r.nextFloat() == v.f0);
        CHECK(r.nextFloat() == v.f1);
        CHECK(r.nextDouble() == v.d);
        CHECK(f.makeAt(v.x, v.y, v.z).nextFloat() == v.fresh);
    }
    CHECK(rs.root().makeAt(0, 0, 0).nextDouble() == 0.04240433108530284);
    CHECK(rs.root().makeAt(5, 0, -7).nextDouble() == 0.732695518775095);
    CHECK(rs.root().makeAt(1000, 0, 2000).nextDouble() == 0.05796596146475086);

    // aquifer noise functions (point path)
    for (const AqVec& v : kAqVecs) {
        CHECK(router.aquiferBarrier().compute(v.x, v.y, v.z) == v.barrier);
        CHECK(router.aquiferFluidLevelFloodedness().compute(v.x, v.y, v.z) == v.floodedness);
        CHECK(router.aquiferFluidLevelSpread().compute(v.x, v.y, v.z) == v.spread);
        CHECK(router.aquiferLava().compute(v.x, v.y, v.z) == v.lava);
        CHECK(router.aquiferExclusion().compute(v.x, v.y, v.z) == v.exclusion);
    }
    // preliminary surface level
    CHECK(router.aquiferSurfaceLevel().compute(0, 0, 0) == 32.0f);
    CHECK(router.aquiferSurfaceLevel().compute(1000, 0, 2000) == 48.0f);
    CHECK(router.aquiferSurfaceLevel().compute(-2688, 0, 1792) == 88.0f);

    // aquifer cell centres = the positional random's first three draws
    {
        std::string err;
        const std::shared_ptr<AquiferShared> shared = AquiferShared::create(router, err);
        CHECK(shared && shared->valid);
        Aquifer aq(*shared);
        aq.begin(0, 0);
        const Aquifer::Center c0 = aq.cellCenter(0, 0, 0);
        CHECK(c0.x == 9 && c0.y == 3 && c0.z == 1);
        aq.begin(1, 3);
        const Aquifer::Center c1 = aq.cellCenter(1, 2, 3);
        CHECK(c1.x == 16 + 6 && c1.y == 2 * 12 + 0 && c1.z == 48 + 6);
        aq.begin(-1, -5);
        const Aquifer::Center c2 = aq.cellCenter(-1, -2, -5);
        CHECK(c2.x == -16 + 5 && c2.y == -2 * 12 + 0 && c2.z == -80 + 2);
        // The global fluid picker and the lock-free cache
        CHECK(shared->globalFluid(-55).at(-55) == Fluid::Lava && shared->globalFluid(-54).at(-54) == Fluid::Water && shared->globalFluid(62).at(62) == Fluid::Water &&
              shared->globalFluid(63).at(63) == Fluid::Air && shared->globalFluid(-54).level == 63 && shared->globalFluid(-60).level == -54);
        QuartColumnCache cache(8);
        int v = 0;
        CHECK(!cache.find(3, -4, v));
        cache.store(3, -4, 88);
        CHECK(cache.find(3, -4, v) && v == 88);
        cache.store(3, -4, -17);
        CHECK(cache.find(3, -4, v) && v == -17 && !cache.find(-4, 3, v));
        cache.store(-5000000, 7000000, 5);
        CHECK(cache.find(-5000000, 7000000, v) && v == 5);
    }

    // surface depth, secondary noise, clay bands
    const MaterialSystem& mat = gen.material();
    for (const DepthVec& v : kDepthVecs) {
        checkInt("surfaceDepth(" + std::to_string(v.x) + "," + std::to_string(v.z) + ")", mat.surfaceDepth(v.x, v.z), v.depth);
        CHECK(mat.surfaceSecondary(v.x, v.z) == v.secondary);
    }
    for (const BandVec& v : kBandVecs) {
        const auto want = registry.parse(v.block);
        CHECK(want && mat.clayBand(v.x, v.y, v.z) == *want);
    }
    {
        const std::string expected =
            "WRRTTOTTTLWLTTTTLWLBOTTWLTTBBOTBBBOTOTTOTLWLTTTOTLWBBBBTTTYYYYBBBLWOTTTOTOTTTTOTTTTOWLOTOTTTOTTTOTTYYTLWOTTTTOLWTYTORLWTTOTTTRTTTOTOTLWBBOTTOTTYYRRTTTOWLYYLWLBRRRTTOTTTTOTTTTBBBBTOTOYYYYTTTTTO";
        CHECK(expected.size() == 192);
        const std::map<char, const char*> names = {{'T', "minecraft:terracotta"},     {'O', "minecraft:orange_terracotta"}, {'Y', "minecraft:yellow_terracotta"},
                                                   {'B', "minecraft:brown_terracotta"}, {'R', "minecraft:red_terracotta"},    {'W', "minecraft:white_terracotta"},
                                                   {'L', "minecraft:light_gray_terracotta"}};
        int bad = 0;
        for (std::size_t i = 0; i < 192; ++i) {
            const auto want = registry.parse(names.at(expected[i]));
            if (!want || mat.clayBands()[i] != *want) ++bad;
        }
        checkInt("clay band array (192 entries)", bad, 0);
    }
}

// =================================================================================================================
// Chunk building, threads, presets, spawn
// The terrain of a few chunks of seed 12345, as one number: every block of eight chunks in different parts of the world, in order, hashed (FNV-1a). The number was made by the Windows
// build, whose chunks are the ones the game makes (the whole-chunk comparison above); this keeps every other system, compiler and C library to the same terrain (the generator is
// float and double arithmetic and the C library's own functions, which are not the same everywhere), and a later change from changing it without anybody noticing.
void testTerrainChecksum(const OverworldGenerator& gen) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& [cx, cz] : {std::pair{0, 0}, std::pair{-8, -8}, std::pair{44, 22}, std::pair{-168, 112}, std::pair{3, -7}, std::pair{100, 100}, std::pair{-50, 60}, std::pair{13, 13}}) {
        const std::shared_ptr<falcon::Chunk> chunk = gen.generate(cx, cz);
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                for (int y = falcon::kMinY; y < falcon::kMaxY; ++y) {
                    hash = (hash ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunk->at(x, y, z)))) * 1099511628211ull;
                }
            }
        }
    }
    constexpr std::uint64_t kExpected = 0x3566119324597cc7ull;
    if (hash != kExpected) std::fprintf(stderr, "terrain checksum of seed 12345: 0x%016llx\n", static_cast<unsigned long long>(hash));
    CHECK(hash == kExpected);
}

void testChunkObject(const OverworldGenerator& gen) {
    for (const auto& [cx, cz] : {std::pair{0, 0}, std::pair{-8, -8}, std::pair{44, 22}, std::pair{-168, 112}}) {
        DenseChunk dense;
        gen.generateDense(cx, cz, OverworldGenerator::Stage::Surface, dense);
        ChunkExtras extras;
        const std::shared_ptr<falcon::Chunk> chunk = gen.generate(cx, cz, extras);
        const std::shared_ptr<falcon::Chunk> plain = gen.generate(cx, cz);
        long bad = 0, solid = 0;
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                for (int y = falcon::kMinY; y < falcon::kMaxY; ++y) {
                    const falcon::Block want = dense.blocks[static_cast<std::size_t>(y - falcon::kMinY) + 384u * static_cast<std::size_t>(x + 16 * z)];
                    if (chunk->at(x, y, z) != want || plain->at(x, y, z) != want) ++bad;
                    if (want != falcon::Block::Air) ++solid;
                }
            }
        }
        checkInt("Chunk sections hold the dense blocks of chunk " + std::to_string(cx) + "," + std::to_string(cz), bad, 0);
        CHECK(solid > 10000);
        // heightmaps: WORLD_SURFACE >= OCEAN_FLOOR, MOTION_BLOCKING in between; the surface height matches the dense array
        long hbad = 0;
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const int i = x + 16 * z;
                const int ws = extras.heightmaps.worldSurface[static_cast<std::size_t>(i)];
                const int of = extras.heightmaps.oceanFloor[static_cast<std::size_t>(i)];
                const int mb = extras.heightmaps.motionBlocking[static_cast<std::size_t>(i)];
                if (!(ws >= mb && mb >= of)) ++hbad;
                if (ws - 1 >= falcon::kMinY && chunk->at(x, ws - 1, z) == falcon::Block::Air) ++hbad;
                if (ws < falcon::kMaxY && chunk->at(x, ws, z) != falcon::Block::Air) ++hbad;
                if (gen.blockFacts().blocksMotion(chunk->at(x, of - 1, z)) == false && of > falcon::kMinY) ++hbad;
                // the biome storage is the quart-resolution noise biome
                const BiomeId b = extras.biomes.atBlock(x, 64, z);
                if (b != gen.biomeSource().noiseBiome(cx * 4 + (x >> 2), 64 >> 2, cz * 4 + (z >> 2))) ++hbad;
            }
        }
        checkInt("heightmaps and biome storage of chunk " + std::to_string(cx) + "," + std::to_string(cz), hbad, 0);
    }
    // a chunk deep in the ocean has uniform sections that are shared, not copied
    {
        const std::shared_ptr<falcon::Chunk> a = gen.generate(0, 0);
        const std::shared_ptr<falcon::Chunk> b = gen.generate(0, 0);
        CHECK(a->sections[23] == nullptr && b->sections[23] == nullptr);  // all air at the top
        CHECK(a->sections[0] != nullptr);
        std::size_t shared = 0;
        for (int i = 0; i < falcon::kSectionCount; ++i) {
            if (a->sections[static_cast<std::size_t>(i)] && a->sections[static_cast<std::size_t>(i)]->isUniform() &&
                a->sections[static_cast<std::size_t>(i)] == b->sections[static_cast<std::size_t>(i)]) {
                ++shared;
            }
        }
        std::printf("  chunk (0,0): %zu of %d sections are shared uniform sections, %zu bytes\n", shared, falcon::kSectionCount, a->memoryBytes());
    }
}

std::uint64_t chunkChecksum(const falcon::Chunk& chunk) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (int y = falcon::kMinY; y < falcon::kMaxY; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                h ^= falcon::stateId(chunk.at(x, y, z));
                h *= 0x100000001b3ull;
            }
        }
    }
    return h;
}

void testThreads(const OverworldGenerator& gen) {
    const std::vector<std::pair<int, int>> coords = {{0, 0}, {-1, -1}, {5, 7}, {-12, 3}, {44, 22}, {18, 98}, {-8, -8}, {3, -9}, {100, 100}, {-60, 45}, {7, 7}, {-3, 12}};
    std::vector<std::uint64_t> single(coords.size());
    for (std::size_t i = 0; i < coords.size(); ++i) single[i] = chunkChecksum(*gen.generate(coords[i].first, coords[i].second));
    std::vector<std::uint64_t> multi(coords.size(), 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&, t]() {
            for (std::size_t i = static_cast<std::size_t>(t); i < coords.size(); i += 3) multi[i] = chunkChecksum(*gen.generate(coords[i].first, coords[i].second));
        });
    }
    for (std::thread& th : threads) th.join();
    int bad = 0;
    for (std::size_t i = 0; i < coords.size(); ++i) {
        if (single[i] != multi[i]) ++bad;
    }
    checkInt("3 worker threads produce the same chunks as one thread", bad, 0);

    // Generating a chunk again (a second instance, other query history) gives the same blocks; another seed does not.
    std::string error;
    const std::filesystem::path gameData = falcon::locateAssets(std::nullopt).location->root.parent_path();
    const std::filesystem::path cache = std::filesystem::temp_directory_path() / "falcon_worldgen_chunk_test";
    const auto registry = falcon::BlockRegistry::loadFromGameData(gameData, cache, error);
    if (registry) {
        auto again = OverworldGenerator::create(gameData, *registry, 12345, error);
        auto other = OverworldGenerator::create(gameData, *registry, -987654321, error);
        CHECK(again && other);
        if (again && other) {
            int sameAgain = 0, sameOther = 0;
            for (std::size_t i = 0; i < 6; ++i) {
                (void)again->generate(900 + static_cast<int>(i), 5);  // other history first
                if (chunkChecksum(*again->generate(coords[i].first, coords[i].second)) == single[i]) ++sameAgain;
                if (chunkChecksum(*other->generate(coords[i].first, coords[i].second)) == single[i]) ++sameOther;
            }
            checkInt("a second generator with the same seed makes identical chunks", sameAgain, 6);
            CHECK(sameOther == 0);
            // the edge of the world: 29.99 million blocks away, negative and positive
            for (const auto& [cx, cz] : {std::pair{1874999, -1874999}, std::pair{-1875000, 1875000}}) {
                const auto a = again->generate(cx, cz);
                const auto b = again->generate(cx, cz);
                CHECK(chunkChecksum(*a) == chunkChecksum(*b) && a->at(0, falcon::kMinY, 0) != falcon::Block::Air);
            }
        }
    }
}

void testPresetsAndSpawn(const GameData& gd, const falcon::BlockRegistry& registry, const OverworldGenerator& gen) {
    GeneratorOptions normal, amplified, large, single;
    std::string error;
    CHECK(loadWorldPreset(gd.root, "minecraft:normal", normal, error) && normal.noiseSettings == "minecraft:overworld" && normal.singleBiome.empty());
    CHECK(loadWorldPreset(gd.root, "minecraft:amplified", amplified, error) && amplified.noiseSettings == "minecraft:amplified");
    CHECK(loadWorldPreset(gd.root, "minecraft:large_biomes", large, error) && large.noiseSettings == "minecraft:large_biomes");
    CHECK(loadWorldPreset(gd.root, "minecraft:single_biome_surface", single, error) && single.singleBiome == "minecraft:plains");
    CHECK(!loadWorldPreset(gd.root, "minecraft:flat", normal, error));

    // Amplified: same pipeline, much taller terrain somewhere
    {
        auto amp = OverworldGenerator::create(gd.root, registry, 12345, error, amplified);
        if (!amp) {
            std::fprintf(stderr, "amplified: %s\n", error.c_str());
            ++g_failures;
        } else {
            int tallest = 0, tallestNormal = 0;
            for (int cx = -6; cx <= 6; cx += 3) {
                for (int cz = -6; cz <= 6; cz += 3) {
                    tallest = std::max(tallest, amp->surfaceHeightAt(cx * 16, cz * 16));
                    tallestNormal = std::max(tallestNormal, gen.surfaceHeightAt(cx * 16, cz * 16));
                }
            }
            std::printf("  amplified: tallest sampled surface %d (normal %d)\n", tallest, tallestNormal);
            CHECK(tallest > tallestNormal);
            ChunkExtras extras;
            const auto chunk = amp->generate(3, -4, extras);
            CHECK(chunk && chunk->at(0, falcon::kMinY, 0) != falcon::Block::Air);
        }
        auto lg = OverworldGenerator::create(gd.root, registry, 12345, error, large);
        CHECK(lg && lg->generate(2, 2) != nullptr);
    }
    // Single biome: plains everywhere, surface rules still run (grass on top of dry land)
    {
        auto sb = OverworldGenerator::create(gd.root, registry, 12345, error, single);
        CHECK(sb != nullptr);
        if (sb) {
            CHECK(sb->biomeNameAt(100, 70, -300) == "minecraft:plains" && sb->biomeNameAt(-5000, -64, 5000) == "minecraft:plains");
            ChunkExtras extras;
            const auto chunk = sb->generate(0, 0, extras);
            const BiomeId plains = sb->biomes().find("plains");
            CHECK(extras.biomes.at(1, 10, 2) == plains && extras.biomes.at(3, -16, 3) == plains);
        }
    }

    // Spawn: dry ground, found without trouble
    const OverworldGenerator::Spawn sp = gen.findSpawn();
    std::printf("  spawn for seed 12345: %.1f %.1f %.1f (biome %s)\n", sp.x, sp.y, sp.z, std::string(gen.biomeNameAt(static_cast<int>(std::floor(sp.x)), static_cast<int>(sp.y), static_cast<int>(std::floor(sp.z)))).c_str());
    CHECK(sp.y >= gen.seaLevel());
    const auto chunk = gen.generate(static_cast<int>(std::floor(sp.x)) >> 4, static_cast<int>(std::floor(sp.z)) >> 4);
    const int lx = static_cast<int>(std::floor(sp.x)) & 15, lz = static_cast<int>(std::floor(sp.z)) & 15;
    const int sy = static_cast<int>(sp.y);
    CHECK(chunk->at(lx, sy, lz) == falcon::Block::Air && chunk->at(lx, sy - 1, lz) != falcon::Block::Air && !gen.blockFacts().isFluid(chunk->at(lx, sy - 1, lz)));
    // other seeds: a spawn is always found, on dry ground with air above
    for (const std::int64_t seed : {1ll, -1ll, 987654321ll, 42ll, 2024ll}) {
        auto other = OverworldGenerator::create(gd.root, registry, seed, error);
        CHECK(other != nullptr);
        if (!other) continue;
        const OverworldGenerator::Spawn s = other->findSpawn();
        const int bx = static_cast<int>(std::floor(s.x)), bz = static_cast<int>(std::floor(s.z)), by = static_cast<int>(s.y);
        const auto c = other->generate(bx >> 4, bz >> 4);
        const falcon::Block below = c->at(bx & 15, by - 1, bz & 15);
        std::printf("  spawn for seed %lld: %d %d %d (%s)\n", static_cast<long long>(seed), bx, by, bz, std::string(other->biomeNameAt(bx, by, bz)).c_str());
        CHECK(by >= other->seaLevel() && c->at(bx & 15, by, bz & 15) == falcon::Block::Air && !other->blockFacts().isFluid(below) && !other->blockFacts().isAir(below));
    }
    // the estimate of the surface height without generating a chunk agrees with the real chunk
    int agree = 0, total = 0;
    for (int i = 0; i < 24; ++i) {
        const int x = -70 + i * 13, z = 40 - i * 9;
        const auto c = gen.generate(x >> 4, z >> 4);
        int top = falcon::kMinY;
        for (int y = falcon::kMaxY - 1; y >= falcon::kMinY; --y) {
            const falcon::Block b = c->at(x & 15, y, z & 15);
            if (b != falcon::Block::Air && !gen.blockFacts().isFluid(b)) {
                top = y;
                break;
            }
        }
        ++total;
        if (std::abs(top - gen.surfaceHeightAt(x, z)) <= 2) ++agree;
    }
    std::printf("  surfaceHeightAt agrees (+-2) with the generated chunk in %d of %d columns\n", agree, total);
    CHECK(agree >= total * 3 / 4);
}

void testErrorsAndFacts(const GameData& gd, const falcon::BlockRegistry& registry, const OverworldGenerator& gen) {
    std::string error;
    CHECK(OverworldGenerator::create(gd.root / "does-not-exist", registry, 1, error) == nullptr && !error.empty());
    error.clear();
    GeneratorOptions bad;
    bad.noiseSettings = "minecraft:nope";
    CHECK(OverworldGenerator::create(gd.root, registry, 1, error, bad) == nullptr && !error.empty());
    error.clear();
    GeneratorOptions badBiome;
    badBiome.singleBiome = "minecraft:not_a_biome";
    CHECK(OverworldGenerator::create(gd.root, registry, 1, error, badBiome) == nullptr && error.find("not_a_biome") != std::string::npos);
    error.clear();
    CHECK(BiomeRegistry::load(gd.root / "nowhere", error) == nullptr && !error.empty());
    // The minimal built-in registry has no deepslate: the generator refuses it with a message instead of crashing
    error.clear();
    const auto minimal = falcon::BlockRegistry::makeMinimal();
    CHECK(OverworldGenerator::create(gd.root, *minimal, 1, error) == nullptr && !error.empty());

    // block facts the generator relies on
    const BlockFacts& f = gen.blockFacts();
    const falcon::Block air = *registry.find("air"), stone = *registry.find("stone"), water = *registry.find("water"), lava = *registry.find("lava"),
                      deepslate = *registry.find("deepslate"), caveAir = *registry.find("cave_air"), ice = *registry.find("packed_ice");
    CHECK(f.isAir(air) && f.isAir(caveAir) && !f.isAir(stone) && !f.isAir(water));
    CHECK(f.isFluid(water) && f.isFluid(lava) && !f.isFluid(stone) && !f.isFluid(air));
    CHECK(f.blocksMotion(stone) && f.blocksMotion(deepslate) && f.blocksMotion(ice) && !f.blocksMotion(water) && !f.blocksMotion(air));
    CHECK(f.isMotionBlocking(stone) && f.isMotionBlocking(water) && f.isMotionBlocking(lava) && !f.isMotionBlocking(air));

    // the preset mapping
    CHECK(std::string(worldPresetFor(falcon::TerrainStyle::Hills)) == "minecraft:normal" && std::string(worldPresetFor(falcon::TerrainStyle::Amplified)) == "minecraft:amplified" &&
          worldPresetFor(falcon::TerrainStyle::Flat) == nullptr);
    CHECK(std::string(worldPresetFor(falcon::NewWorldType::Default)) == "minecraft:normal" && std::string(worldPresetFor(falcon::NewWorldType::LargeBiomes)) == "minecraft:large_biomes" &&
          std::string(worldPresetFor(falcon::NewWorldType::Amplified)) == "minecraft:amplified" && std::string(worldPresetFor(falcon::NewWorldType::SingleBiome)) == "minecraft:single_biome_surface" &&
          worldPresetFor(falcon::NewWorldType::Superflat) == nullptr);
}
