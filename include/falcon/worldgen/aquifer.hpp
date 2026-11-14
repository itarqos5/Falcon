#pragma once

// The noise-based aquifer (Aquifer$NoiseBasedAquifer, 26.3) plugged into NoiseBasedChunkGenerator.doFill:
// decides, for every block whose density is <= 0, whether it is stone (a barrier wall), air, water or lava.
//
//   AquiferShared  everything that is the same for all chunks of a world: the six density functions of the noise
//                  settings' "aquifers" block, the positional random factory "minecraft:aquifer", sea level, a lock-free
//                  cache of preliminary surface levels shared by all threads
//   Aquifer        one per working thread; begin(chunkX, chunkZ) starts a chunk (resets its small per-chunk caches and
//                  computes skipSamplingAboveY), computeSubstance() is called in doFill order
//
// The algorithm is a pure function of the block position (and the chunk only through skipSamplingAboveY, which
// only decides where the exact aquifer logic can be skipped), so results do not depend on generation order.

#include "falcon/worldgen/density.hpp"
#include "falcon/worldgen/noise_router.hpp"
#include "falcon/worldgen/random.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace falcon::worldgen {

enum class Fluid : std::uint8_t { Air = 0, Water = 1, Lava = 2 };

struct FluidStatus {
    std::int32_t level = 0;
    Fluid type = Fluid::Air;
    [[nodiscard]] Fluid at(int y) const { return y < level ? type : Fluid::Air; }  // FluidStatus.at
    bool operator==(const FluidStatus&) const = default;
};

// A lock-free map (qx, qz) -> int for values that are pure functions of the key (the game recomputes them per chunk;
// sharing them across chunks and threads saves a lot of work). A racing writer can only store the same value; a
// collision just overwrites.
class QuartColumnCache {
public:
    explicit QuartColumnCache(unsigned log2Size = 17);
    [[nodiscard]] bool find(int qx, int qz, int& value) const;
    void store(int qx, int qz, int value);
    [[nodiscard]] std::size_t memoryBytes() const { return (mask_ + 1) * sizeof(std::atomic<std::uint64_t>); }

private:
    [[nodiscard]] static std::uint64_t keyOf(int qx, int qz);
    std::unique_ptr<std::atomic<std::uint64_t>[]> table_;
    std::size_t mask_;
};

struct AquiferShared {
    // Functions from noise_settings "aquifers" (the router owns the graph). `valid` is false for settings without them.
    DensityFunction barrier, floodedness, spread, lava, exclusion, surfaceLevel;
    XoroshiroPositionalRandomFactory random;  // RandomState.getOrCreateRandomFactory("minecraft:aquifer")
    int seaLevel = 63;
    Fluid defaultFluid = Fluid::Water;
    int minY = -64;
    int height = 384;
    bool valid = false;
    mutable QuartColumnCache surfaceCache;

    // Reads everything from the router (its settings and aquifer functions) and the random state.
    [[nodiscard]] static std::shared_ptr<AquiferShared> create(const NoiseRouter& router, std::string& error);

    // NoiseBasedChunkGenerator.createFluidPicker: lava below y -54 (and below sea level), else the default fluid.
    [[nodiscard]] FluidStatus globalFluid(int y) const {
        constexpr int kLavaLevel = -54;
        return y < (seaLevel < kLavaLevel ? seaLevel : kLavaLevel) ? FluidStatus{kLavaLevel, Fluid::Lava} : FluidStatus{seaLevel, defaultFluid};
    }
    // The chunk-independent surface level of the aquifer: floor(surface_level(quart-snapped x, 0, quart-snapped z)).
    [[nodiscard]] int surfaceLevelAt(int x, int z) const;
};

class Aquifer {
public:
    enum class Substance : std::uint8_t {
        Default,  // "null": the caller places the settings' default block (stone)
        Air,
        Water,
        Lava,
    };

    // `trackFluidUpdates`: also compute shouldScheduleFluidUpdate() (costs extra fluid-status lookups).
    explicit Aquifer(const AquiferShared& shared, bool trackFluidUpdates = false);

    // Starts a chunk: caches are reset, skipSamplingAboveY is computed (point-samples the surface level around the chunk).
    void begin(int chunkX, int chunkZ);
    [[nodiscard]] int skipSamplingAboveY() const { return skipSamplingAboveY_; }

    // NoiseBasedAquifer.computeSubstance(x, y, z, density), with the density widened from the float the fill produced.
    [[nodiscard]] Substance computeSubstance(int x, int y, int z, double density);
    // After computeSubstance: whether the fluid block placed there is to be ticked once after generation.
    [[nodiscard]] bool shouldScheduleFluidUpdate() const { return shouldSchedule_; }

    // For tests: the status of the aquifer cell with the given grid coordinates, and its centre.
    [[nodiscard]] FluidStatus cellStatus(int gridX, int gridY, int gridZ);
    struct Center {
        int x, y, z;
    };
    [[nodiscard]] Center cellCenter(int gridX, int gridY, int gridZ);
    [[nodiscard]] FluidStatus computeFluidAt(int x, int y, int z) { return computeFluid(x, y, z); }
    [[nodiscard]] int surfaceLevelAt(int x, int z) const { return shared_.surfaceLevelAt(x, z); }

    static constexpr int kWayBelowMinY = -32512;  // DimensionType.WAY_BELOW_MIN_Y

private:
    struct Cell {
        std::int32_t x = 0, y = 0, z = 0;
        std::int32_t level = 0;
        Fluid type = Fluid::Air;
        std::uint8_t have = 0;  // 1: centre known, 2: status known
    };

    [[nodiscard]] std::size_t cellIndex(int gx, int gy, int gz) const;
    [[nodiscard]] Cell& cell(int gx, int gy, int gz);
    [[nodiscard]] FluidStatus status(std::size_t index, int gx, int gy, int gz);
    [[nodiscard]] FluidStatus computeFluid(int x, int y, int z);
    [[nodiscard]] int computeSurfaceLevel(int x, int y, int z, const FluidStatus& global, int minSurface, bool flag);
    [[nodiscard]] int computeRandomizedFluidSurfaceLevel(int x, int y, int z, int minSurface);
    [[nodiscard]] Fluid computeFluidType(int x, int y, int z, const FluidStatus& global, int level);
    [[nodiscard]] double calculatePressure(int x, int y, int z, double& barrier, const FluidStatus& a, const FluidStatus& b);

    const AquiferShared& shared_;
    bool track_;
    bool shouldSchedule_ = false;
    int skipSamplingAboveY_ = 0;
    int minGridX_ = 0, minGridY_ = 0, minGridZ_ = 0;
    int sizeX_ = 0, sizeY_ = 0, sizeZ_ = 0;
    std::vector<Cell> cells_;
};

}  // namespace falcon::worldgen
