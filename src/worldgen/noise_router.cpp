#include "falcon/worldgen/noise_router.hpp"

#include "falcon/worldgen/chunk_fill.hpp"

#include <cstring>

namespace falcon::worldgen {

std::filesystem::path worldgenDirFromAssetsRoot(const std::filesystem::path& assetsRoot) {
    return assetsRoot.parent_path() / "data" / "minecraft" / "worldgen";
}

namespace {

std::string stripNamespace(std::string_view s) {
    constexpr std::string_view kPrefix = "minecraft:";
    if (s.substr(0, kPrefix.size()) == kPrefix) s.remove_prefix(kPrefix.size());
    return std::string(s);
}

// "minecraft:stone" or {"Name": "minecraft:stone", ...}
bool blockName(const JsonValue* v, std::string& out) {
    if (!v) return false;
    if (v->isString()) {
        out = v->text;
        return true;
    }
    if (const JsonValue* n = v ? v->find("Name") : nullptr) {
        if (n->isString()) {
            out = n->text;
            return true;
        }
    }
    return false;
}

}  // namespace

std::unique_ptr<NoiseRouter> NoiseRouter::create(const std::filesystem::path& worldgenDir, std::int64_t seed, std::string& error,
                                                 std::string_view settingsName, const std::vector<std::string>& extraFunctions) {
    JsonValue js;
    if (!parseJsonFile(worldgenDir / "noise_settings" / (stripNamespace(settingsName) + ".json"), js, error)) return nullptr;
    if (!js.isObject()) {
        error = "noise settings: not an object";
        return nullptr;
    }

    std::unique_ptr<NoiseRouter> r(new NoiseRouter());
    NoiseSettings& s = r->settings_;
    if (const JsonValue* noise = js.find("noise")) {
        std::int32_t v = 0;
        if (const JsonValue* m = noise->find("min_y")) {
            if (m->toInt(v)) s.minY = v;
        }
        if (const JsonValue* h = noise->find("height")) {
            if (h->toInt(v)) s.height = v;
        }
    }
    if (s.height <= 0 || s.height % 16 != 0 || s.minY % 16 != 0) {
        error = "noise settings: noise.min_y / noise.height must be positive multiples of 16";
        return nullptr;
    }
    if (const JsonValue* sl = js.find("sea_level")) {
        std::int32_t v = 0;
        if (sl->toInt(v)) s.seaLevel = v;
    }
    (void)blockName(js.find("default_block"), s.defaultBlock);
    (void)blockName(js.find("default_fluid"), s.defaultFluid);
    if (const JsonValue* lg = js.find("legacy_random_source")) s.legacyRandomSource = lg->kind == JsonValue::Kind::Bool && lg->boolean;
    if (s.legacyRandomSource) {
        error = "noise settings: legacy_random_source worlds are not supported";
        return nullptr;
    }

    const JsonValue* router = js.find("noise_router");
    if (!router || !router->isObject()) {
        error = "noise settings: noise_router missing";
        return nullptr;
    }

    r->state_ = std::make_shared<RandomState>(seed, worldgenDir);
    DensityGraphBuilder builder(r->state_);
    const auto add = [&](const JsonValue* v, const char* what, int& id) {
        if (!v) {
            error = std::string("noise settings: ") + what + " missing";
            return false;
        }
        id = builder.addJson(*v);
        if (id < 0) {
            error = std::string(what) + ": " + builder.error();
            return false;
        }
        return true;
    };
    int temperature = -1, vegetation = -1, continents = -1, erosion = -1, depth = -1, ridges = -1, chunkSurface = -1, finalDensity = -1;
    if (!add(router->find("temperature"), "noise_router.temperature", temperature) || !add(router->find("vegetation"), "noise_router.vegetation", vegetation) ||
        !add(router->find("continents"), "noise_router.continents", continents) || !add(router->find("erosion"), "noise_router.erosion", erosion) ||
        !add(router->find("depth"), "noise_router.depth", depth) || !add(router->find("ridges"), "noise_router.ridges", ridges) ||
        !add(router->find("chunk_surface_level"), "noise_router.chunk_surface_level", chunkSurface) ||
        !add(router->find("final_density"), "noise_router.final_density", finalDensity)) {
        return nullptr;
    }
    int barrier = -1, floodedness = -1, spread = -1, lava = -1, exclusion = -1, surface = -1;
    const JsonValue* aq = js.find("aquifers");
    if (aq && aq->isObject()) {
        if (!add(aq->find("barrier"), "aquifers.barrier", barrier) || !add(aq->find("fluid_level_floodedness"), "aquifers.fluid_level_floodedness", floodedness) ||
            !add(aq->find("fluid_level_spread"), "aquifers.fluid_level_spread", spread) || !add(aq->find("lava"), "aquifers.lava", lava) ||
            !add(aq->find("exclusion"), "aquifers.exclusion", exclusion) || !add(aq->find("surface_level"), "aquifers.surface_level", surface)) {
            return nullptr;
        }
    }
    std::vector<std::pair<std::string, int>> extras;
    for (const std::string& ref : extraFunctions) {
        const int id = builder.addRef(ref);
        if (id < 0) {
            error = ref + ": " + builder.error();
            return nullptr;
        }
        extras.emplace_back(ref, id);
    }

    r->graph_ = builder.build();
    if (!r->graph_) {
        error = builder.error();
        return nullptr;
    }
    const DensityGraph& g = *r->graph_;
    r->temperature_ = g.function(temperature);
    r->vegetation_ = g.function(vegetation);
    r->continents_ = g.function(continents);
    r->erosion_ = g.function(erosion);
    r->depth_ = g.function(depth);
    r->ridges_ = g.function(ridges);
    r->chunkSurfaceLevel_ = g.function(chunkSurface);
    r->finalDensity_ = g.function(finalDensity);
    if (barrier >= 0) {
        r->aquiferBarrier_ = g.function(barrier);
        r->aquiferFloodedness_ = g.function(floodedness);
        r->aquiferSpread_ = g.function(spread);
        r->aquiferLava_ = g.function(lava);
        r->aquiferExclusion_ = g.function(exclusion);
        r->aquiferSurface_ = g.function(surface);
    }
    for (const auto& e : extras) r->extra_[e.first] = g.function(e.second);
    return r;
}

DensityFunction NoiseRouter::function(std::string_view ref) const {
    const auto it = extra_.find(ref);
    return it == extra_.end() ? DensityFunction{} : it->second;
}

DensityVolume NoiseRouter::chunkVolume(int chunkX, int chunkZ) const {
    DensityVolume v;
    v.sizeX = kChunkSize;
    v.sizeY = settings_.height;
    v.sizeZ = kChunkSize;
    v.minX = chunkX * kChunkSize;
    v.minY = settings_.minY;
    v.minZ = chunkZ * kChunkSize;
    return v;
}

void NoiseRouter::finalDensityVolume(int chunkX, int chunkZ, float* out) const { finalDensity_.fillVolume(chunkVolume(chunkX, chunkZ), out); }

std::vector<float> NoiseRouter::finalDensityVolume(int chunkX, int chunkZ) const {
    std::vector<float> out(chunkVolumeSize());
    finalDensityVolume(chunkX, chunkZ, out.data());
    return out;
}

const float* NoiseRouter::finalDensityVolumeScratch(int chunkX, int chunkZ) const {
    thread_local std::vector<float> buffer;
    if (buffer.size() < chunkVolumeSize()) buffer.resize(chunkVolumeSize());
    finalDensityVolume(chunkX, chunkZ, buffer.data());
    return buffer.data();
}

void NoiseRouter::chunkSurfaceLevelVolume(int chunkX, int chunkZ, float* out) const {
    DensityVolume v;
    v.sizeX = kChunkSize;
    v.sizeY = 1;
    v.sizeZ = kChunkSize;
    v.minX = chunkX * kChunkSize;
    v.minY = 0;
    v.minZ = chunkZ * kChunkSize;
    chunkSurfaceLevel_.fillVolume(v, out);
}

// ---------------------------------------------------------------------------------------------------------------
std::vector<std::uint8_t> stoneAirFromDensity(const float* volume, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = volume[i] > 0.0f ? static_cast<std::uint8_t>(FillBlock::Stone) : static_cast<std::uint8_t>(FillBlock::Air);
    return out;
}

std::vector<ColumnRun> columnRuns(const std::uint8_t* blocks, int localX, int localZ, int minY, int height) {
    std::vector<ColumnRun> runs;
    const std::uint8_t* col = blocks + static_cast<std::size_t>(height) * (static_cast<std::size_t>(localX) + 16u * static_cast<std::size_t>(localZ));
    for (int yIndex = height - 1; yIndex >= 0; --yIndex) {
        const int y = minY + yIndex;
        if (!runs.empty() && runs.back().block == col[yIndex]) {
            runs.back().bottomY = y;
        } else {
            runs.push_back({y, y, col[yIndex]});
        }
    }
    return runs;
}

std::uint64_t densityChecksum(const float* values, std::size_t count) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &values[i], sizeof(bits));
        h ^= static_cast<std::uint64_t>(bits);
        h *= 0x100000001b3ull;
    }
    return h;
}

}  // namespace falcon::worldgen
