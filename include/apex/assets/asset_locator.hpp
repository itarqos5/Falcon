#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apex {

// A directory laid out like the "assets" folder of an unpacked client:
//   <root>/minecraft/textures/..., <root>/minecraft/lang/..., <root>/minecraft/font/...
struct AssetLocation {
    std::filesystem::path root;
    std::string source;  // where it was found, for the startup log
};

struct AssetSearchResult {
    std::optional<AssetLocation> location;
    std::vector<std::string> notes;  // what was tried, for error messages
};

// True when `root` contains the files the title screen needs.
[[nodiscard]] bool isValidAssetRoot(const std::filesystem::path& root);

// Looks for the game assets, in this order:
//   1. `explicitDir` (from --assets) - authoritative: no fallback if it is invalid
//   2. $FALCON_ASSETS_DIR (or the old $APEX_ASSETS_DIR) - authoritative as well
//   3. the newest extracted 26.3 asset tree under %APPDATA%/horizon-launcher/versions
[[nodiscard]] AssetSearchResult locateAssets(const std::optional<std::filesystem::path>& explicitDir);

}  // namespace apex
