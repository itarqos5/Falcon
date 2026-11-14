#include "falcon/assets/asset_index.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace falcon {
namespace fs = std::filesystem;

std::optional<AssetIndex> AssetIndex::open(const fs::path& assetsDir) {
    std::error_code ec;
    fs::path best;
    long bestNumber = -1;
    for (fs::directory_iterator it(assetsDir / "indexes", ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->path().extension() != ".json") continue;
        long number = 0;
        try {
            number = std::stol(it->path().stem().string());
        } catch (...) {
            number = 0;  // non-numeric index names still count, just with the lowest priority
        }
        if (number > bestNumber) {
            bestNumber = number;
            best = it->path();
        }
    }
    if (best.empty()) return std::nullopt;

    std::ifstream in(best, std::ios::binary);
    if (!in) return std::nullopt;
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!json.is_object() || !json.contains("objects") || !json["objects"].is_object()) {
        return std::nullopt;
    }

    AssetIndex index;
    index.dir_ = assetsDir;
    index.id_ = best.stem().string();
    for (const auto& [name, entry] : json["objects"].items()) {
        if (entry.is_object() && entry.contains("hash") && entry["hash"].is_string()) {
            index.hashes_.emplace(name, entry["hash"].get<std::string>());
        }
    }
    return index;
}

std::optional<std::vector<unsigned char>> AssetIndex::read(const std::string& name) const {
    const auto it = hashes_.find(name);
    if (it == hashes_.end() || it->second.size() < 2) return std::nullopt;
    const fs::path file = dir_ / "objects" / it->second.substr(0, 2) / it->second;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

std::vector<fs::path> candidateAssetIndexDirs() {
    std::vector<fs::path> dirs;
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr) {
        dirs.push_back(fs::path(appdata) / ".minecraft" / "assets");
        dirs.push_back(fs::path(appdata) / "horizon-launcher" / "minecraft" / "assets");
    }
    return dirs;
}

}  // namespace falcon
