#include "apex/worldgen/random_state.hpp"

#include <algorithm>
#include <system_error>

namespace apex::worldgen {

namespace {
constexpr std::string_view kNamespacePrefix = "minecraft:";

// "continentalness" and "minecraft:continentalness" both name noise/continentalness.json.
std::string canonicalKey(std::string_view key) {
    if (key.find(':') == std::string_view::npos) return std::string(kNamespacePrefix) + std::string(key);
    return std::string(key);
}
}  // namespace

RandomState::RandomState(std::int64_t seed, std::filesystem::path worldgenDir) : seed_(seed), worldgenDir_(std::move(worldgenDir)) {
    XoroshiroRandomSource r(seed);
    root_ = r.forkPositionalValue();
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
    XoroshiroRandomSource source = root_.makeFromHashOf(full);
    auto created = std::make_unique<NormalNoise>(NormalNoise::create(source, params));
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

}  // namespace apex::worldgen
