#pragma once

#include "http.hpp"
#include "result.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

// Where Mojang says a version's files are.
struct VersionInfo {
    std::string id;
    std::string clientUrl, clientSha1;
    std::uint64_t clientSize = 0;
    std::string assetIndexId, assetIndexUrl, assetIndexSha1;
    std::uint64_t assetIndexSize = 0;
};

struct AssetObject {
    std::string key;   // "minecraft/sounds/mob/cow/say1.ogg"
    std::string hash;  // SHA-1: the object is stored on Mojang's server under <first two characters>/<hash>
    std::uint64_t size = 0;
};

struct InstallProgress {
    std::string phase;  // "version", "client", "extract", "assets", "done"
    std::string label;  // what to tell the player
    std::uint64_t done = 0;
    std::uint64_t total = 0;  // 0: not known
};
using ProgressFn = std::function<void(const InstallProgress&)>;

// The files the client needs from a Minecraft version, downloaded from Mojang's own servers (the same ones the official launcher
// uses) for an account that owns the game, into <root>/<version>/:
//   assets/   the client jar's `assets` folder (textures, models, block states, fonts, shaders, ...) plus every sound and language file
//             from the version's asset index, unpacked where `Falcon.exe --assets` looks for them
//   data/     the client jar's `data` folder (the data pack: tags, recipes, loot tables, world generation)
//   asset-index.json   the version's asset index
// Nothing of it is shipped with Falcon or kept anywhere but on the player's own PC.
class GameFiles {
public:
    GameFiles(HttpClient& http, std::filesystem::path root) : http_(http), root_(std::move(root)) {}

    [[nodiscard]] std::filesystem::path versionDir(const std::string& version) const { return root_ / version; }
    [[nodiscard]] std::filesystem::path assetsDir(const std::string& version) const { return versionDir(version) / "assets"; }
    // Everything was downloaded and unpacked (the marker is written last).
    [[nodiscard]] bool isInstalled(const std::string& version) const;

    [[nodiscard]] Result<VersionInfo> resolveVersion(const std::string& version);
    // Installs what is missing (a second call after an interruption carries on rather than starting again).
    [[nodiscard]] Result<bool> install(const std::string& version, const ProgressFn& progress, const std::atomic<bool>& cancel);

    // ---- pure parts, exposed for the tests ----
    [[nodiscard]] static Result<std::string> findVersionUrl(const std::string& manifestJson, const std::string& version);
    [[nodiscard]] static Result<VersionInfo> parseVersionJson(const std::string& versionJson);
    [[nodiscard]] static Result<std::vector<AssetObject>> parseAssetIndex(const std::string& indexJson);
    [[nodiscard]] static std::string assetObjectUrl(std::string_view hash);
    // A path inside the install folder: relative, '/'-separated, no "..", no drive letter, no empty parts.
    [[nodiscard]] static bool isSafeRelativePath(std::string_view path);

    // The game's exported data: the tables Falcon's singleplayer reads (registries.json, gameplay-facts.json, ...). Mojang's downloads do not contain
    // them; they are produced from a Minecraft install by horizon-launcher. True when `dir` holds the whole set.
    [[nodiscard]] static bool isGameDataDir(const std::filesystem::path& dir);
    // The folder to give the game as --data for `version`: this launcher's own game folder when it has the set, else the newest complete export
    // in horizon-launcher's installs under `roaming` (%APPDATA%horizon-launcherersions*.localhorizon-client<version>). Empty when there is none.
    [[nodiscard]] std::optional<std::filesystem::path> findGameData(const std::string& version, const std::filesystem::path& roaming) const;

private:
    HttpClient& http_;
    std::filesystem::path root_;
};

}  // namespace launcher
