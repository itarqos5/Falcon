#include "apex/assets/asset_locator.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace apex {
namespace fs = std::filesystem;

namespace {

constexpr const char* kRequiredFiles[] = {
    "minecraft/textures/gui/title/minecraft.png",
    "minecraft/textures/gui/title/edition.png",
    "minecraft/textures/gui/title/background/panorama_0.png",
    "minecraft/textures/gui/sprites/widget/button.png",
    "minecraft/textures/font/ascii.png",
    "minecraft/font/default.json",
    "minecraft/lang/en_us.json",
    "minecraft/texts/splashes.txt",
};

// Accept both ".../assets" and ".../assets/minecraft".
fs::path normalizeRoot(fs::path p) {
    p = p.lexically_normal();
    if (!p.has_filename()) p = p.parent_path();
    if (p.filename() == "minecraft" && fs::exists(p / "textures")) p = p.parent_path();
    return p;
}

std::string describe(const fs::path& p) { return p.generic_string(); }

}  // namespace

bool isValidAssetRoot(const fs::path& root) {
    std::error_code ec;
    return std::ranges::all_of(kRequiredFiles, [&](const char* rel) {
        return fs::is_regular_file(root / rel, ec);
    });
}

AssetSearchResult locateAssets(const std::optional<fs::path>& explicitDir) {
    AssetSearchResult result;

    const auto tryAuthoritative = [&](const fs::path& dir, const std::string& source) {
        const fs::path root = normalizeRoot(dir);
        if (isValidAssetRoot(root)) {
            result.location = AssetLocation{root, source};
        } else {
            result.notes.push_back(source + ": " + describe(root) +
                                   " is missing required menu assets");
        }
    };

    if (explicitDir) {
        tryAuthoritative(*explicitDir, "--assets");
        return result;
    }
    if (const char* env = std::getenv("FALCON_ASSETS_DIR"); env != nullptr && *env != '\0') {
        tryAuthoritative(env, "FALCON_ASSETS_DIR");
        return result;
    }
    if (const char* env = std::getenv("APEX_ASSETS_DIR"); env != nullptr && *env != '\0') {  // its name before the rename
        tryAuthoritative(env, "APEX_ASSETS_DIR");
        return result;
    }

    // horizon-launcher keeps an unpacked copy of the 26.3 assets per launcher build.
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr) {
        const fs::path versions = fs::path(appdata) / "horizon-launcher" / "versions";
        std::error_code ec;
        std::optional<fs::path> best;
        fs::file_time_type bestTime{};
        for (fs::directory_iterator it(versions, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path candidate =
                it->path() / ".local" / "horizon-client" / "26.3" / "assets";
            if (!isValidAssetRoot(candidate)) continue;
            std::error_code timeEc;
            const auto t = fs::last_write_time(candidate, timeEc);
            if (!best || (!timeEc && t > bestTime)) {
                best = candidate;
                if (!timeEc) bestTime = t;
            }
        }
        if (best) {
            result.location = AssetLocation{*best, "horizon-launcher 26.3 assets"};
            return result;
        }
        result.notes.push_back("no valid 26.3 asset tree under " + describe(versions));
    } else {
        result.notes.push_back("%APPDATA% is not set");
    }
    return result;
}

}  // namespace apex
