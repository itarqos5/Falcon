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

}  // namespace

LodSurface::LodSurface(std::shared_ptr<const OverworldGenerator> generator, std::vector<LodTrees> trees) : generator_(std::move(generator)), trees_(std::move(trees)) {
    const BiomeRegistry& registry = generator_->biomes();
    looks_.resize(registry.size());
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const BiomeInfo& info = registry.info(static_cast<BiomeId>(i));
        std::string_view name = info.name;
        if (name.rfind("minecraft:", 0) == 0) name.remove_prefix(10);
        Look& look = looks_[i];
        look.temperature = info.temperature;
        // What the top of the ground is made of, from the biome's name (the surface rules choose it by the same families of biome).
        Rgb ground;
        if (has(name, "ocean") || has(name, "river")) {
            look.kind = Kind::Water;
            ground = fromHex(0x8C8F6A);  // the floor under it (not seen)
        } else if (has(name, "desert") || has(name, "beach")) {
            look.kind = Kind::Sand;
            ground = fromHex(has(name, "snowy") ? 0xDDE0D0 : 0xDBCFA3);
        } else if (has(name, "badlands")) {
            look.kind = Kind::Terracotta;
            ground = fromHex(0xB5643C);
        } else if (has(name, "mushroom")) {
            look.kind = Kind::Mycelium;
            ground = fromHex(0x6F6369);
        } else if (has(name, "ice_spikes") || has(name, "snowy") || has(name, "frozen") || has(name, "grove") || has(name, "jagged_peaks")) {
            look.kind = Kind::Snow;
            ground = fromHex(0xF4F8F8);
        } else if (has(name, "stony") || has(name, "windswept_gravelly")) {
            look.kind = Kind::Stone;
            ground = fromHex(0x8B8B8B);
        } else {
            look.kind = Kind::Grass;
            ground = info.grassColor ? fromHex(*info.grassColor) : grassOf(info.temperature, info.downfall);
            if (info.grassColorModifier == GrassColorModifier::Swamp) ground = fromHex(0x6A7039);
            if (info.grassColorModifier == GrassColorModifier::DarkForest) ground = {ground.r * 0.5f + 0x28 * 0.5f, ground.g * 0.5f + 0x34 * 0.5f, ground.b * 0.5f + 0x0A * 0.5f};
        }
        // A texture is darker than the colour it is tinted with: the grass block's top averages about 0.7 of white, a stone block about 0.5.
        float factor = 1.0f;
        switch (look.kind) {
            case Kind::Grass: factor = 0.74f; break;
            case Kind::Sand: factor = 0.93f; break;
            case Kind::Terracotta: factor = 0.9f; break;
            case Kind::Snow: factor = 0.98f; break;
            case Kind::Stone: factor = 0.55f; break;
            case Kind::Mycelium: factor = 0.85f; break;
            case Kind::Gravel: factor = 0.6f; break;
            case Kind::Water: factor = 0.9f; break;
        }
        look.r = byteOf(ground.r * factor);
        look.g = byteOf(ground.g * factor);
        look.b = byteOf(ground.b * factor);
        const Rgb water = fromHex(info.waterColor.value_or(0x3F76E4u));
        look.waterR = byteOf(water.r * 0.78f);
        look.waterG = byteOf(water.g * 0.78f);
        look.waterB = byteOf(water.b * 0.78f);
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
    const BiomeId biome = generator_->biomeAt(x, floor, z);
    const Look& look = looks_[biome < looks_.size() ? biome : 0];

    const std::uint32_t h = hashPlace(x, z);
    if (floor < kSeaLevel) {  // under the sea: the top is the water, darker the deeper it is, in four steps (so runs of cells are alike)
        out.water = true;
        out.height = kSeaLevel;
        if (look.frozen) {  // an ice sheet on the sea
            out.r = 0xA8, out.g = 0xC4, out.b = 0xF2;
            return out;
        }
        const float deep = 1.0f - 0.5f * std::floor(std::clamp(static_cast<float>(kSeaLevel - floor) / 50.0f, 0.0f, 1.0f) * 4.0f) / 4.0f;
        out.r = byteOf(look.waterR * deep);
        out.g = byteOf(look.waterG * deep);
        out.b = byteOf(look.waterB * deep);
        return out;
    }

    out.height = static_cast<std::int16_t>(floor);
    std::uint8_t r = look.r, g = look.g, b = look.b;
    // Snow where it is cold at that height (the game cools the air 0.00125 a block above y 80), and bare rock above the trees on a mountain.
    const float temperature = look.temperature - std::max(0, floor - 80) * 0.00125f;
    if (temperature < 0.15f && look.kind != Kind::Water) {
        r = 0xF2, g = 0xF6, b = 0xF6;
    } else if (look.kind == Kind::Grass && floor > 135) {
        const float rock = std::clamp(static_cast<float>(floor - 135) / 40.0f, 0.0f, 1.0f);
        r = byteOf(r + (0x6E - r) * rock);
        g = byteOf(g + (0x6E - g) * rock);
        b = byteOf(b + (0x6E - b) * rock);
    }
    if (varied) {
        // A forest: a column of the tree cover has the colour of the leaves, as high as the tree. The cover is the share of the ground that the canopies of the biome's trees
        // shade (a Poisson cover, for trees of about 5 x 5), and only land that is not bare (sand, snow, rock) has trees.
        const LodTrees trees = look.kind != Kind::Water && biome < trees_.size() ? trees_[biome] : LodTrees{};
        if (trees.kind != 0 && out.height > kSeaLevel && look.kind != Kind::Sand && look.kind != Kind::Stone && look.kind != Kind::Terracotta) {
            const float cover = 1.0f - std::exp(-trees.perChunk * 22.0f / 256.0f);
            if (static_cast<float>(h & 0xFFFFu) / 65536.0f < cover) {
                Rgb leaf{static_cast<float>(look.r), static_cast<float>(look.g), static_cast<float>(look.b)};  // the biome's tint (oak, acacia, jungle, dark oak)
                float factor = 0.78f;
                switch (trees.kind) {
                    case 2: leaf = fromHex(0x80A755), factor = 0.72f; break;   // birch
                    case 3: leaf = fromHex(0x619961), factor = 0.6f; break;    // spruce
                    case 5: factor = 0.82f; break;                             // jungle
                    case 6: leaf = fromHex(0xE5B5D3), factor = 0.88f; break;   // cherry
                    case 7: factor = 0.62f; break;                             // dark oak
                    default: break;
                }
                r = byteOf(leaf.r * factor);
                g = byteOf(leaf.g * factor);
                b = byteOf(leaf.b * factor);
                out.height = static_cast<std::int16_t>(out.height + trees.height + static_cast<int>((h >> 20) & 3u) - 1);
            }
        }
        // No two blocks are quite alike: a few percent of lightness either way, so that open ground shows the cells it is made of.
        const float shade = 1.0f + (static_cast<float>((h >> 24) & 15u) - 7.5f) / 150.0f;
        r = byteOf(static_cast<float>(r) * shade);
        g = byteOf(static_cast<float>(g) * shade);
        b = byteOf(static_cast<float>(b) * shade);
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
        for (int gj = 0; gj < side; ++gj) {
            for (int gi = 0; gi < side; ++gi) floors[at(gi, gj)] = generator_->surfaceHeightQuickAt(xOf(gi), zOf(gj));
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
