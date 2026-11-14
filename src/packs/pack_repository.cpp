#include "apex/packs/pack_repository.hpp"

#include "apex/assets/zip_archive.hpp"
#include "apex/util/text_util.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>

namespace apex {
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
                                    const fs::path& gameDir) {
    PackRepository repo;
    repo.assetsRoot_ = assetsRoot;
    repo.gameDir_ = gameDir;

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

    // Packs the player dropped into <game dir>/resourcepacks: folders and .zip files.
    std::error_code ec;
    for (fs::directory_iterator it(repo.packDirectory(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path path = it->path();
        const std::string name = path.filename().string();
        PackInfo p;
        p.id = "file/" + name;
        p.title = name;
        p.descriptionColor = kPackDescriptionColor;

        std::optional<std::string> mcmeta;
        std::optional<std::vector<unsigned char>> icon;
        if (it->is_directory(ec)) {
            mcmeta = readFileText(path / "pack.mcmeta");
            icon = readFileBytes(path / "pack.png");
        } else if (path.extension() == ".zip") {
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
        repo.packs_.emplace(p.id, std::move(p));
    }
    return repo;
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
    const fs::path path = packDirectory() / id.substr(kFilePrefix.size());
    std::error_code ec;
    if (fs::is_directory(path, ec)) return makeDirectoryPack(path);
    return openZip(path);
}

const PackInfo* PackRepository::find(const std::string& id) const {
    const auto it = packs_.find(id);
    return it != packs_.end() ? &it->second : nullptr;
}

}  // namespace apex
