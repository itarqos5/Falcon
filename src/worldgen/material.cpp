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

// ===============================================================================================================
BlockFacts::BlockFacts(const BlockRegistry& r) : flags_(r.stateCount(), 0) {
    for (const BlockType& t : r.blocks()) {
        const bool fluid = t.name == "water" || t.name == "lava";
        for (int s = 0; s < t.stateCount; ++s) {
            const std::size_t id = static_cast<std::size_t>(t.firstState) + static_cast<std::size_t>(s);
            if (id >= flags_.size()) continue;
            const StateInfo& info = r.state(static_cast<Block>(id));
            std::uint8_t f = 0;
            if (info.has(kStateAir)) f |= kAir;
            if (fluid) f |= kFluid;
            if (info.has(kStateFullCollision) || info.has(kStateOpaqueCube)) f |= kSolidMotion;
            if (f & (kSolidMotion | kFluid)) f |= kMotionBlocking;
            flags_[id] = f;
        }
    }
}

// ===============================================================================================================
namespace {

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

}  // namespace

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
namespace {

class Compiler {
public:
    Compiler(MaterialSystem::Impl& m, std::string& error) : m_(m), error_(error) {}

    [[nodiscard]] bool compileRoot(const std::string& ruleName) {
        const std::int32_t r = rule(JsonValueRef(ruleName));
        if (r < 0) return false;
        m_.rootRule = r;
        return true;
    }

private:
    // A rule or condition is either an inline object or a string naming a registry entry.
    struct JsonValueRef {
        explicit JsonValueRef(std::string n) : name(std::move(n)) {}
        std::string name;
    };

    bool fail(const std::string& msg) {
        if (error_.empty()) error_ = msg;
        return false;
    }

    bool loadNamed(const char* registry, const std::string& id, JsonValue& out) {
        const std::filesystem::path file = m_.worldgenDir / registry / (stripNs(id) + ".json");
        std::string err;
        if (!parseJsonFile(file, out, err)) return fail(std::string(registry) + " '" + id + "': " + err);
        return true;
    }

    bool intField(const JsonValue& o, const char* key, std::int32_t& out, bool required, std::int32_t def = 0) {
        const JsonValue* v = o.find(key);
        if (!v) {
            if (required) return fail(std::string("missing field ") + key);
            out = def;
            return true;
        }
        if (!v->toInt(out)) return fail(std::string("field ") + key + " is not an integer");
        return true;
    }
    bool boolField(const JsonValue& o, const char* key, bool& out, bool required, bool def = false) {
        const JsonValue* v = o.find(key);
        if (!v) {
            if (required) return fail(std::string("missing field ") + key);
            out = def;
            return true;
        }
        if (v->kind != JsonValue::Kind::Bool) return fail(std::string("field ") + key + " is not a boolean");
        out = v->boolean;
        return true;
    }

    bool anchor(const JsonValue* v, std::int32_t& out) {
        if (!v || !v->isObject()) return fail("missing vertical anchor");
        std::int32_t n = 0;
        if (const JsonValue* a = v->find("absolute")) {
            if (!a->toInt(n)) return fail("bad anchor");
            out = n;
        } else if (const JsonValue* a2 = v->find("above_bottom")) {
            if (!a2->toInt(n)) return fail("bad anchor");
            out = m_.minY + n;
        } else if (const JsonValue* a3 = v->find("below_top")) {
            if (!a3->toInt(n)) return fail("bad anchor");
            out = m_.height - 1 + m_.minY - n;
        } else {
            return fail("unknown vertical anchor type");
        }
        return true;
    }

    bool blockState(const JsonValue* v, Block& out) {
        if (!v) return fail("missing block state");
        std::string text;
        if (v->isString()) {
            text = v->text;
        } else if (v->isObject()) {
            const JsonValue* name = v->find("Name");
            if (!name || !name->isString()) return fail("block state without Name");
            text = name->text;
            if (const JsonValue* props = v->find("Properties"); props && props->isObject() && !props->members.empty()) {
                text += '[';
                bool first = true;
                for (const auto& [k, val] : props->members) {
                    if (!first) text += ',';
                    first = false;
                    text += k;
                    text += '=';
                    text += val.kind == JsonValue::Kind::Bool ? (val.boolean ? "true" : "false") : val.text;
                }
                text += ']';
            }
        } else {
            return fail("bad block state");
        }
        const auto b = m_.blocks->parse(text);
        if (!b) return fail("unknown block state '" + text + "'");
        out = *b;
        return true;
    }

    // biome_is: "id", "#tag" or [ids/tags]
    bool addBiomeEntry(const std::string& entry, std::array<std::uint64_t, 4>& set, int depth) {
        if (!entry.empty() && entry[0] == '#') {
            if (depth > 8) return fail("biome tag nesting too deep");
            const std::filesystem::path file = m_.worldgenDir.parent_path() / "tags" / "worldgen" / "biome" / (stripNs(entry.substr(1)) + ".json");
            JsonValue tag;
            std::string err;
            if (!parseJsonFile(file, tag, err)) return fail("biome tag " + entry + ": " + err);
            const JsonValue* values = tag.find("values");
            if (!values || !values->isArray()) return fail("biome tag " + entry + " has no values");
            for (const JsonValue& v : values->items) {
                const std::string* s = v.isString() ? &v.text : nullptr;
                if (!s && v.isObject()) {
                    if (const JsonValue* id = v.find("id"); id && id->isString()) s = &id->text;
                }
                if (!s) return fail("bad entry in biome tag " + entry);
                if (!addBiomeEntry(*s, set, depth + 1)) return false;
            }
            return true;
        }
        const BiomeId id = m_.biomeRegistry->find(entry);
        if (id == kNoBiome) return fail("unknown biome '" + entry + "' in a material rule");
        set[id >> 6] |= std::uint64_t{1} << (id & 63);
        return true;
    }

    std::int32_t addCond(CondNode&& n) {
        m_.conds.push_back(std::move(n));
        return static_cast<std::int32_t>(m_.conds.size() - 1);
    }
    std::int32_t addRule(const RuleNode& n) {
        m_.rules.push_back(n);
        return static_cast<std::int32_t>(m_.rules.size() - 1);
    }

    const NormalNoise* noiseFor(const std::string& key) {
        std::string err;
        const NormalNoise* n = m_.router->randomState()->noise(key, err);
        if (!n) fail("noise " + key + ": " + err);
        return n;
    }

    // A slot for `f` that covers at least [lo, hi] (widened when the function is used by several veins).
    std::int32_t volumeSlot(const DensityFunction& f, int lo, int hi) {
        for (std::size_t i = 0; i < m_.volumeSlots.size(); ++i) {
            VolumeSlot& s = m_.volumeSlots[i];
            if (s.fn.nodeId() == f.nodeId()) {
                s.yLo = std::min(s.yLo, lo);
                s.yHi = std::max(s.yHi, hi);
                return static_cast<std::int32_t>(i);
            }
        }
        VolumeSlot s;
        s.fn = f;
        s.yLo = lo;
        s.yHi = hi;
        m_.volumeSlots.push_back(s);
        return static_cast<std::int32_t>(m_.volumeSlots.size() - 1);
    }

    // The y range outside which a vein density function is a constant <= 0: its root is `range_choice` on `minecraft:y` with a
    // constant "out of range" branch (true of the shipped copper and iron densities); anything else keeps the full height.
    void veinYRange(const std::string& ref, int& lo, int& hi) {
        JsonValue js;
        std::string err;
        if (!parseJsonFile(m_.worldgenDir / "density_function" / (stripNs(ref) + ".json"), js, err) || !js.isObject()) return;
        const JsonValue* type = js.find("type");
        const JsonValue* input = js.find("input");
        const JsonValue* out = js.find("when_out_of_range");
        const JsonValue* mn = js.find("min_inclusive");
        const JsonValue* mx = js.find("max_exclusive");
        float outside = 0.0f;
        double a = 0.0, b = 0.0;
        if (!type || !type->isString() || stripNs(type->text) != "range_choice" || !input || !input->isString() || stripNs(input->text) != "y" || !out || !out->toFloat(outside) ||
            !(outside <= 0.0f) || !mn || !mx || !mn->toDouble(a) || !mx->toDouble(b)) {
            return;
        }
        lo = std::max(lo, static_cast<int>(std::ceil(a)));
        hi = std::min(hi, static_cast<int>(std::ceil(b)) - 1);
    }

    // ---- conditions ---------------------------------------------------------------------------------------------
    std::int32_t condition(const JsonValue& js) {
        if (js.isString()) {
            const auto it = condNames_.find(js.text);
            if (it != condNames_.end()) return it->second;
            JsonValue file;
            if (!loadNamed("material_condition", js.text, file)) return -1;
            const std::int32_t c = condition(file);
            if (c >= 0) condNames_[js.text] = c;
            return c;
        }
        if (!js.isObject()) {
            fail("material condition is neither a name nor an object");
            return -1;
        }
        const JsonValue* type = js.find("type");
        if (!type || !type->isString()) {
            fail("material condition without type");
            return -1;
        }
        const std::string t = stripNs(type->text);
        CondNode n;
        if (t == "biome") {
            n.kind = CondKind::Biome;
            const JsonValue* is = js.find("biome_is");
            if (!is) {
                fail("biome condition without biome_is");
                return -1;
            }
            if (is->isString()) {
                if (!addBiomeEntry(is->text, n.biomes, 0)) return -1;
            } else if (is->isArray()) {
                for (const JsonValue& e : is->items) {
                    if (!e.isString() || !addBiomeEntry(e.text, n.biomes, 0)) {
                        fail("bad biome_is entry");
                        return -1;
                    }
                }
            } else {
                fail("bad biome_is");
                return -1;
            }
        } else if (t == "stone_depth") {
            n.kind = CondKind::StoneDepth;
            std::string surfaceType = "floor";
            if (const JsonValue* st = js.find("surface_type"); st && st->isString()) surfaceType = st->text;
            n.flagB = surfaceType == "ceiling";
            if (!intField(js, "offset", n.i0, true) || !boolField(js, "add_surface_depth", n.flagA, true) || !intField(js, "secondary_depth_range", n.i1, true)) return -1;
        } else if (t == "y_above") {
            n.kind = CondKind::YAbove;
            std::int32_t a = 0, mult = 0;
            if (!anchor(js.find("anchor"), a) || !intField(js, "surface_depth_multiplier", mult, true) || !boolField(js, "add_stone_depth", n.flagA, true)) return -1;
            n.i0 = a;
            n.i1 = mult;
        } else if (t == "water") {
            n.kind = CondKind::Water;
            if (!intField(js, "offset", n.i0, true) || !intField(js, "surface_depth_multiplier", n.i1, true) || !boolField(js, "add_stone_depth", n.flagA, true)) return -1;
        } else if (t == "vertical_gradient") {
            n.kind = CondKind::VerticalGradient;
            const JsonValue* name = js.find("random_name");
            if (!name || !name->isString() || !anchor(js.find("true_at_and_below"), n.i0) || !anchor(js.find("false_at_and_above"), n.i1)) {
                fail("bad vertical_gradient condition");
                return -1;
            }
            n.factory = m_.router->randomState()->randomFactory(name->text.find(':') == std::string::npos ? "minecraft:" + name->text : name->text);
        } else if (t == "noise_threshold") {
            n.kind = CondKind::NoiseThreshold;
            const JsonValue* noise = js.find("noise");
            const JsonValue* mn = js.find("min_threshold");
            const JsonValue* mx = js.find("max_threshold");
            if (!noise || !noise->isString() || !mn || !mx || !mn->toDouble(n.d0) || !mx->toDouble(n.d1)) {
                fail("bad noise_threshold condition");
                return -1;
            }
            if (!boolField(js, "is_3d", n.flagA, false, false)) return -1;
            n.noise = noiseFor(noise->text);
            if (!n.noise) return -1;
        } else if (t == "not") {
            n.kind = CondKind::Not;
            const JsonValue* inv = js.find("invert");
            if (!inv) {
                fail("not condition without invert");
                return -1;
            }
            n.child = condition(*inv);
            if (n.child < 0) return -1;
        } else if (t == "steep") {
            n.kind = CondKind::Steep;
        } else if (t == "hole") {
            n.kind = CondKind::Hole;
        } else if (t == "above_preliminary_surface") {
            n.kind = CondKind::AbovePreliminarySurface;
        } else if (t == "temperature") {
            n.kind = CondKind::Temperature;
        } else {
            fail("unsupported material condition type " + type->text);
            return -1;
        }
        return addCond(std::move(n));
    }

    // ---- rules --------------------------------------------------------------------------------------------------
    std::int32_t rule(const JsonValueRef& ref) {
        const auto it = ruleNames_.find(ref.name);
        if (it != ruleNames_.end()) return it->second;
        JsonValue file;
        if (!loadNamed("material_rule", ref.name, file)) return -1;
        const std::int32_t r = rule(file);
        if (r >= 0) ruleNames_[ref.name] = r;
        return r;
    }

    std::int32_t rule(const JsonValue& js) {
        if (js.isString()) return rule(JsonValueRef(js.text));
        if (!js.isObject()) {
            fail("material rule is neither a name nor an object");
            return -1;
        }
        const JsonValue* type = js.find("type");
        if (!type || !type->isString()) {
            fail("material rule without type");
            return -1;
        }
        const std::string t = stripNs(type->text);
        RuleNode n;
        if (t == "sequence") {
            const JsonValue* seq = js.find("sequence");
            if (!seq || !seq->isArray()) {
                fail("sequence rule without sequence");
                return -1;
            }
            std::vector<std::int32_t> kids;
            for (const JsonValue& e : seq->items) {
                const std::int32_t k = rule(e);
                if (k < 0) return -1;
                kids.push_back(k);
            }
            n.kind = RuleKind::Sequence;
            n.a = static_cast<std::int32_t>(m_.childIndex.size());
            n.b = static_cast<std::int32_t>(kids.size());
            m_.childIndex.insert(m_.childIndex.end(), kids.begin(), kids.end());
        } else if (t == "condition") {
            const JsonValue* ifTrue = js.find("if_true");
            const JsonValue* thenRun = js.find("then_run");
            if (!ifTrue || !thenRun) {
                fail("condition rule needs if_true and then_run");
                return -1;
            }
            const std::int32_t c = condition(*ifTrue);
            if (c < 0) return -1;
            const std::int32_t r = rule(*thenRun);
            if (r < 0) return -1;
            n.kind = RuleKind::Condition;
            n.a = c;
            n.b = r;
        } else if (t == "block") {
            n.kind = RuleKind::BlockResult;
            if (!blockState(js.find("result_state"), n.block)) return -1;
        } else if (t == "bandlands") {
            n.kind = RuleKind::Bandlands;
        } else if (t == "ore_vein") {
            n.kind = RuleKind::OreVein;
            OreVein v;
            const auto fn = [&](const char* key, DensityFunction& out) {
                const JsonValue* ref = js.find(key);
                if (!ref || !ref->isString()) return fail(std::string("ore_vein without ") + key);
                out = m_.router->function(ref->text);
                if (!out.valid()) return fail("density function " + ref->text + " was not built into the router (pass collectDensityRefs() to NoiseRouter::create)");
                return true;
            };
            DensityFunction density, richness;
            if (!fn("density", density) || !fn("richness", richness) || !fn("filler_gap", v.gap)) return -1;
            int lo = m_.minY, hi = m_.minY + m_.height - 1;
            if (const JsonValue* ref = js.find("density"); ref && ref->isString()) veinYRange(ref->text, lo, hi);
            v.yLo = lo;
            v.yHi = hi;
            v.densitySlot = volumeSlot(density, lo, hi);
            v.richnessSlot = volumeSlot(richness, lo, hi);
            if (!blockState(js.find("ore_block"), v.ore) || !blockState(js.find("raw_ore_block"), v.raw) || !blockState(js.find("filler_block"), v.filler)) return -1;
            const JsonValue* chance = js.find("raw_ore_chance");
            if (!chance || !chance->toFloat(v.rawChance)) {
                fail("ore_vein without raw_ore_chance");
                return -1;
            }
            v.factory = m_.router->randomState()->randomFactory("minecraft:ore");
            n.a = static_cast<std::int32_t>(m_.veins.size());
            m_.veins.push_back(std::move(v));
        } else {
            fail("unsupported material rule type " + type->text);
            return -1;
        }
        return addRule(n);
    }

    MaterialSystem::Impl& m_;
    std::string& error_;
    std::map<std::string, std::int32_t> ruleNames_, condNames_;
};

// ---- collecting the density functions of ore veins (before the router is built)
bool collectRefs(const std::filesystem::path& worldgenDir, const JsonValue& js, std::set<std::string>& visitedRules, std::vector<std::string>& refs, std::string& error, int depth) {
    if (depth > 64) {
        error = "material rules nest too deeply";
        return false;
    }
    if (js.isString()) {
        if (!visitedRules.insert(js.text).second) return true;
        JsonValue file;
        if (!parseJsonFile(worldgenDir / "material_rule" / (stripNs(js.text) + ".json"), file, error)) return false;
        return collectRefs(worldgenDir, file, visitedRules, refs, error, depth + 1);
    }
    if (!js.isObject()) return true;
    const JsonValue* type = js.find("type");
    if (!type || !type->isString()) return true;
    const std::string t = stripNs(type->text);
    if (t == "sequence") {
        if (const JsonValue* seq = js.find("sequence"); seq && seq->isArray()) {
            for (const JsonValue& e : seq->items) {
                if (!collectRefs(worldgenDir, e, visitedRules, refs, error, depth + 1)) return false;
            }
        }
    } else if (t == "condition") {
        if (const JsonValue* r = js.find("then_run")) return collectRefs(worldgenDir, *r, visitedRules, refs, error, depth + 1);
    } else if (t == "ore_vein") {
        for (const char* key : {"density", "richness", "filler_gap"}) {
            if (const JsonValue* ref = js.find(key); ref && ref->isString() && std::find(refs.begin(), refs.end(), ref->text) == refs.end()) refs.push_back(ref->text);
        }
    }
    return true;
}

}  // namespace

// ===============================================================================================================
MaterialSystem::MaterialSystem() = default;
MaterialSystem::~MaterialSystem() = default;

bool MaterialSystem::collectDensityRefs(const std::filesystem::path& worldgenDir, const std::string& ruleName, std::vector<std::string>& refs, std::string& error) {
    std::set<std::string> visited;
    JsonValue name;
    name.kind = JsonValue::Kind::String;
    name.text = ruleName;
    return collectRefs(worldgenDir, name, visited, refs, error, 0);
}

std::unique_ptr<MaterialSystem> MaterialSystem::create(const Params& p, std::string& error) {
    if (!p.router || !p.biomes || !p.blocks || !p.facts) {
        error = "material system: missing parameters";
        return nullptr;
    }
    std::unique_ptr<MaterialSystem> sys(new MaterialSystem());
    sys->impl_ = std::make_unique<Impl>();
    Impl& m = *sys->impl_;
    m.id = g_nextSystemId.fetch_add(1);
    m.router = p.router;
    m.blocks = p.blocks;
    m.facts = p.facts;
    m.biomeRegistry = p.biomes;
    m.worldgenDir = p.worldgenDir;
    m.minY = p.router->settings().minY;
    m.height = p.router->settings().height;
    m.seaLevel = p.router->settings().seaLevel;
    m.root = p.router->randomState()->root();

    const auto block = [&](const std::string& name, Block& out) {
        const auto b = p.blocks->parse(name);
        if (!b) {
            error = "material system: unknown block " + name;
            return false;
        }
        out = *b;
        return true;
    };
    Block terracotta, orange, yellow, brown, red, white, lightGray;
    if (!block(p.router->settings().defaultBlock, m.defaultBlock) || !block("minecraft:snow_block", m.snowBlock) || !block("minecraft:packed_ice", m.packedIce) ||
        !block("minecraft:terracotta", terracotta) || !block("minecraft:orange_terracotta", orange) || !block("minecraft:yellow_terracotta", yellow) ||
        !block("minecraft:brown_terracotta", brown) || !block("minecraft:red_terracotta", red) || !block("minecraft:white_terracotta", white) ||
        !block("minecraft:light_gray_terracotta", lightGray)) {
        return nullptr;
    }
    m.defaultBlockType = p.blocks->state(m.defaultBlock).block;
    const auto water = p.blocks->findBlock("water");
    m.waterBlockType = water ? *water : -1;
    m.erodedBadlands = p.biomes->find("minecraft:eroded_badlands");
    m.frozenOcean = p.biomes->find("minecraft:frozen_ocean");
    m.deepFrozenOcean = p.biomes->find("minecraft:deep_frozen_ocean");

    // noises (one shared instance per key, from the random state)
    const auto noise = [&](const char* key, const NormalNoise*& out) {
        std::string err;
        out = p.router->randomState()->noise(key, err);
        if (!out) {
            error = std::string("material system: noise ") + key + ": " + err;
            return false;
        }
        return true;
    };
    if (!noise("minecraft:surface", m.surfaceNoise) || !noise("minecraft:surface_secondary", m.secondaryNoise) || !noise("minecraft:clay_bands_offset", m.clayOffsetNoise) ||
        !noise("minecraft:badlands_surface", m.badlandsSurface) || !noise("minecraft:badlands_pillar", m.badlandsPillar) ||
        !noise("minecraft:badlands_pillar_roof", m.badlandsRoof) || !noise("minecraft:iceberg_surface", m.icebergSurface) ||
        !noise("minecraft:iceberg_pillar", m.icebergPillar) || !noise("minecraft:iceberg_pillar_roof", m.icebergRoof)) {
        return nullptr;
    }

    // clay bands: MaterialSystem.generateBands(root.fromHashOf("minecraft:clay_bands"))
    {
        XoroshiroRandomSource random = m.root.makeFromHashOf("minecraft:clay_bands");
        m.bands.fill(terracotta);
        for (int i = 0; i < kBandCount; ++i) {
            i += random.nextInt(5) + 1;
            if (i < kBandCount) m.bands[static_cast<std::size_t>(i)] = orange;
        }
        const auto makeBands = [&](int minSize, Block s) {
            const int count = random.nextIntBetweenInclusive(6, 15);
            for (int j = 0; j < count; ++j) {
                const int size = minSize + random.nextInt(3);
                const int start = random.nextInt(kBandCount);
                for (int k = 0; start + k < kBandCount && k < size; ++k) m.bands[static_cast<std::size_t>(start + k)] = s;
            }
        };
        makeBands(1, yellow);
        makeBands(2, brown);
        makeBands(1, red);
        const int n = random.nextIntBetweenInclusive(9, 15);
        for (int placed = 0, i = 0; placed < n && i < kBandCount; ++placed, i += random.nextInt(16) + 4) {
            m.bands[static_cast<std::size_t>(i)] = white;
            if (i - 1 > 0 && random.nextBoolean()) m.bands[static_cast<std::size_t>(i - 1)] = lightGray;
            if (i + 1 < kBandCount && random.nextBoolean()) m.bands[static_cast<std::size_t>(i + 1)] = lightGray;
        }
    }

    Compiler compiler(m, error);
    if (!compiler.compileRoot(p.ruleName)) {
        if (error.empty()) error = "material system: rule compilation failed";
        return nullptr;
    }
    // The evaluated y range of each ore volume: whole 16-block layers from the one containing the lowest vein block
    // (see VolumeSlot) up to the highest vein block.
    for (VolumeSlot& s : m.volumeSlots) {
        s.yLo = std::max(s.yLo, m.minY);
        s.yHi = std::min(s.yHi, m.minY + m.height - 1);
        s.base = std::max(m.minY, floorDiv(s.yLo, 16) * 16);
        s.size = std::max(0, s.yHi + 1 - s.base);
    }
    return sys;
}

// ---------------------------------------------------------------------------------------------------------------
Block MaterialSystem::clayBand(int x, int y, int z) const { return impl_->bandAt(x, y, z); }
const std::array<Block, 192>& MaterialSystem::clayBands() const { return impl_->bands; }
int MaterialSystem::minY() const { return impl_->minY; }
int MaterialSystem::height() const { return impl_->height; }
std::size_t MaterialSystem::ruleNodeCount() const { return impl_->rules.size(); }
std::size_t MaterialSystem::conditionNodeCount() const { return impl_->conds.size(); }
std::size_t MaterialSystem::scratchBytes() const {
    std::size_t bytes = 0;
    for (const VolumeSlot& s : impl_->volumeSlots) bytes += static_cast<std::size_t>(std::max(0, s.size)) * 256u * sizeof(float);
    return bytes;
}
float MaterialSystem::biomeTemperature(BiomeId biome, int x, int y, int z) const { return impl_->biomeTemperature(biome, x, y, z); }

int MaterialSystem::surfaceDepth(int x, int z) const {
    const Impl& m = *impl_;
    // (int)((double)surfaceNoise.get(x, 0, z) * 2.75 + 3.0 + ROOT.at(x, 0, z).nextDouble() * 0.25)
    XoroshiroRandomSource r = m.root.makeAt(x, 0, z);
    const double noise = static_cast<double>(m.surfaceNoise->get(static_cast<double>(x), 0.0, static_cast<double>(z)));
    const double a = noise * 2.75;
    const double b = a + 3.0;
    const double c = r.nextDouble() * 0.25;
    return javaD2I(b + c);
}

double MaterialSystem::surfaceSecondary(int x, int z) const {
    return static_cast<double>(impl_->secondaryNoise->get(static_cast<double>(x), 0.0, static_cast<double>(z)));
}

// ---------------------------------------------------------------------------------------------------------------
void MaterialSystem::buildSurface(Workspace& ws, const SurfaceTarget& t) const {
    const Impl& m = *impl_;
    if (!ws.impl_ || ws.impl_->systemId != m.id) {
        ws.impl_ = std::make_shared<Workspace::Impl>();
        ws.impl_->systemId = m.id;
        ws.impl_->volumes.resize(m.volumeSlots.size());
        ws.impl_->condStamp.assign(m.conds.size(), 0);
        ws.impl_->condValue.assign(m.conds.size(), 0.0);
        ws.impl_->condPossible.assign(m.conds.size(), 1);
    }
    Workspace::Impl& w = *ws.impl_;
    std::fill(w.condStamp.begin(), w.condStamp.end(), 0u);
    for (std::size_t i = 0; i < m.conds.size(); ++i) {
        if (m.conds[i].kind == CondKind::Biome) w.condPossible[i] = t.biomes->mayContain(m.conds[i].biomes) ? 1 : 0;
    }

    // The ore-vein densities over the chunk volume (the game reads its buffered volume sample inside the chunk); only the
    // y range a vein can exist in is computed.
    {
        for (std::size_t i = 0; i < m.volumeSlots.size(); ++i) {
            const VolumeSlot& s = m.volumeSlots[i];
            if (s.size <= 0) continue;
            DensityVolume vol;
            vol.sizeX = 16;
            vol.sizeY = s.size;
            vol.sizeZ = 16;
            vol.minX = t.chunkX * 16;
            vol.minY = s.base;
            vol.minZ = t.chunkZ * 16;
            w.volumes[i].resize(vol.size());
            s.fn.fillVolume(vol, w.volumes[i].data());
        }
    }

    Impl::Ctx c;
    c.m = &m;
    c.ws = &w;
    c.t = &t;
    const int chunkMinX = t.chunkX * 16, chunkMinZ = t.chunkZ * 16;
    const int maxY = m.minY + m.height - 1;
    std::uint32_t columnStamp = 0, blockStamp = 0;

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            const int x = chunkMinX + lx, z = chunkMinZ + lz;
            const int firstStartY = t.worldSurface[lx + 16 * lz];  // getHeight(WORLD_SURFACE_WG) + 1
            const BiomeId biome = t.biomes->biomeAt(x, firstStartY, z);
            if (biome == m.erodedBadlands) m.erodedBadlandsExtension(t, lx, lz, x, z, firstStartY);
            const int startY = t.worldSurface[lx + 16 * lz];  // re-read: the extension raised the live heightmap

            const auto hm = [&](int hx, int hz) { return static_cast<int>(t.worldSurface[hx + 16 * hz]); };
            c.lx = lx;
            c.lz = lz;
            c.x = x;
            c.z = z;
            c.gradX = hm(std::min(lx + 1, 15), lz) - hm(std::max(lx - 1, 0), lz);
            c.gradZ = hm(lx, std::min(lz + 1, 15)) - hm(lx, std::max(lz - 1, 0));
            c.surfaceDepth = surfaceDepth(x, z);
            c.minSurface = javaFloorF(t.chunkSurfaceLevel[lx + 16 * lz]) + c.surfaceDepth - 8;
            c.secondaryValid = false;
            c.columnStamp = ++columnStamp;

            int stoneDepthAbove = 0;
            int waterHeight = std::numeric_limits<int>::min();
            int stoneDepthBelow = std::numeric_limits<int>::max();
            const Block* col = t.blocks + static_cast<std::size_t>(m.height) * (static_cast<std::size_t>(lx) + 16u * static_cast<std::size_t>(lz));
            for (int y = startY; y >= m.minY; --y) {
                const Block bs = y > maxY ? Block::Air : col[y - m.minY];
                if (m.facts->isAir(bs)) {
                    stoneDepthAbove = 0;
                    waterHeight = std::numeric_limits<int>::min();
                    continue;
                }
                if (m.facts->isFluid(bs)) {  // water and lava
                    if (waterHeight == std::numeric_limits<int>::min()) waterHeight = y + 1;
                    continue;
                }
                if (stoneDepthBelow >= y) {
                    stoneDepthBelow = kWayBelowMinY;
                    for (int yy = y - 1; yy >= m.minY - 1; --yy) {
                        const Block below = yy < m.minY ? Block::Air : col[yy - m.minY];
                        if (!m.isStone(below)) {
                            stoneDepthBelow = yy + 1;
                            break;
                        }
                    }
                }
                ++stoneDepthAbove;
                c.y = y;
                c.above = stoneDepthAbove;
                c.below = y - stoneDepthBelow + 1;
                c.waterHeight = waterHeight;
                c.biomeValid = false;
                c.blockStamp = ++blockStamp;
                const Block r = m.apply(m.rootRule, c);
                if (r != kNone) m.setBlock(t, lx, lz, y, r);
            }
            if (biome == m.frozenOcean || biome == m.deepFrozenOcean) m.frozenOceanExtension(t, c.minSurface, biome, lx, lz, x, z, firstStartY);
        }
    }
}

}  // namespace falcon::worldgen
