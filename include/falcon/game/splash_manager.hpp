#pragma once

#include "falcon/assets/resource_manager.hpp"

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace falcon {

// Picks the yellow splash text from minecraft:texts/splashes.txt, with the game's special
// texts on Dec 24, Jan 1 and Oct 31.
class SplashManager {
public:
    explicit SplashManager(const ResourceManager& resources);

    // Special text for a calendar day (month 1-12), if any.
    [[nodiscard]] static std::optional<std::string> specialSplash(int month, int day);

    [[nodiscard]] std::optional<std::string> pick(int month, int day, std::mt19937& rng) const;
    [[nodiscard]] std::size_t size() const { return splashes_.size(); }

private:
    std::vector<std::string> splashes_;
};

}  // namespace falcon
