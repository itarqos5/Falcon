#pragma once

// RandomState: everything a world seed turns into for terrain generation (Mojang's RandomState, Xoroshiro flavour,
// i.e. `legacy_random_source: false` as in the Overworld).
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
    RandomState(std::int64_t seed, std::filesystem::path worldgenDir);

    [[nodiscard]] std::int64_t seed() const { return seed_; }
    [[nodiscard]] const std::filesystem::path& worldgenDir() const { return worldgenDir_; }
    [[nodiscard]] const XoroshiroPositionalRandomFactory& root() const { return root_; }

    // The shared NormalNoise for a registry key ("minecraft:continentalness", or just "continentalness"). nullptr and
    // `error` set when the parameter file is missing or invalid.
    [[nodiscard]] const NormalNoise* noise(std::string_view key, std::string& error) const;

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
    XoroshiroPositionalRandomFactory root_;
    mutable std::mutex mutex_;
    mutable std::map<std::string, std::unique_ptr<NormalNoise>, std::less<>> noises_;
};

}  // namespace falcon::worldgen
