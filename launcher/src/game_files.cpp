#include "game_files.hpp"

#include "falcon/assets/zip_archive.hpp"
#include "data_tables.hpp"
#include "sha1.hpp"
#include "util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

namespace launcher {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* kManifestUrl = "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json";
constexpr int kDownloadThreads = 8;
constexpr int kAttempts = 3;

json parseJson(const std::string& text) { return json::parse(text, nullptr, /*allow_exceptions=*/false); }

std::string stringOf(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::uint64_t numberOf(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_unsigned() ? it->get<std::uint64_t>() : 0;
}

// Tells the UI at most ten times a second.
class Throttle {
public:
    explicit Throttle(const ProgressFn& fn) : fn_(fn) {}
    void report(const InstallProgress& p, bool force = false) {
        if (!fn_) return;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (!force && now - last_ < std::chrono::milliseconds(100)) return;
        last_ = now;
        fn_(p);
    }

private:
    const ProgressFn& fn_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point last_{};
};

}  // namespace

DataTablesSource builtinDataTables() { return {kDataTablesUrl, kDataTablesSha256, kDataTablesSize}; }

const std::vector<std::string>& GameFiles::dataTableFiles() {
    // Every file the client reads from its data folder besides the game's own data/ (the loaders of the block, item, shape, model, behaviour, command, movement and creative tables, and
    // the biome list of the world generator).
    static const std::vector<std::string> kFiles = {"registries.json", "model-states.json", "gameplay-facts.json", "facts.json", "items.json", "block-behaviours.json", "cached-collision.nbt",
                                                    "creative-inventory.json", "entity-models.json", "movement-properties.nbt", "commands.json", "biome-parameters/overworld.json"};
    return kFiles;
}

bool GameFiles::isGameDataDir(const fs::path& dir) {
    std::error_code ec;
    for (const std::string& name : dataTableFiles()) {
        if (!fs::is_regular_file(dir / name, ec)) return false;
    }
    return fs::is_directory(dir / "data" / "minecraft", ec);
}

bool GameFiles::hasTables(const std::string& version) const {
    std::string text;
    if (!readFile(versionDir(version) / "tables.json", text) || stringOf(parseJson(text), "sha256") != tables_.sha256) return false;
    return isGameDataDir(versionDir(version));
}

std::optional<fs::path> GameFiles::findGameData(const std::string& version) const {
    if (isGameDataDir(versionDir(version))) return versionDir(version);
    return std::nullopt;
}

// Falcon's data tables: one zip from Falcon's own release. It is downloaded whole, checked against the SHA-256 the launcher carries, and only then are the files in it (a fixed
// list of names, nothing else) written, each beside its final place first and then moved there, so that an interruption never leaves half a table.
Result<bool> GameFiles::installTables(const std::string& version, const ProgressFn& progress, const std::atomic<bool>& cancel) {
    using R = Result<bool>;
    Throttle throttle(progress);
    const fs::path dir = versionDir(version);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return R::fail("disk", "Cannot create " + narrow(dir.wstring()) + ".");
    if (tables_.url.rfind("https://", 0) != 0 || tables_.sha256.size() != 64) return R::fail("no_tables", "This launcher has no address for Falcon's data tables.");
    const fs::path zipFile = dir / "data-tables.zip";
    if (sha256FileHex(zipFile) != tables_.sha256) {
        throttle.report({"tables", "Downloading Falcon's data tables", 0, tables_.size}, true);
        HttpResponse r;
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            r = http_.download(
                tables_.url, zipFile, [&](std::uint64_t done, std::uint64_t total) { throttle.report({"tables", "Downloading Falcon's data tables", done, total ? total : tables_.size}); }, &cancel);
            if (r.ok() || cancel.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
        }
        if (cancel.load()) return R::fail("cancelled", "Cancelled.");
        if (!r.ok()) return R::fail(r.status == 0 ? "network" : "service", "Falcon's data tables could not be downloaded: " + (r.error.empty() ? "status " + std::to_string(r.status) : r.error) + ".");
        if (sha256FileHex(zipFile) != tables_.sha256) {
            fs::remove(zipFile, ec);
            return R::fail("corrupt", "The downloaded data tables did not match their checksum. Try again.");
        }
    }
    std::vector<std::pair<std::string, std::vector<unsigned char>>> files;
    {  // (the zip is closed again before it is deleted)
        const auto zip = falcon::ZipArchive::open(zipFile);
        if (!zip) {
            fs::remove(zipFile, ec);
            return R::fail("corrupt", "The downloaded data tables could not be opened.");
        }
        for (const std::string& name : dataTableFiles()) {
            throttle.report({"tables", "Unpacking Falcon's data tables", files.size(), dataTableFiles().size()});
            const auto bytes = zip->read(name);
            if (!bytes) return R::fail("corrupt", "The data tables download is missing " + name + ".");
            files.emplace_back(name, *bytes);
        }
    }
    for (const auto& [name, bytes] : files) {
        const fs::path out = dir / fs::path(widen(name));
        const fs::path temp = dir / fs::path(widen(name + ".part"));
        fs::create_directories(out.parent_path(), ec);
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file) return R::fail("disk", "Cannot write " + narrow(temp.wstring()) + " (is the disk full?).");
        }
        fs::rename(temp, out, ec);
        if (ec) {
            fs::remove(out, ec);
            fs::rename(temp, out, ec);
            if (ec) return R::fail("disk", "Cannot write " + narrow(out.wstring()) + ".");
        }
    }
    writeFileAtomic(dir / "tables.json", json{{"sha256", tables_.sha256}, {"files", files.size()}}.dump());
    fs::remove(zipFile, ec);
    throttle.report({"tables", "Falcon's data tables are ready", 1, 1}, true);
    return R::success(true);
}

bool GameFiles::isSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 400 || path.front() == '/' || path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

std::string GameFiles::assetObjectUrl(std::string_view hash) { return "https://resources.download.minecraft.net/" + std::string(hash.substr(0, 2)) + "/" + std::string(hash); }

Result<std::string> GameFiles::findVersionUrl(const std::string& manifestJson, const std::string& version) {
    const json j = parseJson(manifestJson);
    if (j.is_discarded() || !j.is_object() || !j.contains("versions") || !j["versions"].is_array()) return Result<std::string>::fail("bad_response", "Mojang's version list was not understood.");
    for (const json& v : j["versions"]) {
        if (v.is_object() && stringOf(v, "id") == version) {
            const std::string url = stringOf(v, "url");
            if (url.rfind("https://", 0) == 0) return Result<std::string>::success(url);
        }
    }
    return Result<std::string>::fail("no_version", "Mojang's version list has no Minecraft " + version + ".");
}

Result<VersionInfo> GameFiles::parseVersionJson(const std::string& versionJson) {
    const json j = parseJson(versionJson);
    if (j.is_discarded() || !j.is_object()) return Result<VersionInfo>::fail("bad_response", "The version description was not understood.");
    VersionInfo v;
    v.id = stringOf(j, "id");
    if (j.contains("downloads") && j["downloads"].is_object() && j["downloads"].contains("client") && j["downloads"]["client"].is_object()) {
        const json& c = j["downloads"]["client"];
        v.clientUrl = stringOf(c, "url");
        v.clientSha1 = stringOf(c, "sha1");
        v.clientSize = numberOf(c, "size");
    }
    if (j.contains("assetIndex") && j["assetIndex"].is_object()) {
        const json& a = j["assetIndex"];
        v.assetIndexId = stringOf(a, "id");
        v.assetIndexUrl = stringOf(a, "url");
        v.assetIndexSha1 = stringOf(a, "sha1");
        v.assetIndexSize = numberOf(a, "size");
    }
    if (v.clientUrl.rfind("https://", 0) != 0 || v.clientSha1.size() != 40 || v.assetIndexUrl.rfind("https://", 0) != 0) {
        return Result<VersionInfo>::fail("bad_response", "The version description has no client download.");
    }
    return Result<VersionInfo>::success(std::move(v));
}

Result<std::vector<AssetObject>> GameFiles::parseAssetIndex(const std::string& indexJson) {
    const json j = parseJson(indexJson);
    if (j.is_discarded() || !j.is_object() || !j.contains("objects") || !j["objects"].is_object()) {
        return Result<std::vector<AssetObject>>::fail("bad_response", "The asset list was not understood.");
    }
    std::vector<AssetObject> objects;
    for (auto it = j["objects"].begin(); it != j["objects"].end(); ++it) {
        // Only what the client reads from its assets folder; the rest (window icons, the realms pictures) is not needed.
        if (it.key().rfind("minecraft/", 0) != 0 || !isSafeRelativePath(it.key()) || !it.value().is_object()) continue;
        AssetObject o;
        o.key = it.key();
        o.hash = stringOf(it.value(), "hash");
        o.size = numberOf(it.value(), "size");
        if (o.hash.size() != 40) continue;
        objects.push_back(std::move(o));
    }
    std::sort(objects.begin(), objects.end(), [](const AssetObject& a, const AssetObject& b) { return a.key < b.key; });
    return Result<std::vector<AssetObject>>::success(std::move(objects));
}

bool GameFiles::isInstalled(const std::string& version) const {
    std::string text;
    if (!readFile(versionDir(version) / "installed.json", text)) return false;
    const json j = parseJson(text);
    return j.is_object() && stringOf(j, "id") == version && fs::exists(assetsDir(version) / "minecraft" / "sounds.json") && fs::exists(versionDir(version) / "data" / "minecraft") && hasTables(version);
}

Result<VersionInfo> GameFiles::resolveVersion(const std::string& version) {
    const HttpResponse manifest = http_.get(kManifestUrl);
    if (!manifest.ok()) return Result<VersionInfo>::fail(manifest.status == 0 ? "network" : "service", manifest.status == 0 ? "Could not reach Mojang: " + manifest.error : "Mojang's version list is not available (" + std::to_string(manifest.status) + ").");
    const auto url = findVersionUrl(manifest.body, version);
    if (!url) return Result<VersionInfo>::fail(url.error);
    const HttpResponse detail = http_.get(*url);
    if (!detail.ok()) return Result<VersionInfo>::fail(detail.status == 0 ? "network" : "service", "Could not read the description of Minecraft " + version + ".");
    return parseVersionJson(detail.body);
}

Result<bool> GameFiles::install(const std::string& version, const ProgressFn& progress, const std::atomic<bool>& cancel) {
    using R = Result<bool>;
    Throttle throttle(progress);
    const auto cancelled = [&] { return cancel.load(); };
    const fs::path dir = versionDir(version);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return R::fail("disk", "Cannot create " + narrow(dir.wstring()) + ".");

    throttle.report({"version", "Looking up Minecraft " + version, 0, 0}, true);
    const auto info = resolveVersion(version);
    if (!info) return R::fail(info.error);
    if (cancelled()) return R::fail("cancelled", "Cancelled.");

    // ---- the client jar: its assets and data ----
    const fs::path marker = dir / "extracted.json";
    std::string markerText;
    const bool extracted = readFile(marker, markerText) && stringOf(parseJson(markerText), "sha1") == info->clientSha1 && fs::exists(dir / "data" / "minecraft") &&
                           fs::exists(dir / "assets" / "minecraft");
    if (!extracted) {
        const fs::path jar = dir / "client.jar";
        if (sha1FileHex(jar) != info->clientSha1) {
            const HttpResponse r = http_.download(
                info->clientUrl, jar,
                [&](std::uint64_t done, std::uint64_t total) { throttle.report({"client", "Downloading Minecraft " + version, done, total ? total : info->clientSize}); }, &cancel);
            if (!r.ok()) return R::fail(cancelled() ? "cancelled" : (r.status == 0 ? "network" : "service"), cancelled() ? "Cancelled." : "The game download failed: " + (r.error.empty() ? "status " + std::to_string(r.status) : r.error));
            if (sha1FileHex(jar) != info->clientSha1) {
                fs::remove(jar, ec);
                return R::fail("corrupt", "The downloaded game file did not match Mojang's checksum. Try again.");
            }
        }
        const auto zip = falcon::ZipArchive::open(jar);
        if (!zip) return R::fail("corrupt", "The downloaded game file could not be opened.");
        std::vector<std::string> names;
        for (std::string& name : zip->entryNames()) {
            if ((name.rfind("assets/", 0) == 0 || name.rfind("data/", 0) == 0) && name.back() != '/' && isSafeRelativePath(name)) names.push_back(std::move(name));
        }
        std::uint64_t index = 0;
        for (const std::string& name : names) {
            if (cancelled()) return R::fail("cancelled", "Cancelled.");
            throttle.report({"extract", "Unpacking the game's files", index, names.size()});
            const auto bytes = zip->read(name);
            if (!bytes) return R::fail("corrupt", "Could not unpack " + name + ".");
            const fs::path out = dir / fs::path(widen(name));
            fs::create_directories(out.parent_path(), ec);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
            if (!file) return R::fail("disk", "Cannot write " + narrow(out.wstring()) + " (is the disk full?).");
            ++index;
        }
        writeFileAtomic(marker, json{{"sha1", info->clientSha1}, {"files", names.size()}}.dump());
    }

    // ---- the asset index: sounds and languages ----
    const fs::path indexFile = dir / "asset-index.json";
    std::string indexText;
    if (!readFile(indexFile, indexText) || sha1Hex(indexText) != info->assetIndexSha1) {
        throttle.report({"assets", "Reading the list of sounds and languages", 0, 0}, true);
        const HttpResponse r = http_.get(info->assetIndexUrl);
        if (!r.ok()) return R::fail(r.status == 0 ? "network" : "service", "Could not download the list of sounds and languages.");
        if (sha1Hex(r.body) != info->assetIndexSha1) return R::fail("corrupt", "The list of sounds and languages did not match Mojang's checksum.");
        indexText = r.body;
        writeFileAtomic(indexFile, indexText);
    }
    const auto objects = parseAssetIndex(indexText);
    if (!objects) return R::fail(objects.error);

    std::uint64_t totalBytes = 0;
    for (const AssetObject& o : *objects) totalBytes += o.size;
    std::atomic<std::uint64_t> doneBytes{0};
    std::atomic<std::size_t> next{0};
    std::atomic<int> failures{0};
    std::mutex errorMutex;
    std::string firstError;
    const auto work = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= objects->size() || cancelled()) return;
            const AssetObject& o = (*objects)[i];
            const fs::path out = dir / "assets" / fs::path(widen(o.key));
            std::error_code localEc;
            if (fs::exists(out, localEc) && fs::file_size(out, localEc) == o.size) {  // already here (an earlier, interrupted install)
                doneBytes += o.size;
                continue;
            }
            bool ok = false;
            std::string why;
            for (int attempt = 0; attempt < kAttempts && !ok && !cancelled(); ++attempt) {
                std::uint64_t counted = 0;
                const HttpResponse r = http_.download(assetObjectUrl(o.hash), out,
                                                      [&](std::uint64_t done, std::uint64_t) {
                                                          doneBytes += done - counted;
                                                          counted = done;
                                                          throttle.report({"assets", "Downloading sounds and languages", doneBytes.load(), totalBytes});
                                                      },
                                                      &cancel);
                if (r.ok() && sha1FileHex(out) == o.hash) {
                    ok = true;
                    doneBytes += o.size > counted ? o.size - counted : 0;
                } else {
                    doneBytes -= std::min<std::uint64_t>(counted, doneBytes.load());
                    why = r.ok() ? "checksum mismatch" : (r.error.empty() ? "status " + std::to_string(r.status) : r.error);
                    fs::remove(out, localEc);
                    if (attempt + 1 < kAttempts) std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
                }
            }
            if (!ok && !cancelled()) {
                ++failures;
                std::lock_guard<std::mutex> lock(errorMutex);
                if (firstError.empty()) firstError = o.key + ": " + why;
            }
        }
    };
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < kDownloadThreads; ++t) threads.emplace_back(work);
        for (std::thread& t : threads) t.join();
    }
    if (cancelled()) return R::fail("cancelled", "Cancelled.");
    if (failures.load() > 0) return R::fail("network", std::to_string(failures.load()) + " sound or language files could not be downloaded (" + firstError + "). Try again.");
    throttle.report({"assets", "Downloading sounds and languages", totalBytes, totalBytes}, true);

    // ---- Falcon's data tables ----
    if (!hasTables(version)) {
        const auto tables = installTables(version, progress, cancel);
        if (!tables) return tables;
    }

    writeFileAtomic(dir / "installed.json", json{{"id", version}, {"clientSha1", info->clientSha1}, {"assetIndexSha1", info->assetIndexSha1}, {"assets", objects->size()}}.dump());
    fs::remove(dir / "client.jar", ec);  // unpacked: the 40 MB jar is not needed again
    throttle.report({"done", "Game files are ready", 1, 1}, true);
    return R::success(true);
}

}  // namespace launcher
