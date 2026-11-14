#pragma once

// The simplex noise stack the game keeps for biome temperature (Biome.TEMPERATURE_NOISE, FROZEN_TEMPERATURE_NOISE,
// BIOME_INFO_NOISE): all of them world-seed independent, seeded from the legacy 48-bit LCG.
//
//   TEMPERATURE_NOISE         PerlinSimplexNoise(LegacyRandomSource(1234), octaves {0})
//   FROZEN_TEMPERATURE_NOISE  PerlinSimplexNoise(LegacyRandomSource(3456), octaves {-2, -1, 0})
//   BIOME_INFO_NOISE          PerlinSimplexNoise(LegacyRandomSource(2345), octaves {0})   (= one SimplexNoise)
//
// They only matter for (a) the `frozen` temperature modifier, i.e. whether a frozen-ocean iceberg melts slightly,
// and (b) the (unused by the shipped data) `temperature` surface condition. The octave layout of the stacks is
// reconstructed from the decompiled structure of the game and verified against the frozen-ocean chunk dumps; see tests.

#include "falcon/worldgen/random.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace falcon::worldgen {

class SimplexNoise {
public:
    explicit SimplexNoise(RandomSource& random);
    [[nodiscard]] double getValue(double x, double y) const;
    double xo = 0.0, yo = 0.0, zo = 0.0;

private:
    [[nodiscard]] int p(int i) const { return perm_[static_cast<std::size_t>(i) & 255u]; }
    int perm_[256] = {};
};

class PerlinSimplexNoise {
public:
    // `octaves` are the (sorted, unique) octave numbers, all <= 0 here.
    PerlinSimplexNoise(RandomSource& random, const std::vector<int>& octaves);
    [[nodiscard]] double getValue(double x, double y, bool useNoiseOffsets) const;

private:
    std::vector<std::unique_ptr<SimplexNoise>> levels_;  // null for an octave that is not in the stack
    double highestFreqValueFactor_ = 1.0;
    double highestFreqInputFactor_ = 1.0;
};

// Biome.TemperatureModifier.FROZEN and the height lapse of Biome.getHeightAdjustedTemperature.
class BiomeTemperatureNoise {
public:
    [[nodiscard]] static const BiomeTemperatureNoise& instance();
    // FROZEN.modifyTemperature(pos, base)
    [[nodiscard]] float frozen(int x, int z, float baseTemperature) const;
    // The height lapse for y > seaLevel + 17.
    [[nodiscard]] float lapse(float temperature, int x, int y, int z, int seaLevel) const;
    // Biome.BIOME_INFO_NOISE.getValue(x, z): the swamp grass colour modifier uses it at (x * 0.0225, z * 0.0225).
    [[nodiscard]] double biomeInfo(double x, double z) const;

private:
    BiomeTemperatureNoise();
    PerlinSimplexNoise temperature_;
    PerlinSimplexNoise frozen_;
    PerlinSimplexNoise info_;
};

}  // namespace falcon::worldgen
