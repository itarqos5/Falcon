#pragma once

// What the material system is made of inside (material.cpp): the nodes the rules and conditions compile to, and the private parts of MaterialSystem.
#include "falcon/worldgen/material.hpp"
#include "falcon/worldgen/java_math.hpp"
#include "falcon/worldgen/legacy_noise.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace falcon::worldgen {
namespace material_detail {

constexpr std::uint16_t kNoBlock = 0xFFFF;
constexpr Block kNone = static_cast<Block>(kNoBlock);
constexpr int kBandCount = 192;
constexpr int kWayBelowMinY = -32512;

enum class RuleKind : std::uint8_t { Sequence, Condition, BlockResult, Bandlands, OreVein };
struct RuleNode {
    RuleKind kind = RuleKind::BlockResult;
    std::int32_t a = 0;  // Sequence: first child in childIndex; Condition: condition index; OreVein: vein index
    std::int32_t b = 0;  // Sequence: child count; Condition: rule index
    Block block = Block::Air;
};

enum class CondKind : std::uint8_t { Biome, StoneDepth, YAbove, Water, VerticalGradient, NoiseThreshold, Not, Steep, Hole, AbovePreliminarySurface, Temperature };
struct CondNode {
    CondKind kind = CondKind::Steep;
    bool flagA = false;  // StoneDepth: add_surface_depth; YAbove/Water: add_stone_depth; NoiseThreshold: is_3d
    bool flagB = false;  // StoneDepth: ceiling
    std::int32_t i0 = 0, i1 = 0;
    double d0 = 0.0, d1 = 0.0;
    const NormalNoise* noise = nullptr;
    XoroshiroPositionalRandomFactory factory;
    std::array<std::uint64_t, 4> biomes{};
    std::int32_t child = -1;
};

// A density function evaluated over (a y-range of) the chunk volume. Outside the vein's y-range its density function is
// a constant <= 0, so only that range is computed; the range starts on a multiple of 16 so every interpolation cell is
// whole and the values are bit-identical to the full-height volume the game samples.
struct VolumeSlot {
    DensityFunction fn;
    int yLo = 0, yHi = 0;  // inclusive world y range that is ever read
    int base = 0, size = 0;  // the evaluated range: [base, base + size)
};

struct OreVein {
    std::int32_t densitySlot = 0, richnessSlot = 0;
    int yLo = 0, yHi = 0;  // inclusive: outside it the density is a constant <= 0 (no vein)
    DensityFunction gap;
    Block ore = Block::Air, raw = Block::Air, filler = Block::Air;
    float rawChance = 0.0f;
    XoroshiroPositionalRandomFactory factory;
};

[[nodiscard]] inline double lerpD(double t, double a, double b) { return a + t * (b - a); }
[[nodiscard]] inline double mapD(double v, double a, double b, double c, double d) { return lerpD((v - a) / (b - a), c, d); }

std::string stripNs(const std::string& s) {
    constexpr const char* kPrefix = "minecraft:";
    return s.compare(0, 10, kPrefix) == 0 ? s.substr(10) : s;
}

std::atomic<std::uint64_t> g_nextSystemId{1};

}  // namespace material_detail
using namespace material_detail;

// ---------------------------------------------------------------------------------------------------------------
struct MaterialSystem::Workspace::Impl {
    std::uint64_t systemId = 0;
    std::vector<std::vector<float>> volumes;  // ore-vein density/richness over the chunk volume, one buffer per slot
    std::vector<std::uint8_t> condPossible;   // per condition: false for a biome condition that cannot match anywhere in this chunk
    std::vector<std::uint32_t> condStamp;     // per condition: stamp of the cached noise value
    std::vector<double> condValue;
};

struct MaterialSystem::Impl {
    // ---- settings
    int minY = -64, height = 384, seaLevel = 63;
    const NoiseRouter* router = nullptr;
    const BlockRegistry* blocks = nullptr;
    const BlockFacts* facts = nullptr;
    std::shared_ptr<const BiomeRegistry> biomeRegistry;
    std::uint64_t id = 0;
    std::filesystem::path worldgenDir;

    // ---- random sources and noises
    XoroshiroPositionalRandomFactory root;
    const NormalNoise *surfaceNoise = nullptr, *secondaryNoise = nullptr, *clayOffsetNoise = nullptr;
    const NormalNoise *badlandsSurface = nullptr, *badlandsPillar = nullptr, *badlandsRoof = nullptr;
    const NormalNoise *icebergSurface = nullptr, *icebergPillar = nullptr, *icebergRoof = nullptr;
    std::array<Block, kBandCount> bands{};
    Block defaultBlock = Block::Air, snowBlock = Block::Air, packedIce = Block::Air;
    int defaultBlockType = -1, waterBlockType = -1;
    BiomeId erodedBadlands = kNoBiome, frozenOcean = kNoBiome, deepFrozenOcean = kNoBiome;

    // ---- the compiled rule tree
    std::vector<RuleNode> rules;
    std::vector<CondNode> conds;
    std::vector<std::int32_t> childIndex;
    std::vector<OreVein> veins;
    std::vector<VolumeSlot> volumeSlots;  // unique density functions evaluated over (part of) the chunk volume
    std::int32_t rootRule = -1;

    // ---- per-column / per-block evaluation context
    struct Ctx {
        const Impl* m = nullptr;
        Workspace::Impl* ws = nullptr;
        const SurfaceTarget* t = nullptr;
        // column
        int lx = 0, lz = 0, x = 0, z = 0;
        int surfaceDepth = 0, gradX = 0, gradZ = 0, minSurface = 0;
        double secondary = 0.0;
        bool secondaryValid = false;
        std::uint32_t columnStamp = 0;
        // block
        int y = 0, above = 0, below = 0, waterHeight = std::numeric_limits<int>::min();
        BiomeId biome = 0;
        bool biomeValid = false;
        std::uint32_t blockStamp = 0;

        BiomeId getBiome() {
            if (!biomeValid) {
                biome = t->biomes->biomeAt(x, y, z);
                biomeValid = true;
            }
            return biome;
        }
        double getSecondary() {
            if (!secondaryValid) {
                secondary = static_cast<double>(m->secondaryNoise->get(static_cast<double>(x), 0.0, static_cast<double>(z)));
                secondaryValid = true;
            }
            return secondary;
        }
    };

    [[nodiscard]] Block bandAt(int x, int y, int z) const {
        // Math.round(noise * 4.0f): a float multiply, rounded half up
        const float scaled = clayOffsetNoise->get(static_cast<double>(x), 0.0, static_cast<double>(z)) * 4.0f;
        const int off = static_cast<int>(std::floor(static_cast<double>(scaled) + 0.5));
        return bands[static_cast<std::size_t>(floorMod(y + off + kBandCount, kBandCount))];
    }

    [[nodiscard]] float biomeTemperature(BiomeId b, int x, int y, int z) const {
        const BiomeInfo& info = biomeRegistry->info(b);
        const BiomeTemperatureNoise& noise = BiomeTemperatureNoise::instance();
        const float base = info.temperatureModifier == TemperatureModifier::Frozen ? noise.frozen(x, z, info.temperature) : info.temperature;
        return noise.lapse(base, x, y, z, seaLevel);
    }

    [[nodiscard]] bool test(std::int32_t ci, Ctx& c) const {
        const CondNode& n = conds[static_cast<std::size_t>(ci)];
        switch (n.kind) {
            case CondKind::Biome: {
                if (!c.ws->condPossible[static_cast<std::size_t>(ci)]) return false;
                const BiomeId b = c.getBiome();
                return b < 256 && ((n.biomes[b >> 6] >> (b & 63)) & 1u) != 0;
            }
            case CondKind::StoneDepth: {
                const int depth = n.flagB ? c.below : c.above;
                const int add = n.flagA ? c.surfaceDepth : 0;
                const int secondary = n.i1 == 0 ? 0 : javaD2I(mapD(c.getSecondary(), -1.0, 1.0, 0.0, static_cast<double>(n.i1)));
                return depth <= 1 + n.i0 + add + secondary;
            }
            case CondKind::YAbove: return c.y + (n.flagA ? c.above : 0) >= n.i0 + c.surfaceDepth * n.i1;
            case CondKind::Water:
                return c.waterHeight == std::numeric_limits<int>::min() || c.y + (n.flagA ? c.above : 0) >= c.waterHeight + n.i0 + c.surfaceDepth * n.i1;
            case CondKind::VerticalGradient: {
                if (c.y <= n.i0) return true;
                if (c.y >= n.i1) return false;
                const double t = mapD(static_cast<double>(c.y), static_cast<double>(n.i0), static_cast<double>(n.i1), 1.0, 0.0);
                XoroshiroRandomSource r = n.factory.makeAt(c.x, c.y, c.z);
                return static_cast<double>(r.nextFloat()) < t;
            }
            case CondKind::NoiseThreshold: {
                double v;
                std::uint32_t& stamp = c.ws->condStamp[static_cast<std::size_t>(ci)];
                const std::uint32_t want = n.flagA ? c.blockStamp : c.columnStamp;
                if (stamp == want) {
                    v = c.ws->condValue[static_cast<std::size_t>(ci)];
                } else {
                    v = static_cast<double>(n.noise->get(static_cast<double>(c.x), n.flagA ? static_cast<double>(c.y) : 0.0, static_cast<double>(c.z)));
                    stamp = want;
                    c.ws->condValue[static_cast<std::size_t>(ci)] = v;
                }
                return v >= n.d0 && v <= n.d1;
            }
            case CondKind::Not: return !test(n.child, c);
            case CondKind::Steep: return c.gradX <= -4 || c.gradZ >= 4;
            case CondKind::Hole: return c.surfaceDepth <= 0;
            case CondKind::AbovePreliminarySurface: return c.y >= c.minSurface;
            case CondKind::Temperature: return !(biomeTemperature(c.getBiome(), c.x, c.y, c.z) >= 0.15f);  // coldEnoughToSnow
        }
        return false;
    }

    [[nodiscard]] Block applyOre(const OreVein& v, Ctx& c) const {
        if (c.y < v.yLo || c.y > v.yHi) return kNone;  // the density is a constant <= 0 out there
        const VolumeSlot& ds = volumeSlots[static_cast<std::size_t>(v.densitySlot)];
        const VolumeSlot& rs = volumeSlots[static_cast<std::size_t>(v.richnessSlot)];
        const std::size_t column = static_cast<std::size_t>(c.lx) + 16u * static_cast<std::size_t>(c.lz);
        const float solid = c.ws->volumes[static_cast<std::size_t>(v.densitySlot)][static_cast<std::size_t>(c.y - ds.base) + static_cast<std::size_t>(ds.size) * column];
        if (solid <= 0.0f) return kNone;
        XoroshiroRandomSource r = v.factory.makeAt(c.x, c.y, c.z);
        if (r.nextFloat() > solid) return kNone;
        const float rich = c.ws->volumes[static_cast<std::size_t>(v.richnessSlot)][static_cast<std::size_t>(c.y - rs.base) + static_cast<std::size_t>(rs.size) * column];
        if (r.nextFloat() < rich && v.gap.compute(c.x, c.y, c.z) < 0.0f) return r.nextFloat() < v.rawChance ? v.raw : v.ore;
        return v.filler;
    }

    [[nodiscard]] Block apply(std::int32_t ri, Ctx& c) const {
        const RuleNode& r = rules[static_cast<std::size_t>(ri)];
        switch (r.kind) {
            case RuleKind::Sequence: {
                const std::int32_t* kids = &childIndex[static_cast<std::size_t>(r.a)];
                for (std::int32_t i = 0; i < r.b; ++i) {
                    const Block b = apply(kids[i], c);
                    if (b != kNone) return b;
                }
                return kNone;
            }
            case RuleKind::Condition: return test(r.a, c) ? apply(r.b, c) : kNone;
            case RuleKind::BlockResult: return r.block;
            case RuleKind::Bandlands: return bandAt(c.x, c.y, c.z);
            case RuleKind::OreVein: return applyOre(veins[static_cast<std::size_t>(r.a)], c);
        }
        return kNone;
    }

    // ---- block access helpers over the dense chunk array
    [[nodiscard]] bool isAirAt(const SurfaceTarget& t, int lx, int lz, int y) const {
        if (y < minY || y >= minY + height) return true;  // outside the chunk: void air
        return facts->isAir(t.blocks[static_cast<std::size_t>(y - minY) + static_cast<std::size_t>(height) * (static_cast<std::size_t>(lx) + 16u * static_cast<std::size_t>(lz))]);
    }
    [[nodiscard]] Block blockAt(const SurfaceTarget& t, int lx, int lz, int y) const {
        if (y < minY || y >= minY + height) return Block::Air;
        return t.blocks[static_cast<std::size_t>(y - minY) + static_cast<std::size_t>(height) * (static_cast<std::size_t>(lx) + 16u * static_cast<std::size_t>(lz))];
    }
    [[nodiscard]] bool isStone(Block b) const { return !facts->isAir(b) && !facts->isFluid(b); }
    [[nodiscard]] bool isWater(Block b) const { return blocks->state(b).block == waterBlockType; }
    [[nodiscard]] bool isDefaultBlock(Block b) const { return blocks->state(b).block == defaultBlockType; }

    // ChunkAccess.setBlockState for a ProtoChunk, with the live WORLD_SURFACE_WG heightmap update (Heightmap.update).
    void setBlock(const SurfaceTarget& t, int lx, int lz, int y, Block b) const {
        if (y < minY || y >= minY + height) return;
        Block* col = t.blocks + static_cast<std::size_t>(height) * (static_cast<std::size_t>(lx) + 16u * static_cast<std::size_t>(lz));
        col[y - minY] = b;
        std::int16_t& first = t.worldSurface[lx + 16 * lz];
        const int i = first;
        if (y <= i - 2) return;
        if (!facts->isAir(b)) {
            if (y >= i) first = static_cast<std::int16_t>(y + 1);
        } else if (i - 1 == y) {
            for (int j = y - 1; j >= minY; --j) {
                if (!facts->isAir(col[j - minY])) {
                    first = static_cast<std::int16_t>(j + 1);
                    return;
                }
            }
            first = static_cast<std::int16_t>(minY);
        }
    }

    // ---- eroded badlands pillars
    void erodedBadlandsExtension(const SurfaceTarget& t, int lx, int lz, int x, int z, int heightAbove) const {
        const double surf = std::fabs(static_cast<double>(badlandsSurface->get(static_cast<double>(x), 0.0, static_cast<double>(z))) * 8.25);
        const double pill = static_cast<double>(badlandsPillar->get(static_cast<double>(x) * 0.2, 0.0, static_cast<double>(z) * 0.2) * 15.0f);
        const double d1 = std::min(surf, pill);
        if (d1 <= 0.0) return;
        const double roof = std::fabs(static_cast<double>(badlandsRoof->get(static_cast<double>(x) * 0.75, 0.0, static_cast<double>(z) * 0.75)) * 1.5);
        const double d5 = 64.0 + std::min(d1 * d1 * 2.5, std::ceil(roof * 50.0) + 24.0);
        const int top = javaFloor(d5);
        if (heightAbove > top) return;
        for (int y = top; y >= minY; --y) {
            const Block s = blockAt(t, lx, lz, y);
            if (isDefaultBlock(s)) break;
            if (isWater(s)) return;
        }
        for (int y = top; y >= minY && isAirAt(t, lx, lz, y); --y) setBlock(t, lx, lz, y, defaultBlock);
    }

    // ---- frozen ocean icebergs
    void frozenOceanExtension(const SurfaceTarget& t, int minSurfaceLevel, BiomeId biome, int lx, int lz, int x, int z, int heightAbove) const {
        const double surf = std::fabs(static_cast<double>(icebergSurface->get(static_cast<double>(x), 0.0, static_cast<double>(z))) * 8.25);
        const double pill = static_cast<double>(icebergPillar->get(static_cast<double>(x) * 1.28, 0.0, static_cast<double>(z) * 1.28) * 15.0f);
        const double d1 = std::min(surf, pill);
        if (d1 <= 1.8) return;
        const double roof = std::fabs(static_cast<double>(icebergRoof->get(static_cast<double>(x) * 1.17, 0.0, static_cast<double>(z) * 1.17)) * 1.5);
        double top = std::min(d1 * d1 * 1.2, std::ceil(roof * 40.0) + 14.0);
        if (biomeTemperature(biome, x, seaLevel, z) > 0.1f) top -= 2.0;  // shouldMeltFrozenOceanIcebergSlightly
        if (top <= 2.0) return;
        const double water = static_cast<double>(seaLevel) - top - 7.0;
        top += static_cast<double>(seaLevel);
        const double topCopy = top;
        XoroshiroRandomSource r = root.makeAt(x, 0, z);
        const int snowRun = 2 + r.nextInt(4);
        const int snowStart = seaLevel + 18 + r.nextInt(10);
        int k = 0;
        for (int y = std::max(heightAbove, javaD2I(topCopy) + 1); y >= minSurfaceLevel; --y) {
            const Block b = blockAt(t, lx, lz, y);
            const bool place = (facts->isAir(b) && y < javaD2I(topCopy) && r.nextDouble() > 0.01) ||
                               (isWater(b) && y > javaD2I(water) && y < seaLevel && r.nextDouble() > 0.15);
            if (place) {
                if (k <= snowRun && y > snowStart) {
                    setBlock(t, lx, lz, y, snowBlock);
                    ++k;
                } else {
                    setBlock(t, lx, lz, y, packedIce);
                }
            }
        }
    }
};

// ===============================================================================================================
// Compilation of the JSON rule tree

}  // namespace falcon::worldgen
