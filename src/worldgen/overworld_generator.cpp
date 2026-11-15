#include "falcon/worldgen/overworld_generator.hpp"

#include "falcon/worldgen/java_math.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace falcon::worldgen {

namespace {

std::atomic<std::uint64_t> g_nextGeneratorId{1};

// Where the time of a chunk goes, for finding what to speed up: FALCON_PROFILE_WORLDGEN=1 prints the totals when the generator is destroyed (at the
// exit of the game). Off, a lap costs one branch. Wall-clock time summed over the threads.
constexpr int kProfileStages = 7;
const char* const kProfileNames[kProfileStages] = {"biomes", "density", "fill (aquifer)", "surface level", "surface rules", "caves+ores", "chunk build"};
struct StageProfile {
    bool enabled = std::getenv("FALCON_PROFILE_WORLDGEN") != nullptr;
    std::atomic<std::int64_t> nanos[kProfileStages]{};
    std::atomic<long> chunks{0};
};
StageProfile g_profile;

class Lap {
public:
    Lap() : on_(g_profile.enabled), last_(on_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    void to(int stage) {
        if (!on_) return;
        const auto now = std::chrono::steady_clock::now();
        g_profile.nanos[stage] += std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_).count();
        last_ = now;
    }

private:
    bool on_;
    std::chrono::steady_clock::time_point last_;
};

// <gameData>/data/minecraft/worldgen, or the directory itself when it is already the worldgen folder.
std::filesystem::path resolveWorldgenDir(const std::filesystem::path& dir) {
    std::error_code ec;
    const std::filesystem::path nested = dir / "data" / "minecraft" / "worldgen";
    if (std::filesystem::is_directory(nested, ec)) return nested;
    if (std::filesystem::is_directory(dir / "noise_settings", ec)) return dir;
    return nested;
}

// The folder holding data/ and biome-parameters/ for a worldgen directory.
std::filesystem::path gameDataDirOf(const std::filesystem::path& worldgenDir) { return worldgenDir.parent_path().parent_path().parent_path(); }

std::string stripNamespace(std::string_view s) {
    constexpr std::string_view kPrefix = "minecraft:";
    if (s.substr(0, kPrefix.size()) == kPrefix) s.remove_prefix(kPrefix.size());
    return std::string(s);
}

std::filesystem::path findParameterFile(const std::filesystem::path& worldgenDir, const std::string& preset) {
    const std::string name = stripNamespace(preset) + ".json";
    const std::filesystem::path gd = gameDataDirOf(worldgenDir);
    for (const std::filesystem::path& p : {gd / "biome-parameters" / name, gd / "worldgen" / "biome_parameters" / name, worldgenDir / "biome_parameters" / name}) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return gd / "biome-parameters" / name;
}

}  // namespace

bool loadWorldPreset(const std::filesystem::path& gameDataDir, std::string_view preset, GeneratorOptions& out, std::string& error) {
    const std::filesystem::path worldgen = resolveWorldgenDir(gameDataDir);
    JsonValue js;
    if (!parseJsonFile(worldgen / "world_preset" / (stripNamespace(preset) + ".json"), js, error)) return false;
    const JsonValue* dims = js.find("dimensions");
    const JsonValue* overworld = dims ? dims->find("minecraft:overworld") : nullptr;
    const JsonValue* gen = overworld ? overworld->find("generator") : nullptr;
    if (!gen || !gen->isObject()) {
        error = "world preset has no overworld generator";
        return false;
    }
    const JsonValue* type = gen->find("type");
    if (!type || !type->isString() || type->text != "minecraft:noise") {
        error = "world preset overworld is not a noise generator";
        return false;
    }
    const JsonValue* settings = gen->find("settings");
    if (!settings || !settings->isString()) {
        error = "world preset overworld generator has no settings";
        return false;
    }
    out.noiseSettings = settings->text;
    out.singleBiome.clear();
    const JsonValue* src = gen->find("biome_source");
    const JsonValue* srcType = src ? src->find("type") : nullptr;
    if (srcType && srcType->isString() && srcType->text == "minecraft:fixed") {
        const JsonValue* biome = src->find("biome");
        if (!biome || !biome->isString()) {
            error = "fixed biome source without biome";
            return false;
        }
        out.singleBiome = biome->text;
    } else if (srcType && srcType->isString() && srcType->text == "minecraft:multi_noise") {
        if (const JsonValue* p = src->find("preset"); p && p->isString()) out.biomePreset = p->text;
    } else {
        error = "unsupported biome source in world preset";
        return false;
    }
    return true;
}

// ===============================================================================================================
// Per-thread scratch
struct OverworldGenerator::Workspace {
    // BiomeManager.getBiome through a chunk-local table of the quart cells: the 4 x 96 x 4 of the chunk plus a one-cell border
    // (the zoom can pick the neighbouring cell), all filled when the chunk starts.
    class Lookup final : public BiomeLookup {
    public:
        static constexpr int kW = 6;
        static constexpr int kQ = ChunkBiomes::kQuartsY;
        void begin(const BiomeSource& src, BiomeSource::Context& ctx, int chunkX, int chunkZ, const ChunkBiomes& interior) {
            src_ = &src;
            ctx_ = &ctx;
            baseQx_ = chunkX * 4 - 1;
            baseQz_ = chunkZ * 4 - 1;
            cache_.fill(0xFF);
            for (int qz = 0; qz < 4; ++qz) {
                for (int qx = 0; qx < 4; ++qx) {
                    std::memcpy(&cache_[static_cast<std::size_t>(((qz + 1) * kW + (qx + 1)) * kQ)], &interior.ids[ChunkBiomes::index(qx, 0, qz)], kQ);
                }
            }
            // The border cells too (the zoom can pick them), so that "which biomes can this chunk see" is known up front.
            if (!src.isFixed()) {
                for (int rz = 0; rz < kW; ++rz) {
                    for (int rx = 0; rx < kW; ++rx) {
                        if (rx >= 1 && rx <= 4 && rz >= 1 && rz <= 4) continue;
                        std::uint8_t* col = &cache_[static_cast<std::size_t>((rz * kW + rx) * kQ)];
                        for (int i = 0; i < kQ; ++i) col[i] = static_cast<std::uint8_t>(src.noiseBiome(ctx, baseQx_ + rx, ChunkBiomes::kMinQuartY + i, baseQz_ + rz));
                    }
                }
            }
            present_ = {};
            if (src.isFixed()) {
                const BiomeId b = src.noiseBiome(ctx, 0, 0, 0);
                present_[b >> 6] |= std::uint64_t{1} << (b & 63);
            } else {
                for (const std::uint8_t b : cache_) present_[b >> 6] |= std::uint64_t{1} << (b & 63);
            }
        }
        [[nodiscard]] bool mayContain(const std::array<std::uint64_t, 4>& set) const override {
            for (std::size_t i = 0; i < 4; ++i) {
                if (set[i] & present_[i]) return true;
            }
            return false;
        }
        BiomeId biomeAt(int x, int y, int z) override {
            if (src_->isFixed()) return src_->noiseBiome(*ctx_, 0, 0, 0);
            const BiomeSource::QuartPos q = src_->zoom(x, y, z);
            const int qy = std::clamp(q.y, ChunkBiomes::kMinQuartY, ChunkBiomes::kMinQuartY + kQ - 1);
            const int rx = q.x - baseQx_, rz = q.z - baseQz_;
            if (rx < 0 || rx >= kW || rz < 0 || rz >= kW) return src_->noiseBiome(*ctx_, q.x, qy, q.z);
            std::uint8_t& e = cache_[static_cast<std::size_t>((rz * kW + rx) * kQ + (qy - ChunkBiomes::kMinQuartY))];
            if (e == 0xFF) e = static_cast<std::uint8_t>(src_->noiseBiome(*ctx_, q.x, qy, q.z));
            return e;
        }

    private:
        const BiomeSource* src_ = nullptr;
        BiomeSource::Context* ctx_ = nullptr;
        int baseQx_ = 0, baseQz_ = 0;
        std::array<std::uint64_t, 4> present_{};
        std::array<std::uint8_t, static_cast<std::size_t>(kW) * kW * kQ> cache_{};
    };

    explicit Workspace(const OverworldGenerator& g)
        : generatorId(g.id_), aquifer(*g.aquiferShared_, g.options_.trackFluidUpdates), biomeCtx(*g.biomeSource_) {
        const NoiseSettings& s = g.router_->settings();
        blocks.assign(static_cast<std::size_t>(s.height) * 256u, Block::Air);
    }

    std::uint64_t generatorId;
    std::vector<Block> blocks;                    // index (y - minY) + height * (x + 16 * z)
    std::array<std::int16_t, 256> worldSurface{};  // live WORLD_SURFACE_WG
    std::array<float, 256> chunkSurface{};
    Aquifer aquifer;
    MaterialSystem::Workspace material;
    BiomeSource::Context biomeCtx;
    ChunkBiomes biomes;
    Lookup lookup;
    SectionBuilder sectionBuilder;
    std::vector<std::uint32_t> fluidUpdates;
};

OverworldGenerator::OverworldGenerator() = default;
OverworldGenerator::~OverworldGenerator() {
    const long chunks = g_profile.chunks.load();
    if (!g_profile.enabled || chunks == 0) return;
    double total = 0.0;
    for (const auto& n : g_profile.nanos) total += static_cast<double>(n.load()) / 1.0e6;
    std::fprintf(stderr, "[worldgen profile] %ld chunks, %.2f ms each:", chunks, total / static_cast<double>(chunks));
    for (int i = 0; i < kProfileStages; ++i) std::fprintf(stderr, " %s %.2f", kProfileNames[i], static_cast<double>(g_profile.nanos[i].load()) / 1.0e6 / static_cast<double>(chunks));
    std::fputs("\n", stderr);
}

OverworldGenerator::Workspace& OverworldGenerator::workspace() const {
    thread_local std::vector<std::unique_ptr<Workspace>> spaces;
    for (const auto& w : spaces) {
        if (w->generatorId == id_) return *w;
    }
    if (spaces.size() >= 3) spaces.erase(spaces.begin());
    spaces.push_back(std::make_unique<Workspace>(*this));
    return *spaces.back();
}

// ===============================================================================================================
std::unique_ptr<OverworldGenerator> OverworldGenerator::create(const std::filesystem::path& gameDataDir, const BlockRegistry& registry, std::int64_t seed,
                                                               std::string& error, const GeneratorOptions& options) {
    std::unique_ptr<OverworldGenerator> g(new OverworldGenerator());
    g->seed_ = seed;
    g->id_ = g_nextGeneratorId.fetch_add(1);
    g->options_ = options;
    g->registry_ = &registry;
    g->worldgenDir_ = resolveWorldgenDir(gameDataDir);
    const std::filesystem::path& wg = g->worldgenDir_;

    // The noise settings: which rule tree, which spawn target.
    JsonValue settingsJson;
    if (!parseJsonFile(wg / "noise_settings" / (stripNamespace(options.noiseSettings) + ".json"), settingsJson, error)) return nullptr;
    std::string ruleName = "minecraft:overworld";
    if (const JsonValue* mr = settingsJson.find("material_rule"); mr && mr->isString()) ruleName = mr->text;

    std::vector<std::string> densityRefs;
    if (!MaterialSystem::collectDensityRefs(wg, ruleName, densityRefs, error)) return nullptr;
    g->router_ = NoiseRouter::create(wg, seed, error, options.noiseSettings, densityRefs);
    if (!g->router_) return nullptr;
    const NoiseSettings& ns = g->router_->settings();
    if (ns.minY != kMinY || ns.height != kWorldHeight) {
        error = "only worlds with min_y -64 and height 384 are supported";
        return nullptr;
    }

    g->biomeRegistry_ = BiomeRegistry::load(wg / "biome", error);
    if (!g->biomeRegistry_) return nullptr;
    if (!options.singleBiome.empty()) {
        const BiomeId id = g->biomeRegistry_->find(options.singleBiome);
        if (id == kNoBiome) {
            error = "unknown single biome " + options.singleBiome;
            return nullptr;
        }
        g->biomeSource_ = BiomeSource::createFixed(g->biomeRegistry_, id, seed);
    } else {
        g->biomeSource_ = BiomeSource::create(findParameterFile(wg, options.biomePreset), g->biomeRegistry_, *g->router_, seed, error);
        if (!g->biomeSource_) return nullptr;
    }

    g->facts_ = BlockFacts(registry);
    const auto block = [&](const std::string& name, Block& out) {
        const auto b = registry.parse(name);
        if (!b) {
            error = "the block registry does not know " + name;
            return false;
        }
        out = *b;
        return true;
    };
    if (!block(ns.defaultBlock, g->stone_) || !block(ns.defaultFluid, g->water_) || !block("minecraft:lava", g->lava_) || !block("minecraft:air", g->air_)) return nullptr;
    if (ns.defaultFluid != "minecraft:water") {
        error = "only water is supported as the default fluid";
        return nullptr;
    }

    g->aquiferShared_ = AquiferShared::create(*g->router_, error);
    if (!g->aquiferShared_) return nullptr;

    MaterialSystem::Params mp;
    mp.worldgenDir = wg;
    mp.ruleName = ruleName;
    mp.router = g->router_.get();
    mp.biomes = g->biomeRegistry_;
    mp.blocks = &registry;
    mp.facts = &g->facts_;
    g->material_ = MaterialSystem::create(mp, error);
    if (!g->material_) return nullptr;

    // spawn_target: [{ "<router function>": [min, max], ... }] -> climate parameter points
    if (const JsonValue* targets = settingsJson.find("spawn_target"); targets && targets->isArray()) {
        for (const JsonValue& t : targets->items) {
            climate::ParameterPoint p;
            for (auto& s : p.space) s = {climate::quantize(-2.0f), climate::quantize(2.0f)};
            p.space[6] = {0, 0};
            for (const auto& [key, value] : t.members) {
                const std::size_t slash = key.rfind('/');
                const std::string name = slash == std::string::npos ? key : key.substr(slash + 1);
                int axis = -1;
                if (name == "temperature") axis = 0;
                else if (name == "vegetation" || name == "humidity") axis = 1;
                else if (name == "continents") axis = 2;
                else if (name == "erosion") axis = 3;
                else if (name == "depth") axis = 4;
                else if (name == "ridges") axis = 5;
                if (axis < 0) continue;
                float a = 0.0f, b = 0.0f;
                if (value.isNumber() && value.toFloat(a)) p.space[axis] = {climate::quantize(a), climate::quantize(a)};
                else if (value.isArray() && value.items.size() == 2 && value.items[0].toFloat(a) && value.items[1].toFloat(b)) p.space[axis] = {climate::quantize(a), climate::quantize(b)};
            }
            g->spawnTargets_.push_back(p);
        }
    }
    return g;
}

// ===============================================================================================================
void OverworldGenerator::runStages(Workspace& w, int cx, int cz, Stage upTo) const {
    const NoiseSettings& st = router_->settings();
    const int minY = st.minY, height = st.height;

    // Biomes of the chunk (what the game stores), and the chunk-local lookup the surface rules read through.
    Lap lap;
    biomeSource_->fillChunk(w.biomeCtx, cx, cz, w.biomes);
    w.lookup.begin(*biomeSource_, w.biomeCtx, cx, cz, w.biomes);
    lap.to(0);

    // NoiseBasedChunkGenerator.doFill: z outermost, x, then y from the top; the aquifer decides everything that is not solid.
    const float* density = router_->finalDensityVolumeScratch(cx, cz);
    lap.to(1);
    w.aquifer.begin(cx, cz);
    w.fluidUpdates.clear();
    const bool track = options_.trackFluidUpdates;
    Block* blocks = w.blocks.data();
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const std::size_t off = static_cast<std::size_t>(height) * (static_cast<std::size_t>(x) + 16u * static_cast<std::size_t>(z));
            Block* col = blocks + off;
            const float* dcol = density + off;
            const int wx = cx * 16 + x, wz = cz * 16 + z;
            int first = minY;  // first available y of WORLD_SURFACE_WG
            bool seen = false;
            for (int yi = height - 1; yi >= 0; --yi) {
                const int y = minY + yi;
                const float d = dcol[yi];
                Block b;
                if (d > 0.0f) {
                    b = stone_;
                } else {
                    switch (w.aquifer.computeSubstance(wx, y, wz, static_cast<double>(d))) {
                        case Aquifer::Substance::Default: b = stone_; break;
                        case Aquifer::Substance::Water: b = water_; break;
                        case Aquifer::Substance::Lava: b = lava_; break;
                        case Aquifer::Substance::Air: b = air_; break;
                        default: b = air_; break;
                    }
                    if (track && b != air_ && w.aquifer.shouldScheduleFluidUpdate()) {
                        w.fluidUpdates.push_back(static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(z) << 4) | (static_cast<std::uint32_t>(yi) << 8));
                    }
                }
                col[yi] = b;
                if (!seen && b != air_) {
                    first = y + 1;
                    seen = true;
                }
            }
            w.worldSurface[static_cast<std::size_t>(x + 16 * z)] = static_cast<std::int16_t>(first);
        }
    }
    lap.to(2);
    if (upTo == Stage::Fill) return;

    router_->chunkSurfaceLevelVolume(cx, cz, w.chunkSurface.data());
    lap.to(3);
    SurfaceTarget target;
    target.chunkX = cx;
    target.chunkZ = cz;
    target.blocks = blocks;
    target.worldSurface = w.worldSurface.data();
    target.chunkSurfaceLevel = w.chunkSurface.data();
    target.biomes = &w.lookup;
    material_->buildSurface(w.material, target);
    lap.to(4);
}

void OverworldGenerator::buildChunk(Workspace& w, Chunk& chunk, ChunkExtras* extras) const {
    const int height = router_->settings().height;
    const Block* blocks = w.blocks.data();
    auto& raw = w.sectionBuilder.raw();
    for (int s = 0; s < kSectionCount; ++s) {
        const Block first = blocks[static_cast<std::size_t>(s) * 16u];
        bool uniform = true;
        for (std::size_t c = 0; c < 256 && uniform; ++c) {
            const Block* p = blocks + c * static_cast<std::size_t>(height) + static_cast<std::size_t>(s) * 16u;
            for (int ly = 0; ly < 16; ++ly) {
                if (p[ly] != first) {
                    uniform = false;
                    break;
                }
            }
        }
        if (uniform) {
            chunk.sections[static_cast<std::size_t>(s)] = first == Block::Air ? nullptr : uniformSection(first);
            continue;
        }
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const Block* p = blocks + static_cast<std::size_t>(height) * (static_cast<std::size_t>(x) + 16u * static_cast<std::size_t>(z)) + static_cast<std::size_t>(s) * 16u;
                for (int ly = 0; ly < 16; ++ly) raw[static_cast<std::size_t>((ly << 8) | (z << 4) | x)] = p[ly];
            }
        }
        chunk.sections[static_cast<std::size_t>(s)] = w.sectionBuilder.build();
    }

    if (!extras) return;
    extras->biomes = w.biomes;
    extras->fluidUpdates = w.fluidUpdates;
    const int minY = router_->settings().minY;
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const Block* col = blocks + static_cast<std::size_t>(height) * (static_cast<std::size_t>(x) + 16u * static_cast<std::size_t>(z));
            int ws = minY, of = minY, mb = minY;
            bool haveWs = false, haveOf = false, haveMb = false;
            for (int yi = height - 1; yi >= 0 && !(haveWs && haveOf && haveMb); --yi) {
                const Block b = col[yi];
                const std::uint8_t f = facts_.flags(b);
                if (!haveWs && !(f & BlockFacts::kAir)) {
                    ws = minY + yi + 1;
                    haveWs = true;
                }
                if (!haveOf && (f & BlockFacts::kSolidMotion)) {
                    of = minY + yi + 1;
                    haveOf = true;
                }
                if (!haveMb && (f & BlockFacts::kMotionBlocking)) {
                    mb = minY + yi + 1;
                    haveMb = true;
                }
            }
            const auto i = static_cast<std::size_t>(x + 16 * z);
            extras->heightmaps.worldSurface[i] = static_cast<std::int16_t>(ws);
            extras->heightmaps.oceanFloor[i] = static_cast<std::int16_t>(of);
            extras->heightmaps.motionBlocking[i] = static_cast<std::int16_t>(mb);
        }
    }
}

std::shared_ptr<Chunk> OverworldGenerator::generate(int chunkX, int chunkZ, ChunkExtras& extras) const {
    Workspace& w = workspace();
    runStages(w, chunkX, chunkZ, Stage::Surface);
    Lap lap;
    if (stage_) stage_(w.blocks, chunkX, chunkZ);
    lap.to(5);
    auto chunk = std::make_shared<Chunk>();
    buildChunk(w, *chunk, &extras);
    lap.to(6);
    ++g_profile.chunks;
    return chunk;
}

std::shared_ptr<Chunk> OverworldGenerator::generate(int chunkX, int chunkZ) const {
    Workspace& w = workspace();
    runStages(w, chunkX, chunkZ, Stage::Surface);
    if (stage_) stage_(w.blocks, chunkX, chunkZ);
    auto chunk = std::make_shared<Chunk>();
    buildChunk(w, *chunk, nullptr);
    return chunk;
}

void OverworldGenerator::generateDense(int chunkX, int chunkZ, Stage upTo, DenseChunk& out) const {
    Workspace& w = workspace();
    runStages(w, chunkX, chunkZ, upTo);
    out.blocks = w.blocks;
    out.worldSurface = w.worldSurface;
    out.biomes = w.biomes;
}

// ===============================================================================================================
int OverworldGenerator::surfaceHeightAt(int x, int z) const {
    const NoiseSettings& s = router_->settings();
    const int maxY = s.minY + s.height - 1;
    int start = maxY;
    if (router_->hasAquifers()) start = std::clamp(javaFloorF(router_->aquiferSurfaceLevel().compute(x, 0, z)) + 24, s.minY, maxY);
    for (int y = start; y >= s.minY; --y) {
        if (router_->finalDensity().compute(x, y, z) > 0.0f) return y;
    }
    return s.minY;
}

// ---------------------------------------------------------------------------------------------------------------
// The default spawn (UNVERIFIED port, from memory of the game's logic; the research spec does not cover it):
//   1. Climate.findSpawnPosition: start at (0, 0), then two radial searches (radius 2048 in steps of 512, then radius
//      512 in steps of 32) around the best position so far, minimising
//      fitness = distance penalty + best fitness of the climate at that position against the settings' spawn_target
//      (continentalness >= -0.11, weirdness away from the river valleys, ...). Single-biome worlds start at (0, 0).
//   2. A square spiral of up to 11 x 11 chunks around the chunk of that position; in the first chunk that has one,
//      the first column (x outer, z inner) with dry solid ground (not under water, a full block to stand on) is the spawn.
OverworldGenerator::Spawn OverworldGenerator::findSpawn() const {
    int bestX = 0, bestZ = 0;
    if (!biomeSource_->isFixed() && !spawnTargets_.empty()) {
        BiomeSource::Context& ctx = biomeSource_->threadContext();
        const auto fitnessAt = [&](int x, int z) -> long long {
            const double d0 = 2500.0 * 2500.0;
            const double px = static_cast<double>(static_cast<long long>(x) * x + static_cast<long long>(z) * z);
            const auto penalty = static_cast<long long>(static_cast<double>(1.0E8f) * std::pow(px / d0, 2.0));
            climate::TargetPoint t = biomeSource_->sampleClimate(ctx, x >> 2, 0, z >> 2);
            t.depth = 0;
            long long best = INT64_MAX;
            for (const climate::ParameterPoint& p : spawnTargets_) best = std::min<long long>(best, climate::fitness(p, t));
            return penalty + best;
        };
        long long bestFitness = fitnessAt(0, 0);
        const auto radial = [&](float maxRadius, float increment) {
            float angle = 0.0f;
            float radius = increment;
            const int cx = bestX, cz = bestZ;
            while (radius <= maxRadius) {
                const int x = cx + static_cast<int>(std::sin(static_cast<double>(angle)) * static_cast<double>(radius));
                const int z = cz + static_cast<int>(std::cos(static_cast<double>(angle)) * static_cast<double>(radius));
                const long long f = fitnessAt(x, z);
                if (f < bestFitness) {
                    bestFitness = f;
                    bestX = x;
                    bestZ = z;
                }
                angle += increment / radius;
                if (angle > 6.2831855f) {
                    angle = 0.0f;
                    radius += increment;
                }
            }
        };
        radial(2048.0f, 512.0f);
        radial(512.0f, 32.0f);
    }

    const int minY = router_->settings().minY;
    const int startChunkX = bestX >> 4, startChunkZ = bestZ >> 4;
    int j = 0, k = 0, l = 0, i1 = -1;
    for (int step = 0; step < 11 * 11; ++step) {
        if (j >= -5 && j <= 5 && k >= -5 && k <= 5) {
            ChunkExtras extras;
            const std::shared_ptr<Chunk> chunk = generate(startChunkX + j, startChunkZ + k, extras);
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const auto idx = static_cast<std::size_t>(x + 16 * z);
                    const int top = extras.heightmaps.motionBlocking[idx] - 1;
                    if (top < minY) continue;
                    const int surface = extras.heightmaps.worldSurface[idx] - 1;
                    if (surface <= top && surface > extras.heightmaps.oceanFloor[idx] - 1) continue;  // fluid on top
                    for (int y = top + 1; y >= minY; --y) {
                        const Block b = chunk->at(x, y, z);
                        if (facts_.isFluid(b)) break;
                        if (registry_->state(b).has(kStateFullCollision)) {
                            return {static_cast<double>((startChunkX + j) * 16 + x) + 0.5, static_cast<double>(y + 1), static_cast<double>((startChunkZ + k) * 16 + z) + 0.5};
                        }
                    }
                }
            }
        }
        if (j == k || (j < 0 && j == -k) || (j > 0 && j == 1 - k)) {
            const int t = l;
            l = -i1;
            i1 = t;
        }
        j += l;
        k += i1;
    }
    // Nothing dry within 5 chunks: stand on the terrain at the climate position, or at sea level.
    const int y = std::max(surfaceHeightAt(bestX, bestZ) + 1, seaLevel());
    return {static_cast<double>(bestX) + 0.5, static_cast<double>(y), static_cast<double>(bestZ) + 0.5};
}

std::size_t OverworldGenerator::sharedMemoryBytes() const {
    std::size_t bytes = sizeof(*this) + facts_.size();
    bytes += biomeSource_->tree().memoryBytes();
    bytes += biomeSource_->parameters().points.capacity() * sizeof(climate::ParameterPoint) + biomeSource_->parameters().biomes.capacity() * sizeof(BiomeId);
    bytes += sizeof(AquiferShared) + aquiferShared_->surfaceCache.memoryBytes();
    return bytes;
}

std::size_t OverworldGenerator::perThreadScratchBytes() const {
    // the dense block array, the density volume the router keeps per thread, the ore-vein volumes, the biome window
    const NoiseSettings& s = router_->settings();
    return static_cast<std::size_t>(s.height) * 256u * (sizeof(Block) + sizeof(float)) + material_->scratchBytes() + sizeof(Workspace);
}

const char* worldPresetFor(TerrainStyle style) {
    switch (style) {
        case TerrainStyle::Flat: return nullptr;
        case TerrainStyle::Hills: return "minecraft:normal";
        case TerrainStyle::Amplified: return "minecraft:amplified";
    }
    return nullptr;
}

const char* worldPresetFor(NewWorldType type) {
    switch (type) {
        case NewWorldType::Default: return "minecraft:normal";
        case NewWorldType::Superflat: return nullptr;
        case NewWorldType::LargeBiomes: return "minecraft:large_biomes";
        case NewWorldType::Amplified: return "minecraft:amplified";
        case NewWorldType::SingleBiome: return "minecraft:single_biome_surface";
    }
    return nullptr;
}

}  // namespace falcon::worldgen
