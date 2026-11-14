#pragma once

// The 26.3 material system (net.minecraft.world.level.levelgen.material.MaterialSystem): everything that happens to
// the solid blocks after the noise fill, driven by the data-defined `material_rule` of the noise settings:
//   * bedrock floor and deepslate (vertical gradients), ore veins (copper / iron, filler, raw ore blocks)
//   * the surface rules: grass, sand, gravel, terracotta bands, snow, ice, mud, ... per biome
//   * clay bands (`bandlands`), eroded-badlands pillars and frozen-ocean icebergs
//
// The rule tree (data/minecraft/worldgen/material_rule/**.json, material_condition/*.json) is read at runtime and
// compiled into flat arrays; the column loop follows MaterialSystem.buildSurface (x outer, z inner, live
// WORLD_SURFACE_WG heightmap, stone-depth look-ahead).

#include "falcon/world/block_registry.hpp"
#include "falcon/worldgen/biome.hpp"
#include "falcon/worldgen/noise_router.hpp"
#include "falcon/worldgen/random.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon::worldgen {

// Facts about block states that generation needs, resolved once from a BlockRegistry.
class BlockFacts {
public:
    enum Flag : std::uint8_t { kAir = 1, kFluid = 2, kMotionBlocking = 4, kSolidMotion = 8 };
    BlockFacts() = default;
    explicit BlockFacts(const BlockRegistry& registry);
    [[nodiscard]] bool isAir(Block b) const { return (flags_[idx(b)] & kAir) != 0; }
    [[nodiscard]] bool isFluid(Block b) const { return (flags_[idx(b)] & kFluid) != 0; }     // water or lava (non-empty fluid state)
    [[nodiscard]] bool blocksMotion(Block b) const { return (flags_[idx(b)] & kSolidMotion) != 0; }
    [[nodiscard]] bool isMotionBlocking(Block b) const { return (flags_[idx(b)] & kMotionBlocking) != 0; }
    [[nodiscard]] std::uint8_t flags(Block b) const { return flags_[idx(b)]; }
    [[nodiscard]] std::size_t size() const { return flags_.size(); }

private:
    [[nodiscard]] std::size_t idx(Block b) const {
        const auto i = static_cast<std::size_t>(stateId(b));
        return i < flags_.size() ? i : 0;
    }
    std::vector<std::uint8_t> flags_;
};

// The biome of a block position for the surface rules (BiomeManager.getBiome through a chunk-local cache).
class BiomeLookup {
public:
    virtual ~BiomeLookup() = default;
    [[nodiscard]] virtual BiomeId biomeAt(int x, int y, int z) = 0;
    // False only when none of the biomes in `set` (a bitset over biome ids) can be returned by biomeAt for any position of
    // the chunk being built: biome conditions on such a set are then constant false and need no zoom. Optional optimisation.
    [[nodiscard]] virtual bool mayContain(const std::array<std::uint64_t, 4>& set) const {
        (void)set;
        return true;
    }
};

// What buildSurface works on: one chunk's dense block array and live heightmap.
struct SurfaceTarget {
    int chunkX = 0, chunkZ = 0;
    Block* blocks = nullptr;                    // index (y - minY) + height * (x + 16 * z)
    std::int16_t* worldSurface = nullptr;       // live WORLD_SURFACE_WG: y of the highest non-air block + 1, per column (x + 16 * z)
    const float* chunkSurfaceLevel = nullptr;   // router chunk_surface_level over the chunk, 16 x 16, index x + 16 * z
    BiomeLookup* biomes = nullptr;
};

class MaterialSystem {
public:
    struct Params {
        std::filesystem::path worldgenDir;
        std::string ruleName = "minecraft:overworld";  // the noise settings' material_rule
        const NoiseRouter* router = nullptr;
        std::shared_ptr<const BiomeRegistry> biomes;
        const BlockRegistry* blocks = nullptr;
        const BlockFacts* facts = nullptr;
    };

    // The density functions the rule tree's ore veins use ("minecraft:overworld/ore_vein/copper_density" ...). They must be
    // passed to NoiseRouter::create as extra functions before the system can be built.
    [[nodiscard]] static bool collectDensityRefs(const std::filesystem::path& worldgenDir, const std::string& ruleName, std::vector<std::string>& refs,
                                                  std::string& error);
    [[nodiscard]] static std::unique_ptr<MaterialSystem> create(const Params& params, std::string& error);

    // Per-thread scratch (ore-vein volumes, condition caches). Create one per thread that builds surfaces.
    class Workspace {
    public:
        Workspace() = default;
        struct Impl;  // internal
    private:
        friend class MaterialSystem;
        std::shared_ptr<Impl> impl_;
    };

    // MaterialSystem.buildSurface for one chunk (after the noise fill).
    void buildSurface(Workspace& ws, const SurfaceTarget& target) const;

    // ---- pieces, exposed for tests ---------------------------------------------------------------------------------
    [[nodiscard]] Block clayBand(int x, int y, int z) const;                       // MaterialSystem.getBand
    [[nodiscard]] const std::array<Block, 192>& clayBands() const;
    [[nodiscard]] int surfaceDepth(int x, int z) const;                            // getSurfaceDepth
    [[nodiscard]] double surfaceSecondary(int x, int z) const;                     // getSurfaceSecondary
    [[nodiscard]] int minY() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] std::size_t ruleNodeCount() const;
    [[nodiscard]] std::size_t conditionNodeCount() const;
    // Bytes of scratch memory one Workspace holds for the ore-vein volumes.
    [[nodiscard]] std::size_t scratchBytes() const;
    // Biome.getTemperature at (x, seaLevel, z) for a biome: base temperature through its temperature modifier.
    [[nodiscard]] float biomeTemperature(BiomeId biome, int x, int y, int z) const;

    ~MaterialSystem();
    struct Impl;  // internal

private:
    MaterialSystem();
    std::unique_ptr<Impl> impl_;
};

}  // namespace falcon::worldgen
