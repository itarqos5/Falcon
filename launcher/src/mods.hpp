#pragma once

#include "http.hpp"
#include "result.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace launcher {

// One mod of a search, as Modrinth (https://modrinth.com) lists it.
struct ModHit {
    std::string projectId, slug, title, description, author, iconUrl;
    std::int64_t downloads = 0;
    std::vector<std::string> categories;  // the loaders and kinds it is filed under: "fabric", "neoforge", "optimization" ...
};

struct ModSearchPage {
    std::vector<ModHit> hits;
    int offset = 0, limit = 0, total = 0;
};

struct ModQuery {
    std::string text;         // empty: the most downloaded
    std::string gameVersion;  // "26.3": only mods that list it
    std::string loader;       // "", "fabric", "forge", "neoforge" or "quilt"
    int offset = 0;
    int limit = 20;
};

// The file a mod's newest fitting version offers, and what to check it against.
struct ModFile {
    std::string projectId, versionId, versionNumber, loader, filename, url, sha1;
    std::uint64_t size = 0;
};

struct InstalledMod {
    std::string projectId, title, versionNumber, loader, file, sha1;
};

// The part of Modrinth's public API (v2) the Mods tab uses: search, the versions of a project, and the .jar of one of them. Everything that comes back is treated as untrusted: addresses must be
// Modrinth's own file servers, a file name is cut down to plain characters and a ".jar" before it is written, and the file is checked against the SHA-1 the API names before it is kept.
class Modrinth {
public:
    explicit Modrinth(HttpClient& http, std::string apiBase = "https://api.modrinth.com");

    [[nodiscard]] Result<ModSearchPage> search(const ModQuery& query);
    // The pictures as "data:image/...;base64,..." addresses (the page may not load anything from the network itself), in the order of `urls`; "" where there is none. Fetched side by side, remembered.
    [[nodiscard]] std::vector<std::string> icons(const std::vector<std::string>& urls);
    // The newest release (else the newest version) of the project for the game version, with its .jar (the one marked primary when there are several).
    [[nodiscard]] Result<ModFile> chooseFile(const std::string& projectId, const std::string& gameVersion, const std::string& loader);
    // Downloads the file into `modsDir` (through a temporary name, checked, then moved to its place; a file of the same name is replaced).
    [[nodiscard]] Result<InstalledMod> install(const ModFile& file, const std::string& title, const std::filesystem::path& modsDir, const std::atomic<bool>& cancel);

    // ---- pure parts, exposed for the tests ----
    [[nodiscard]] std::string searchUrl(const ModQuery& query) const;
    [[nodiscard]] std::string versionsUrl(const std::string& projectId, const std::string& gameVersion, const std::string& loader) const;
    [[nodiscard]] static Result<ModSearchPage> parseSearch(const std::string& json);
    [[nodiscard]] static Result<ModFile> pickFile(const std::string& versionsJson, const std::string& projectId);
    // https on Modrinth's file servers (cdn.modrinth.com), nothing else: no other host, no port, no login part.
    [[nodiscard]] static bool isModrinthFileUrl(std::string_view url);
    // A project id ("AANobbMI") or slug ("sodium"): letters, digits, "-" and "_".
    [[nodiscard]] static bool isProjectId(std::string_view id);
    // The name to write a file under: plain characters only, ending in ".jar"; empty when nothing usable is left.
    [[nodiscard]] static std::string safeFileName(std::string_view name);
    // "image/png", "image/webp" ... from the first bytes; empty when it is not a picture a page may show.
    [[nodiscard]] static std::string imageMime(std::string_view bytes);

private:
    HttpClient& http_;
    std::string apiBase_;
    std::mutex iconMutex_;
    std::unordered_map<std::string, std::string> iconCache_;
};

}  // namespace launcher
