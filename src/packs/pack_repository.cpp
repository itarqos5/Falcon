#include "falcon/packs/pack_repository.hpp"

#include "falcon/assets/zip_archive.hpp"
#include "falcon/util/text_util.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>

namespace falcon {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr std::uint32_t kBuiltInDescriptionColor = 0xFFAAAAAAu;  // ChatFormatting.GRAY
constexpr std::uint32_t kPackDescriptionColor = 0xFF808080u;

std::optional<std::string> readFileText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::optional<Image> readIcon(const std::optional<std::vector<unsigned char>>& bytes) {
    if (!bytes) return std::nullopt;
    return decodePng(*bytes);
}

std::optional<std::vector<unsigned char>> readFileBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

}  // namespace

PackCompatibility PackRepository::checkFormat(const std::string& mcmetaJson) {
    const json root = json::parse(mcmetaJson, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object() || !root.contains("pack") || !root["pack"].is_object()) {
        return PackCompatibility::Broken;
    }
    const json& pack = root["pack"];
    int minFormat = -1;
    int maxFormat = -1;
    if (pack.contains("min_format") && pack["min_format"].is_number_integer()) {
        minFormat = pack["min_format"].get<int>();
        maxFormat = pack.contains("max_format") && pack["max_format"].is_number_integer()
                        ? pack["max_format"].get<int>()
                        : minFormat;
    } else if (pack.contains("pack_format") && pack["pack_format"].is_number_integer()) {
        minFormat = maxFormat = pack["pack_format"].get<int>();
    } else {
        return PackCompatibility::Broken;
    }
    if (kResourcePackFormat < minFormat) return PackCompatibility::TooNew;
    if (kResourcePackFormat > maxFormat) return PackCompatibility::TooOld;
    return PackCompatibility::Compatible;
}

std::string PackRepository::readDescription(const std::string& mcmetaJson) {
    const json root = json::parse(mcmetaJson, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object() || !root.contains("pack") || !root["pack"].is_object()) return {};
    const json& d = root["pack"].value("description", json());
    if (d.is_string()) return d.get<std::string>();
    if (d.is_object() && d.contains("text") && d["text"].is_string()) return d["text"].get<std::string>();
    return {};
}

PackRepository PackRepository::load(const Language& language, const fs::path& assetsRoot,
                                    const fs::path& gameDir, std::vector<fs::path> extraPackDirectories) {
    PackRepository repo;
    repo.assetsRoot_ = assetsRoot;
    repo.gameDir_ = gameDir;
    repo.extraDirectories_ = std::move(extraPackDirectories);

    const auto builtInDescription = [&](const std::string& text) {
        return formatLanguageString(language.get("pack.nameAndSource"),
                                    {text, language.get("pack.source.builtin")});
    };

    // Default: the game's own resources. Its icon is the pack.png that sits next to "assets".
    {
        PackInfo p;
        p.id = "vanilla";
        p.title = language.get("resourcePack.vanilla.name");
        p.description = builtInDescription(language.get("resourcePack.vanilla.description"));
        p.descriptionColor = kBuiltInDescriptionColor;
        p.required = true;
        p.builtIn = true;
        p.icon = readIcon(readFileBytes(assetsRoot.parent_path() / "pack.png"));
        repo.packs_.emplace(p.id, std::move(p));
    }

    // High Contrast and Programmer Art ship as zips inside the assets.
    for (const char* id : {"high_contrast", "programmer_art"}) {
        const auto zip = ZipArchive::open(assetsRoot / "minecraft" / "resourcepacks" / (std::string(id) + ".zip"));
        if (!zip) continue;
        PackInfo p;
        p.id = id;
        p.title = language.get(std::string("resourcePack.") + id + ".name");
        const auto mcmeta = zip->readText("pack.mcmeta");
        p.description = builtInDescription(mcmeta ? readDescription(*mcmeta) : std::string());
        p.descriptionColor = kBuiltInDescriptionColor;
        p.builtIn = true;
        p.compatibility = mcmeta ? checkFormat(*mcmeta) : PackCompatibility::Broken;
        p.icon = readIcon(zip->read("pack.png"));
        repo.packs_.emplace(p.id, std::move(p));
    }

    repo.scanFolders();
    return repo;
}

std::vector<fs::path> PackRepository::packDirectories() const {
    std::vector<fs::path> dirs = {packDirectory()};
    for (const fs::path& extra : extraDirectories_) dirs.push_back(extra);
    return dirs;
}

// Packs the player dropped into the pack folders: folders and .zip files.
void PackRepository::scanFolders() {
    std::error_code ec;
    for (const fs::path& directory : packDirectories()) {
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path path = it->path();
            const std::string name = path.filename().string();
            if (packs_.count("file/" + name) != 0) continue;  // (the same name in an earlier folder wins)
            PackInfo p;
            p.id = "file/" + name;
            p.title = name;
            p.descriptionColor = kPackDescriptionColor;
            p.path = path;

            std::optional<std::string> mcmeta;
            std::optional<std::vector<unsigned char>> icon;
            if (it->is_directory(ec)) {
                mcmeta = readFileText(path / "pack.mcmeta");
                icon = readFileBytes(path / "pack.png");
            } else if (path.extension() == ".zip" || path.extension() == ".ZIP") {
                if (const auto zip = ZipArchive::open(path)) {
                    mcmeta = zip->readText("pack.mcmeta");
                    icon = zip->read("pack.png");
                }
            } else {
                continue;  // not a pack
            }
            p.description = mcmeta ? readDescription(*mcmeta) : std::string();
            p.compatibility = mcmeta ? checkFormat(*mcmeta) : PackCompatibility::Broken;
            p.icon = readIcon(icon);
            packs_.emplace(p.id, std::move(p));
        }
        ec.clear();
    }
}

bool PackRepository::refresh() {
    std::map<std::string, std::pair<std::filesystem::path, std::string>> before;
    for (const auto& [id, info] : packs_) {
        if (id.rfind("file/", 0) == 0) before[id] = {info.path, info.description};
    }
    for (auto it = packs_.begin(); it != packs_.end();) {
        it = it->first.rfind("file/", 0) == 0 ? packs_.erase(it) : std::next(it);
    }
    scanFolders();
    std::map<std::string, std::pair<std::filesystem::path, std::string>> after;
    for (const auto& [id, info] : packs_) {
        if (id.rfind("file/", 0) == 0) after[id] = {info.path, info.description};
    }
    return before != after;
}

std::shared_ptr<const ResourceSource> PackRepository::openSource(const std::string& id) const {
    const PackInfo* info = find(id);
    if (info == nullptr || id == "vanilla") return nullptr;

    const auto openZip = [](const fs::path& file) -> std::shared_ptr<const ResourceSource> {
        auto zip = ZipArchive::open(file);
        return zip ? makeZipPack(std::move(*zip)) : nullptr;
    };
    if (info->builtIn) return openZip(assetsRoot_ / "minecraft" / "resourcepacks" / (id + ".zip"));

    constexpr std::string_view kFilePrefix = "file/";
    if (id.rfind(kFilePrefix, 0) != 0) return nullptr;
    const fs::path path = !info->path.empty() ? info->path : packDirectory() / id.substr(kFilePrefix.size());
    std::error_code ec;
    if (fs::is_directory(path, ec)) return makeDirectoryPack(path);
    return openZip(path);
}

const PackInfo* PackRepository::find(const std::string& id) const {
    const auto it = packs_.find(id);
    return it != packs_.end() ? &it->second : nullptr;
}

}  // namespace falcon
