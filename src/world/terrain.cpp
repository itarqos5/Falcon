#include "falcon/world/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace falcon {
namespace {

std::uint64_t splitmix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// A repeatable value 0..1 for a block position (for the ragged bedrock floor).
double hash01(std::uint64_t seed, int x, int y, int z) {
    const std::uint64_t h = splitmix(seed ^ splitmix(static_cast<std::uint64_t>(x) * 73856093u ^ static_cast<std::uint64_t>(y) * 19349663u ^
                                                    static_cast<std::uint64_t>(z) * 83492791u));
    return static_cast<double>(h >> 11) / 9007199254740992.0;
}

double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
double lerp(double t, double a, double b) { return a + t * (b - a); }

double gradient(int hash, double x, double z) {
    switch (hash & 7) {
        case 0: return x + z;
        case 1: return x - z;
        case 2: return -x + z;
        case 3: return -x - z;
        case 4: return x;
        case 5: return -x;
        case 6: return z;
        default: return -z;
    }
}

}  // namespace

TerrainGenerator::TerrainGenerator(std::int64_t seed, TerrainStyle style) : style_(style) {
    std::array<std::uint8_t, 256> p{};
    std::iota(p.begin(), p.end(), std::uint8_t{0});
    std::mt19937_64 rng(splitmix(static_cast<std::uint64_t>(seed)));
    std::shuffle(p.begin(), p.end(), rng);
    for (std::size_t i = 0; i < 512; ++i) perm_[i] = p[i & 255];
}

double TerrainGenerator::noise(double x, double z) const {
    const int xi = static_cast<int>(std::floor(x));
    const int zi = static_cast<int>(std::floor(z));
    const double xf = x - xi;
    const double zf = z - zi;
    const int x0 = xi & 255;
    const int z0 = zi & 255;
    const double u = fade(xf);
    const double v = fade(zf);
    const int aa = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(x0)] + z0)];
    const int ab = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(x0)] + z0 + 1)];
    const int ba = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(x0 + 1)] + z0)];
    const int bb = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(x0 + 1)] + z0 + 1)];
    return lerp(v, lerp(u, gradient(aa, xf, zf), gradient(ba, xf - 1, zf)),
                lerp(u, gradient(ab, xf, zf - 1), gradient(bb, xf - 1, zf - 1)));
}

int TerrainGenerator::surfaceHeight(int x, int z) const {
    if (style_ == TerrainStyle::Flat) return kFlatSurface;
    // Four octaves of noise: wide rolling land with smaller bumps on it.
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0 / 220.0;
    double norm = 0.0;
    for (int octave = 0; octave < 4; ++octave) {
        sum += noise(x * frequency + 17.3 * octave, z * frequency - 9.1 * octave) * amplitude;
        norm += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    sum /= norm;  // about -0.6 .. 0.6
    const double scale = style_ == TerrainStyle::Amplified ? 120.0 : 52.0;
    const double base = style_ == TerrainStyle::Amplified ? 76.0 : 66.0;
    return std::clamp(static_cast<int>(std::lround(base + sum * scale)), kMinY + 8, kMaxY - 8);
}

std::shared_ptr<Chunk> TerrainGenerator::generate(int chunkX, int chunkZ) const {
    auto chunk = std::make_shared<Chunk>();
    const CommonBlocks& cb = commonBlocks();
    const std::uint64_t seedMix = 0x5EEDu;

    if (style_ == TerrainStyle::Flat) {
        // Section 0 holds y -64..-49: bedrock, 2 dirt, grass, then air.
        SectionBuilder b;
        b.fillLayers(0, 1, cb.bedrock);
        b.fillLayers(1, 3, cb.dirt);
        b.fillLayers(3, 4, cb.grassBlock);
        chunk->sections[0] = b.build();
        return chunk;
    }

    std::array<int, 256> height{};
    int lowest = kMaxY;
    int highest = kMinY;
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const int h = surfaceHeight(chunkX * 16 + x, chunkZ * 16 + z);
            height[static_cast<std::size_t>(z * 16 + x)] = h;
            lowest = std::min(lowest, h);
            highest = std::max(highest, h);
        }
    }
    const int top = std::max(highest, kSeaLevel - 1);  // the highest block that is not air

    for (int sy = 0; sy < kSectionCount; ++sy) {
        const int y0 = kMinY + sy * 16;
        if (y0 > top) break;  // everything above is air
        // Rock all the way through: stone to a depth of 4 below the lowest column.
        if (y0 > kMinY + 4 && y0 + 15 <= lowest - 4) {
            chunk->sections[static_cast<std::size_t>(sy)] = uniformSection(cb.stone);
            continue;
        }
        SectionBuilder b;
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const int h = height[static_cast<std::size_t>(z * 16 + x)];
                const bool dry = h > kSeaLevel + 1;
                for (int ly = 0; ly < 16; ++ly) {
                    const int y = y0 + ly;
                    Block block = Block::Air;
                    if (y == kMinY) {
                        block = cb.bedrock;
                    } else if (y < kMinY + 5 && hash01(seedMix, chunkX * 16 + x, y, chunkZ * 16 + z) < (kMinY + 5 - y) / 6.0) {
                        block = cb.bedrock;  // the ragged bedrock floor
                    } else if (y <= h - 4) {
                        block = cb.stone;
                    } else if (y < h) {
                        block = dry ? cb.dirt : cb.sand;
                    } else if (y == h) {
                        block = dry ? cb.grassBlock : cb.sand;
                    } else if (y < kSeaLevel) {
                        block = cb.water;
                    }
                    if (block != Block::Air) b.set(x, ly, z, block);
                }
            }
        }
        chunk->sections[static_cast<std::size_t>(sy)] = b.build();
    }
    return chunk;
}

TerrainGenerator::Spawn TerrainGenerator::findSpawn() const {
    if (style_ == TerrainStyle::Flat) return {0.5, kFlatSurface + 1.0, 0.5};
    // The nearest dry land to the origin: look at rings of growing radius.
    for (int radius = 0; radius < 400; radius += 8) {
        for (int step = 0; step < std::max(1, radius * 2); step += 4) {
            const double angle = step * 6.283185307179586 / std::max(1, radius * 2);
            const int x = static_cast<int>(std::lround(std::cos(angle) * radius));
            const int z = static_cast<int>(std::lround(std::sin(angle) * radius));
            const int h = surfaceHeight(x, z);
            if (h > kSeaLevel + 1) return {x + 0.5, h + 1.0, z + 0.5};
        }
    }
    return {0.5, surfaceHeight(0, 0) + 1.0, 0.5};
}

}  // namespace falcon
