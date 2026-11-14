#pragma once

// The Overworld terrain generator (Minecraft 26.3, bit-exact port): biomes, density-function terrain, aquifers, ore
// veins, bedrock/deepslate, surface rules, clay bands, pillars and icebergs. No caves, features, structures or
// carvers yet (those are later stages); what it makes is the "noise" + "surface" status of a proto chunk.
//
//   std::string error;
//   auto gen = OverworldGenerator::create(gameDataDir, blockRegistry, seed, error);   // loads everything once
//   std::shared_ptr<Chunk> chunk = gen->generate(cx, cz);                              // thread-safe
//
// Everything is read from the game's JSON at runtime (noise settings with their material rule tree, biomes, the
// multi-noise parameter list, density functions, noise parameters). `gameDataDir` is the folder that holds
// data/minecraft/worldgen and biome-parameters, i.e. the parent of the assets root.
//
// Threading: generate() and the query functions are const and thread-safe with no locks in the hot path (per-thread
// scratch memory, a lock-free cache of surface levels shared by the threads).

#include "apex/world/block_registry.hpp"
#include "apex/world/chunk.hpp"
#include "apex/world/new_world.hpp"
#include "apex/world/terrain.hpp"
#include "apex/worldgen/aquifer.hpp"
#include "apex/worldgen/biome.hpp"
#include "apex/worldgen/material.hpp"
#include "apex/worldgen/noise_router.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace apex::worldgen {

// Which generator settings a world uses: the Default / Large Biomes / Amplified / Single Biome presets differ only
// in these (they come from data/minecraft/worldgen/world_preset/*.json).
struct GeneratorOptions {
    std::string noiseSettings = "minecraft:overworld";           // noise_settings/<name>.json (amplified, large_biomes ...)
    std::string biomePreset = "minecraft:overworld";             // the multi-noise parameter list (biome-parameters/<name>.json)
    std::string singleBiome;                                     // non-empty: this biome everywhere ("minecraft:fixed" source)
    bool trackFluidUpdates = false;                              // also report the fluid blocks the aquifer wants ticked
};

// The world preset behind a client terrain style or a Create World "World Type" button value ("minecraft:normal",
// "minecraft:large_biomes", "minecraft:amplified", "minecraft:single_biome_surface"); nullptr for the worlds this
// generator does not make (Superflat / the client's own Flat terrain). Feed it to loadWorldPreset().
[[nodiscard]] const char* worldPresetFor(TerrainStyle style);
[[nodiscard]] const char* worldPresetFor(NewWorldType type);

// Reads the overworld dimension of a world preset ("minecraft:normal", "minecraft:amplified", "minecraft:large_biomes",
// "minecraft:single_biome_surface") into GeneratorOptions. False and `error` when the preset has no noise overworld.
[[nodiscard]] bool loadWorldPreset(const std::filesystem::path& gameDataDir, std::string_view preset, GeneratorOptions& out, std::string& error);

// WORLD_SURFACE / OCEAN_FLOOR / MOTION_BLOCKING of a chunk. Each value is the y of the first free block above the
// highest block that counts (Heightmap.getFirstAvailable), so the top block is value - 1; a column without any is minY
// (-64). Index x + 16 * z with chunk-local x, z.
//   WORLD_SURFACE   any block that is not air
//   OCEAN_FLOOR     blocks that stop motion (full collision cubes)
//   MOTION_BLOCKING those, and fluids
struct ChunkHeightmaps {
    std::array<std::int16_t, 256> worldSurface{};
    std::array<std::int16_t, 256> oceanFloor{};
    std::array<std::int16_t, 256> motionBlocking{};
    [[nodiscard]] static constexpr int index(int x, int z) { return x + 16 * z; }
};

// What a generated chunk carries besides its blocks.
struct ChunkExtras {
    ChunkBiomes biomes;
    ChunkHeightmaps heightmaps;
    // Fluid blocks the aquifer wants ticked once (GeneratorOptions::trackFluidUpdates): x | z << 4 | (y - minY) << 8.
    std::vector<std::uint32_t> fluidUpdates;
};

// The raw block array, for tests and tools. Index (y - minY) + 384 * (x + 16 * z).
struct DenseChunk {
    std::vector<Block> blocks;
    std::array<std::int16_t, 256> worldSurface{};  // live WORLD_SURFACE_WG as the surface stage saw it
    ChunkBiomes biomes;
};

class OverworldGenerator {
public:
    // Loads the game data and builds the router, biome source, aquifer and material rules. nullptr and `error` on failure.
    // `registry` must outlive the generator.
    [[nodiscard]] static std::unique_ptr<OverworldGenerator> create(const std::filesystem::path& gameDataDir, const BlockRegistry& registry, std::int64_t seed,
                                                                     std::string& error, const GeneratorOptions& options = {});
    ~OverworldGenerator();
    OverworldGenerator(const OverworldGenerator&) = delete;
    OverworldGenerator& operator=(const OverworldGenerator&) = delete;

    // The chunk's blocks (24 sections; uniform ones are shared). Thread-safe.
    [[nodiscard]] std::shared_ptr<Chunk> generate(int chunkX, int chunkZ) const;
    // The same with the chunk's biomes, heightmaps and fluid updates.
    [[nodiscard]] std::shared_ptr<Chunk> generate(int chunkX, int chunkZ, ChunkExtras& extras) const;

    enum class Stage { Fill, Surface };  // Fill: the noise fill with aquifers only; Surface: everything
    void generateDense(int chunkX, int chunkZ, Stage upTo, DenseChunk& out) const;

    // ---- queries ---------------------------------------------------------------------------------------------------
    [[nodiscard]] const BiomeRegistry& biomes() const { return *biomeRegistry_; }
    [[nodiscard]] const BiomeSource& biomeSource() const { return *biomeSource_; }
    // BiomeManager.getBiome: the zoomed biome at a block position (any y; works without generating a chunk).
    [[nodiscard]] BiomeId biomeAt(int x, int y, int z) const { return biomeSource_->biomeAt(x, y, z); }
    [[nodiscard]] std::string_view biomeNameAt(int x, int y, int z) const { return biomeRegistry_->name(biomeAt(x, y, z)); }
    // The y of the highest solid terrain block of a column (an estimate, no chunk is generated: the density function is
    // point-sampled downward from the preliminary surface; it can differ from the real chunk by a block next to
    // caves). Fluids and surface rules are ignored; below sea level this is the sea floor.
    [[nodiscard]] int surfaceHeightAt(int x, int z) const;

    struct Spawn {
        double x, y, z;
    };
    // The default world spawn. See the .cpp: the climate search of Climate.findSpawnPosition over the noise settings'
    // spawn_target, then the first dry column of the spiral of chunks around it (UNVERIFIED port of the game's logic).
    [[nodiscard]] Spawn findSpawn() const;

    [[nodiscard]] std::int64_t seed() const { return seed_; }
    [[nodiscard]] const NoiseRouter& router() const { return *router_; }
    [[nodiscard]] const MaterialSystem& material() const { return *material_; }
    [[nodiscard]] const BlockFacts& blockFacts() const { return facts_; }
    [[nodiscard]] const BlockRegistry& blockRegistry() const { return *registry_; }
    [[nodiscard]] const GeneratorOptions& options() const { return options_; }
    [[nodiscard]] int seaLevel() const { return router_->settings().seaLevel; }
    // Bytes held by this instance's shared tables (not counting per-thread scratch or the chunks it made).
    [[nodiscard]] std::size_t sharedMemoryBytes() const;
    // Bytes of scratch memory one worker thread holds.
    [[nodiscard]] std::size_t perThreadScratchBytes() const;

    struct Workspace;  // internal per-thread scratch

private:
    OverworldGenerator();
    [[nodiscard]] Workspace& workspace() const;
    void runStages(Workspace& w, int chunkX, int chunkZ, Stage upTo) const;
    void buildChunk(Workspace& w, Chunk& chunk, ChunkExtras* extras) const;

    std::int64_t seed_ = 0;
    std::uint64_t id_ = 0;
    GeneratorOptions options_;
    std::filesystem::path worldgenDir_;
    const BlockRegistry* registry_ = nullptr;
    BlockFacts facts_;
    Block stone_{}, water_{}, lava_{}, air_{};
    std::unique_ptr<NoiseRouter> router_;
    std::shared_ptr<const BiomeRegistry> biomeRegistry_;
    std::unique_ptr<BiomeSource> biomeSource_;
    std::shared_ptr<AquiferShared> aquiferShared_;
    std::unique_ptr<MaterialSystem> material_;
    std::vector<climate::ParameterPoint> spawnTargets_;
};

}  // namespace apex::worldgen
