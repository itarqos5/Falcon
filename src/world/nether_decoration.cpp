#include "falcon/world/nether_decoration.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace falcon {
namespace {

constexpr int kNetherTop = 128;  // the Nether's blocks are y 0 to 127

std::uint64_t mix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// A small deterministic generator for one chunk.
struct Rng {
    std::uint64_t state;
    std::uint64_t next() { return mix64(state += 0x9E3779B97F4A7C15ULL); }
    int below(int bound) { return bound <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(bound)); }
    int between(int lo, int hiInclusive) { return lo + below(hiInclusive - lo + 1); }
    float unit() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
};

// The blocks y 0 to 127 of a chunk, to be edited freely and written back.
struct Canvas {
    Chunk& chunk;
    Block outside;           // what a cell outside the chunk reads as (a wall)
    std::vector<Block> data;  // (y * 16 + z) * 16 + x
    Canvas(Chunk& c, Block wall) : chunk(c), outside(wall), data(static_cast<std::size_t>(16 * 16 * kNetherTop), Block::Air) {
        std::vector<Block> section(kSectionVolume);
        for (int s = 0; s < kNetherTop / 16; ++s) {
            const SectionPtr& sec = chunk.sections[static_cast<std::size_t>(s + (0 - kMinY) / 16)];
            if (!sec) continue;
            sec->decode(section.data());
            for (int ly = 0; ly < 16; ++ly) {
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) data[index(x, s * 16 + ly, z)] = section[static_cast<std::size_t>((ly << 8) | (z << 4) | x)];
                }
            }
        }
    }
    static std::size_t index(int x, int y, int z) { return (static_cast<std::size_t>(y) * 16 + static_cast<std::size_t>(z)) * 16 + static_cast<std::size_t>(x); }
    [[nodiscard]] static bool inside(int x, int y, int z) { return x >= 0 && x < 16 && z >= 0 && z < 16 && y >= 0 && y < kNetherTop; }
    [[nodiscard]] Block get(int x, int y, int z) const { return inside(x, y, z) ? data[index(x, y, z)] : outside; }
    void set(int x, int y, int z, Block b) {
        if (inside(x, y, z)) data[index(x, y, z)] = b;
    }
    void write() {
        SectionBuilder builder;
        auto& raw = builder.raw();
        for (int s = 0; s < kNetherTop / 16; ++s) {
            for (int ly = 0; ly < 16; ++ly) {
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) raw[static_cast<std::size_t>((ly << 8) | (z << 4) | x)] = data[index(x, s * 16 + ly, z)];
                }
            }
            chunk.sections[static_cast<std::size_t>(s + (0 - kMinY) / 16)] = builder.build();
        }
    }
};

}  // namespace

std::array<EndSpike, 10> endSpikes(std::int64_t seed) {
    // EndSpikeFeature.SpikeCacheLoader: ten pillars on a circle of 42 blocks, taller and wider in turn, in an order that the seed shuffles.
    std::array<int, 10> order{};
    std::iota(order.begin(), order.end(), 0);
    Rng rng{static_cast<std::uint64_t>(seed) ^ 0xE4D5F0A1ULL};
    for (int i = 9; i > 0; --i) std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(rng.below(i + 1))]);
    std::array<EndSpike, 10> spikes{};
    for (int i = 0; i < 10; ++i) {
        const double angle = 2.0 * (-3.14159265358979323846 + 3.14159265358979323846 / 10.0 * i);
        EndSpike& s = spikes[static_cast<std::size_t>(i)];
        const int k = order[static_cast<std::size_t>(i)];
        s.centerX = static_cast<int>(std::floor(42.0 * std::cos(angle)));
        s.centerZ = static_cast<int>(std::floor(42.0 * std::sin(angle)));
        s.radius = 2 + k / 3;
        s.height = 76 + k * 3;
        s.guarded = k == 1 || k == 2;
    }
    return spikes;
}

NetherDecorator::NetherDecorator(const worldgen::OverworldGenerator& generator, const BlockRegistry& registry, std::int64_t seed, int dimension)
    : generator_(generator), registry_(registry), seed_(seed), dimension_(dimension) {
    const auto find = [&](const char* name) {
        const auto b = registry.find(name);
        return b ? *b : Block::Air;
    };
    b_.air = Block::Air;
    b_.netherrack = find("netherrack");
    b_.glowstone = find("glowstone");
    b_.quartzOre = find("nether_quartz_ore");
    b_.goldOre = find("nether_gold_ore");
    b_.magma = find("magma_block");
    b_.fire = find("fire");
    b_.soulFire = find("soul_fire");
    b_.brownMushroom = find("brown_mushroom");
    b_.redMushroom = find("red_mushroom");
    b_.crimsonRoots = find("crimson_roots");
    b_.warpedRoots = find("warped_roots");
    b_.crimsonFungus = find("crimson_fungus");
    b_.warpedFungus = find("warped_fungus");
    b_.sprouts = find("nether_sprouts");
    b_.crimsonStem = find("crimson_stem");
    b_.warpedStem = find("warped_stem");
    b_.wartBlock = find("nether_wart_block");
    b_.warpedWartBlock = find("warped_wart_block");
    b_.shroomlight = find("shroomlight");
    b_.weepingVines = find("weeping_vines");
    b_.twistingVines = find("twisting_vines");
    b_.basalt = find("basalt");
    b_.crimsonNylium = find("crimson_nylium");
    b_.warpedNylium = find("warped_nylium");
    b_.soulSand = find("soul_sand");
    b_.soulSoil = find("soul_soil");
    b_.lava = find("lava");
    b_.obsidian = find("obsidian");
    b_.bedrock = find("bedrock");
    b_.ironBars = find("iron_bars");
    b_.endStone = find("end_stone");
    spikes_ = endSpikes(seed);
}

void NetherDecorator::decorate(Chunk& chunk, int chunkX, int chunkZ, const worldgen::ChunkExtras& extras) const {
    if (dimension_ == 2) {
        (void)extras;
        // ---- the End: the pillars that reach into this chunk ----
        const int x0 = chunkX * 16, z0 = chunkZ * 16;
        bool any = false;
        for (const EndSpike& s : spikes_) {
            if (s.centerX + s.radius + 3 >= x0 && s.centerX - s.radius - 3 <= x0 + 15 && s.centerZ + s.radius + 3 >= z0 && s.centerZ - s.radius - 3 <= z0 + 15) any = true;
        }
        if (!any) return;
        for (int sy = 0; sy < kSectionCount; ++sy) {
            const int yBase = kMinY + sy * 16;
            if (yBase < 0 || yBase >= 128 + 16 * 4) continue;  // (only up to the tops of the pillars)
            SectionBuilder builder;
            builder.load(chunk.sections[static_cast<std::size_t>(sy)]);
            bool changed = false;
            for (const EndSpike& s : spikes_) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int lx = 0; lx < 16; ++lx) {
                        const int dx = x0 + lx - s.centerX, dz = z0 + lz - s.centerZ, d2 = dx * dx + dz * dz;
                        for (int ly = 0; ly < 16; ++ly) {
                            const int y = yBase + ly;
                            Block put = Block::Air;
                            bool place = false;
                            if (d2 <= s.radius * s.radius + 1 && y <= s.height) {
                                place = true;
                                put = y == s.height && dx == 0 && dz == 0 ? b_.bedrock : b_.obsidian;
                            } else if (d2 <= s.radius * s.radius + 1 && y == s.height + 1 && dx == 0 && dz == 0) {
                                place = true;
                                put = b_.fire;
                            } else if (s.guarded && y >= s.height && y <= s.height + 3) {  // the cage: bars on a ring, and a roof
                                const int outer = (s.radius + 2) * (s.radius + 2);
                                const bool ring = d2 <= outer && d2 > (s.radius + 1) * (s.radius + 1);
                                const bool roof = y == s.height + 3 && d2 <= outer;
                                if (ring || roof) {
                                    place = true;
                                    put = b_.ironBars;
                                }
                            }
                            if (place && put != Block::Air) {
                                builder.set(lx, ly, lz, put);
                                changed = true;
                            }
                        }
                    }
                }
            }
            if (changed) chunk.sections[static_cast<std::size_t>(sy)] = builder.build();
        }
        return;
    }

    // ---- the Nether ----
    Canvas c(chunk, b_.bedrock);
    Rng r{mix64(static_cast<std::uint64_t>(seed_) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32 | static_cast<std::uint32_t>(chunkZ)))};
    const auto isAir = [&](Block b) { return b == Block::Air; };
    const auto biomeAt = [&](int x, int y, int z) -> const std::string& {
        return generator_.biomes().name(extras.biomes.at(x >> 2, y >> 2, z >> 2));
    };
    // A blob of `ore` of about `size` blocks in the rock round (x, y, z).
    const auto vein = [&](Block ore, int x, int y, int z, int size) {
        for (int i = 0; i < size; ++i) {
            const int px = x + r.between(-2, 2), py = y + r.between(-1, 1), pz = z + r.between(-2, 2);
            if (c.get(px, py, pz) == b_.netherrack) c.set(px, py, pz, ore);
        }
    };
    // The floor of an open place: from a random height, down to the first block that is not air. `floorY` is the block under the air.
    const auto findFloor = [&](int x, int z, int fromY, int& floorY) {
        int y = fromY;
        while (y > 1 && isAir(c.get(x, y, z))) --y;
        if (y <= 1 || y >= kNetherTop - 2 || y < 32) return false;
        floorY = y;
        return isAir(c.get(x, y + 1, z)) && isAir(c.get(x, y + 2, z));
    };

    // Ore: quartz and gold in the netherrack, magma at the shore of the lava sea.
    for (int i = 0; i < 16; ++i) vein(b_.quartzOre, r.between(1, 14), r.between(10, 117), r.between(1, 14), 14);
    for (int i = 0; i < 10; ++i) vein(b_.goldOre, r.between(1, 14), r.between(10, 117), r.between(1, 14), 10);
    for (int i = 0; i < 4; ++i) vein(b_.magma, r.between(1, 14), r.between(27, 36), r.between(1, 14), 24);

    // Glowstone: blobs that hang from the ceiling.
    for (int i = 0; i < 10; ++i) {
        const int x = r.between(2, 13), z = r.between(2, 13), y = r.between(25, 118);
        if (!(isAir(c.get(x, y, z)) && c.get(x, y + 1, z) == b_.netherrack)) continue;
        c.set(x, y, z, b_.glowstone);
        const int n = r.between(12, 36);
        for (int k = 0; k < n; ++k) {
            const int px = x + r.between(-3, 3), py = y - r.between(0, 4), pz = z + r.between(-3, 3);
            if (!isAir(c.get(px, py, pz))) continue;
            bool next = false;
            for (const auto& d : {std::array<int, 3>{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}}) next = next || c.get(px + d[0], py + d[1], pz + d[2]) == b_.glowstone;
            if (next) c.set(px, py, pz, b_.glowstone);
        }
    }

    // The floor: fire, mushrooms, roots and fungi, vines, basalt columns, and the huge fungi of the forests.
    for (int i = 0; i < 40; ++i) {
        const int x = r.between(2, 13), z = r.between(2, 13);
        int floorY = 0;
        if (!findFloor(x, z, r.between(33, 118), floorY)) continue;
        const Block floor = c.get(x, floorY, z);
        const std::string& biome = biomeAt(x, floorY, z);
        const int above = floorY + 1;
        if (floor == b_.netherrack || floor == b_.soulSand || floor == b_.soulSoil) {
            if (biome == "minecraft:soul_sand_valley") {
                if (r.below(4) == 0 && (floor == b_.soulSoil || floor == b_.soulSand)) c.set(x, above, z, b_.soulFire);
            } else if (biome == "minecraft:nether_wastes") {
                const int roll = r.below(10);
                if (roll < 3 && floor == b_.netherrack) c.set(x, above, z, b_.fire);
                else if (roll == 3) c.set(x, above, z, b_.brownMushroom);
                else if (roll == 4) c.set(x, above, z, b_.redMushroom);
            } else if (biome == "minecraft:basalt_deltas") {
                if (r.below(3) == 0) {
                    const int h = r.between(2, 6);
                    for (int k = 0; k < h && isAir(c.get(x, above + k, z)); ++k) c.set(x, above + k, z, b_.basalt);
                }
            }
        } else if (floor == b_.crimsonNylium) {
            const int roll = r.below(12);
            if (roll < 6) c.set(x, above, z, b_.crimsonRoots);
            else if (roll < 8) c.set(x, above, z, b_.crimsonFungus);
            else if (roll == 8 || roll == 9) c.set(x, above, z, b_.twistingVines);
            else if (roll == 10 && floorY + 14 < kNetherTop) {  // a huge fungus: a trunk with a cap of warts
                bool room = true;
                for (int k = 1; k <= 12 && room; ++k) room = isAir(c.get(x, floorY + k, z));
                if (room) {
                    const int h = r.between(5, 9);
                    for (int k = 1; k <= h; ++k) c.set(x, floorY + k, z, b_.crimsonStem);
                    for (int dy = -1; dy <= 2; ++dy) {
                        for (int dx = -3; dx <= 3; ++dx) {
                            for (int dz = -3; dz <= 3; ++dz) {
                                if (dx * dx + dz * dz + dy * dy * 2 > 10 || (dx == 0 && dz == 0 && dy <= 0)) continue;
                                const int px = x + dx, py = floorY + h + dy, pz = z + dz;
                                if (isAir(c.get(px, py, pz))) c.set(px, py, pz, dy == -1 && r.below(5) == 0 ? b_.shroomlight : b_.wartBlock);
                            }
                        }
                    }
                    c.set(x, floorY + h + 2, z, b_.shroomlight);
                }
            }
        } else if (floor == b_.warpedNylium) {
            const int roll = r.below(12);
            if (roll < 4) c.set(x, above, z, b_.warpedRoots);
            else if (roll < 6) c.set(x, above, z, b_.warpedFungus);
            else if (roll < 8) c.set(x, above, z, b_.sprouts);
            else if (roll == 8 || roll == 9) c.set(x, above, z, b_.twistingVines);
            else if (roll == 10 && floorY + 14 < kNetherTop) {
                bool room = true;
                for (int k = 1; k <= 12 && room; ++k) room = isAir(c.get(x, floorY + k, z));
                if (room) {
                    const int h = r.between(5, 9);
                    for (int k = 1; k <= h; ++k) c.set(x, floorY + k, z, b_.warpedStem);
                    for (int dy = -1; dy <= 2; ++dy) {
                        for (int dx = -3; dx <= 3; ++dx) {
                            for (int dz = -3; dz <= 3; ++dz) {
                                if (dx * dx + dz * dz + dy * dy * 2 > 10 || (dx == 0 && dz == 0 && dy <= 0)) continue;
                                const int px = x + dx, py = floorY + h + dy, pz = z + dz;
                                if (isAir(c.get(px, py, pz))) c.set(px, py, pz, dy == -1 && r.below(5) == 0 ? b_.shroomlight : b_.warpedWartBlock);
                            }
                        }
                    }
                }
            }
        }
    }
    // Weeping vines on the ceiling of the crimson forest.
    for (int i = 0; i < 24; ++i) {
        const int x = r.between(1, 14), z = r.between(1, 14), y = r.between(34, 118);
        if (!(isAir(c.get(x, y, z)) && c.get(x, y + 1, z) == b_.netherrack) || biomeAt(x, y, z) != "minecraft:crimson_forest") continue;
        const int h = r.between(1, 5);
        for (int k = 0; k < h && isAir(c.get(x, y - k, z)); ++k) c.set(x, y - k, z, b_.weepingVines);
    }
    c.write();
}

}  // namespace falcon
