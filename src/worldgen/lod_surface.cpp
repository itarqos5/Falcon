#include "falcon/worldgen/lod_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace falcon::worldgen {

namespace {

struct Rgb {
    float r, g, b;
};

Rgb fromHex(std::uint32_t rgb) {
    return {static_cast<float>((rgb >> 16) & 255u), static_cast<float>((rgb >> 8) & 255u), static_cast<float>(rgb & 255u)};
}

std::uint8_t byteOf(float v) { return static_cast<std::uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f)); }

bool has(std::string_view name, std::string_view part) { return name.find(part) != std::string_view::npos; }

// The game's grass colour map is a triangle over temperature and rainfall (rainfall counted only as far as it is warm); these are its three corners.
Rgb grassOf(float temperature, float downfall) {
    const float t = std::clamp(temperature, 0.0f, 1.0f);
    const float d = std::clamp(downfall, 0.0f, 1.0f) * t;
    const Rgb cold = fromHex(0x80B497), dry = fromHex(0xBFB755), wet = fromHex(0x59C93C);
    const float wCold = 1.0f - t, wDry = t - d, wWet = d;
    return {cold.r * wCold + dry.r * wDry + wet.r * wWet, cold.g * wCold + dry.g * wDry + wet.g * wWet, cold.b * wCold + dry.b * wDry + wet.b * wWet};
}

// The same for the foliage colour map (the leaves of oak, acacia, jungle and dark oak): its corners are the cold, the hot and dry, and the hot and wet.
Rgb foliageOf(float temperature, float downfall) {
    const float t = std::clamp(temperature, 0.0f, 1.0f);
    const float d = std::clamp(downfall, 0.0f, 1.0f) * t;
    const Rgb cold = fromHex(0x60A17B), dry = fromHex(0xAEA42A), wet = fromHex(0x48B518);
    const float wCold = 1.0f - t, wDry = t - d, wWet = d;
    return {cold.r * wCold + dry.r * wDry + wet.r * wWet, cold.g * wCold + dry.g * wDry + wet.g * wWet, cold.b * wCold + dry.b * wDry + wet.b * wWet};
}

}  // namespace

LodSurface::LodSurface(std::shared_ptr<const OverworldGenerator> generator, std::vector<LodTrees> trees, LodColourMaps maps) : generator_(std::move(generator)), trees_(std::move(trees)) {
    const BiomeRegistry& registry = generator_->biomes();
    looks_.resize(registry.size());
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const BiomeInfo& info = registry.info(static_cast<BiomeId>(i));
        std::string_view name = info.name;
        if (name.rfind("minecraft:", 0) == 0) name.remove_prefix(10);
        Look& look = looks_[i];
        look.temperature = info.temperature;
        // What the top of the ground is made of, from the biome's name (the surface rules choose it by the same families of biome).
        // Only grass is tinted (by the biome); the other tops are drawn with their texture as it is.
        Rgb ground{255.0f, 255.0f, 255.0f};
        if (has(name, "ocean") || has(name, "river")) {
            look.kind = Kind::Water;
        } else if (has(name, "desert") || has(name, "beach")) {
            look.kind = Kind::Sand;
        } else if (has(name, "badlands")) {
            look.kind = Kind::Terracotta;
        } else if (has(name, "mushroom")) {
            look.kind = Kind::Mycelium;
        } else if (has(name, "ice_spikes") || has(name, "snowy") || has(name, "frozen") || has(name, "grove") || has(name, "jagged_peaks")) {
            look.kind = Kind::Snow;
        } else if (has(name, "stony") || has(name, "windswept_gravelly")) {
            look.kind = Kind::Stone;
        } else {
            look.kind = Kind::Grass;
            ground = info.grassColor ? fromHex(*info.grassColor) : maps.grass ? fromHex(maps.grass(info.temperature, info.downfall)) : grassOf(info.temperature, info.downfall);
            if (info.grassColorModifier == GrassColorModifier::Swamp) ground = fromHex(0x6A7039);
            if (info.grassColorModifier == GrassColorModifier::DarkForest) ground = {ground.r * 0.5f + 0x28 * 0.5f, ground.g * 0.5f + 0x34 * 0.5f, ground.b * 0.5f + 0x0A * 0.5f};
        }
        look.r = byteOf(ground.r);
        look.g = byteOf(ground.g);
        look.b = byteOf(ground.b);
        const Rgb water = fromHex(info.waterColor.value_or(0x3F76E4u));
        // Leaves: the foliage tint of the biome (a swamp has its own), which is not its grass tint.
        const Rgb foliage = info.foliageColor ? fromHex(*info.foliageColor) : maps.foliage ? fromHex(maps.foliage(info.temperature, info.downfall)) : foliageOf(info.temperature, info.downfall);
        look.leafR = byteOf(foliage.r);
        look.leafG = byteOf(foliage.g);
        look.leafB = byteOf(foliage.b);
        look.waterR = byteOf(water.r);
        look.waterG = byteOf(water.g);
        look.waterB = byteOf(water.b);
        look.frozen = has(name, "frozen");
    }
}

LodColumn LodSurface::column(int x, int z, LodPredictor how) const {
    return paint(x, z, how == LodPredictor::Density ? generator_->surfaceHeightQuickAt(x, z) : generator_->preliminarySurfaceAt(x, z), false);
}

namespace {

// A cheap hash of a place (the same everywhere, so a tile made twice is the same): what decides whether a column has a tree and how its colour varies.
std::uint32_t hashPlace(int x, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9E3779B1u ^ static_cast<std::uint32_t>(z) * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

}  // namespace

LodColumn LodSurface::paint(int x, int z, int floor, bool varied) const {
    LodColumn out;
    out.floor = static_cast<std::int16_t>(floor);
    out.ground = LodGround::Water;
    const BiomeId biome = generator_->biomeAt(x, floor, z);
    const Look& look = looks_[biome < looks_.size() ? biome : 0];

    if (floor < kSeaLevel) {  // under the sea: the top is the water, with a subtle depth gradient
        out.water = true;
        out.height = kSeaLevel;
        if (look.frozen) {  // an ice sheet on the sea
            out.ice = true;
            out.r = out.g = out.b = 255;
            return out;
        }
        // (Measured against the real sea in the same view, the opaque colour was about a quarter too bright: the real water lets the dark sea floor show through it. x 0.78.)
        const float deep = 0.78f * (1.0f - 0.15f * std::clamp(static_cast<float>(kSeaLevel - floor) / 32.0f, 0.0f, 1.0f));
        out.r = byteOf(look.waterR * deep);
        out.g = byteOf(look.waterG * deep);
        out.b = byteOf(look.waterB * deep);
        return out;
    }

    out.height = static_cast<std::int16_t>(floor);
    out.ground = look.kind == Kind::Water ? Kind::Sand : look.kind;  // (the shore of an ocean biome is sand)
    std::uint8_t r = look.r, g = look.g, b = look.b;
    // Snow where it is cold at that height (the game cools the air 0.00125 a block above y 80), and bare rock above the trees on a mountain.
    const float temperature = look.temperature - std::max(0, floor - 80) * 0.00125f;
    if (temperature < 0.15f && look.kind != Kind::Water) {
        r = g = b = 255;
        out.bareSnow = look.kind != Kind::Grass && look.kind != Kind::Mycelium;  // (a cold plain has earth under its snow; a peak has rock)
        out.ground = Kind::Snow;
    } else if (look.kind == Kind::Grass && floor > 155) {
        r = g = b = 255;  // bare rock from here up: no earth on it, so the top and the side of a cliff are stone
        out.ground = Kind::Stone;
    }
    if (varied) {
        // A forest: trees generate in natural clusters with coherent 5x5 rounded canopies, forming continuous forest
        // canopies instead of noisy isolated 1x1 pillars.
        const LodTrees trees = look.kind != Kind::Water && biome < trees_.size() ? trees_[biome] : LodTrees{};
        if (trees.kind != 0 && out.height > kSeaLevel && look.kind != Kind::Sand && look.kind != Kind::Stone && look.kind != Kind::Terracotta && out.ground != Kind::Stone) {
            const int cx = x >= 0 ? x / 4 : (x - 3) / 4;
            const int cz = z >= 0 ? z / 4 : (z - 3) / 4;
            const int remX = x - cx * 4;
            const int remZ = z - cz * 4;
            const int qx = remX >= 2 ? 1 : -1;
            const int qz = remZ >= 2 ? 1 : -1;
            const float prob = std::min(0.92f, trees.perChunk * 16.0f / 256.0f);
            int bestTall = 0;
            const int cellDirs[4][2] = {{0, 0}, {qx, 0}, {0, qz}, {qx, qz}};
            for (const auto& dir : cellDirs) {
                const int scx = cx + dir[0], scz = cz + dir[1];
                const std::uint32_t th = hashPlace(scx * 0x45D9F3Bu + 0x12345u, scz * 0x45D9F3Bu + 0x67891u);
                if (static_cast<float>(th & 0xFFFFu) / 65536.0f < prob) {
                    const int tx = scx * 4 + static_cast<int>((th >> 16) & 3u);
                    const int tz = scz * 4 + static_cast<int>((th >> 18) & 3u);
                    const int dx = x - tx, dz = z - tz;
                    if (dx * dx + dz * dz <= 5) {
                        const int tall = std::max(1, trees.height + static_cast<int>((th >> 20) & 1u));
                        bestTall = std::max(bestTall, tall);
                    }
                }
            }
            if (bestTall > 0) {
                // The leaf texture is multiplied by the foliage tint of the biome (oak, acacia, jungle, dark oak); birch and spruce leaves have a colour of their own, cherry leaves none.
                r = look.leafR, g = look.leafG, b = look.leafB;
                switch (trees.kind) {
                    case 2: r = 0x80, g = 0xA7, b = 0x55; break;
                    case 3: r = 0x61, g = 0x99, b = 0x61; break;
                    case 6: r = g = b = 255; break;
                    default: break;
                }
                out.height = static_cast<std::int16_t>(out.height + bestTall);
                out.canopy = static_cast<std::uint8_t>(std::min(bestTall, 255));
                out.tree = static_cast<std::uint8_t>(trees.kind);
            }
        }
    }
    out.r = r;
    out.g = g;
    out.b = b;
    return out;
}

LodGrid LodSurface::grid(int originX, int originZ, int cell, int cells, LodMode mode) const {
    LodGrid out;
    const int side = cells + 3;
    out.side = side;
    const auto count = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
    out.columns.resize(count);
    std::vector<int> floors(count);
    // A column is read in the middle of its cell (g = i + 1).
    const auto xOf = [&](int g) { return originX + (g - 1) * cell + cell / 2; };
    const auto zOf = [&](int g) { return originZ + (g - 1) * cell + cell / 2; };
    const auto at = [&](int gi, int gj) { return static_cast<std::size_t>(gj * side + gi); };

    if (mode == LodMode::Chunk) {
        // The ground of whole chunks, made once each however many of their columns the cells use.
        std::unordered_map<std::uint64_t, std::array<std::int16_t, 256>> chunks;
        for (int gj = 0; gj < side; ++gj) {
            for (int gi = 0; gi < side; ++gi) {
                const int x = xOf(gi), z = zOf(gj);
                const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x >> 4)) << 32) | static_cast<std::uint32_t>(z >> 4);
                auto it = chunks.find(key);
                if (it == chunks.end()) {
                    std::array<std::int16_t, 256> heights;
                    generator_->surfaceHeights(x >> 4, z >> 4, heights);
                    it = chunks.emplace(key, heights).first;
                }
                floors[at(gi, gj)] = it->second[static_cast<std::size_t>((x & 15) + 16 * (z & 15))];
            }
        }
    } else if (mode == LodMode::Direct) {
        // The real height of every column, found fast: the full search on a sparse grid (every 4th column and the last), and every other column searched from a guess, the
        // interpolation of the nodes corrected by how wrong it was at the two columns just made (terrain errors run in patches), which the search usually confirms in two or three probes.
        std::vector<int> nodes;
        for (int g = 0; g < side; g += 4) nodes.push_back(g);
        if (nodes.back() != side - 1) nodes.push_back(side - 1);
        const auto n = static_cast<int>(nodes.size());
        std::vector<float> nodeHeight(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
        for (int b = 0; b < n; ++b) {
            for (int a = 0; a < n; ++a) {
                nodeHeight[static_cast<std::size_t>(b * n + a)] =
                    static_cast<float>(generator_->surfaceHeightQuickAt(xOf(nodes[static_cast<std::size_t>(a)]), zOf(nodes[static_cast<std::size_t>(b)])));
            }
        }
        std::vector<int> segment(static_cast<std::size_t>(side));
        std::vector<float> along(static_cast<std::size_t>(side));
        std::vector<bool> isNode(static_cast<std::size_t>(side), false);
        for (const int g : nodes) isNode[static_cast<std::size_t>(g)] = true;
        for (int g = 0, s = 0; g < side; ++g) {
            while (s + 2 < n && nodes[static_cast<std::size_t>(s + 1)] <= g) ++s;
            segment[static_cast<std::size_t>(g)] = s;
            along[static_cast<std::size_t>(g)] = static_cast<float>(g - nodes[static_cast<std::size_t>(s)]) / static_cast<float>(nodes[static_cast<std::size_t>(s + 1)] - nodes[static_cast<std::size_t>(s)]);
        }
        std::vector<float> smooth(count);  // the interpolation of the nodes, at every column
        const auto h = [&](int a, int b) { return nodeHeight[static_cast<std::size_t>(b * n + a)]; };
        for (int gj = 0; gj < side; ++gj) {
            const int sj = segment[static_cast<std::size_t>(gj)];
            const float fj = along[static_cast<std::size_t>(gj)];
            for (int gi = 0; gi < side; ++gi) {
                const int si = segment[static_cast<std::size_t>(gi)];
                const float fi = along[static_cast<std::size_t>(gi)];
                const float low = h(si, sj) * (1.0f - fi) + h(si + 1, sj) * fi;
                const float high = h(si, sj + 1) * (1.0f - fi) + h(si + 1, sj + 1) * fi;
                smooth[at(gi, gj)] = low * (1.0f - fj) + high * fj;
                if (isNode[static_cast<std::size_t>(gi)] && isNode[static_cast<std::size_t>(gj)]) {
                    floors[at(gi, gj)] = static_cast<int>(smooth[at(gi, gj)]);  // (exact: the node's own height)
                    continue;
                }
                float correction = 0.0f;
                int known = 0;
                if (gi > 0) correction += static_cast<float>(floors[at(gi - 1, gj)]) - smooth[at(gi - 1, gj)], ++known;
                if (gj > 0) correction += static_cast<float>(floors[at(gi, gj - 1)]) - smooth[at(gi, gj - 1)], ++known;
                const float guess = smooth[at(gi, gj)] + (known > 0 ? correction / static_cast<float>(known) : 0.0f);
                floors[at(gi, gj)] = generator_->surfaceHeightNear(xOf(gi), zOf(gj), static_cast<int>(std::lround(guess)));
            }
        }
    } else {
        for (int gj = 0; gj < side; ++gj) {
            for (int gi = 0; gi < side; ++gi) floors[at(gi, gj)] = generator_->preliminarySurfaceAt(xOf(gi), zOf(gj));
        }
    }
    if (mode == LodMode::Residual) {
        // The real height on a sparse grid (every 4th column, and the last one), and the difference from the preliminary surface spread over the columns between.
        std::vector<int> nodes;
        for (int g = 0; g < side; g += 4) nodes.push_back(g);
        if (nodes.back() != side - 1) nodes.push_back(side - 1);
        const auto n = static_cast<int>(nodes.size());
        std::vector<float> residual(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
        for (int b = 0; b < n; ++b) {
            for (int a = 0; a < n; ++a) {
                const int gi = nodes[static_cast<std::size_t>(a)], gj = nodes[static_cast<std::size_t>(b)];
                residual[static_cast<std::size_t>(b * n + a)] = static_cast<float>(generator_->surfaceHeightQuickAt(xOf(gi), zOf(gj)) - floors[at(gi, gj)]);
            }
        }
        // Which pair of nodes holds column g, and how far along it is.
        std::vector<int> segment(static_cast<std::size_t>(side));
        std::vector<float> along(static_cast<std::size_t>(side));
        for (int g = 0, s = 0; g < side; ++g) {
            while (s + 2 < n && nodes[static_cast<std::size_t>(s + 1)] <= g) ++s;
            segment[static_cast<std::size_t>(g)] = s;
            const float span = static_cast<float>(nodes[static_cast<std::size_t>(s + 1)] - nodes[static_cast<std::size_t>(s)]);
            along[static_cast<std::size_t>(g)] = static_cast<float>(g - nodes[static_cast<std::size_t>(s)]) / span;
        }
        for (int gj = 0; gj < side; ++gj) {
            const int sj = segment[static_cast<std::size_t>(gj)];
            const float fj = along[static_cast<std::size_t>(gj)];
            for (int gi = 0; gi < side; ++gi) {
                const int si = segment[static_cast<std::size_t>(gi)];
                const float fi = along[static_cast<std::size_t>(gi)];
                const auto r = [&](int a, int b) { return residual[static_cast<std::size_t>(b * n + a)]; };
                const float low = r(si, sj) * (1.0f - fi) + r(si + 1, sj) * fi;
                const float high = r(si, sj + 1) * (1.0f - fi) + r(si + 1, sj + 1) * fi;
                floors[at(gi, gj)] += static_cast<int>(std::lround(low * (1.0f - fj) + high * fj));
            }
        }
    }
    for (int gj = 0; gj < side; ++gj) {
        for (int gi = 0; gi < side; ++gi) out.columns[at(gi, gj)] = paint(xOf(gi), zOf(gj), floors[at(gi, gj)], true);
    }
    return out;
}

}  // namespace falcon::worldgen
