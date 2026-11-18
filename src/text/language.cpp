#include "falcon/text/language.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace falcon {
namespace {

// Adds the entries of every copy of lang/<code>.json (the game's, then each pack's, a later one
// overriding single keys). Returns false if there was no usable file at all.
std::vector<std::string>& modNamespaces() {
    static std::vector<std::string> namespaces;
    return namespaces;
}

bool mergeLanguageFile(const ResourceManager& resources, const std::string& ns, const std::string& code, std::unordered_map<std::string, std::string>& entries) {
    const ResourceLocation loc(ns, "lang/" + code + ".json");
    bool anyValid = false;
    for (const std::string& text : resources.readAllText(loc)) {
        const nlohmann::json json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (!json.is_object()) continue;  // a broken file is skipped, the rest still load
        anyValid = true;
        if (entries.empty()) entries.reserve(json.size());
        for (const auto& [key, value] : json.items()) {
            if (value.is_string()) entries.insert_or_assign(key, value.get<std::string>());
        }
    }
    return anyValid;
}

bool mergeLanguageFiles(const ResourceManager& resources, const std::string& code, std::unordered_map<std::string, std::string>& entries) {
    const bool valid = mergeLanguageFile(resources, "minecraft", code, entries);
    for (const std::string& ns : modNamespaces()) mergeLanguageFile(resources, ns, code, entries);  // (a mod without this language is not an error)
    return valid;
}

}  // namespace

void Language::setModNamespaces(std::vector<std::string> namespaces) { modNamespaces() = std::move(namespaces); }

void Language::mergeMods(const ResourceManager& resources, std::string_view code) {
    for (const std::string& ns : modNamespaces()) {
        mergeLanguageFile(resources, ns, "en_us", entries_);
        if (code != "en_us" && !code.empty()) mergeLanguageFile(resources, ns, std::string(code), entries_);
    }
}

Language Language::load(const ResourceManager& resources, std::string_view code) {
    // English is always loaded first; another language is laid on top, so any line it lacks stays
    // English (the game does the same).
    Language lang;
    if (!mergeLanguageFiles(resources, "en_us", lang.entries_)) {
        throw std::runtime_error("missing or malformed language file minecraft:lang/en_us.json");
    }
    if (code != "en_us" && !code.empty()) {
        if (!mergeLanguageFiles(resources, std::string(code), lang.entries_)) {
            throw std::runtime_error("translation file for " + std::string(code) + " was empty or missing");
        }
    }
    return lang;
}

std::string Language::get(const std::string& key) const {
    const auto it = entries_.find(key);
    return it != entries_.end() ? it->second : key;
}

std::vector<LanguageInfo> loadLanguageList(const ResourceManager& resources) {
    std::map<std::string, LanguageInfo> byCode;  // sorted by code, like the game's TreeMap
    std::ifstream in(resources.root() / "pack.mcmeta", std::ios::binary);
    if (in) {
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const nlohmann::json json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (json.is_object() && json.contains("language") && json["language"].is_object()) {
            for (const auto& [code, info] : json["language"].items()) {
                if (!info.is_object()) continue;
                LanguageInfo l;
                l.code = code;
                l.name = info.value("name", code);
                l.region = info.value("region", std::string());
                l.rtl = info.value("bidirectional", false);
                byCode.emplace(code, std::move(l));
            }
        }
    }
    if (byCode.empty()) byCode.emplace("en_us", LanguageInfo{"en_us", "English", "US", false});  // the game's fallback
    std::vector<LanguageInfo> list;
    list.reserve(byCode.size());
    for (auto& entry : byCode) list.push_back(std::move(entry.second));
    return list;
}

}  // namespace falcon
