#pragma once

// NoiseRouter: the density functions of a noise-settings file (noise_settings/overworld.json) compiled for one world
// seed, plus the chunk-sized volume entry points the terrain fill uses.
//
//   auto router = NoiseRouter::create(worldgenDir, seed, error);
//   router->continents().compute(x, y, z)                        // climate functions, any block position
//   router->finalDensityVolume(chunkX, chunkZ, buffer)           // 16 x height x 16 floats, y fastest
//
// Everything here is immutable after create(): any number of threads may use one router.

#include "apex/worldgen/density.hpp"
#include "apex/worldgen/random_state.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace apex::worldgen {

struct NoiseSettings {
    int minY = -64;
    int height = 384;
    int seaLevel = 63;
    std::string defaultBlock = "minecraft:stone";
    std::string defaultFluid = "minecraft:water";
    bool legacyRandomSource = false;
};

// <game data dir>/data/minecraft/worldgen, where <game data dir> is the parent of the assets root.
[[nodiscard]] std::filesystem::path worldgenDirFromAssetsRoot(const std::filesystem::path& assetsRoot);

class NoiseRouter {
public:
    static constexpr int kChunkSize = 16;

    // Reads <worldgen>/noise_settings/<settings>.json and compiles its noise_router (and aquifers), plus the extra
    // density functions named in `extraFunctions` (files like "minecraft:overworld/sloped_cheese"; handy for debugging
    // and tests). nullptr and `error` on failure. Only the Xoroshiro (non-legacy) random source is supported.
    [[nodiscard]] static std::unique_ptr<NoiseRouter> create(const std::filesystem::path& worldgenDir, std::int64_t seed, std::string& error,
                                                              std::string_view settings = "minecraft:overworld",
                                                              const std::vector<std::string>& extraFunctions = {});

    [[nodiscard]] std::int64_t seed() const { return state_->seed(); }
    [[nodiscard]] const NoiseSettings& settings() const { return settings_; }
    [[nodiscard]] const std::shared_ptr<const RandomState>& randomState() const { return state_; }
    [[nodiscard]] const DensityGraph& graph() const { return *graph_; }

    // The router's fields (noise_router in the settings file).
    [[nodiscard]] const DensityFunction& temperature() const { return temperature_; }
    [[nodiscard]] const DensityFunction& vegetation() const { return vegetation_; }
    [[nodiscard]] const DensityFunction& continents() const { return continents_; }
    [[nodiscard]] const DensityFunction& erosion() const { return erosion_; }
    [[nodiscard]] const DensityFunction& depth() const { return depth_; }
    [[nodiscard]] const DensityFunction& ridges() const { return ridges_; }
    [[nodiscard]] const DensityFunction& chunkSurfaceLevel() const { return chunkSurfaceLevel_; }
    [[nodiscard]] const DensityFunction& finalDensity() const { return finalDensity_; }

    // The aquifer inputs (noise settings "aquifers"); !valid() handles when the settings have none.
    [[nodiscard]] bool hasAquifers() const { return aquiferBarrier_.valid(); }
    [[nodiscard]] const DensityFunction& aquiferBarrier() const { return aquiferBarrier_; }
    [[nodiscard]] const DensityFunction& aquiferFluidLevelFloodedness() const { return aquiferFloodedness_; }
    [[nodiscard]] const DensityFunction& aquiferFluidLevelSpread() const { return aquiferSpread_; }
    [[nodiscard]] const DensityFunction& aquiferLava() const { return aquiferLava_; }
    [[nodiscard]] const DensityFunction& aquiferExclusion() const { return aquiferExclusion_; }
    // preliminary surface level (find_top_surface, depends on x and z only): evaluate at (x, 0, z) or any y.
    [[nodiscard]] const DensityFunction& aquiferSurfaceLevel() const { return aquiferSurface_; }

    // One of the extraFunctions given to create() (an invalid handle for any other name).
    [[nodiscard]] DensityFunction function(std::string_view ref) const;

    // ---- chunk volumes ------------------------------------------------------------------------------------------
    // The chunk's block volume: 16 x height x 16 starting at (chunkX*16, minY, chunkZ*16).
    // Buffer index (y FASTEST, then x, then z):  (y - minY) + height * (x + 16 * z)   with x, z in 0..15 inside the chunk.
    [[nodiscard]] DensityVolume chunkVolume(int chunkX, int chunkZ) const;
    [[nodiscard]] std::size_t chunkVolumeSize() const { return static_cast<std::size_t>(kChunkSize) * static_cast<std::size_t>(settings_.height) * kChunkSize; }

    // final_density over a chunk: the exact values NoiseBasedChunkGenerator.doFill receives. `out` holds chunkVolumeSize() floats.
    void finalDensityVolume(int chunkX, int chunkZ, float* out) const;
    [[nodiscard]] std::vector<float> finalDensityVolume(int chunkX, int chunkZ) const;
    // Same, into a buffer owned by the calling thread (valid until that thread's next call). No allocation after the first.
    [[nodiscard]] const float* finalDensityVolumeScratch(int chunkX, int chunkZ) const;

    // chunk_surface_level over a chunk (the surface-rule input): 16 x 16 floats, index x + 16 * z.
    void chunkSurfaceLevelVolume(int chunkX, int chunkZ, float* out) const;

private:
    NoiseRouter() = default;

    NoiseSettings settings_;
    std::shared_ptr<const RandomState> state_;
    std::unique_ptr<DensityGraph> graph_;
    DensityFunction temperature_, vegetation_, continents_, erosion_, depth_, ridges_, chunkSurfaceLevel_, finalDensity_;
    DensityFunction aquiferBarrier_, aquiferFloodedness_, aquiferSpread_, aquiferLava_, aquiferExclusion_, aquiferSurface_;
    std::map<std::string, DensityFunction, std::less<>> extra_;
};

}  // namespace apex::worldgen
