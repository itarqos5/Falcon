#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace falcon {

// A launcher-style "assets" folder: indexes/<id>.json maps asset names (like
// "icons/icon_32x32.png") to content-addressed files at objects/<first two hash chars>/<hash>.
class AssetIndex {
public:
    // Opens the highest-numbered index under `assetsDir`; nullopt if there is none.
    [[nodiscard]] static std::optional<AssetIndex> open(const std::filesystem::path& assetsDir);

    [[nodiscard]] bool contains(const std::string& name) const { return hashes_.contains(name); }
    [[nodiscard]] std::optional<std::vector<unsigned char>> read(const std::string& name) const;

    [[nodiscard]] const std::filesystem::path& directory() const { return dir_; }
    [[nodiscard]] const std::string& indexId() const { return id_; }

private:
    std::filesystem::path dir_;
    std::string id_;
    std::unordered_map<std::string, std::string> hashes_;
};

// Launcher asset folders worth searching, best first:
// %APPDATA%/.minecraft/assets, then the one inside %APPDATA%/horizon-launcher/minecraft.
[[nodiscard]] std::vector<std::filesystem::path> candidateAssetIndexDirs();

}  // namespace falcon
