#include "mods.hpp"

#include "avatar.hpp"  // base64
#include "sha1.hpp"
#include "util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <thread>

namespace launcher {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::size_t kMaxIconBytes = 400 * 1024;
constexpr std::uint64_t kMaxModBytes = 512ull * 1024 * 1024;

bool isHex40(std::string_view text) {
    return text.size() == 40 && std::all_of(text.begin(), text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

bool endsWithJar(std::string_view name) {
    return name.size() > 4 && lowerCase(std::string(name.substr(name.size() - 4))) == ".jar";
}

bool isLoader(std::string_view loader) { return loader == "fabric" || loader == "forge" || loader == "neoforge" || loader == "quilt"; }

// A game version as Modrinth writes it ("26.3", "1.21.1", "26.4-snapshot-10"): letters, digits, ".", "-", "_" and "+".
bool isGameVersion(std::string_view version) {
    return !version.empty() && version.size() <= 40 && std::all_of(version.begin(), version.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-' || c == '_' || c == '+'; });
}

template <class T>
Result<T> httpFailure(const HttpResponse& r, const char* what) {
    if (r.status == 0) return Result<T>::fail("network", std::string("Modrinth could not be reached (") + (r.error.empty() ? "no answer" : r.error) + ").");
    if (r.status == 429) return Result<T>::fail("rate_limited", "Modrinth asks for a pause: too many requests. Try again in a minute.");
    return Result<T>::fail("http", std::string("Modrinth answered ") + what + " with status " + std::to_string(r.status) + ".");
}

std::string stringOf(const json& object, const char* key) {
    return object.is_object() && object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
}

}  // namespace

Modrinth::Modrinth(HttpClient& http, std::string apiBase) : http_(http), apiBase_(std::move(apiBase)) {
    while (!apiBase_.empty() && apiBase_.back() == '/') apiBase_.pop_back();
}

// ---- pure parts ---------------------------------------------------------------------------------------------------------------------------------------------

bool Modrinth::isProjectId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_'; });
}

bool Modrinth::isModrinthFileUrl(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size() || url.compare(0, scheme.size(), scheme) != 0) return false;
    if (std::any_of(url.begin(), url.end(), [](char c) { return static_cast<unsigned char>(c) <= 0x20 || c == '\\' || c == '"' || c == '<' || c == '>'; })) return false;
    const std::string_view rest = url.substr(scheme.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos || slash + 1 >= rest.size()) return false;
    const std::string_view host = rest.substr(0, slash);  // (no "@", no ":", so no login part and no port: the host is the whole of it)
    return host == "cdn.modrinth.com" || host == "cdn-raw.modrinth.com";
}

std::string Modrinth::safeFileName(std::string_view name) {
    // Only the last part of a path counts, whichever slash was used.
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name = name.substr(slash + 1);
    std::string out;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool plain = std::isalnum(u) != 0 || c == '.' || c == '-' || c == '_' || c == '+' || c == '(' || c == ')' || c == ' ';
        out.push_back(u < 0x80 && plain ? c : '_');  // (a name in another script becomes underscores: the file is still told apart by its version)
    }
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '.') && !endsWithJar(out)) out.pop_back();
    if (out.size() > 150) out = out.substr(out.size() - 150);
    if (!endsWithJar(out)) return {};
    // Windows keeps a few names for devices.
    std::string stem = lowerCase(out.substr(0, out.find('.')));
    static const char* const reserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* r : reserved) {
        if (stem == r) return "_" + out;
    }
    return out;
}

std::string Modrinth::imageMime(std::string_view b) {
    const auto starts = [&](std::string_view prefix) { return b.size() >= prefix.size() && b.compare(0, prefix.size(), prefix) == 0; };
    if (starts("\x89PNG\r\n\x1a\n")) return "image/png";
    if (starts("\xFF\xD8\xFF")) return "image/jpeg";
    if (starts("GIF87a") || starts("GIF89a")) return "image/gif";
    if (b.size() >= 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return "image/webp";
    // An SVG shown in an <img> runs no script; it is told by its first bytes.
    std::string_view head = b.substr(0, 1024);
    std::size_t at = head.find_first_not_of(" \t\r\n");
    if (head.size() >= 3 && head.compare(0, 3, "\xEF\xBB\xBF") == 0) at = head.find_first_not_of(" \t\r\n", 3);
    if (at != std::string_view::npos && (head.compare(at, 4, "<svg") == 0 || head.compare(at, 5, "<?xml") == 0) && head.find("<svg") != std::string_view::npos) return "image/svg+xml";
    return {};
}

std::string Modrinth::searchUrl(const ModQuery& q) const {
    json facets = json::array({json::array({"project_type:mod"})});
    if (isGameVersion(q.gameVersion)) facets.push_back(json::array({"versions:" + q.gameVersion}));
    if (isLoader(q.loader)) facets.push_back(json::array({"categories:" + q.loader}));
    const int limit = std::clamp(q.limit, 1, 50);
    const int offset = std::max(q.offset, 0);
    return apiBase_ + "/v2/search?" + formEncode({{"query", q.text.substr(0, 100)},
                                                   {"facets", facets.dump()},
                                                   {"limit", std::to_string(limit)},
                                                   {"offset", std::to_string(offset)},
                                                   {"index", q.text.empty() ? "downloads" : "relevance"}});
}

std::string Modrinth::versionsUrl(const std::string& projectId, const std::string& gameVersion, const std::string& loader) const {
    std::vector<std::pair<std::string, std::string>> fields;
    if (isGameVersion(gameVersion)) fields.emplace_back("game_versions", json::array({gameVersion}).dump());
    if (isLoader(loader)) fields.emplace_back("loaders", json::array({loader}).dump());
    return apiBase_ + "/v2/project/" + projectId + "/version" + (fields.empty() ? std::string() : "?" + formEncode(fields));
}

Result<ModSearchPage> Modrinth::parseSearch(const std::string& text) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("hits") || !j["hits"].is_array()) return Result<ModSearchPage>::fail("bad_reply", "Modrinth's answer was not a list of mods.");
    ModSearchPage page;
    for (const json& hit : j["hits"]) {
        ModHit h;
        h.projectId = stringOf(hit, "project_id");
        h.title = stringOf(hit, "title");
        if (!isProjectId(h.projectId) || h.title.empty()) continue;
        h.slug = stringOf(hit, "slug");
        h.description = stringOf(hit, "description");
        h.author = stringOf(hit, "author");
        h.iconUrl = stringOf(hit, "icon_url");
        if (hit.contains("downloads") && hit["downloads"].is_number_integer()) h.downloads = hit["downloads"].get<std::int64_t>();
        const char* categoryKey = hit.contains("display_categories") && hit["display_categories"].is_array() ? "display_categories" : "categories";
        if (hit.contains(categoryKey) && hit[categoryKey].is_array()) {
            for (const json& c : hit[categoryKey]) {
                if (c.is_string() && h.categories.size() < 8) h.categories.push_back(c.get<std::string>());
            }
        }
        page.hits.push_back(std::move(h));
    }
    page.offset = j.value("offset", 0);
    page.limit = j.value("limit", static_cast<int>(page.hits.size()));
    page.total = j.value("total_hits", static_cast<int>(page.hits.size()));
    return Result<ModSearchPage>::success(std::move(page));
}

Result<ModFile> Modrinth::pickFile(const std::string& text, const std::string& projectId) {
    const json versions = json::parse(text, nullptr, false);
    if (!versions.is_array()) return Result<ModFile>::fail("bad_reply", "Modrinth's answer was not a list of versions.");
    // The newest release first (the list is newest first); when the project has no release for this game version, the newest version of any kind.
    for (const bool releasesOnly : {true, false}) {
        for (const json& version : versions) {
            if (!version.is_object() || !version.contains("files") || !version["files"].is_array()) continue;
            if (releasesOnly && stringOf(version, "version_type") != "release") continue;
            const json* best = nullptr;
            for (const json& file : version["files"]) {
                if (!file.is_object() || !endsWithJar(stringOf(file, "filename")) || !isModrinthFileUrl(stringOf(file, "url"))) continue;
                if (!file.contains("hashes") || !isHex40(stringOf(file["hashes"], "sha1"))) continue;
                if (file.contains("size") && file["size"].is_number_unsigned() && file["size"].get<std::uint64_t>() > kMaxModBytes) continue;
                if (best == nullptr || (file.value("primary", false) && !best->value("primary", false))) best = &file;
            }
            if (best == nullptr) continue;
            ModFile out;
            out.projectId = projectId;
            out.versionId = stringOf(version, "id");
            out.versionNumber = stringOf(version, "version_number");
            if (version.contains("loaders") && version["loaders"].is_array() && !version["loaders"].empty() && version["loaders"][0].is_string()) out.loader = version["loaders"][0].get<std::string>();
            out.filename = stringOf(*best, "filename");
            out.url = stringOf(*best, "url");
            out.sha1 = lowerCase(stringOf((*best)["hashes"], "sha1"));
            if (best->contains("size") && (*best)["size"].is_number_unsigned()) out.size = (*best)["size"].get<std::uint64_t>();
            return Result<ModFile>::success(std::move(out));
        }
    }
    return Result<ModFile>::fail("no_file", "This mod has no .jar file for this version of Minecraft.");
}

// ---- the network --------------------------------------------------------------------------------------------------------------------------------------------

Result<ModSearchPage> Modrinth::search(const ModQuery& query) {
    const HttpResponse r = http_.get(searchUrl(query), {{"Accept", "application/json"}});
    if (!r.ok()) return httpFailure<ModSearchPage>(r, "the search");
    return parseSearch(r.body);
}

std::vector<std::string> Modrinth::icons(const std::vector<std::string>& urls) {
    std::vector<std::string> out(urls.size());
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (std::size_t i = next++; i < urls.size(); i = next++) {
            const std::string& url = urls[i];
            if (!isModrinthFileUrl(url)) continue;
            {
                std::lock_guard<std::mutex> lock(iconMutex_);
                const auto found = iconCache_.find(url);
                if (found != iconCache_.end()) {
                    out[i] = found->second;
                    continue;
                }
            }
            const HttpResponse r = http_.get(url, {{"Accept", "image/*"}});
            if (!r.ok() || r.body.empty() || r.body.size() > kMaxIconBytes) continue;
            const std::string mime = imageMime(r.body);
            if (mime.empty()) continue;
            out[i] = "data:" + mime + ";base64," + base64(r.body);
            std::lock_guard<std::mutex> lock(iconMutex_);
            if (iconCache_.size() >= 400) iconCache_.clear();
            iconCache_[url] = out[i];
        }
    };
    const std::size_t threads = std::min<std::size_t>(6, urls.size());
    std::vector<std::thread> pool;
    for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(work);
    if (!urls.empty()) work();
    for (std::thread& t : pool) t.join();
    return out;
}

Result<ModFile> Modrinth::chooseFile(const std::string& projectId, const std::string& gameVersion, const std::string& loader) {
    if (!isProjectId(projectId)) return Result<ModFile>::fail("bad_project", "That is not a Modrinth project.");
    const HttpResponse r = http_.get(versionsUrl(projectId, gameVersion, loader), {{"Accept", "application/json"}});
    if (!r.ok()) return httpFailure<ModFile>(r, "the versions of the mod");
    return pickFile(r.body, projectId);
}

Result<InstalledMod> Modrinth::install(const ModFile& file, const std::string& title, const fs::path& modsDir, const std::atomic<bool>& cancel) {
    using R = Result<InstalledMod>;
    if (!isModrinthFileUrl(file.url) || !isHex40(file.sha1)) return R::fail("bad_file", "Modrinth named a file this launcher will not download.");
    const std::string name = safeFileName(file.filename);
    if (name.empty()) return R::fail("bad_file", "The file's name is not one this launcher will write.");
    std::error_code ec;
    fs::create_directories(modsDir, ec);
    if (ec) return R::fail("disk", "The mods folder could not be made: " + ec.message() + ".");

    const fs::path temporary = modsDir / (".falcon-" + randomHex(6) + ".part");
    const HttpResponse r = http_.download(file.url, temporary, nullptr, &cancel);
    const auto discard = [&] {
        std::error_code ignored;
        fs::remove(temporary, ignored);
    };
    if (!r.ok()) {
        discard();
        if (r.error == "cancelled") return R::fail("cancelled", "The download was cancelled.");
        if (r.status == 0) return R::fail("network", "The file could not be downloaded (" + (r.error.empty() ? std::string("no answer") : r.error) + ").");
        return R::fail("http", "The download failed with status " + std::to_string(r.status) + ".");
    }
    const std::uintmax_t size = fs::file_size(temporary, ec);
    if (ec || size == 0 || size > kMaxModBytes || (file.size != 0 && size != file.size)) {
        discard();
        return R::fail("bad_file", "The downloaded file is not the size Modrinth named, so it was not installed.");
    }
    if (sha1FileHex(temporary) != file.sha1) {
        discard();
        return R::fail("hash_mismatch", "The download does not match Modrinth's checksum, so it was not installed.");
    }
    const fs::path target = modsDir / name;
    fs::remove(target, ec);  // (an update of the same file, or the same mod again)
    fs::rename(temporary, target, ec);
    if (ec) {
        discard();
        return R::fail("disk", "The mod could not be put in the mods folder: " + ec.message() + ".");
    }
    logLine("installed the mod " + title + " (" + file.versionNumber + ") as " + name);
    InstalledMod done;
    done.projectId = file.projectId;
    done.title = title;
    done.versionNumber = file.versionNumber;
    done.loader = file.loader;
    done.file = name;
    done.sha1 = file.sha1;
    return R::success(std::move(done));
}

}  // namespace launcher
