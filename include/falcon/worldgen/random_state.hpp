#pragma once

// RandomState: everything a world seed turns into for terrain generation (Mojang's RandomState): the Xoroshiro flavour of the Overworld
// (`legacy_random_source: false`), described below, or the legacy one of the Nether and the End (see the constructor).
//
//   root   = XoroshiroRandomSource(seed).forkPositional()
//   noise  = NormalNoise.create(root.fromHashOf("minecraft:<path>"))     (one shared instance per key)
//   terrain= root.fromHashOf("minecraft:terrain")                        (the old_blended_noise source)
//   named factories (aquifer, ore, surface rules ...) = root.fromHashOf(id).forkPositional()
//
// Noise parameters are read at runtime from <worldgen>/noise/<path>.json (the game's own data, not hard-coded).
// Instances are created lazily and never move; `noise()` is thread-safe.

#include "falcon/worldgen/noise.hpp"
#include "falcon/worldgen/random.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace falcon::worldgen {

class RandomState {
public:
    // `worldgenDir` is <game data>/data/minecraft/worldgen
    // `legacy`: the settings say legacy_random_source (the Nether and the End): RandomState.random is LegacyRandomSource(seed).forkPositional(), the Nether's temperature and
    // vegetation noises are made the old way from LegacyRandomSource(seed) and (seed + 1), and the old blended noise draws from LegacyRandomSource(seed).
    RandomState(std::int64_t seed, std::filesystem::path worldgenDir, bool legacy = false);
    [[nodiscard]] bool legacy() const { return legacy_; }
    // RandomState.random as a value of either kind, and getOrCreateRandomFactory(id) = random.fromHashOf(id).forkPositional().
    [[nodiscard]] AnyPositionalFactory rootAny() const { return rootAny_; }
    [[nodiscard]] AnyPositionalFactory randomFactoryAny(std::string_view id) const;
    // DensityFunction.CompileContext.createRandom(id): where a density function draws its random numbers from.
    [[nodiscard]] std::unique_ptr<RandomSource> createRandom(std::string_view id) const;

    [[nodiscard]] std::int64_t seed() const { return seed_; }
    [[nodiscard]] const std::filesystem::path& worldgenDir() const { return worldgenDir_; }
    [[nodiscard]] const XoroshiroPositionalRandomFactory& root() const { return root_; }

    // The shared NormalNoise for a registry key ("minecraft:continentalness", or just "continentalness"). nullptr and
    // `error` set when the parameter file is missing or invalid.
    [[nodiscard]] const NormalNoise* noise(std::string_view key, std::string& error) const;

    // The random source the End's outer islands are made from: LegacyRandomSource(seed) with its first 17292 values skipped.
    struct SimplexSeed {
        std::unique_ptr<RandomSource> random;
    };
    [[nodiscard]] SimplexSeed legacyIslandSeed() const {
        SimplexSeed s;
        auto legacy = std::make_unique<LegacyRandomSource>(seed_);
        legacy->consumeCount(17292);
        s.random = std::move(legacy);
        return s;
    }
    // root.fromHashOf("minecraft:terrain"): the random source BlendedNoise is built from.
    [[nodiscard]] XoroshiroRandomSource terrainRandom() const { return root_.makeFromHashOf("minecraft:terrain"); }

    // getOrCreateRandomFactory(id) = root.fromHashOf(id).forkPositional()   (id like "minecraft:aquifer")
    [[nodiscard]] XoroshiroPositionalRandomFactory randomFactory(std::string_view id) const {
        XoroshiroRandomSource r = root_.makeFromHashOf(id);
        return r.forkPositionalValue();
    }

    // Every noise key in the data (noise/**/*.json), as "minecraft:<path>", sorted.
    [[nodiscard]] std::vector<std::string> noiseKeys() const;
    // Instantiates all of them (a startup check / warm-up). False and `error` on the first failure.
    [[nodiscard]] bool createAllNoises(std::string& error) const;

private:
    std::int64_t seed_;
    std::filesystem::path worldgenDir_;
    bool legacy_ = false;
    XoroshiroPositionalRandomFactory root_;
    AnyPositionalFactory rootAny_;
    std::int64_t legacyFactorySeed_ = 0;  // LegacyRandomSource(seed).nextLong(): the seed of the legacy positional factory
    mutable std::mutex mutex_;
    mutable std::map<std::string, std::unique_ptr<NormalNoise>, std::less<>> noises_;
};

}  // namespace falcon::worldgen
