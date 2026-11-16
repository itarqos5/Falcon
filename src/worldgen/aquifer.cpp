#include "falcon/worldgen/aquifer.hpp"

#include "falcon/worldgen/java_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace falcon::worldgen {

// ===============================================================================================================
// Shared, lock-free column cache
QuartColumnCache::QuartColumnCache(unsigned log2Size) : table_(new std::atomic<std::uint64_t>[std::size_t{1} << log2Size]), mask_((std::size_t{1} << log2Size) - 1) {
    for (std::size_t i = 0; i <= mask_; ++i) table_[i].store(0, std::memory_order_relaxed);
}

std::uint64_t QuartColumnCache::keyOf(int qx, int qz) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(qx) & 0x3FFFFFFu) << 26) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(qz)) & 0x3FFFFFFu);
}

bool QuartColumnCache::find(int qx, int qz, int& value) const {
    const std::uint64_t key = keyOf(qx, qz);
    const std::uint64_t entry = table_[(key * 0x9E3779B97F4A7C15ull >> 20) & mask_].load(std::memory_order_relaxed);
    if (entry == 0 || (entry >> 12) != key) return false;
    value = static_cast<int>(entry & 0xFFFu) - 2048;
    return true;
}

void QuartColumnCache::store(int qx, int qz, int value) {
    if (value <= -2048 || value >= 2047) return;  // entry value 0 means "empty": only 1..4095 are representable
    const std::uint64_t key = keyOf(qx, qz);
    table_[(key * 0x9E3779B97F4A7C15ull >> 20) & mask_].store((key << 12) | static_cast<std::uint64_t>(value + 2048), std::memory_order_relaxed);
}

// ===============================================================================================================
std::shared_ptr<AquiferShared> AquiferShared::create(const NoiseRouter& router, std::string& error) {
    auto s = std::make_shared<AquiferShared>();
    s->seaLevel = router.settings().seaLevel;
    s->minY = router.settings().minY;
    s->height = router.settings().height;
    s->defaultFluid = router.settings().defaultFluid == "minecraft:lava" ? Fluid::Lava : router.settings().defaultFluid == "minecraft:air" ? Fluid::Air : Fluid::Water;
    s->random = router.randomState()->randomFactory("minecraft:aquifer");
    if (router.hasAquifers()) {
        s->barrier = router.aquiferBarrier();
        s->floodedness = router.aquiferFluidLevelFloodedness();
        s->spread = router.aquiferFluidLevelSpread();
        s->lava = router.aquiferLava();
        s->exclusion = router.aquiferExclusion();
        s->surfaceLevel = router.aquiferSurfaceLevel();
        s->valid = true;
    } else if (router.settings().defaultFluid != "minecraft:water" && router.settings().defaultFluid != "minecraft:lava" && router.settings().defaultFluid != "minecraft:air") {
        error = "aquifer: unsupported default fluid " + router.settings().defaultFluid;
        return nullptr;
    }
    return s;
}

int AquiferShared::surfaceLevelAt(int x, int z) const {
    const int qx = x >> 2, qz = z >> 2;
    int v = 0;
    if (surfaceCache.find(qx, qz, v)) return v;
    v = javaFloorF(surfaceLevel.compute(qx * 4, 0, qz * 4));  // QuartPos.toBlock(QuartPos.fromBlock(x)); Mth.floor(float)
    surfaceCache.store(qx, qz, v);
    return v;
}

// ===============================================================================================================
namespace {

constexpr int kSpacingX = 16, kSpacingY = 12, kSpacingZ = 16;
constexpr int kOffsetX = -5, kOffsetY = 1, kOffsetZ = -5;

[[nodiscard]] inline int gridX(int x) { return x >> 4; }
[[nodiscard]] inline int gridZ(int z) { return z >> 4; }
[[nodiscard]] inline int gridY(int y) { return floorDiv(y, kSpacingY); }
[[nodiscard]] inline int fromGridX(int g, int off) { return (g << 4) + off; }
[[nodiscard]] inline int fromGridZ(int g, int off) { return (g << 4) + off; }
[[nodiscard]] inline int fromGridY(int g, int off) { return g * kSpacingY + off; }

[[nodiscard]] inline double similarity(int d1, int d2) { return 1.0 - static_cast<double>(d2 - d1) / 25.0; }
const double kFlowingUpdateSimilarity = similarity(10 * 10, 12 * 12);

// Mth helpers on doubles
[[nodiscard]] inline double lerpD(double t, double a, double b) { return a + t * (b - a); }
[[nodiscard]] inline double inverseLerpD(double v, double a, double b) { return (v - a) / (b - a); }
[[nodiscard]] inline double mapD(double v, double a, double b, double c, double d) { return lerpD(inverseLerpD(v, a, b), c, d); }
[[nodiscard]] inline double clampedLerpD(double t, double a, double b) { return t < 0.0 ? a : (t > 1.0 ? b : lerpD(t, a, b)); }
[[nodiscard]] inline double clampedMapD(double v, double a, double b, double c, double d) { return clampedLerpD(inverseLerpD(v, a, b), c, d); }
[[nodiscard]] inline double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The 13 neighbouring chunk columns computeFluid looks at, in the game's order.
constexpr int kOffsets[13][2] = {{0, 0}, {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {-3, 0}, {-2, 0}, {-1, 0}, {1, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}};

[[nodiscard]] inline Aquifer::Substance toSubstance(Fluid f) {
    switch (f) {
        case Fluid::Water: return Aquifer::Substance::Water;
        case Fluid::Lava: return Aquifer::Substance::Lava;
        case Fluid::Air: break;
    }
    return Aquifer::Substance::Air;
}

}  // namespace

Aquifer::Aquifer(const AquiferShared& shared, bool trackFluidUpdates) : shared_(shared), track_(trackFluidUpdates) {}

void Aquifer::begin(int chunkX, int chunkZ) {
    const int minBlockX = chunkX * 16, minBlockZ = chunkZ * 16;
    const int maxBlockX = minBlockX + 15, maxBlockZ = minBlockZ + 15;
    const int maxBlockY = shared_.minY + shared_.height - 1;
    minGridX_ = gridX(minBlockX + kOffsetX);
    const int maxGridX = gridX(maxBlockX + kOffsetX) + 1;
    minGridZ_ = gridZ(minBlockZ + kOffsetZ);
    const int maxGridZ = gridZ(maxBlockZ + kOffsetZ) + 1;
    minGridY_ = gridY(shared_.minY + kOffsetY) - 1;
    const int maxGridY = gridY(maxBlockY + kOffsetY) + 1;
    sizeX_ = maxGridX - minGridX_ + 1;
    sizeY_ = maxGridY - minGridY_ + 1;
    sizeZ_ = maxGridZ - minGridZ_ + 1;
    const std::size_t count = static_cast<std::size_t>(sizeX_) * static_cast<std::size_t>(sizeY_) * static_cast<std::size_t>(sizeZ_);
    cells_.assign(count, Cell{});

    if (!shared_.valid) {
        skipSamplingAboveY_ = std::numeric_limits<int>::max();
        return;
    }
    // maxSurfaceLevel over the quart columns covering grid cells minGrid..maxGrid (their centres lie up to 9 blocks in)
    const int x0 = fromGridX(minGridX_, 0), z0 = fromGridZ(minGridZ_, 0);
    const int x1 = fromGridX(maxGridX, 9), z1 = fromGridZ(maxGridZ, 9);
    int maxSurface = std::numeric_limits<int>::min();
    for (int qz = z0 >> 2; qz <= (z1 >> 2); ++qz) {
        for (int qx = x0 >> 2; qx <= (x1 >> 2); ++qx) maxSurface = std::max(maxSurface, shared_.surfaceLevelAt(qx * 4, qz * 4));
    }
    const int top = maxSurface + 8;  // adjustSurfaceLevel
    const int gy = gridY(top + kSpacingY) + 1;
    skipSamplingAboveY_ = fromGridY(gy, 11) - 1;
}

std::size_t Aquifer::cellIndex(int gx, int gy, int gz) const {
    const int ix = std::clamp(gx - minGridX_, 0, sizeX_ - 1);
    const int iy = std::clamp(gy - minGridY_, 0, sizeY_ - 1);
    const int iz = std::clamp(gz - minGridZ_, 0, sizeZ_ - 1);
    return (static_cast<std::size_t>(ix) * static_cast<std::size_t>(sizeZ_) + static_cast<std::size_t>(iz)) * static_cast<std::size_t>(sizeY_) + static_cast<std::size_t>(iy);
}

Aquifer::Cell& Aquifer::cell(int gx, int gy, int gz) {
    Cell& c = cells_[cellIndex(gx, gy, gz)];
    if (!(c.have & 1)) {
        XoroshiroRandomSource r = shared_.random.makeAt(gx, gy, gz);
        const int ox = r.nextInt(10);  // call order: x, y, z
        const int oy = r.nextInt(9);
        const int oz = r.nextInt(10);
        c.x = fromGridX(gx, ox);
        c.y = fromGridY(gy, oy);
        c.z = fromGridZ(gz, oz);
        c.have |= 1;
    }
    return c;
}

FluidStatus Aquifer::status(std::size_t index, int, int, int) {
    Cell& c = cells_[index];
    if (!(c.have & 2)) {
        const FluidStatus s = computeFluid(c.x, c.y, c.z);
        c.level = s.level;
        c.type = s.type;
        c.have |= 2;
    }
    return {c.level, c.type};
}

FluidStatus Aquifer::cellStatus(int gx, int gy, int gz) {
    (void)cell(gx, gy, gz);
    return status(cellIndex(gx, gy, gz), gx, gy, gz);
}

Aquifer::Center Aquifer::cellCenter(int gx, int gy, int gz) {
    const Cell& c = cell(gx, gy, gz);
    return {c.x, c.y, c.z};
}

// ---------------------------------------------------------------------------------------------------------------
FluidStatus Aquifer::computeFluid(int x, int y, int z) {
    const FluidStatus global = shared_.globalFluid(y);
    int minSurface = std::numeric_limits<int>::max();
    const int topY = y + kSpacingY, bottomY = y - kSpacingY;
    bool flag = false;
    for (const auto& o : kOffsets) {
        const int sx = x + o[0] * 16;  // SectionPos.sectionToBlockCoord
        const int sz = z + o[1] * 16;
        const int pre = shared_.surfaceLevelAt(sx, sz);
        const int adj = pre + 8;
        const bool center = o[0] == 0 && o[1] == 0;
        if (center && bottomY > adj) return global;  // the aquifer centre is far above the terrain
        const bool above = topY > adj;
        if (above || center) {
            const FluidStatus gs = shared_.globalFluid(adj);
            if (gs.at(adj) != Fluid::Air) {
                if (center) flag = true;
                if (above) return gs;
            }
        }
        minSurface = std::min(minSurface, pre);
    }
    const int level = computeSurfaceLevel(x, y, z, global, minSurface, flag);
    return {level, computeFluidType(x, y, z, global, level)};
}

int Aquifer::computeSurfaceLevel(int x, int y, int z, const FluidStatus& global, int minSurface, bool flag) {
    double d0, d1;
    if (static_cast<double>(shared_.exclusion.compute(x, y, z)) > 0.0) {  // the deep-dark region: no fluid
        d0 = -1.0;
        d1 = -1.0;
    } else {
        const int i = (minSurface + 8) - y;  // adjustSurfaceLevel(minSurface) - y
        const double d2 = flag ? clampedMapD(static_cast<double>(i), 0.0, 64.0, 1.0, 0.0) : 0.0;
        const double d3 = clampD(static_cast<double>(shared_.floodedness.compute(x, y, z)), -1.0, 1.0);
        const double d4 = mapD(d2, 1.0, 0.0, -0.3, 0.8);
        const double d5 = mapD(d2, 1.0, 0.0, -0.8, 0.4);
        d0 = d3 - d5;
        d1 = d3 - d4;
    }
    if (d1 > 0.0) return global.level;
    if (d0 > 0.0) return computeRandomizedFluidSurfaceLevel(x, y, z, minSurface);
    return kWayBelowMinY;
}

int Aquifer::computeRandomizedFluidSurfaceLevel(int x, int y, int z, int minSurface) {
    const int k = floorDiv(x, 16), l = floorDiv(y, 40), m = floorDiv(z, 16);
    const int base = l * 40 + 20;
    const float spread = shared_.spread.compute(k, l, m);  // the grid coordinates are used as block coordinates
    const double d = static_cast<double>(spread * 10.0f);  // the multiply is a float multiply
    const int q = javaFloor(d / 3.0) * 3;                  // Mth.quantize(d, 3)
    return std::min(minSurface, base + q);
}

Fluid Aquifer::computeFluidType(int x, int y, int z, const FluidStatus& global, int level) {
    Fluid t = global.type;
    if (level <= -10 && level != kWayBelowMinY && global.type != Fluid::Lava) {
        const double n = static_cast<double>(shared_.lava.compute(floorDiv(x, 64), floorDiv(y, 40), floorDiv(z, 64)));
        if (std::fabs(n) > 0.3) t = Fluid::Lava;
    }
    return t;
}

double Aquifer::calculatePressure(int x, int y, int z, double& barrier, const FluidStatus& a, const FluidStatus& b) {
    const Fluid fa = a.at(y), fb = b.at(y);
    if ((fa == Fluid::Lava && fb == Fluid::Water) || (fa == Fluid::Water && fb == Fluid::Lava)) return 2.0;
    const int diff = std::abs(a.level - b.level);
    if (diff == 0) return 0.0;
    const double mid = 0.5 * static_cast<double>(a.level + b.level);
    const double d1 = static_cast<double>(y) + 0.5 - mid;
    const double d2 = static_cast<double>(diff) / 2.0;
    const double d9 = d2 - std::fabs(d1);
    double d10;
    if (d1 > 0.0) {
        const double t = 0.0 + d9;
        d10 = t > 0.0 ? t / 1.5 : t / 2.5;
    } else {
        const double t = 3.0 + d9;
        d10 = t > 0.0 ? t / 3.0 : t / 10.0;
    }
    double noise;
    if (d10 < -2.0 || d10 > 2.0) {
        noise = 0.0;
    } else {
        if (std::isnan(barrier)) barrier = static_cast<double>(shared_.barrier.compute(x, y, z));
        noise = barrier;
    }
    return 2.0 * (noise + d10);
}

// ---------------------------------------------------------------------------------------------------------------
Aquifer::Substance Aquifer::computeSubstance(int x, int y, int z, double density) {
    if (density > 0.0) {
        shouldSchedule_ = false;
        return Substance::Default;
    }
    const FluidStatus global = shared_.globalFluid(y);
    if (!shared_.valid || y > skipSamplingAboveY_) {
        shouldSchedule_ = false;
        return toSubstance(global.at(y));
    }
    if (global.at(y) == Fluid::Lava) {
        shouldSchedule_ = false;
        return Substance::Lava;
    }

    const int gx = gridX(x + kOffsetX), gy = gridY(y + kOffsetY), gz = gridZ(z + kOffsetZ);
    int d0 = std::numeric_limits<int>::max(), d1 = d0, d2 = d0, d3 = d0;
    std::size_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
    for (int dx = 0; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = 0; dz <= 1; ++dz) {
                const Cell& c = cell(gx + dx, gy + dy, gz + dz);
                const std::size_t idx = cellIndex(gx + dx, gy + dy, gz + dz);
                const int ex = c.x - x, ey = c.y - y, ez = c.z - z;
                const int dist = ex * ex + ey * ey + ez * ez;
                if (d0 >= dist) {
                    i3 = i2; d3 = d2;
                    i2 = i1; d2 = d1;
                    i1 = i0; d1 = d0;
                    i0 = idx; d0 = dist;
                } else if (d1 >= dist) {
                    i3 = i2; d3 = d2;
                    i2 = i1; d2 = d1;
                    i1 = idx; d1 = dist;
                } else if (d2 >= dist) {
                    i3 = i2; d3 = d2;
                    i2 = idx; d2 = dist;
                } else if (d3 >= dist) {
                    i3 = idx; d3 = dist;
                }
            }
        }
    }

    const FluidStatus s0 = status(i0, 0, 0, 0);
    const double sim01 = similarity(d0, d1);
    const Fluid state = s0.at(y);
    if (sim01 <= 0.0) {
        shouldSchedule_ = track_ && sim01 >= kFlowingUpdateSimilarity ? !(s0 == status(i1, 0, 0, 0)) : false;
        return toSubstance(state);
    }
    if (state == Fluid::Water && shared_.globalFluid(y - 1).at(y - 1) == Fluid::Lava) {
        shouldSchedule_ = true;
        return toSubstance(state);
    }
    double barrier = std::numeric_limits<double>::quiet_NaN();
    const FluidStatus s1 = status(i1, 0, 0, 0);
    const double p01 = sim01 * calculatePressure(x, y, z, barrier, s0, s1);
    if (density + p01 > 0.0) {
        shouldSchedule_ = false;
        return Substance::Default;
    }
    const FluidStatus s2 = status(i2, 0, 0, 0);
    const double sim02 = similarity(d0, d2);
    if (sim02 > 0.0) {
        const double p = sim01 * sim02 * calculatePressure(x, y, z, barrier, s0, s2);
        if (density + p > 0.0) {
            shouldSchedule_ = false;
            return Substance::Default;
        }
    }
    const double sim12 = similarity(d1, d2);
    if (sim12 > 0.0) {
        const double p = sim01 * sim12 * calculatePressure(x, y, z, barrier, s1, s2);
        if (density + p > 0.0) {
            shouldSchedule_ = false;
            return Substance::Default;
        }
    }
    if (track_) {
        const bool a = !(s0 == s1);
        const bool b = sim12 >= kFlowingUpdateSimilarity && !(s1 == s2);
        const bool c = sim02 >= kFlowingUpdateSimilarity && !(s0 == s2);
        if (a || b || c) {
            shouldSchedule_ = true;
        } else {
            shouldSchedule_ = sim02 >= kFlowingUpdateSimilarity && similarity(d0, d3) >= kFlowingUpdateSimilarity && !(s0 == status(i3, 0, 0, 0));
        }
    } else {
        shouldSchedule_ = false;
    }
    return toSubstance(state);
}

}  // namespace falcon::worldgen
