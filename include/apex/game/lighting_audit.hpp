#pragma once

#include "apex/assets/resource_manager.hpp"

#include <filesystem>

namespace apex {

// `Falcon --audit-lighting`: a developer check of Smooth Lighting that needs no window. Every block of the game is meshed next to two
// pillars of stone (so its faces have something to be shaded against), once with Smooth Lighting on and once off, and the light and
// shade at the four corners of each of its quads are compared. A model that says "ambientocclusion": false should come out flat (the
// game lights those flat); anything else whose quads are all the same at every corner is NOT being smoothed - those are listed. Prints
// the report and returns 0 (1 when the game's data cannot be read).
int runLightingAudit(const ResourceManager& resources, const std::filesystem::path& gameDataDir, const std::filesystem::path& cacheDir);

}  // namespace apex
