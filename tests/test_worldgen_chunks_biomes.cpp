// D:/Projects/Falcon/tests/worldgen_chunks tests: biomes.
#include "test_worldgen_chunks_support.hpp"

// =================================================================================================================
// SHA-256 and the obfuscated seed                                   [REAL: BiomeManager.obfuscateSeed, truth3]
void testSeedHash() {
    checkStr("sha256('')", hex(sha256("", 0)), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    checkStr("sha256('abc')", hex(sha256("abc", 3)), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 56 bytes: padding spills into a second block
    checkStr("sha256(56 bytes)", hex(sha256(two.data(), two.size())), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string million(1000000, 'a');
    checkStr("sha256(1e6 a)", hex(sha256(million.data(), million.size())), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    struct Obf {
        std::int64_t seed;
        std::int64_t want;
    };
    const Obf vecs[] = {{0, 8794265229978523055ll},
                        {1, -6467378160175308932ll},
                        {-1, 6759447113877070610ll},
                        {12345, 293737985876514017ll},
                        {INT64_MIN, 6374347445474471398ll},
                        {INT64_MAX, 7179146226492139882ll},
                        {987654321987ll, 753886273564143802ll}};
    for (const Obf& v : vecs) checkInt("obfuscateSeed(" + std::to_string(v.seed) + ")", obfuscateSeed(v.seed), v.want);
}

// =================================================================================================================
// Quantisation and search with synthetic data (no game files)
void testClimateBasics() {
    using namespace climate;
    CHECK(quantize(-1.05f) == -10500);
    CHECK(quantize(0.0f) == 0 && quantize(1.0f) == 10000 && quantize(-0.455f) == -4550);
    CHECK(quantize(0.99999f) == 9999 && quantize(-0.00009f) == 0);
    const Parameter p{-100, 200};
    CHECK(p.distance(-100) == 0 && p.distance(200) == 0 && p.distance(0) == 0 && p.distance(-101) == 1 && p.distance(205) == 5 && p.distance(-150) == 50);

    // A tiny registry-free list: the tree must agree with brute force on a grid of queries (no exact ties on this data).
    ParameterList list;
    std::uint32_t s = 12345;
    const auto next = [&]() {
        s = s * 1664525u + 1013904223u;
        return static_cast<std::int64_t>((s >> 8) % 20001) - 10000;
    };
    for (int i = 0; i < 600; ++i) {
        ParameterPoint pt;
        for (int d = 0; d < kDims; ++d) {
            const std::int64_t a = next(), b = next();
            pt.space[d] = d == 6 ? Parameter{0, 0} : Parameter{std::min(a, b), std::max(a, b)};
        }
        list.points.push_back(pt);
        list.biomes.push_back(static_cast<BiomeId>(i));
    }
    const RTree tree(list);
    RTreeCache cache;
    int mismatches = 0;
    for (int i = 0; i < 4000; ++i) {
        TargetPoint t{next(), next(), next(), next(), next(), next()};
        const std::int32_t brute = RTree::bruteForce(list, t);
        const std::int32_t leaf = tree.searchLeaf(t, cache);
        if (fitness(list.points[static_cast<std::size_t>(leaf)], t) != fitness(list.points[static_cast<std::size_t>(brute)], t)) ++mismatches;
    }
    checkInt("rtree vs brute force (fitness), synthetic list", mismatches, 0);
    CHECK(tree.leafCount() == 600 && tree.depth() >= 2);
}

struct BiomeVec {
    int x, y, z;
    std::int64_t t, h, c, e, d, w;
    const char* noiseBiome;
    const char* zoomedBiome;
};
const BiomeVec kBiomeVecs[] = {
#include "data/biome_vectors.inc"
};
struct ZoomVec {
    int x, y, z;
    const char* zoomed;
    const char* cell;
};
const ZoomVec kZoomVecs[] = {
#include "data/zoom_vectors.inc"
};


void testBiomes(const GameData& gd) {
    std::string error;
    const auto registry = BiomeRegistry::load(gd.worldgen / "biome", error);
    if (!registry) {
        std::fprintf(stderr, "biome registry: %s\n", error.c_str());
        ++g_failures;
        return;
    }
    CHECK(registry->size() == 67);
    const BiomeId plains = registry->find("minecraft:plains");
    CHECK(plains != kNoBiome && registry->find("plains") == plains && registry->find("minecraft:nope") == kNoBiome);
    CHECK(registry->info(plains).temperature == 0.8f && registry->info(plains).downfall == 0.4f && registry->info(plains).hasPrecipitation);
    const BiomeInfo& swamp = registry->info(registry->find("swamp"));
    CHECK(swamp.grassColorModifier == GrassColorModifier::Swamp && swamp.waterColor == 0x617b64u && swamp.foliageColor == 0x6a7039u);
    CHECK(registry->info(registry->find("frozen_ocean")).temperatureModifier == TemperatureModifier::Frozen && registry->info(registry->find("frozen_ocean")).temperature == 0.0f);
    CHECK(registry->info(registry->find("badlands")).grassColor.has_value() || !registry->info(registry->find("badlands")).hasPrecipitation);
    CHECK(registry->info(registry->find("sulfur_caves")).name == "minecraft:sulfur_caves");

    const std::int64_t seed = 12345;
    auto router = NoiseRouter::create(gd.worldgen, seed, error);
    if (!router) {
        std::fprintf(stderr, "router: %s\n", error.c_str());
        ++g_failures;
        return;
    }
    const std::filesystem::path paramFile = gd.root / "biome-parameters" / "overworld.json";
    auto source = BiomeSource::create(paramFile, registry, *router, seed, error);
    if (!source) {
        std::fprintf(stderr, "biome source: %s\n", error.c_str());
        ++g_failures;
        return;
    }
    CHECK(source->parameters().points.size() == 7594);
    checkInt("zoom seed", source->zoomSeed(), 293737985876514017ll);

    BiomeSource::Context ctx(*source);
    // The 76 climate vectors and the biomes at them (noise biome of the quart cell, zoomed biome of the block).
    int climateBad = 0, noiseBad = 0, zoomBad = 0;
    for (const BiomeVec& v : kBiomeVecs) {
        const climate::TargetPoint t = source->sampleClimate(ctx, v.x >> 2, v.y >> 2, v.z >> 2);
        if (t.temperature != v.t || t.humidity != v.h || t.continentalness != v.c || t.erosion != v.e || t.depth != v.d || t.weirdness != v.w) {
            ++climateBad;
            std::fprintf(stderr, "MISMATCH climate at %d %d %d: got %lld %lld %lld %lld %lld %lld want %lld %lld %lld %lld %lld %lld\n", v.x, v.y, v.z,
                         static_cast<long long>(t.temperature), static_cast<long long>(t.humidity), static_cast<long long>(t.continentalness),
                         static_cast<long long>(t.erosion), static_cast<long long>(t.depth), static_cast<long long>(t.weirdness), static_cast<long long>(v.t),
                         static_cast<long long>(v.h), static_cast<long long>(v.c), static_cast<long long>(v.e), static_cast<long long>(v.d), static_cast<long long>(v.w));
        }
        const BiomeId noise = source->noiseBiome(ctx, v.x >> 2, v.y >> 2, v.z >> 2);
        if (registry->name(noise) != v.noiseBiome) {
            ++noiseBad;
            std::fprintf(stderr, "MISMATCH noise biome at %d %d %d: got %s want %s\n", v.x, v.y, v.z, registry->name(noise).c_str(), v.noiseBiome);
        }
        const BiomeId zoomed = source->biomeAt(ctx, v.x, v.y, v.z);
        if (registry->name(zoomed) != v.zoomedBiome) {
            ++zoomBad;
            std::fprintf(stderr, "MISMATCH zoomed biome at %d %d %d: got %s want %s\n", v.x, v.y, v.z, registry->name(zoomed).c_str(), v.zoomedBiome);
        }
    }
    checkInt("climate vectors", climateBad, 0);
    checkInt("noise biome vectors", noiseBad, 0);
    checkInt("zoomed biome (A lines)", zoomBad, 0);
    CHECK(sizeof(kBiomeVecs) / sizeof(kBiomeVecs[0]) == 76);

    // Positions where the zoom changes the biome (truth4).
    int zb = 0;
    for (const ZoomVec& v : kZoomVecs) {
        const BiomeId zoomed = source->biomeAt(ctx, v.x, v.y, v.z);
        const BiomeId cell = source->noiseBiome(ctx, v.x >> 2, v.y >> 2, v.z >> 2);
        if (registry->name(zoomed) != v.zoomed || registry->name(cell) != v.cell) {
            ++zb;
            std::fprintf(stderr, "MISMATCH zoom case %d %d %d: got %s (cell %s) want %s (cell %s)\n", v.x, v.y, v.z, registry->name(zoomed).c_str(),
                         registry->name(cell).c_str(), v.zoomed, v.cell);
        }
    }
    checkInt("zoom cases (truth4)", zb, 0);
    CHECK(sizeof(kZoomVecs) / sizeof(kZoomVecs[0]) == 40);

    // The R-tree against brute force on real climate points: differences are allowed only on exact fitness ties.
    {
        BiomeSource::Context c2(*source);
        std::uint32_t rs = 77;
        const auto rnd = [&](int n) {
            rs = rs * 1664525u + 1013904223u;
            return static_cast<int>((rs >> 8) % static_cast<std::uint32_t>(n));
        };
        int differing = 0, nonTies = 0;
        const int total = 6000;
        for (int i = 0; i < total; ++i) {
            const int qx = rnd(4001) - 2000, qz = rnd(4001) - 2000, qy = rnd(96) - 16;
            const climate::TargetPoint t = source->sampleClimate(c2, qx, qy, qz);
            const std::int32_t leaf = source->tree().searchLeaf(t, c2.rtreeCache());
            const std::int32_t brute = climate::RTree::bruteForce(source->parameters(), t);
            if (leaf != brute) {
                ++differing;
                const std::int64_t fa = climate::fitness(source->parameters().points[static_cast<std::size_t>(leaf)], t);
                const std::int64_t fb = climate::fitness(source->parameters().points[static_cast<std::size_t>(brute)], t);
                if (fa != fb) ++nonTies;
            }
        }
        std::printf("  rtree vs brute force on %d climate points: %d differ, of which %d are not exact fitness ties\n", total, differing, nonTies);
        checkInt("rtree: every difference from brute force is an exact fitness tie", nonTies, 0);

        // The canonical search (what BiomeSource uses) is a pure function of the target: the previous query does not matter,
        // and its fitness is the minimum.
        int canonicalDiffers = 0, canonicalNotMinimal = 0;
        climate::RTreeCache fresh, warm;
        for (int i = 0; i < total; ++i) {
            const int qx = rnd(4001) - 2000, qz = rnd(4001) - 2000, qy = rnd(96) - 16;
            const climate::TargetPoint t = source->sampleClimate(c2, qx, qy, qz);
            fresh.lastLeaf = -1;
            warm.lastLeaf = static_cast<std::int32_t>(rnd(7594));
            const std::int32_t a = source->tree().searchLeafCanonical(t, fresh);
            const std::int32_t b = source->tree().searchLeafCanonical(t, warm);
            if (a != b) ++canonicalDiffers;
            const std::int32_t brute = climate::RTree::bruteForce(source->parameters(), t);
            if (climate::fitness(source->parameters().points[static_cast<std::size_t>(a)], t) != climate::fitness(source->parameters().points[static_cast<std::size_t>(brute)], t)) ++canonicalNotMinimal;
        }
        checkInt("rtree canonical search does not depend on the previous query", canonicalDiffers, 0);
        checkInt("rtree canonical search finds the minimum fitness", canonicalNotMinimal, 0);
    }
}
