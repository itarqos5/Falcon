#include "falcon/worldgen/material.hpp"
#include "material_compiler.hpp"
#include "material_internal.hpp"

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
    m.rootX = p.router->randomState()->root();
    m.root = p.router->randomState()->rootAny();

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
        XoroshiroRandomSource random = m.rootX.makeFromHashOf("minecraft:clay_bands");
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
    AnyRandom r = m.root.makeAt(x, 0, z);
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
