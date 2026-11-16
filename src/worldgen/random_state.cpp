#include "falcon/worldgen/random_state.hpp"

#include <algorithm>
#include <system_error>

namespace falcon::worldgen {

namespace {
constexpr std::string_view kNamespacePrefix = "minecraft:";

// "continentalness" and "minecraft:continentalness" both name noise/continentalness.json.
std::string canonicalKey(std::string_view key) {
    if (key.find(':') == std::string_view::npos) return std::string(kNamespacePrefix) + std::string(key);
    return std::string(key);
}
}  // namespace

RandomState::RandomState(std::int64_t seed, std::filesystem::path worldgenDir, bool legacy) : seed_(seed), worldgenDir_(std::move(worldgenDir)), legacy_(legacy) {
    XoroshiroRandomSource r(seed);
    root_ = r.forkPositionalValue();
    if (legacy_) {
        LegacyRandomSource legacySource(seed);
        legacyFactorySeed_ = legacySource.nextLong();
        rootAny_ = AnyPositionalFactory(legacyFactorySeed_);
    } else {
        rootAny_ = AnyPositionalFactory(root_);
    }
}

AnyPositionalFactory RandomState::randomFactoryAny(std::string_view id) const {
    if (!legacy_) {
        XoroshiroRandomSource r = root_.makeFromHashOf(id);
        return AnyPositionalFactory(r.forkPositionalValue());
    }
    // random.fromHashOf(id) is a LegacyRandomSource seeded from the name's hash, and .forkPositional() draws the new factory's seed from it
    LegacyRandomSource source(static_cast<std::int64_t>(javaStringHashCode(id)) ^ legacyFactorySeed_);
    return AnyPositionalFactory(source.nextLong());
}

std::unique_ptr<RandomSource> RandomState::createRandom(std::string_view id) const {
    const std::string full = canonicalKey(id);
    if (legacy_) {
        if (full == "minecraft:terrain") return std::make_unique<LegacyRandomSource>(seed_);  // newLegacyInstance(0)
        return std::make_unique<LegacyRandomSource>(static_cast<std::int64_t>(javaStringHashCode(full)) ^ legacyFactorySeed_);
    }
    return std::make_unique<XoroshiroRandomSource>(root_.makeFromHashOf(full));
}

const NormalNoise* RandomState::noise(std::string_view key, std::string& error) const {
    const std::string full = canonicalKey(key);
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = noises_.find(full);
    if (it != noises_.end()) return it->second.get();
    if (full.compare(0, kNamespacePrefix.size(), kNamespacePrefix) != 0) {
        error = "noise '" + full + "': only the minecraft namespace is supported";
        return nullptr;
    }
    NoiseParameters params;
    const std::filesystem::path file = worldgenDir_ / "noise" / (full.substr(kNamespacePrefix.size()) + ".json");
    if (!loadNoiseParameters(file, params, error)) return nullptr;
    std::unique_ptr<NormalNoise> created;
    if (legacy_ && (full == "minecraft:nether/temperature" || full == "minecraft:nether/vegetation")) {
        // RandomState$1.createNoiseSampler: the Nether's biome noises are made the old way, from LegacyRandomSource(seed) and (seed + 1)
        LegacyRandomSource source(seed_ + (full == "minecraft:nether/temperature" ? 0 : 1));
        created = std::make_unique<NormalNoise>(NormalNoise::createForLegacyNetherBiome(source, params));
    } else {
        const std::unique_ptr<RandomSource> source = createRandom(full);  // Noises.instantiate: NormalNoise.create(random.fromHashOf(key))
        created = std::make_unique<NormalNoise>(NormalNoise::create(*source, params));
    }
    const NormalNoise* raw = created.get();
    noises_.emplace(full, std::move(created));
    return raw;
}

std::vector<std::string> RandomState::noiseKeys() const {
    std::vector<std::string> keys;
    std::error_code ec;
    const std::filesystem::path dir = worldgenDir_ / "noise";
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".json") continue;
        std::string rel = std::filesystem::relative(it->path(), dir).generic_string();
        rel.resize(rel.size() - 5);  // ".json"
        keys.push_back(std::string(kNamespacePrefix) + rel);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool RandomState::createAllNoises(std::string& error) const {
    for (const std::string& key : noiseKeys()) {
        if (!noise(key, error)) return false;
    }
    return true;
}

}  // namespace falcon::worldgen
