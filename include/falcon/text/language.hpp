#pragma once

#include "falcon/assets/resource_manager.hpp"

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace falcon {

// Translation table built from minecraft:lang/<code>.json.
class Language {
public:
    // Loads English first and then `code` on top of it (so a missing line stays English); packs'
    // language files are merged in too. Throws std::runtime_error if English, or the requested
    // language, has no usable file.
    static Language load(const ResourceManager& resources, std::string_view code);
    // The namespaces mods bring (assets/<namespace>/lang/<code>.json): their translations are merged into every language that is loaded from now on, and into this one by mergeMods.
    static void setModNamespaces(std::vector<std::string> namespaces);
    void mergeMods(const ResourceManager& resources, std::string_view code);

    // Returns the translation, or the key itself when it is missing (like the game does).
    [[nodiscard]] std::string get(const std::string& key) const;
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    std::unordered_map<std::string, std::string> entries_;
};

// One language the game ships, from the `language` section of the assets' pack.mcmeta.
struct LanguageInfo {
    std::string code;    // "de_de"
    std::string name;    // "Deutsch"
    std::string region;  // "Deutschland"
    bool rtl = false;    // written right to left

    // "Deutsch (Deutschland)": how the list shows it.
    [[nodiscard]] std::string display() const { return name + " (" + region + ")"; }
};

// All the languages, ordered by code (not by name), like the game's language manager; only
// English if the assets carry no language list.
[[nodiscard]] std::vector<LanguageInfo> loadLanguageList(const ResourceManager& resources);

}  // namespace falcon
