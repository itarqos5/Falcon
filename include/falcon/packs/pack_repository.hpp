#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_source.hpp"
#include "falcon/text/language.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace falcon {

// Whether a pack's declared format range includes the game's resource pack format.
enum class PackCompatibility { Compatible, TooOld, TooNew, Broken };

struct PackInfo {
    std::string id;               // "vanilla", "high_contrast", "programmer_art", "file/<name>"
    std::string title;
    std::string description;      // as shown, including any "(built-in)" suffix
    std::uint32_t descriptionColor = 0xFF808080u;
    bool required = false;        // cannot be unselected (the Default pack)
    bool builtIn = false;
    PackCompatibility compatibility = PackCompatibility::Compatible;
    std::optional<Image> icon;    // decoded pack.png, if the pack has one
    std::filesystem::path path;   // a pack the player put in a folder: where it is (a folder or a .zip)
};

// The packs the Resource Packs screen can list: the game's three built-in packs plus anything in
// <game dir>/resourcepacks and in the extra folders (Falcon's own, %LOCALAPPDATA%\Falcon\resourcepacks): folders and .zip files.
class PackRepository {
public:
    static constexpr int kResourcePackFormat = 97;  // 26.3

    // `assetsRoot` is the unpacked "assets" folder (it holds minecraft/resourcepacks/*.zip and,
    // next to it, the Default pack's pack.png). `gameDir` is the .minecraft folder.
    static PackRepository load(const Language& language, const std::filesystem::path& assetsRoot,
                               const std::filesystem::path& gameDir, std::vector<std::filesystem::path> extraPackDirectories = {});

    // Looks in the pack folders again (a pack that was put there while the game ran, or taken away). True when the list of packs changed.
    bool refresh();
    // The folders the packs are looked for in, the game's first: a pack of the same name in a later one is not listed.
    [[nodiscard]] std::vector<std::filesystem::path> packDirectories() const;

    // Packs keyed and ordered by id, like the game's repository.
    [[nodiscard]] const std::map<std::string, PackInfo>& packs() const { return packs_; }
    [[nodiscard]] const PackInfo* find(const std::string& id) const;
    [[nodiscard]] std::filesystem::path packDirectory() const { return gameDir_ / "resourcepacks"; }

    // Opens a pack's files so they can be layered over the game's resources (zips are
    // memory-mapped, so this is instant). Null for "vanilla" (the base assets themselves), for an
    // unknown id, and for a pack that cannot be opened.
    [[nodiscard]] std::shared_ptr<const ResourceSource> openSource(const std::string& id) const;

    // Compatibility of a pack.mcmeta "pack" block: min_format/max_format if present, else
    // pack_format (which must match exactly).
    [[nodiscard]] static PackCompatibility checkFormat(const std::string& mcmetaJson);
    // The pack.mcmeta description as plain text (a string or a {"text": ...} component).
    [[nodiscard]] static std::string readDescription(const std::string& mcmetaJson);

private:
    std::map<std::string, PackInfo> packs_;
    std::filesystem::path assetsRoot_;
    std::filesystem::path gameDir_;
    std::vector<std::filesystem::path> extraDirectories_;
    void scanFolders();  // adds the packs of the folders (not the built-in ones)
};

}  // namespace falcon
