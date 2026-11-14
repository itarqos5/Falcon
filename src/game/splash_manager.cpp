#include "apex/game/splash_manager.hpp"

#include "apex/util/text_util.hpp"

#include <sstream>

namespace apex {
namespace {

// The game drops the one splash line whose String.hashCode() is this value.
constexpr std::int32_t kExcludedSplashHash = 125780783;

}  // namespace

SplashManager::SplashManager(const ResourceManager& resources) {
    const auto text = resources.readText(ResourceLocation("minecraft", "texts/splashes.txt"));
    if (!text) return;  // no splashes: the title screen simply shows none

    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line)) {
        std::string trimmed = javaTrim(line);
        if (javaStringHash(trimmed) == kExcludedSplashHash) continue;
        splashes_.push_back(std::move(trimmed));
    }
}

std::optional<std::string> SplashManager::specialSplash(int month, int day) {
    if (month == 12 && day == 24) return "Merry X-mas!";
    if (month == 1 && day == 1) return "Happy new year!";
    if (month == 10 && day == 31) return "OOoooOOOoooo! Spooky!";
    return std::nullopt;
}

std::optional<std::string> SplashManager::pick(int month, int day, std::mt19937& rng) const {
    if (auto special = specialSplash(month, day)) return special;
    if (splashes_.empty()) return std::nullopt;
    std::uniform_int_distribution<std::size_t> dist(0, splashes_.size() - 1);
    return splashes_[dist(rng)];
}

}  // namespace apex
