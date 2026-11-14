#include "falcon/worldgen/underground.hpp"

#include "falcon/worldgen/java_math.hpp"
#include "falcon/worldgen/overworld_generator.hpp"
#include "falcon/worldgen/random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>

namespace falcon::worldgen {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kSeaLevel = 63;
constexpr int kLavaLevel = -56;  // the carvers' lava_level: 8 above the bottom of the world
constexpr int kMaxY = 319;
constexpr int kTunnelRange = (4 * 2 - 1) * 16;  // CaveWorldCarver: the longest tunnel, 112 blocks

bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// WorldgenRandom.setLargeFeatureSeed.
void setLargeFeatureSeed(LegacyRandomSource& random, std::int64_t seed, int chunkX, int chunkZ) {
    random.setSeed(seed);
    const std::uint64_t a = static_cast<std::uint64_t>(random.nextLong());
    const std::uint64_t b = static_cast<std::uint64_t>(random.nextLong());
    random.setSeed(static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkX)) * a ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkZ)) * b ^
                                              static_cast<std::uint64_t>(seed)));
}

// UniformFloat.sample.
float uniformFloat(RandomSource& r, float min, float max) { return r.nextFloat() * (max - min) + min; }
// (a - b) * c with the draws in the order Java makes them.
float wobble(RandomSource& r, float scale) {
    const float a = r.nextFloat();
    const float b = r.nextFloat();
    const float c = r.nextFloat();
    return (a - b) * c * scale;
}

// A small generator for the ore veins: each vein has its own, so any chunk can work out any vein it needs without playing the others first.
struct VeinRandom {
    std::uint64_t s;
    explicit VeinRandom(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    int nextInt(int bound) { return static_cast<int>(next() % static_cast<std::uint64_t>(bound)); }
    float nextFloat() { return static_cast<float>(next() >> 40) * 5.9604645E-8f; }
    double nextDouble() { return static_cast<double>(next() >> 11) * 1.1102230246251565E-16; }
};
std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    a ^= b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2);
    a = (a ^ (a >> 30)) * 0xBF58476D1CE4E5B9ULL;
    return a ^ (a >> 27);
}

}  // namespace

struct Underground::Context {
    std::vector<Block>& blocks;
    int minX, minZ;                       // the chunk's corner
    std::array<std::uint8_t, 256> ocean;  // the column has water as its top block
    [[nodiscard]] static std::size_t index(int lx, int y, int lz) { return static_cast<std::size_t>(y - kMinY) + static_cast<std::size_t>(kHeight) * static_cast<std::size_t>(lx + 16 * lz); }
};

Underground::Underground(const BlockRegistry& registry, std::int64_t seed) : registry_(registry), seed_(seed) {
    const auto named = [&](const char* text) { return registry.parse(text).value_or(Block::Air); };
    b_.lava = named("lava");
    b_.water = named("water");
    b_.grass = named("grass_block");
    b_.mycelium = named("mycelium");
    b_.dirt = named("dirt");
    b_.stone = named("stone");
    b_.deepslate = named("deepslate");
    b_.granite = named("granite");
    b_.diorite = named("diorite");
    b_.andesite = named("andesite");
    b_.tuff = named("tuff");
    b_.dirtBlob = b_.dirt;
    b_.gravel = named("gravel");

    // The blocks the carvers cut (#minecraft:overworld_carver_replaceables): the stones, dirt, sand, terracotta, the ores, and a few more. By block (not state).
    carvable_.assign(registry.blocks().size(), 0);
    surfaceKind_.assign(registry.blocks().size(), 0);
    static const char* const kNames[] = {"stone", "granite",   "diorite",   "andesite",   "tuff",   "deepslate", "calcite",   "dirt",     "coarse_dirt", "podzol",    "grass_block", "mycelium", "rooted_dirt",
                                         "sand",  "red_sand",  "gravel",    "sandstone",  "red_sandstone", "terracotta", "clay", "snow_block", "powder_snow", "packed_ice", "snow",  "moss_block"};
    for (std::size_t i = 0; i < registry.blocks().size(); ++i) {
        const std::string& n = registry.blocks()[i].name;
        bool yes = endsWith(n, "_ore") || endsWith(n, "_terracotta");
        for (const char* k : kNames) yes = yes || n == k;
        carvable_[i] = yes ? 1 : 0;
        surfaceKind_[i] = n == "grass_block" ? 1 : n == "mycelium" ? 2 : 0;
    }

    using K = Ore::Kind;
    using W = Ore::Where;
    // name, attempts, rarity, size, minY, maxY, triangle, discard, kind, where   (the game's placed features of the same names; see underground.hpp)
    ores_ = {
        {"coal", 30, 0, 17, 136, 320, false, 0.0f, K::Ore, W::Everywhere},      {"coal", 20, 0, 17, 0, 192, true, 0.5f, K::Ore, W::Everywhere},
        {"iron", 90, 0, 9, 80, 384, true, 0.0f, K::Ore, W::Everywhere},         {"iron", 10, 0, 9, -24, 56, true, 0.0f, K::Ore, W::Everywhere},
        {"iron", 10, 0, 4, -64, 72, false, 0.0f, K::Ore, W::Everywhere},        {"copper", 16, 0, 10, -16, 112, true, 0.0f, K::Ore, W::Everywhere},
        {"gold", 4, 0, 9, -64, 32, true, 0.5f, K::Ore, W::Everywhere},          {"gold", 1, 2, 9, -64, -48, false, 0.5f, K::Ore, W::Everywhere},
        {"gold", 50, 0, 9, 32, 256, false, 0.0f, K::Ore, W::Badlands},          {"redstone", 4, 0, 8, -64, 15, false, 0.0f, K::Ore, W::Everywhere},
        {"redstone", 8, 0, 8, -96, -32, true, 0.0f, K::Ore, W::Everywhere},     {"lapis", 2, 0, 7, -32, 32, true, 0.0f, K::Ore, W::Everywhere},
        {"lapis", 4, 0, 7, -64, 64, false, 1.0f, K::Ore, W::Everywhere},        {"diamond", 7, 0, 4, -144, 16, true, 0.5f, K::Ore, W::Everywhere},
        {"diamond", 1, 9, 12, -144, 16, true, 0.7f, K::Ore, W::Everywhere},     {"diamond", 4, 0, 8, -144, 16, true, 1.0f, K::Ore, W::Everywhere},
        {"emerald", 100, 0, 3, -16, 480, true, 0.0f, K::Ore, W::Windswept},     {"dirt", 7, 0, 33, 0, 160, false, 0.0f, K::Dirt, W::Everywhere},
        {"gravel", 14, 0, 33, -64, 320, false, 0.0f, K::Gravel, W::Everywhere}, {"granite", 1, 6, 64, 64, 128, false, 0.0f, K::Granite, W::Everywhere},
        {"granite", 2, 0, 64, 0, 60, false, 0.0f, K::Granite, W::Everywhere},   {"diorite", 1, 6, 64, 64, 128, false, 0.0f, K::Diorite, W::Everywhere},
        {"diorite", 2, 0, 64, 0, 60, false, 0.0f, K::Diorite, W::Everywhere},   {"andesite", 1, 6, 64, 64, 128, false, 0.0f, K::Andesite, W::Everywhere},
        {"andesite", 2, 0, 64, 0, 60, false, 0.0f, K::Andesite, W::Everywhere}, {"tuff", 2, 0, 64, -64, 0, false, 0.0f, K::Tuff, W::Everywhere},
    };
    oreBlocks_.resize(ores_.size());
    for (std::size_t i = 0; i < ores_.size(); ++i) {
        if (ores_[i].kind != Ore::Kind::Ore) continue;
        const std::string stem = ores_[i].name;
        oreBlocks_[i].stone = named((stem + "_ore").c_str());
        oreBlocks_[i].deepslate = named(("deepslate_" + stem + "_ore").c_str());
        if (registry.isAir(oreBlocks_[i].deepslate)) oreBlocks_[i].deepslate = oreBlocks_[i].stone;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Carvers

void Underground::carve(Context& c, int sourceX, int sourceZ) const {
    LegacyRandomSource random(0);
    const int midX = c.minX + 8, midZ = c.minZ + 8;

    // WorldCarver.carveEllipsoid, with the game's carveBlock for each cell it keeps.
    const auto ellipsoid = [&](double x, double y, double z, double hr, double vr, const auto& skip) {
        if (std::abs(x - midX) > 16.0 + hr * 2.0 || std::abs(z - midZ) > 16.0 + hr * 2.0) return;
        const int x0 = std::max(javaFloor(x - hr) - c.minX - 1, 0), x1 = std::min(javaFloor(x + hr) - c.minX, 15);
        const int y0 = std::max(javaFloor(y - vr) - 1, kMinY), y1 = std::min(javaFloor(y + vr) + 1, kMaxY);
        const int z0 = std::max(javaFloor(z - hr) - c.minZ - 1, 0), z1 = std::min(javaFloor(z + hr) - c.minZ, 15);
        for (int lx = x0; lx <= x1; ++lx) {
            const double dx = (c.minX + lx + 0.5 - x) / hr;
            for (int lz = z0; lz <= z1; ++lz) {
                const double dz = (c.minZ + lz + 0.5 - z) / hr;
                if (dx * dx + dz * dz >= 1.0) continue;
                bool surfaceReached = false;
                Block top = Block::Air;
                for (int wy = y1; wy > y0; --wy) {
                    const double dy = (wy - 0.5 - y) / vr;
                    if (skip(dx, dy, dz, wy)) continue;
                    Block& cell = c.blocks[Context::index(lx, wy, lz)];
                    const std::size_t block = registry_.state(cell).block;
                    if (surfaceKind_[block] != 0) {  // the top of the world is being cut: the soil below must show as the top again
                        surfaceReached = true;
                        top = surfaceKind_[block] == 1 ? b_.grass : b_.mycelium;
                    }
                    if (carvable_[block] == 0) continue;
                    cell = wy <= kLavaLevel ? b_.lava : (wy < kSeaLevel && c.ocean[static_cast<std::size_t>(lx + 16 * lz)] != 0 ? b_.water : Block::Air);
                    if (surfaceReached && wy - 1 >= kMinY) {
                        Block& below = c.blocks[Context::index(lx, wy - 1, lz)];
                        if (below == b_.dirt) below = top;
                    }
                }
            }
        }
    };
    // WorldCarver.canReach.
    const auto canReach = [&](double x, double z, int step, int distance, float thickness) {
        const double dx = midX - x, dz = midZ - z;
        const double left = distance - step;
        const double reach = thickness + 2.0f + 16.0f;
        return dx * dx + dz * dz - left * left <= reach * reach;
    };

    // CaveWorldCarver.createTunnel: a random walk that carves an ellipsoid at most steps, and may split in two once.
    std::function<void(std::int64_t, double, double, double, double, double, float, float, float, int, int, double, double)> tunnel;
    tunnel = [&](std::int64_t tunnelSeed, double x, double y, double z, double hMul, double vMul, float thickness, float yaw, float pitch, int step, int distance, double yScale,
                 double floorLevel) {
        LegacyRandomSource r(tunnelSeed);
        const int splitAt = r.nextInt(distance / 2) + distance / 4;
        const bool steep = r.nextInt(6) == 0;
        float f = 0.0f, g = 0.0f;
        for (int j = step; j < distance; ++j) {
            const double d = 1.5 + static_cast<double>(std::sin(static_cast<float>(j) * kPi / static_cast<float>(distance)) * thickness);
            const double e = d * yScale;
            const float h = std::cos(pitch);
            x += static_cast<double>(std::cos(yaw) * h);
            y += static_cast<double>(std::sin(pitch));
            z += static_cast<double>(std::sin(yaw) * h);
            pitch *= steep ? 0.92f : 0.7f;
            pitch += g * 0.1f;
            yaw += f * 0.1f;
            g *= 0.9f;
            f *= 0.75f;
            g += wobble(r, 2.0f);
            f += wobble(r, 4.0f);
            if (j == splitAt && thickness > 1.0f) {
                const std::int64_t seedA = r.nextLong();
                const float thicknessA = r.nextFloat() * 0.5f + 0.5f;
                tunnel(seedA, x, y, z, hMul, vMul, thicknessA, yaw - kPi / 2.0f, pitch / 3.0f, j, distance, 1.0, floorLevel);
                const std::int64_t seedB = r.nextLong();
                const float thicknessB = r.nextFloat() * 0.5f + 0.5f;
                tunnel(seedB, x, y, z, hMul, vMul, thicknessB, yaw + kPi / 2.0f, pitch / 3.0f, j, distance, 1.0, floorLevel);
                return;
            }
            if (r.nextInt(4) == 0) continue;
            if (!canReach(x, z, j, distance, thickness)) return;
            ellipsoid(x, y, z, d * hMul, e * vMul, [floorLevel](double dx, double dy, double dz, int) { return dy <= floorLevel || dx * dx + dy * dy + dz * dz >= 1.0; });
        }
    };

    struct CaveSettings {
        float probability;
        int maxY;
    };
    // cave.json and cave_extra_underground.json, then canyon.json (below).
    static constexpr CaveSettings kCaves[2] = {{0.15f, 180}, {0.07f, 47}};
    for (int index = 0; index < 2; ++index) {
        setLargeFeatureSeed(random, seed_ + index, sourceX, sourceZ);
        if (!(random.nextFloat() <= kCaves[index].probability)) continue;
        // count: very_biased_to_bottom 0..14 = nextInt(nextInt(nextInt(15) + 1) + 1)
        const int count = random.nextInt(random.nextInt(random.nextInt(15) + 1) + 1);
        for (int k = 0; k < count; ++k) {
            const double x = sourceX * 16 + random.nextInt(16);
            const double y = static_cast<double>(kMinY + 8 + random.nextInt(kCaves[index].maxY - (kMinY + 8) + 1));
            const double z = sourceZ * 16 + random.nextInt(16);
            const double hMul = uniformFloat(random, 0.7f, 1.4f);
            const double vMul = uniformFloat(random, 0.8f, 1.3f);
            const double floorLevel = uniformFloat(random, -1.0f, -0.4f);
            int tunnels = 1;
            if (random.nextInt(4) == 0) {  // a round room first, then a few more tunnels
                const double roomVertical = uniformFloat(random, 0.1f, 0.9f);
                const float roomRadius = 1.0f + random.nextFloat() * 6.0f;
                const double d = 1.5 + static_cast<double>(std::sin(1.5707963705062866f) * roomRadius);
                ellipsoid(x + 1.0, y, z, d, d * roomVertical, [floorLevel](double dx, double dy, double dz, int) { return dy <= floorLevel || dx * dx + dy * dy + dz * dz >= 1.0; });
                tunnels += random.nextInt(4);
            }
            for (int p = 0; p < tunnels; ++p) {
                const float yaw = random.nextFloat() * (kPi * 2.0f);
                const float pitch = (random.nextFloat() - 0.5f) / 4.0f;
                // getThickness: trapezoid(0, 3, plateau 1), and one tunnel in ten is much fatter (weird_thickness_bias)
                const float t1 = random.nextFloat() * 2.0f;
                float thickness = t1 + random.nextFloat();
                if (random.nextInt(10) == 0) {
                    const float a = random.nextFloat();
                    const float b = random.nextFloat();
                    thickness *= a * b * 3.0f + 1.0f;
                }
                const int distance = kTunnelRange - random.nextInt(kTunnelRange / 4);
                const std::int64_t tunnelSeed = random.nextLong();
                tunnel(tunnelSeed, x, y, z, hMul, vMul, thickness, yaw, pitch, 0, distance, 1.0, floorLevel);
            }
        }
    }

    // CanyonWorldCarver (canyon.json): one long, tall, narrow cut with its walls of uneven width, rarely.
    setLargeFeatureSeed(random, seed_ + 2, sourceX, sourceZ);
    if (!(random.nextFloat() <= 0.01f)) return;
    const double x = sourceX * 16 + random.nextInt(16);
    const double y = static_cast<double>(10 + random.nextInt(67 - 10 + 1));
    const double z = sourceZ * 16 + random.nextInt(16);
    const float yaw = random.nextFloat() * (kPi * 2.0f);
    const float pitch0 = uniformFloat(random, -0.125f, 0.125f);
    const float t1 = random.nextFloat() * 4.0f;  // thickness: trapezoid(0, 6, plateau 2)
    const float thickness = t1 + random.nextFloat() * 2.0f;
    const int distance = static_cast<int>(static_cast<float>(kTunnelRange) * uniformFloat(random, 0.75f, 1.0f));
    {
        const std::int64_t canyonSeed = random.nextLong();
        LegacyRandomSource r(canyonSeed);
        // initWidthFactors: the squared width of each layer, changing every third layer or so
        std::array<float, kHeight> widthFactors{};
        float factor = 1.0f;
        for (int i = 0; i < kHeight; ++i) {
            if (i == 0 || r.nextInt(3) == 0) {
                const float a = r.nextFloat();
                factor = 1.0f + a * r.nextFloat();
            }
            widthFactors[static_cast<std::size_t>(i)] = factor * factor;
        }
        float f = 0.0f, g = 0.0f, pitch = pitch0, yawNow = yaw;
        double px = x, py = y, pz = z;
        for (int step = 0; step < distance; ++step) {
            double hr = 1.5 + static_cast<double>(std::sin(static_cast<float>(step) * kPi / static_cast<float>(distance)) * thickness);
            double vr = hr * 3.0;  // y_scale 3
            hr *= static_cast<double>(uniformFloat(r, 0.75f, 1.0f));
            // updateVerticalRadius: widest in the middle of the canyon
            const float mid = 1.0f - std::abs(0.5f - static_cast<float>(step) / static_cast<float>(distance)) * 2.0f;
            vr = static_cast<double>(1.0f + 0.0f * mid) * vr * static_cast<double>(uniformFloat(r, 0.75f, 1.0f));
            const float h = std::cos(pitch);
            const float p = std::sin(pitch);
            px += static_cast<double>(std::cos(yawNow) * h);
            py += static_cast<double>(p);
            pz += static_cast<double>(std::sin(yawNow) * h);
            pitch *= 0.7f;
            pitch += g * 0.05f;
            yawNow += f * 0.05f;
            g *= 0.8f;
            f *= 0.5f;
            g += wobble(r, 2.0f);
            f += wobble(r, 4.0f);
            if (r.nextInt(4) == 0) continue;
            if (!canReach(px, pz, step, distance, thickness)) return;
            ellipsoid(px, py, pz, hr, vr, [&widthFactors](double dx, double dy, double dz, int wy) {
                return (dx * dx + dz * dz) * static_cast<double>(widthFactors[static_cast<std::size_t>(wy - kMinY - 1)]) + dy * dy / 6.0 >= 1.0;
            });
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Ores and blobs

void Underground::placeOres(Context& c, int sourceX, int sourceZ, const OverworldGenerator& generator) const {
    const std::string_view biome = generator.biomeNameAt(sourceX * 16 + 8, 64, sourceZ * 16 + 8);
    const bool badlands = biome.find("badlands") != std::string_view::npos;
    const bool windswept = biome.find("windswept") != std::string_view::npos;
    for (std::size_t o = 0; o < ores_.size(); ++o) {
        const Ore& ore = ores_[o];
        if ((ore.where == Ore::Where::Badlands && !badlands) || (ore.where == Ore::Where::Windswept && !windswept)) continue;
        VeinRandom chance(mix(mix(static_cast<std::uint64_t>(seed_), static_cast<std::uint64_t>(static_cast<std::int64_t>(sourceX)) * 0x9E3779B1ULL ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(sourceZ)) * 0x85EBCA6BULL), o + 1));
        const int attempts = ore.rarity > 0 ? (chance.nextInt(ore.rarity) == 0 ? ore.count : 0) : ore.count;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            VeinRandom r(mix(chance.next(), static_cast<std::uint64_t>(attempt)));
            const int ox = sourceX * 16 + r.nextInt(16);
            const int oz = sourceZ * 16 + r.nextInt(16);
            int oy;
            if (ore.triangle) {
                const int span = ore.maxY - ore.minY;
                const int j = span / 2;
                oy = ore.minY + r.nextInt(span - j + 1) + r.nextInt(j + 1);
            } else {
                oy = ore.minY + r.nextInt(ore.maxY - ore.minY + 1);
            }
            // OreFeature.doPlace: a line through the origin, with a blob at every step along it.
            const float angle = r.nextFloat() * kPi;
            const float f = static_cast<float>(ore.size) / 8.0f;
            const double x1 = ox + std::sin(angle) * f, x2 = ox - std::sin(angle) * f;
            const double z1 = oz + std::cos(angle) * f, z2 = oz - std::cos(angle) * f;
            const double y1 = oy + r.nextInt(3) - 2, y2 = oy + r.nextInt(3) - 2;
            for (int i = 0; i < ore.size; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(ore.size);
                const double bx = x1 + (x2 - x1) * t, by = y1 + (y2 - y1) * t, bz = z1 + (z2 - z1) * t;
                const double radius = ((static_cast<double>(std::sin(kPi * t)) + 1.0) * (r.nextDouble() * ore.size / 16.0) + 1.0) / 2.0;
                const int lx0 = std::max(javaFloor(bx - radius) - c.minX, 0), lx1 = std::min(javaFloor(bx + radius) - c.minX, 15);
                const int lz0 = std::max(javaFloor(bz - radius) - c.minZ, 0), lz1 = std::min(javaFloor(bz + radius) - c.minZ, 15);
                const int y0 = std::max(javaFloor(by - radius), kMinY), y1c = std::min(javaFloor(by + radius), kMaxY);
                for (int lx = lx0; lx <= lx1; ++lx) {
                    const double dx = (c.minX + lx + 0.5 - bx) / radius;
                    for (int lz = lz0; lz <= lz1; ++lz) {
                        const double dz = (c.minZ + lz + 0.5 - bz) / radius;
                        for (int wy = y0; wy <= y1c; ++wy) {
                            const double dy = (wy + 0.5 - by) / radius;
                            if (dx * dx + dy * dy + dz * dz >= 1.0) continue;
                            Block& cell = c.blocks[Context::index(lx, wy, lz)];
                            Block put = Block::Air;
                            const bool inStone = cell == b_.stone || cell == b_.granite || cell == b_.diorite || cell == b_.andesite;
                            const bool inDeepslate = cell == b_.deepslate;
                            switch (ore.kind) {
                                case Ore::Kind::Ore: put = inStone ? oreBlocks_[o].stone : inDeepslate ? oreBlocks_[o].deepslate : Block::Air; break;
                                case Ore::Kind::Granite: put = inStone ? b_.granite : Block::Air; break;
                                case Ore::Kind::Diorite: put = inStone ? b_.diorite : Block::Air; break;
                                case Ore::Kind::Andesite: put = inStone ? b_.andesite : Block::Air; break;
                                case Ore::Kind::Tuff: put = (inStone || inDeepslate) ? b_.tuff : Block::Air; break;
                                case Ore::Kind::Dirt: put = (inStone || inDeepslate || cell == b_.tuff) ? b_.dirtBlob : Block::Air; break;
                                case Ore::Kind::Gravel: put = (inStone || inDeepslate || cell == b_.tuff) ? b_.gravel : Block::Air; break;
                            }
                            if (registry_.isAir(put)) continue;
                            if (ore.discard > 0.0f && r.nextFloat() < ore.discard) {  // next to air: left out (OreFeature.canPlaceOre)
                                bool touchesAir = false;
                                static constexpr int kNeighbours[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                                for (const auto& n : kNeighbours) {
                                    const int nx = lx + n[0], ny = wy + n[1], nz = lz + n[2];
                                    if (nx < 0 || nx > 15 || nz < 0 || nz > 15 || ny < kMinY || ny > kMaxY) continue;
                                    if (registry_.isAir(c.blocks[Context::index(nx, ny, nz)])) touchesAir = true;
                                }
                                if (touchesAir) continue;
                            }
                            cell = put;
                        }
                    }
                }
            }
        }
    }
}

void Underground::apply(std::vector<Block>& blocks, int chunkX, int chunkZ, const OverworldGenerator& generator) const {
    Context c{blocks, chunkX * 16, chunkZ * 16, {}};
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            for (int y = kMaxY; y >= kMinY; --y) {
                const Block b = blocks[Context::index(lx, y, lz)];
                if (registry_.isAir(b)) continue;
                c.ocean[static_cast<std::size_t>(lx + 16 * lz)] = registry_.state(b).has(kStateWater) ? 1 : 0;
                break;
            }
        }
    }
    for (int dz = -8; dz <= 8; ++dz) {  // the carvers of every chunk within eight, as the game replays them
        for (int dx = -8; dx <= 8; ++dx) carve(c, chunkX + dx, chunkZ + dz);
    }
    for (int dz = -1; dz <= 1; ++dz) {  // the veins of the chunks beside this one reach into it
        for (int dx = -1; dx <= 1; ++dx) placeOres(c, chunkX + dx, chunkZ + dz, generator);
    }
}

}  // namespace falcon::worldgen
