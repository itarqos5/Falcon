#pragma once

// Biomes (26.3), bit for bit:
//   * BiomeRegistry   data/minecraft/worldgen/biome/*.json  -> compact ids, climate and colour facts for tints
//   * climate::       the multi-noise parameter list (biome-parameters/overworld.json), its quantisation, the 19-way
//                     R-tree the game searches (with its per-thread "last result" cache)
//   * ClimateSampler  the six climate density functions of the noise router at a quart position
//   * BiomeSource     quart-resolution biomes (what a chunk stores) and BiomeManager's zoomed block-resolution biomes
//                     (fiddled-distance LCG seeded with SHA-256 of the world seed)
//
// Everything reads the game's JSON at runtime; only algorithms are code.

#include "apex/worldgen/noise_router.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apex::worldgen {

// ---------------------------------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4) and BiomeManager.obfuscateSeed
[[nodiscard]] std::array<std::uint8_t, 32> sha256(const void* data, std::size_t size);
// Guava Hashing.sha256().hashLong(seed).asLong(): the digest of the 8 little-endian bytes of the seed, its first 8
// bytes read little-endian. This is the seed of the biome zoom (and the game sends it to clients).
[[nodiscard]] std::int64_t obfuscateSeed(std::int64_t seed);

// ---------------------------------------------------------------------------------------------------------------
// Biome registry
using BiomeId = std::uint16_t;
inline constexpr BiomeId kNoBiome = 0xFFFF;

enum class TemperatureModifier : std::uint8_t { None, Frozen };
enum class GrassColorModifier : std::uint8_t { None, DarkForest, Swamp };

struct BiomeInfo {
    std::string name;  // "minecraft:plains"
    float temperature = 0.5f;
    float downfall = 0.5f;
    TemperatureModifier temperatureModifier = TemperatureModifier::None;
    bool hasPrecipitation = true;
    // 0xRRGGBB colour overrides; unset means "derive from the colour maps" (grass, foliage) or the game's default.
    std::optional<std::uint32_t> waterColor, foliageColor, dryFoliageColor, grassColor;
    std::optional<std::uint32_t> skyColor, fogColor, waterFogColor;
    GrassColorModifier grassColorModifier = GrassColorModifier::None;
};

class BiomeRegistry {
public:
    // Reads every <biomeDir>/*.json. Ids are assigned in sorted-name order (stable for a given data set), 0..size()-1.
    // nullptr and `error` on failure (also when there are more than 255 biomes: chunk storage uses one byte per cell).
    [[nodiscard]] static std::shared_ptr<const BiomeRegistry> load(const std::filesystem::path& biomeDir, std::string& error);

    [[nodiscard]] std::size_t size() const { return biomes_.size(); }
    [[nodiscard]] const BiomeInfo& info(BiomeId id) const { return biomes_[id < biomes_.size() ? id : 0]; }
    [[nodiscard]] const std::string& name(BiomeId id) const { return info(id).name; }
    // "minecraft:plains" or "plains"; kNoBiome when unknown.
    [[nodiscard]] BiomeId find(std::string_view name) const;

private:
    std::vector<BiomeInfo> biomes_;
    std::vector<std::pair<std::string, BiomeId>> byName_;  // sorted by name
};

// ---------------------------------------------------------------------------------------------------------------
// Multi-noise climate: parameters, quantisation, search
namespace climate {

inline constexpr int kDims = 7;  // temperature, humidity, continentalness, erosion, depth, weirdness, offset

// Climate.quantizeCoord: (long)(f * 10000.0f), the multiply rounded to float.
[[nodiscard]] std::int64_t quantize(float f);

struct Parameter {
    std::int64_t min = 0;
    std::int64_t max = 0;
    [[nodiscard]] std::int64_t distance(std::int64_t v) const {
        const std::int64_t d1 = v - max;
        const std::int64_t d2 = min - v;
        return d1 > 0 ? d1 : (d2 > 0 ? d2 : 0);
    }
};

struct ParameterPoint {
    Parameter space[kDims];  // T, H, C, E, D, W, offset
};

struct TargetPoint {
    std::int64_t temperature = 0, humidity = 0, continentalness = 0, erosion = 0, depth = 0, weirdness = 0;
    [[nodiscard]] std::array<std::int64_t, kDims> toArray() const { return {temperature, humidity, continentalness, erosion, depth, weirdness, 0}; }
    bool operator==(const TargetPoint&) const = default;
};

// ParameterPoint.fitness: the sum of the squared per-axis distances.
[[nodiscard]] std::int64_t fitness(const ParameterPoint& p, const TargetPoint& t);

struct ParameterList {
    std::vector<ParameterPoint> points;
    std::vector<BiomeId> biomes;  // same order as the file
};

// biome-parameters/<preset>.json: {"biomes": [{"biome": id, "parameters": {temperature, humidity, ...}}]}
[[nodiscard]] bool parseParameterList(const JsonValue& json, const BiomeRegistry& registry, ParameterList& out, std::string& error);
[[nodiscard]] bool loadParameterList(const std::filesystem::path& file, const BiomeRegistry& registry, ParameterList& out, std::string& error);

// Climate.RTree (19 children per node). search() mirrors the game: the previous result of the calling thread seeds the
// search and wins exact fitness ties, so the caller owns that state (RTreeCache) and passes it in.
struct RTreeCache {
    std::int32_t lastLeaf = -1;
};

class RTree {
public:
    static constexpr int kChildrenPerNode = 19;
    RTree() = default;
    explicit RTree(const ParameterList& list);

    // The game's search: the biome of the best leaf, seeded with (and updating) the thread's previous result. On an exact
    // fitness tie the previous result, or else the leaf met first in depth-first order, wins, so the answer for a tie
    // depends on the order of the queries (the game itself is not deterministic there). Thread-safe with one cache per thread.
    [[nodiscard]] BiomeId search(const TargetPoint& t, RTreeCache& cache) const;
    // The leaf (index into the list the tree was built from) instead of its biome.
    [[nodiscard]] std::int32_t searchLeaf(const TargetPoint& t, RTreeCache& cache) const;
    // The same minimum-fitness leaf wherever the fitness is unique, but exact ties go to the leaf met first in depth-first
    // order whatever the previous query was: a pure function of the target (the cache only makes it faster). This is what
    // BiomeSource uses, so a chunk comes out the same however often or in whatever order it is generated.
    [[nodiscard]] std::int32_t searchLeafCanonical(const TargetPoint& t, RTreeCache& cache) const;
    [[nodiscard]] BiomeId searchCanonical(const TargetPoint& t, RTreeCache& cache) const;
    [[nodiscard]] BiomeId biomeOfLeaf(std::int32_t leaf) const { return leaf < 0 ? kNoBiome : nodes_[static_cast<std::size_t>(leaf)].biome; }
    // Index of the first leaf with the minimum fitness (Climate.ParameterList.findValueBruteForce); tests and tie checks.
    [[nodiscard]] static std::int32_t bruteForce(const ParameterList& list, const TargetPoint& t);

    [[nodiscard]] std::size_t leafCount() const { return leafCount_; }
    [[nodiscard]] int depth() const { return depth_; }
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    struct Node {
        std::int64_t lo[kDims];
        std::int64_t hi[kDims];
        std::int32_t first = 0;    // children: childIndex_[first .. first + count)
        std::int32_t count = 0;    // 0: a leaf
        std::int32_t minRank = 0;  // the lowest depth-first rank among the leaves below (a leaf: its own rank)
        BiomeId biome = kNoBiome;
    };
    [[nodiscard]] static std::int64_t distance(const Node& n, const std::int64_t* t);
    [[nodiscard]] std::int32_t searchNode(std::int32_t node, const std::int64_t* t, std::int32_t best) const;
    void descendCanonical(std::int32_t node, const std::int64_t* t, std::int32_t& best, std::int64_t& bestDist) const;

    std::vector<Node> nodes_;               // leaves first (in list order), then sub-trees
    std::vector<std::int32_t> childIndex_;
    std::int32_t root_ = -1;
    std::size_t leafCount_ = 0;
    int depth_ = 0;
};

}  // namespace climate

// ---------------------------------------------------------------------------------------------------------------
// The six climate density functions of a router, evaluated together at a quart position (one evaluator, so the
// noises shared between them are computed once). Not thread-safe: one sampler per thread.
class ClimateSampler {
public:
    explicit ClimateSampler(const NoiseRouter& router);
    // Climate.Sampler.sample(qx, qy, qz): the functions at block (qx*4, qy*4, qz*4), each quantised.
    [[nodiscard]] climate::TargetPoint sample(int qx, int qy, int qz);
    // Same, but the six raw float values (T, H, C, E, D, W) at a block position.
    void sampleFloats(int x, int y, int z, float out[6]);

private:
    const NoiseRouter* router_;
    DensityEvaluator evaluator_;
};

// ---------------------------------------------------------------------------------------------------------------
// The biomes a chunk stores: one id per 4x4x4-block cell, 4 x 96 x 4 cells for a 16 x 384 x 16 chunk.
struct ChunkBiomes {
    static constexpr int kQuartsXZ = 4;
    static constexpr int kQuartsY = 96;
    static constexpr int kMinQuartY = -16;  // quart y of the bottom cell (block y -64)
    static constexpr std::size_t kCells = static_cast<std::size_t>(kQuartsXZ) * kQuartsXZ * kQuartsY;
    std::array<std::uint8_t, kCells> ids{};  // index ((qz * 4 + qx) * 96 + qyIndex): vertical runs are contiguous

    [[nodiscard]] static constexpr std::size_t index(int qx, int qyIndex, int qz) { return (static_cast<std::size_t>(qz) * kQuartsXZ + static_cast<std::size_t>(qx)) * kQuartsY + static_cast<std::size_t>(qyIndex); }
    // Chunk-local quart x/z (0..3) and world quart y (-16..79; values outside are clamped, as the game does).
    [[nodiscard]] BiomeId at(int qx, int quartY, int qz) const {
        int qy = quartY - kMinQuartY;
        qy = qy < 0 ? 0 : (qy >= kQuartsY ? kQuartsY - 1 : qy);
        return ids[index(qx, qy, qz)];
    }
    // The cell holding chunk-local block x, z and world block y (no zoom).
    [[nodiscard]] BiomeId atBlock(int localX, int y, int localZ) const { return at(localX >> 2, y >> 2, localZ >> 2); }
};

// ---------------------------------------------------------------------------------------------------------------
class BiomeSource {
public:
    // Per-thread state: the climate sampler and the R-tree's last result. Create one per thread that queries.
    class Context {
    public:
        explicit Context(const BiomeSource& source);
        [[nodiscard]] std::uint64_t sourceId() const { return sourceId_; }
        [[nodiscard]] climate::RTreeCache& rtreeCache() { return cache_; }

    private:
        friend class BiomeSource;
        std::uint64_t sourceId_;
        std::unique_ptr<ClimateSampler> sampler_;
        climate::RTreeCache cache_;
    };

    // Multi-noise source: `parameterFile` is biome-parameters/overworld.json. `router` must outlive the source.
    [[nodiscard]] static std::unique_ptr<BiomeSource> create(const std::filesystem::path& parameterFile, std::shared_ptr<const BiomeRegistry> registry,
                                                              const NoiseRouter& router, std::int64_t seed, std::string& error);
    // One biome everywhere (the "single biome" world type / minecraft:fixed).
    [[nodiscard]] static std::unique_ptr<BiomeSource> createFixed(std::shared_ptr<const BiomeRegistry> registry, BiomeId biome, std::int64_t seed);

    [[nodiscard]] const BiomeRegistry& registry() const { return *registry_; }
    [[nodiscard]] const std::shared_ptr<const BiomeRegistry>& registryPtr() const { return registry_; }
    [[nodiscard]] bool isFixed() const { return fixed_ != kNoBiome; }
    [[nodiscard]] std::int64_t seed() const { return seed_; }
    [[nodiscard]] std::int64_t zoomSeed() const { return zoomSeed_; }
    [[nodiscard]] const climate::ParameterList& parameters() const { return list_; }
    [[nodiscard]] const climate::RTree& tree() const { return tree_; }

    // ---- quart resolution (the biome a chunk stores for a 4x4x4 cell) -------------------------------------------
    // A pure function of the seed and the position: the R-tree answer, with exact fitness ties going to the first leaf in depth-first
    // order (RTree::searchCanonical) so chunks do not depend on generation order; the game itself answers ties by history.
    // The quart y is clamped to -16..79.
    [[nodiscard]] BiomeId noiseBiome(Context& ctx, int qx, int qy, int qz) const;
    [[nodiscard]] BiomeId noiseBiome(int qx, int qy, int qz) const;  // thread-local context
    [[nodiscard]] climate::TargetPoint sampleClimate(Context& ctx, int qx, int qy, int qz) const;
    // All 4 x 96 x 4 cells of a chunk (what NoiseBasedChunkGenerator.doCreateBiomes stores).
    void fillChunk(Context& ctx, int chunkX, int chunkZ, ChunkBiomes& out) const;

    // ---- block resolution (BiomeManager.getBiome: the zoom) ------------------------------------------------------
    struct QuartPos {
        int x, y, z;
    };
    // The quart cell BiomeManager.getBiome picks for a block position (before clamping y).
    [[nodiscard]] static QuartPos zoom(std::int64_t zoomSeed, int x, int y, int z);
    [[nodiscard]] QuartPos zoom(int x, int y, int z) const { return zoom(zoomSeed_, x, y, z); }
    [[nodiscard]] BiomeId biomeAt(Context& ctx, int x, int y, int z) const;
    [[nodiscard]] BiomeId biomeAt(int x, int y, int z) const;  // thread-local context

    // ---- spawn target --------------------------------------------------------------------------------------------
    // The thread-local context used by the convenience overloads.
    [[nodiscard]] Context& threadContext() const;

private:
    BiomeSource() = default;

    std::shared_ptr<const BiomeRegistry> registry_;
    const NoiseRouter* router_ = nullptr;
    climate::ParameterList list_;
    climate::RTree tree_;
    BiomeId fixed_ = kNoBiome;
    std::int64_t seed_ = 0;
    std::int64_t zoomSeed_ = 0;
    std::uint64_t id_ = 0;
};

}  // namespace apex::worldgen
