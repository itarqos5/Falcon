// Bit-exact tests for the second half of terrain generation: biomes (SHA-256 seed, climate quantisation, the R-tree,
// the BiomeManager zoom), the aquifer, ore veins, bedrock/deepslate, clay bands, the surface rules and whole chunks.
//
// Ground truth comes from the game's own classes (Minecraft 26.3), run in a standalone harness for seed 12345:
//   biome_vectors.inc / zoom_vectors.inc   climate vectors and zoomed biomes (text, generated from the harness dumps)
//   chunks_seed12345.bin                   40 whole chunks (fill and surface, every block) in a compact run-length format
// Everything is embedded or lives next to this file; the tests that need the game's JSON data find it through
// locateAssets() and print a skip message when it is not there.

#include "falcon/assets/asset_locator.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/worldgen/aquifer.hpp"
#include "falcon/worldgen/biome.hpp"
#include "falcon/worldgen/material.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef FALCON_TEST_DATA_DIR
#define FALCON_TEST_DATA_DIR "tests/data"
#endif

using namespace falcon::worldgen;

namespace {

int g_failures = 0;
long g_checks = 0;

#define CHECK(expr)                                                                          \
    do {                                                                                     \
        ++g_checks;                                                                          \
        if (!(expr)) {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (false)

void checkInt(const std::string& what, long long got, long long want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got %lld want %lld\n", what.c_str(), got, want);
        ++g_failures;
    }
}
void checkStr(const std::string& what, const std::string& got, const std::string& want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got %s want %s\n", what.c_str(), got.c_str(), want.c_str());
        ++g_failures;
    }
}

std::string hex(const std::array<std::uint8_t, 32>& d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : d) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

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

// =================================================================================================================
struct GameData {
    std::filesystem::path root;       // the game data directory (parent of the assets root)
    std::filesystem::path worldgen;   // <root>/data/minecraft/worldgen
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

// =================================================================================================================
// Whole-chunk ground truth (chunks_seed12345.bin: converted from the harness' FILL / SURF run-length dumps)
struct TruthColumn {
    int top = 0;     // WORLD_SURFACE_WG height (y of the highest non-air block)
    int biome = 0;   // index into Truth::biomes (SURF only)
    std::vector<std::pair<int, int>> runs;  // (length, palette index), from y = 319 downward
};
struct TruthChunk {
    int cx = 0, cz = 0;
    bool hasFill = false, hasSurf = false;
    std::vector<TruthColumn> fill, surf;  // 256 each, index x + 16 * z
};
struct Truth {
    std::vector<std::string> palette, biomes;
    std::vector<TruthChunk> chunks;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& d) : d_(d) {}
    [[nodiscard]] std::uint32_t var() {
        std::uint32_t v = 0;
        int shift = 0;
        while (pos_ < d_.size()) {
            const std::uint8_t b = d_[pos_++];
            v |= static_cast<std::uint32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
            shift += 7;
        }
        bad_ = true;
        return 0;
    }
    [[nodiscard]] int zig() {
        const std::uint32_t v = var();
        return static_cast<int>(v >> 1) ^ -static_cast<int>(v & 1);
    }
    [[nodiscard]] std::uint8_t byte() { return pos_ < d_.size() ? d_[pos_++] : (bad_ = true, std::uint8_t{0}); }
    [[nodiscard]] std::string str() {
        const std::uint32_t n = var();
        std::string s;
        for (std::uint32_t i = 0; i < n; ++i) s += static_cast<char>(byte());
        return s;
    }
    [[nodiscard]] bool bad() const { return bad_; }

private:
    const std::vector<std::uint8_t>& d_;
    std::size_t pos_ = 0;
    bool bad_ = false;
};

bool loadTruth(const std::filesystem::path& file, Truth& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 5 || std::memcmp(data.data(), "FWGC", 4) != 0 || data[4] != 1) return false;
    std::vector<std::uint8_t> body(data.begin() + 5, data.end());
    Reader r(body);
    const std::uint32_t pal = r.var();
    for (std::uint32_t i = 0; i < pal; ++i) out.palette.push_back(r.str());
    const std::uint32_t bio = r.var();
    for (std::uint32_t i = 0; i < bio; ++i) out.biomes.push_back(r.str());
    const std::uint32_t count = r.var();
    for (std::uint32_t c = 0; c < count && !r.bad(); ++c) {
        TruthChunk ch;
        ch.cx = r.zig();
        ch.cz = r.zig();
        for (int pass = 0; pass < 2; ++pass) {
            const bool surf = pass == 1;
            if (r.byte() == 0) continue;
            std::vector<TruthColumn>& cols = surf ? ch.surf : ch.fill;
            cols.resize(256);
            for (TruthColumn& col : cols) {
                col.top = r.zig();
                if (surf) col.biome = static_cast<int>(r.var());
                const std::uint32_t runs = r.var();
                for (std::uint32_t i = 0; i < runs; ++i) {
                    const int len = static_cast<int>(r.var());
                    const int p = static_cast<int>(r.var());
                    col.runs.emplace_back(len, p);
                }
            }
            (surf ? ch.hasSurf : ch.hasFill) = true;
        }
        out.chunks.push_back(std::move(ch));
    }
    return !r.bad() && out.chunks.size() == count;
}

std::filesystem::path testDataPath(const char* name) { return std::filesystem::path(FALCON_TEST_DATA_DIR) / name; }

struct CompareStats {
    long blocks = 0;
    long mismatches = 0;         // blocks that differ in a column where no biome tie can explain it: failures
    long tieMismatches = 0;      // blocks that differ in a column that reads a climate cell with an exact fitness tie (see below)
    long tieColumns = 0;
    long tieExplained = 0;       // of those columns: equal to the dump when the tie cells are flipped to the other tied biome
    long tieUnexplained = 0;
    long heightMismatches = 0;
    long biomeMismatches = 0;
    long biomeTies = 0;
};

// An exact fitness tie between leaves of different biomes: the game's R-tree then answers with whichever leaf its thread answered
// last, which no port can reproduce (the research spec: ~0.03% of cells).
// A quart cell of the climate grid that BiomeManager's zoom reads for a column, with an exact fitness tie between two biomes.
struct TieCell {
    std::array<int, 3> key;  // quart x, y (clamped), z
    BiomeId canonical = kNoBiome, other = kNoBiome;
};

// The tie cells among the cells that the zoom picks for some y of the column at (x, z).
std::vector<TieCell> columnTieCells(const OverworldGenerator& gen, int x, int z) {
    const BiomeSource& src = gen.biomeSource();
    BiomeSource::Context ctx(src);
    std::set<std::array<int, 3>> seen;
    std::vector<TieCell> out;
    for (int y = falcon::kMinY; y <= falcon::kMaxY; ++y) {
        const BiomeSource::QuartPos q = src.zoom(x, y, z);
        const int qy = std::clamp(q.y, -16, 79);
        if (!seen.insert({q.x, qy, q.z}).second) continue;
        const climate::TargetPoint t = src.sampleClimate(ctx, q.x, qy, q.z);
        const BiomeId canonical = src.noiseBiome(ctx, q.x, qy, q.z);
        std::int64_t best = INT64_MAX;
        BiomeId other = kNoBiome;
        for (std::size_t i = 0; i < src.parameters().points.size(); ++i) {
            const std::int64_t f = climate::fitness(src.parameters().points[i], t);
            if (f < best) {
                best = f;
                other = kNoBiome;
            }
            if (f == best && src.parameters().biomes[i] != canonical && other == kNoBiome) other = src.parameters().biomes[i];
        }
        if (other != kNoBiome) out.push_back({{q.x, qy, q.z}, canonical, other});
    }
    return out;
}

bool columnHasBiomeTie(const OverworldGenerator& gen, int x, int z) { return !columnTieCells(gen, x, z).empty(); }

// The biomes of the generator, except that the given tie cells answer with the other tied biome. If the game's thread had
// answered that way, the column would look like the dump.
class TieFlipLookup final : public BiomeLookup {
public:
    TieFlipLookup(const BiomeSource& src, BiomeSource::Context& ctx, std::map<std::array<int, 3>, BiomeId>& cache, const std::vector<TieCell>& flipped) : src_(src), ctx_(ctx), cache_(cache) {
        for (const TieCell& c : flipped) flips_[c.key] = c.other;
    }
    BiomeId biomeAt(int x, int y, int z) override {
        const BiomeSource::QuartPos q = src_.zoom(x, y, z);
        const int qy = std::clamp(q.y, -16, 79);
        const std::array<int, 3> key{q.x, qy, q.z};
        const auto flip = flips_.find(key);
        if (flip != flips_.end()) return flip->second;
        const auto [it, inserted] = cache_.try_emplace(key, kNoBiome);
        if (inserted) it->second = src_.noiseBiome(ctx_, q.x, qy, q.z);
        return it->second;
    }

private:
    const BiomeSource& src_;
    BiomeSource::Context& ctx_;
    std::map<std::array<int, 3>, BiomeId>& cache_;
    std::map<std::array<int, 3>, BiomeId> flips_;
};

// The biomes that tie for the minimum fitness in a quart cell, other than the one the generator answers with.
std::vector<BiomeId> tiedAlternatives(const BiomeSource& src, BiomeSource::Context& ctx, const std::array<int, 3>& key, BiomeId canonical) {
    const climate::TargetPoint t = src.sampleClimate(ctx, key[0], key[1], key[2]);
    std::int64_t best = INT64_MAX;
    std::vector<BiomeId> out;
    for (std::size_t i = 0; i < src.parameters().points.size(); ++i) {
        const std::int64_t f = climate::fitness(src.parameters().points[i], t);
        if (f < best) {
            best = f;
            out.clear();
        }
        const BiomeId b = src.parameters().biomes[i];
        if (f == best && b != canonical && std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
    }
    return out;
}

// Why does a column differ from the dump? Only the game's R-tree can answer a tie between two biomes either way. This builds
// the surface of the chunk again with the zoom's quart cells for the differing blocks answering one of the other tied biomes,
// cell by cell: the column must then equal the dump. A difference that no tie answer repairs is a real bug.
bool explainColumnWithTieFlips(const OverworldGenerator& gen, const TruthChunk& tc, const std::vector<TruthColumn>& cols, const std::vector<falcon::Block>& pal, int column,
                               const DenseChunk& fill, const std::vector<float>& surfaceLevel, BiomeSource::Context& ctx, std::map<std::array<int, 3>, BiomeId>& cache) {
    const BiomeSource& src = gen.biomeSource();
    const int x = tc.cx * 16 + (column & 15), z = tc.cz * 16 + (column >> 4);
    const TruthColumn& col = cols[static_cast<std::size_t>(column)];
    const auto build = [&](const std::vector<TieCell>& flips, DenseChunk& d) {
        d = fill;
        TieFlipLookup lookup(src, ctx, cache, flips);
        SurfaceTarget target;
        target.chunkX = tc.cx;
        target.chunkZ = tc.cz;
        target.blocks = d.blocks.data();
        target.worldSurface = d.worldSurface.data();
        target.chunkSurfaceLevel = surfaceLevel.data();
        target.biomes = &lookup;
        MaterialSystem::Workspace ws;
        gen.material().buildSurface(ws, target);
    };
    const auto differingY = [&](const DenseChunk& d) {
        std::vector<int> ys;
        const falcon::Block* got = &d.blocks[static_cast<std::size_t>(falcon::kWorldHeight) * static_cast<std::size_t>(column)];
        int y = 319;
        for (const auto& [len, p] : col.runs) {
            for (int i = 0; i < len; ++i, --y) {
                if (got[y - falcon::kMinY] != pal[static_cast<std::size_t>(p)]) ys.push_back(y);
            }
        }
        return ys;
    };
    DenseChunk d;
    build({}, d);
    const std::vector<int> bad = differingY(d);
    if (bad.empty()) return true;
    std::map<std::array<int, 3>, std::vector<int>> byCell;  // the quart cells the differing blocks read
    for (const int y : bad) {
        const BiomeSource::QuartPos q = src.zoom(x, y, z);
        byCell[{q.x, std::clamp(q.y, -16, 79), q.z}].push_back(y);
    }
    std::vector<TieCell> accepted;
    for (const auto& [key, ys] : byCell) {
        const BiomeId canonical = src.noiseBiome(ctx, key[0], key[1], key[2]);
        bool repaired = false;
        for (const BiomeId alt : tiedAlternatives(src, ctx, key, canonical)) {
            std::vector<TieCell> trial = accepted;
            trial.push_back({key, canonical, alt});
            build(trial, d);
            const std::vector<int> still = differingY(d);
            if (std::none_of(ys.begin(), ys.end(), [&](int y) { return std::find(still.begin(), still.end(), y) != still.end(); })) {
                accepted = trial;
                repaired = true;
                break;
            }
        }
        if (!repaired) return false;
    }
    build(accepted, d);
    return differingY(d).empty();
}

long explainWithTieFlip(const OverworldGenerator& gen, const TruthChunk& tc, const std::vector<TruthColumn>& cols, const std::vector<falcon::Block>& pal, const std::vector<int>& columns) {
    DenseChunk fill;
    gen.generateDense(tc.cx, tc.cz, OverworldGenerator::Stage::Fill, fill);
    std::vector<float> surfaceLevel(256);
    gen.router().chunkSurfaceLevelVolume(tc.cx, tc.cz, surfaceLevel.data());
    BiomeSource::Context ctx(gen.biomeSource());
    std::map<std::array<int, 3>, BiomeId> cache;
    long explained = 0;
    for (const int c : columns) {
        if (explainColumnWithTieFlips(gen, tc, cols, pal, c, fill, surfaceLevel, ctx, cache)) ++explained;
        else std::fprintf(stderr, "  SURF chunk %d,%d column x=%d z=%d differs from the dump and no tie answer repairs it\n", tc.cx, tc.cz, tc.cx * 16 + (c & 15), tc.cz * 16 + (c >> 4));
    }
    return explained;
}


// Compares one stage's dense output with the dump. Prints the first few mismatches of a chunk.
void compareStage(const OverworldGenerator& gen, const falcon::BlockRegistry& registry, const std::vector<falcon::Block>& pal, const char* stage, const TruthChunk& tc,
                  const std::vector<TruthColumn>& cols, const DenseChunk& dense, CompareStats& st, const std::vector<std::string>* biomeNames) {
    int shown = 0;
    long chunkMismatches = 0;
    const int minY = falcon::kMinY;
    std::vector<int> excused;
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const TruthColumn& col = cols[static_cast<std::size_t>(x + 16 * z)];
            const falcon::Block* got = &dense.blocks[static_cast<std::size_t>(falcon::kWorldHeight) * static_cast<std::size_t>(x + 16 * z)];
            int y = 319;
            long columnMismatches = 0;
            int firstBadY = 0;
            std::vector<std::string> lines;
            for (const auto& [len, p] : col.runs) {
                for (int i = 0; i < len; ++i, --y) {
                    ++st.blocks;
                    const falcon::Block want = pal[static_cast<std::size_t>(p)];
                    const falcon::Block have = got[y - minY];
                    if (want != have) {
                        if (columnMismatches++ == 0) firstBadY = y;
                        if (lines.size() < 3) {
                            char buf[256];
                            std::snprintf(buf, sizeof buf, "  %s chunk %d,%d x=%d y=%d z=%d: want %s got %s\n", stage, tc.cx, tc.cz, tc.cx * 16 + x, y, tc.cz * 16 + z,
                                          registry.describe(want).c_str(), registry.describe(have).c_str());
                            lines.emplace_back(buf);
                        }
                    }
                }
            }
            if (columnMismatches > 0) {
                if (biomeNames && columnHasBiomeTie(gen, tc.cx * 16 + x, tc.cz * 16 + z)) {  // only the surface stage reads biomes
                    st.tieMismatches += columnMismatches;
                    ++st.tieColumns;
                    excused.push_back(x + 16 * z);  // judged below: a tie answer must repair it
                } else {
                    chunkMismatches += columnMismatches;
                    st.mismatches += columnMismatches;
                    std::fprintf(stderr, "  %s chunk %d,%d column x=%d z=%d: %ld blocks differ (first at y=%d), no biome tie in this column\n", stage, tc.cx, tc.cz, tc.cx * 16 + x,
                                 tc.cz * 16 + z, columnMismatches, firstBadY);
                    for (const std::string& l : lines) std::fputs(l.c_str(), stderr);
                }
            }
            const int top = dense.worldSurface[static_cast<std::size_t>(x + 16 * z)] - 1;
            if (top != col.top) {
                ++st.heightMismatches;
                if (shown++ < 6) std::fprintf(stderr, "  %s chunk %d,%d x=%d z=%d: heightmap top want %d got %d\n", stage, tc.cx, tc.cz, tc.cx * 16 + x, tc.cz * 16 + z, col.top, top);
            }
            if (biomeNames) {
                const std::string& want = (*biomeNames)[static_cast<std::size_t>(col.biome)];
                const BiomeId have = gen.biomeAt(tc.cx * 16 + x, col.top + 1, tc.cz * 16 + z);
                if (gen.biomes().name(have) != want) {
                    // An exact fitness tie between two leaves of the parameter list is nondeterministic in the game itself.
                    const BiomeSource& src = gen.biomeSource();
                    BiomeSource::Context ctx(src);
                    const BiomeSource::QuartPos q = src.zoom(tc.cx * 16 + x, col.top + 1, tc.cz * 16 + z);
                    const int qy = std::clamp(q.y, -16, 79);
                    const climate::TargetPoint t = src.sampleClimate(ctx, q.x, qy, q.z);
                    std::int64_t bestAll = INT64_MAX, bestWant = INT64_MAX;
                    const BiomeId wantId = gen.biomes().find(want);
                    for (std::size_t i = 0; i < src.parameters().points.size(); ++i) {
                        const std::int64_t f = climate::fitness(src.parameters().points[i], t);
                        bestAll = std::min(bestAll, f);
                        if (src.parameters().biomes[i] == wantId) bestWant = std::min(bestWant, f);
                    }
                    if (bestAll == bestWant) {
                        ++st.biomeTies;
                    } else {
                        ++st.biomeMismatches;
                        if (shown++ < 6) {
                            std::fprintf(stderr, "  %s chunk %d,%d x=%d z=%d: biome at y=%d want %s got %s\n", stage, tc.cx, tc.cz, tc.cx * 16 + x, tc.cz * 16 + z, col.top + 1, want.c_str(),
                                         gen.biomes().name(have).c_str());
                        }
                    }
                }
            }
        }
    }
    if (chunkMismatches > 0) std::fprintf(stderr, "  %s chunk %d,%d: %ld block mismatches\n", stage, tc.cx, tc.cz, chunkMismatches);
    if (!excused.empty() && biomeNames) {
        const long explained = explainWithTieFlip(gen, tc, cols, pal, excused);
        st.tieExplained += explained;
        st.tieUnexplained += static_cast<long>(excused.size()) - explained;
    }
}

void testChunks(const OverworldGenerator& gen, const falcon::BlockRegistry& registry, const Truth& truth) {
    std::vector<falcon::Block> pal;
    for (const std::string& name : truth.palette) {
        const auto b = registry.parse(name);
        if (!b) {
            std::fprintf(stderr, "block registry cannot parse %s\n", name.c_str());
            ++g_failures;
            return;
        }
        pal.push_back(*b);
    }
    CompareStats fillStats, surfStats;
    DenseChunk dense;
    double fillMs = 0.0, surfMs = 0.0;
    int fillChunks = 0, surfChunks = 0;
    for (const TruthChunk& tc : truth.chunks) {
        if (tc.hasFill) {
            const auto t0 = std::chrono::steady_clock::now();
            gen.generateDense(tc.cx, tc.cz, OverworldGenerator::Stage::Fill, dense);
            fillMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++fillChunks;
            compareStage(gen, registry, pal, "FILL", tc, tc.fill, dense, fillStats, nullptr);
        }
        if (tc.hasSurf) {
            const auto t0 = std::chrono::steady_clock::now();
            gen.generateDense(tc.cx, tc.cz, OverworldGenerator::Stage::Surface, dense);
            surfMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++surfChunks;
            compareStage(gen, registry, pal, "SURF", tc, tc.surf, dense, surfStats, &truth.biomes);
        }
    }
    std::printf("  fill:    %d chunks, %ld blocks compared, %ld mismatches, %ld heightmap mismatches (%.1f ms per chunk)\n", fillChunks, fillStats.blocks, fillStats.mismatches,
                fillStats.heightMismatches, fillMs / std::max(1, fillChunks));
    std::printf("  surface: %d chunks, %ld blocks compared, %ld mismatches, %ld heightmap mismatches, %ld biome mismatches (+%ld exact-tie biome differences) (%.1f ms per chunk)\n",
                surfChunks, surfStats.blocks, surfStats.mismatches, surfStats.heightMismatches, surfStats.biomeMismatches, surfStats.biomeTies, surfMs / std::max(1, surfChunks));
    std::printf("  surface: %ld further block differences in %ld columns that read a climate cell with an exact fitness tie (the game's answer there depends on its thread's previous query);\n"
                "           with the tie cells answering the other way, %ld of those columns equal the dump (%ld do not)\n",
                surfStats.tieMismatches, surfStats.tieColumns, surfStats.tieExplained, surfStats.tieUnexplained);
    checkInt("surface: columns with a biome tie that the other tie answer does not explain", surfStats.tieUnexplained, 0);
    checkInt("fill: chunk dumps compared", fillChunks, 39);
    checkInt("surface: chunk dumps compared", surfChunks, 39);
    checkInt("fill: block mismatches", fillStats.mismatches, 0);
    checkInt("fill: WORLD_SURFACE_WG height mismatches", fillStats.heightMismatches, 0);
    checkInt("surface: block mismatches", surfStats.mismatches, 0);
    checkInt("surface: WORLD_SURFACE_WG height mismatches", surfStats.heightMismatches, 0);
    checkInt("surface: biome mismatches (excluding exact fitness ties)", surfStats.biomeMismatches, 0);
}

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

}  // namespace

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
                    testChunkObject(*gen);
                    testThreads(*gen);
                    testPresetsAndSpawn(gd, *registry, *gen);
                    testErrorsAndFacts(gd, *registry, *gen);
                    testFluidTracking(gd, *registry, *gen);
                    performanceReport(gd, *registry);
                }
            }
        }
    }
    std::printf("%ld checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
