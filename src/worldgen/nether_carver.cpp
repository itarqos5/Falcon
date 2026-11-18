#include "falcon/worldgen/nether_carver.hpp"

#include "falcon/worldgen/java_math.hpp"
#include "falcon/worldgen/random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace falcon::worldgen {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kMaxY = NetherCarver::kHeight - 1;
constexpr int kTunnelRange = (4 * 2 - 1) * 16;  // CaveWorldCarver: the longest tunnel, 112 blocks
constexpr float kProbability = 0.2f;            // nether_cave.json
constexpr double kFloorLevel = -0.7;            // floor_level: below this part of its height a cave is not cut
constexpr double kStartVerticalRadius = 5.0;    // start_vertical_radius_multiplier: the first tunnel of a cave is five times as high as it is wide
constexpr double kRoomVerticalRadius = 0.5;     // room_vertical_radius_multiplier

// WorldgenRandom.setLargeFeatureSeed.
void setLargeFeatureSeed(LegacyRandomSource& random, std::int64_t seed, int chunkX, int chunkZ) {
    random.setSeed(seed);
    const std::uint64_t a = static_cast<std::uint64_t>(random.nextLong());
    const std::uint64_t b = static_cast<std::uint64_t>(random.nextLong());
    random.setSeed(static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkX)) * a ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkZ)) * b ^
                                              static_cast<std::uint64_t>(seed)));
}

// (a - b) * c * scale, the draws in the order Java makes them.
float wobble(RandomSource& r, float scale) {
    const float a = r.nextFloat();
    const float b = r.nextFloat();
    const float c = r.nextFloat();
    return (a - b) * c * scale;
}

}  // namespace

struct NetherCarver::Context {
    std::vector<Block>& blocks;
    int minX, minZ;  // the chunk's corner
    [[nodiscard]] static std::size_t index(int lx, int y, int lz) { return static_cast<std::size_t>(y) + static_cast<std::size_t>(NetherCarver::kHeight) * static_cast<std::size_t>(lx + 16 * lz); }
};

NetherCarver::NetherCarver(const BlockRegistry& registry, std::int64_t seed) : registry_(registry), seed_(seed) {
    lava_ = registry.parse("lava").value_or(Block::Air);
    // What the Nether's carver may cut (nether_carver_replaceables): the netherrack and the stones laid on it, the soil of its forests and valleys. By block (not state).
    carvable_.assign(registry.blocks().size(), 0);
    static const char* const kNames[] = {"netherrack", "basalt", "blackstone", "soul_sand", "soul_soil", "crimson_nylium", "warped_nylium", "nether_wart_block", "warped_wart_block",
                                         "stone", "granite", "diorite", "andesite", "dirt", "coarse_dirt", "gravel"};
    for (std::size_t i = 0; i < registry.blocks().size(); ++i) {
        const std::string& n = registry.blocks()[i].name;
        bool yes = false;
        for (const char* k : kNames) yes = yes || n == k;
        carvable_[i] = yes ? 1 : 0;
    }
}

// WorldCarver.carveEllipsoid, with the game's cave skip (the lower part of the ellipsoid, under floor_level, is left) and the Nether's carveBlock for each cell it keeps.
void NetherCarver::carveEllipsoid(Context& c, double x, double y, double z, double hr, double vr) const {
    const int midX = c.minX + 8, midZ = c.minZ + 8;
    if (std::abs(x - midX) > 16.0 + hr * 2.0 || std::abs(z - midZ) > 16.0 + hr * 2.0) return;
    const int x0 = std::max(javaFloor(x - hr) - c.minX - 1, 0), x1 = std::min(javaFloor(x + hr) - c.minX, 15);
    const int y0 = std::max(javaFloor(y - vr) - 1, 0), y1 = std::min(javaFloor(y + vr) + 1, kMaxY);
    const int z0 = std::max(javaFloor(z - hr) - c.minZ - 1, 0), z1 = std::min(javaFloor(z + hr) - c.minZ, 15);
    for (int lx = x0; lx <= x1; ++lx) {
        const double dx = (c.minX + lx + 0.5 - x) / hr;
        for (int lz = z0; lz <= z1; ++lz) {
            const double dz = (c.minZ + lz + 0.5 - z) / hr;
            if (dx * dx + dz * dz >= 1.0) continue;
            for (int wy = y1; wy > y0; --wy) {
                const double dy = (wy - 0.5 - y) / vr;
                if (dy <= kFloorLevel || dx * dx + dy * dy + dz * dz >= 1.0) continue;  // (shouldSkip)
                Block& cell = c.blocks[Context::index(lx, wy, lz)];
                if (carvable_[registry_.state(cell).block] == 0) continue;
                cell = wy < kSeaLevel ? lava_ : Block::Air;  // (the lava sea fills what is cut below it)
            }
        }
    }
}

// CaveWorldCarver.createTunnel: a random walk that carves an ellipsoid at most steps (a quarter of the steps are skipped), and may split in two once into round branches.
void NetherCarver::tunnel(Context& c, std::int64_t tunnelSeed, double x, double y, double z, float thickness, float yaw, float pitch, int step, int distance, double yScale) const {
    LegacyRandomSource r(tunnelSeed);
    const int splitAt = r.nextInt(distance / 2) + distance / 4;
    const bool steep = r.nextInt(6) == 0;
    float f = 0.0f, g = 0.0f;
    const int midX = c.minX + 8, midZ = c.minZ + 8;
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
            // The branches have generators of their own: both are drawn first, then walked (round: the y scale of a branch is 1).
            const std::int64_t seedA = r.nextLong();
            const float thicknessA = r.nextFloat() * 0.5f + 0.5f;
            const std::int64_t seedB = r.nextLong();
            const float thicknessB = r.nextFloat() * 0.5f + 0.5f;
            tunnel(c, seedA, x, y, z, thicknessA, yaw - kPi / 2.0f, pitch / 3.0f, j, distance, 1.0);
            tunnel(c, seedB, x, y, z, thicknessB, yaw + kPi / 2.0f, pitch / 3.0f, j, distance, 1.0);
            return;
        }
        if (r.nextInt(4) == 0) continue;
        // WorldCarver.canReach: a tunnel that can no longer come near this chunk ends here.
        const double dx = midX - x, dz = midZ - z;
        const double left = distance - j;
        const double reach = thickness + 2.0f + 16.0f;
        if (dx * dx + dz * dz - left * left > reach * reach) return;
        carveEllipsoid(c, x, y, z, d, e);  // (horizontal_radius_multiplier and vertical_radius_multiplier are 1)
    }
}

void NetherCarver::apply(std::vector<Block>& blocks, int chunkX, int chunkZ) const {
    Context c{blocks, chunkX * 16, chunkZ * 16};
    LegacyRandomSource random(0);
    for (int dz = -8; dz <= 8; ++dz) {  // the carvers of every chunk within eight, as the game replays them
        for (int dx = -8; dx <= 8; ++dx) {
            const int sourceX = chunkX + dx, sourceZ = chunkZ + dz;
            setLargeFeatureSeed(random, seed_, sourceX, sourceZ);  // (the biome's first carver: index 0)
            if (!(random.nextFloat() <= kProbability)) continue;
            // count: very_biased_to_bottom 0..9 = nextInt(nextInt(nextInt(10) + 1) + 1)
            const int count = random.nextInt(random.nextInt(random.nextInt(10) + 1) + 1);
            for (int k = 0; k < count; ++k) {
                const double x = sourceX * 16 + random.nextInt(16);
                const double y = static_cast<double>(random.nextInt(126 - 0 + 1));  // y: uniform from 0 to 1 below the top
                const double z = sourceZ * 16 + random.nextInt(16);
                int tunnels = 1;
                if (random.nextInt(4) == 0) {  // a round room first, then a few more tunnels
                    const float roomRadius = 1.0f + random.nextFloat() * 6.0f;
                    const double d = 1.5 + static_cast<double>(std::sin(1.5707963705062866f) * roomRadius);
                    carveEllipsoid(c, x + 1.0, y, z, d, d * kRoomVerticalRadius);
                    tunnels += random.nextInt(4);
                }
                for (int p = 0; p < tunnels; ++p) {
                    const float yaw = random.nextFloat() * (kPi * 2.0f);
                    const float pitch = (random.nextFloat() - 0.5f) / 4.0f;
                    // thickness: trapezoid(0, 6, plateau 2) = a roll of 4 and a roll of 2 (no weird bias in the Nether)
                    const float t1 = random.nextFloat() * 4.0f;
                    const float thickness = t1 + random.nextFloat() * 2.0f;
                    const int distance = kTunnelRange - random.nextInt(kTunnelRange / 4);
                    const std::int64_t tunnelSeed = random.nextLong();
                    tunnel(c, tunnelSeed, x, y, z, thickness, yaw, pitch, 0, distance, kStartVerticalRadius);
                }
            }
        }
    }
}

}  // namespace falcon::worldgen
