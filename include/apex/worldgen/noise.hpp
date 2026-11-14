#pragma once

// 26.3 noise primitives (these are NOT the classic 1.18-1.21 ImprovedNoise / PerlinNoise classes):
//   PerlinNoise         one float-based gradient-noise octave (Mojang's synth.PerlinNoise, ex-ImprovedNoise)
//   SmearedPerlinNoise  the same with the old "yScale fudge" (used only by BlendedNoise)
//   NormalNoise         the stack of octaves a `noise/*.json` file describes (two Perlin layers per octave)
//   BlendedNoise        `minecraft:old_blended_noise`, the terrain base noise
//
// Everything after the integer cell/offset step is IEEE float, and every operation is rounded to float: build
// the implementation with -ffp-contract=off (the project's CMake does) and never with -ffast-math.

#include "apex/worldgen/json_tree.hpp"
#include "apex/worldgen/random.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace apex::worldgen {

class PerlinNoise {
public:
    PerlinNoise() = default;
    explicit PerlinNoise(RandomSource& random);  // three nextDouble() (offsets) then 256 nextInt() (permutation)

    [[nodiscard]] float get(double x, double y, double z) const;
    [[nodiscard]] float get(double x, double z) const { return get(x, 0.0, z); }

    [[nodiscard]] double offsetX() const { return ox_; }
    [[nodiscard]] double offsetY() const { return oy_; }
    [[nodiscard]] double offsetZ() const { return oz_; }
    [[nodiscard]] std::uint8_t permutation(int i) const { return perm_[i & 255]; }

protected:
    [[nodiscard]] float sampleAndLerp(std::int32_t xi, std::int32_t yi, std::int32_t zi, float xf, float yf, float zf, float yfOriginal) const;

    double ox_ = 0.0, oy_ = 0.0, oz_ = 0.0;
    std::uint8_t perm_[256] = {};
};

class SmearedPerlinNoise : public PerlinNoise {
public:
    SmearedPerlinNoise() = default;
    SmearedPerlinNoise(RandomSource& random, double fudgeYScale) : PerlinNoise(random), fudgeYScale_(fudgeYScale) {}
    [[nodiscard]] float get(double x, double y, double z) const;

private:
    double fudgeYScale_ = 1.0;
};

// ---------------------------------------------------------------------------------------------------------------
enum class NoiseNormalize : std::uint8_t { Enabled, Disabled, Legacy };

// worldgen/noise/*.json:  {base_octave, base_amplitude?, octave_count?, amplitude_modifiers?, normalize?}
struct NoiseParameters {
    int baseOctave = 0;
    int octaveCount = 1;
    double baseAmplitude = 1.0;
    std::vector<double> amplitudeModifiers;  // empty = all 1.0
    NoiseNormalize normalize = NoiseNormalize::Enabled;
};

[[nodiscard]] bool parseNoiseParameters(const JsonValue& json, NoiseParameters& out, std::string& error);
[[nodiscard]] bool loadNoiseParameters(const std::filesystem::path& file, NoiseParameters& out, std::string& error);

class NormalNoise {
public:
    NormalNoise() = default;
    // NormalNoise.create(RandomSource): `random` is root.fromHashOf("minecraft:<name>") for a world's noise.
    // It forks two positional factories and builds, per octave, a first and a second PerlinNoise from "octave_<n>".
    [[nodiscard]] static NormalNoise create(RandomSource& random, const NoiseParameters& params);

    [[nodiscard]] float get(double x, double y, double z) const;
    [[nodiscard]] float get(double x, double z) const;

    struct OctaveInfo {
        int octave;
        double frequency;
        double amplitude;
    };
    [[nodiscard]] double normalizationFactor() const { return normalizationFactor_; }
    [[nodiscard]] const std::vector<OctaveInfo>& octaves() const { return octaves_; }
    // The layers, in evaluation order (first, second, first, second ...): their frequency and float amplitude.
    [[nodiscard]] std::size_t layerCount() const { return layers_.size(); }
    [[nodiscard]] double layerFrequency(std::size_t i) const { return layers_[i].frequency; }
    [[nodiscard]] float layerAmplitude(std::size_t i) const { return layers_[i].amplitude; }
    [[nodiscard]] const PerlinNoise& layerNoise(std::size_t i) const { return layers_[i].noise; }

private:
    struct Layer {
        PerlinNoise noise;
        double frequency;
        float amplitude;
    };
    std::vector<Layer> layers_;
    std::vector<OctaveInfo> octaves_;
    double normalizationFactor_ = 0.0;
};

// ---------------------------------------------------------------------------------------------------------------
// minecraft:old_blended_noise {xz_scale, y_scale, xz_factor, y_factor, smear_scale_multiplier}
struct BlendedNoiseParams {
    double xzScale = 0.25;
    double yScale = 0.125;
    double xzFactor = 80.0;
    double yFactor = 160.0;
    double smearScaleMultiplier = 8.0;
};

[[nodiscard]] bool parseBlendedNoiseParams(const JsonValue& json, BlendedNoiseParams& out, std::string& error);

class BlendedNoise {
public:
    // `random` is root.fromHashOf("minecraft:terrain"); it is consumed in order: min limit, max limit, main.
    BlendedNoise(const BlendedNoiseParams& params, RandomSource& random);

    // The density at a block position.
    [[nodiscard]] float sample(std::int32_t x, std::int32_t y, std::int32_t z) const;

private:
    struct Layer {
        SmearedPerlinNoise noise;
        double frequency;
        float amplitude;
    };
    using Stack = std::vector<Layer>;
    [[nodiscard]] static Stack createFbm(RandomSource& random, int firstOctave, double fudgeYScale, double amplitudeParam);
    [[nodiscard]] static float stackGet(const Stack& s, double x, double y, double z);

    Stack minLimit_, maxLimit_, main_;
    double xzMult_ = 0.0, yMult_ = 0.0, xzMainMult_ = 0.0, yMainMult_ = 0.0;
};

}  // namespace apex::worldgen
