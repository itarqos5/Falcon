#include "falcon/mods/mod_info.hpp"

#include "falcon/assets/zip_archive.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <system_error>

namespace falcon {
namespace {

using json = nlohmann::json;

std::string trimmed(std::string_view s) {
    std::size_t begin = 0, end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) --end;
    return std::string(s.substr(begin, end - begin));
}

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }
bool startsWith(const std::string& s, const std::string& head) { return s.compare(0, head.size(), head) == 0; }

// A string field of an object ("" when it is missing or is something else).
std::string stringOf(const json& j, const char* key) {
    if (!j.is_object()) return {};
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// People: a list of names, or of objects with a "name" (Fabric: {"name": "...", "contact": {...}}).
std::vector<std::string> peopleOf(const json& list) {
    std::vector<std::string> out;
    if (!list.is_array()) return out;
    for (const json& p : list) {
        if (p.is_string()) out.push_back(p.get<std::string>());
        else if (p.is_object() && !stringOf(p, "name").empty()) out.push_back(stringOf(p, "name"));
    }
    return out;
}

// A licence: a name, a list of names, or (Quilt) objects with a "name" or an "id".
std::string licenseOf(const json& v) {
    std::vector<std::string> names;
    const auto one = [&](const json& item) {
        if (item.is_string()) names.push_back(item.get<std::string>());
        else if (item.is_object()) names.push_back(!stringOf(item, "name").empty() ? stringOf(item, "name") : stringOf(item, "id"));
    };
    if (v.is_array()) {
        for (const json& item : v) one(item);
    } else {
        one(v);
    }
    std::string out;
    for (const std::string& n : names) {
        if (n.empty()) continue;
        out += out.empty() ? n : ", " + n;
    }
    return out;
}

// An icon: a path in the jar, or sizes to paths ({"16": "a.png", "128": "b.png"}): the biggest.
std::string iconPathOf(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (!v.is_object()) return {};
    int best = -1;
    std::string path;
    for (const auto& [size, file] : v.items()) {
        if (!file.is_string()) continue;
        const int n = std::atoi(size.c_str());
        if (n > best) best = n, path = file.get<std::string>();
    }
    return path;
}

// A version range: a string, or a list of them (any of).
std::string rangeOf(const json& v) {
    std::string out;
    if (v.is_string()) out = v.get<std::string>();
    else if (v.is_array()) {
        for (const json& item : v) {
            if (!item.is_string()) continue;
            out += out.empty() ? item.get<std::string>() : " or " + item.get<std::string>();
        }
    }
    return out == "*" ? std::string() : out;
}

std::string dependency(const std::string& id, const std::string& range) { return range.empty() ? id : id + " " + range; }

json parseJson(const std::string& text) { return json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true); }

}  // namespace

std::optional<ModInfo> parseFabricModJson(const std::string& text) {
    const json j = parseJson(text);
    if (!j.is_object() || stringOf(j, "id").empty()) return std::nullopt;
    ModInfo m;
    m.loader = "Fabric";
    m.id = stringOf(j, "id");
    m.name = stringOf(j, "name").empty() ? m.id : stringOf(j, "name");
    m.version = stringOf(j, "version");
    m.description = stringOf(j, "description");
    m.authors = peopleOf(j.value("authors", json::array()));
    m.contributors = peopleOf(j.value("contributors", json::array()));
    if (j.contains("license")) m.license = licenseOf(j["license"]);
    if (j.contains("icon")) m.iconPath = iconPathOf(j["icon"]);
    const std::string environment = stringOf(j, "environment");
    m.environment = environment == "client" || environment == "server" ? environment : std::string();
    if (const auto contact = j.find("contact"); contact != j.end()) {
        m.homepage = stringOf(*contact, "homepage");
        m.sources = stringOf(*contact, "sources");
        m.issues = stringOf(*contact, "issues");
    }
    if (const auto depends = j.find("depends"); depends != j.end() && depends->is_object()) {
        for (const auto& [id, range] : depends->items()) {
            if (id == "minecraft") m.minecraft = rangeOf(range);
            else if (id != "java") m.depends.push_back(dependency(id, rangeOf(range)));
        }
    }
    return m;
}

std::optional<ModInfo> parseQuiltModJson(const std::string& text) {
    const json j = parseJson(text);
    if (!j.is_object() || !j.contains("quilt_loader") || !j["quilt_loader"].is_object()) return std::nullopt;
    const json& loader = j["quilt_loader"];
    if (stringOf(loader, "id").empty()) return std::nullopt;
    ModInfo m;
    m.loader = "Quilt";
    m.id = stringOf(loader, "id");
    m.version = stringOf(loader, "version");
    const json meta = loader.value("metadata", json::object());
    m.name = stringOf(meta, "name").empty() ? m.id : stringOf(meta, "name");
    m.description = stringOf(meta, "description");
    if (meta.contains("license")) m.license = licenseOf(meta["license"]);
    if (meta.contains("icon")) m.iconPath = iconPathOf(meta["icon"]);
    if (const auto people = meta.find("contributors"); people != meta.end() && people->is_object()) {
        for (const auto& [name, role] : people->items()) m.authors.push_back(role.is_string() ? name + " (" + role.get<std::string>() + ")" : name);
    }
    if (const auto contact = meta.find("contact"); contact != meta.end()) {
        m.homepage = stringOf(*contact, "homepage");
        m.sources = stringOf(*contact, "sources");
        m.issues = stringOf(*contact, "issues");
    }
    if (const auto minecraft = j.find("minecraft"); minecraft != j.end()) {
        const std::string environment = stringOf(*minecraft, "environment");
        m.environment = environment == "client" ? "client" : environment == "dedicated_server" ? "server" : "";
    }
    if (const auto depends = loader.find("depends"); depends != loader.end() && depends->is_array()) {
        for (const json& d : *depends) {
            const std::string id = d.is_string() ? d.get<std::string>() : stringOf(d, "id");
            if (id.empty()) continue;
            const std::string range = d.is_object() && d.contains("versions") ? rangeOf(d["versions"]) : std::string();
            if (id == "minecraft") m.minecraft = range;
            else if (id != "java") m.depends.push_back(dependency(id, range));
        }
    }
    return m;
}

namespace {

// The part of TOML a mods.toml uses: tables ([[mods]], [[dependencies.id]]) of "key = value" lines, a value being a string (basic, literal or multi-line) or a plain word or number. Lists are skipped.
struct TomlTable {
    std::string name;
    std::map<std::string, std::string> values;
};

std::vector<TomlTable> readToml(const std::string& text) {
    std::vector<TomlTable> tables(1);
    std::vector<std::string> lines;
    for (std::size_t at = 0; at <= text.size();) {
        const std::size_t end = text.find('\n', at);
        lines.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string line = trimmed(lines[i]);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            const std::size_t first = line.find_first_not_of('['), last = line.find(']');
            if (first == std::string::npos || last == std::string::npos || last < first) continue;
            tables.push_back({trimmed(line.substr(first, last - first)), {}});
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = trimmed(line.substr(0, equals));
        if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'')) key = key.substr(1, key.size() - 2);
        std::string value = trimmed(line.substr(equals + 1));
        std::string result;
        if (startsWith(value, "'''") || startsWith(value, "\"\"\"")) {
            const std::string delimiter = value.substr(0, 3);
            std::string body = value.substr(3);
            std::size_t close = body.find(delimiter);
            while (close == std::string::npos && i + 1 < lines.size()) {
                body += "\n" + lines[++i];
                close = body.find(delimiter);
            }
            result = body.substr(0, close);
            if (!result.empty() && result.front() == '\n') result.erase(0, 1);
            if (!result.empty() && result.front() == '\r') result.erase(0, 2);
        } else if (!value.empty() && value[0] == '"') {
            for (std::size_t k = 1; k < value.size() && value[k] != '"'; ++k) {
                if (value[k] == '\\' && k + 1 < value.size()) {
                    const char e = value[++k];
                    result.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
                } else {
                    result.push_back(value[k]);
                }
            }
        } else if (!value.empty() && value[0] == '\'') {
            const std::size_t close = value.find('\'', 1);
            result = value.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        } else if (!value.empty() && value[0] == '[') {
            int depth = 0;
            for (std::string part = value;; part = lines[++i]) {
                for (const char c : part) depth += c == '[' ? 1 : c == ']' ? -1 : 0;
                if (depth <= 0 || i + 1 >= lines.size()) break;
            }
            continue;  // (a list: not kept)
        } else {
            const std::size_t comment = value.find('#');
            result = trimmed(comment == std::string::npos ? value : value.substr(0, comment));
        }
        tables.back().values[key] = result;
    }
    return tables;
}

std::vector<std::string> splitNames(const std::string& s) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= s.size()) {
        const std::size_t comma = s.find(',', at);
        const std::string name = trimmed(s.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (!name.empty()) out.push_back(name);
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}

}  // namespace

std::optional<ModInfo> parseModsToml(const std::string& text, std::string_view manifestVersion, std::string_view loader) {
    const std::vector<TomlTable> tables = readToml(text);
    const TomlTable* mod = nullptr;
    for (const TomlTable& t : tables) {
        if (t.name == "mods" && t.values.contains("modId")) {
            mod = &t;
            break;
        }
    }
    if (mod == nullptr) return std::nullopt;
    const auto get = [&](const TomlTable& t, const char* key) {
        const auto it = t.values.find(key);
        return it == t.values.end() ? std::string() : it->second;
    };
    ModInfo m;
    m.loader = std::string(loader);
    m.id = get(*mod, "modId");
    m.name = get(*mod, "displayName").empty() ? m.id : get(*mod, "displayName");
    m.version = get(*mod, "version");
    if (m.version.find("${") != std::string::npos) m.version = std::string(manifestVersion);  // ${file.jarVersion}
    m.description = trimmed(get(*mod, "description"));
    m.authors = splitNames(!get(*mod, "authors").empty() ? get(*mod, "authors") : get(tables[0], "authors"));
    m.license = get(tables[0], "license");
    m.homepage = get(*mod, "displayURL");
    m.issues = !get(*mod, "issueTrackerURL").empty() ? get(*mod, "issueTrackerURL") : get(tables[0], "issueTrackerURL");
    m.iconPath = get(*mod, "logoFile");
    if (const std::string credits = trimmed(get(*mod, "credits")); !credits.empty()) m.contributors.push_back(credits);
    const std::string prefix = "dependencies.";
    for (const TomlTable& t : tables) {
        if (!startsWith(t.name, prefix)) continue;
        std::string owner = trimmed(t.name.substr(prefix.size()));
        if (owner.size() >= 2 && (owner.front() == '"' || owner.front() == '\'')) owner = owner.substr(1, owner.size() - 2);
        if (owner != m.id) continue;
        const std::string id = get(t, "modId"), range = get(t, "versionRange");
        const std::string type = get(t, "type");
        const bool required = type == "required" || get(t, "mandatory") == "true" || (type.empty() && get(t, "mandatory").empty());
        if (id.empty()) continue;
        if (id == "minecraft") m.minecraft = range;
        else if (required && id != "java") m.depends.push_back(dependency(id, range));
    }
    return m;
}

std::optional<ModInfo> parseMcmodInfo(const std::string& text) {
    json j = parseJson(text);
    if (j.is_object() && j.contains("modList")) j = j["modList"];
    if (j.is_object() && j.contains("modlist")) j = j["modlist"];
    if (j.is_array()) {
        if (j.empty()) return std::nullopt;
        j = j[0];
    }
    if (!j.is_object() || stringOf(j, "modid").empty()) return std::nullopt;
    ModInfo m;
    m.loader = "Forge (old)";
    m.id = stringOf(j, "modid");
    m.name = stringOf(j, "name").empty() ? m.id : stringOf(j, "name");
    m.version = stringOf(j, "version");
    m.description = stringOf(j, "description");
    m.homepage = stringOf(j, "url");
    m.iconPath = stringOf(j, "logoFile");
    m.minecraft = stringOf(j, "mcversion");
    m.authors = peopleOf(j.value("authorList", j.value("authors", json::array())));
    if (const std::string credits = stringOf(j, "credits"); !credits.empty()) m.contributors.push_back(credits);
    return m;
}

namespace {

// "Implementation-Version: 1.2.3" of META-INF/MANIFEST.MF (what ${file.jarVersion} is in a Forge file).
std::string manifestVersionOf(const ZipArchive& zip) {
    const auto manifest = zip.readText("META-INF/MANIFEST.MF");
    if (!manifest) return {};
    const std::string key = "Implementation-Version:";
    const std::size_t at = manifest->find(key);
    if (at == std::string::npos) return {};
    const std::size_t end = manifest->find_first_of("\r\n", at);
    return trimmed(manifest->substr(at + key.size(), end == std::string::npos ? std::string::npos : end - at - key.size()));
}

}  // namespace

std::optional<ModInfo> readModJar(const std::filesystem::path& jar) {
    const auto zip = ZipArchive::open(jar);
    if (!zip) return std::nullopt;
    std::optional<ModInfo> info;
    if (const auto text = zip->readText("fabric.mod.json")) info = parseFabricModJson(*text);
    if (!info) {
        if (const auto text = zip->readText("quilt.mod.json")) info = parseQuiltModJson(*text);
    }
    if (!info) {
        if (const auto text = zip->readText("META-INF/neoforge.mods.toml")) info = parseModsToml(*text, manifestVersionOf(*zip), "NeoForge");
    }
    if (!info) {
        if (const auto text = zip->readText("META-INF/mods.toml")) info = parseModsToml(*text, manifestVersionOf(*zip), "Forge");
    }
    if (!info) {
        if (const auto text = zip->readText("mcmod.info")) info = parseMcmodInfo(*text);
    }
    if (!info) return std::nullopt;
    info->file = jar;
    std::error_code ec;
    const auto size = std::filesystem::file_size(jar, ec);
    info->size = ec ? 0 : static_cast<std::uint64_t>(size);
    if (!info->iconPath.empty()) {
        std::string path = info->iconPath;
        while (!path.empty() && path.front() == '/') path.erase(0, 1);
        if (auto icon = zip->read(path)) info->icon = std::move(*icon);
    }
    for (const std::string& name : zip->entryNames()) {
        if ((startsWith(name, "META-INF/jars/") || startsWith(name, "META-INF/jarjar/")) && endsWith(lowered(name), ".jar")) ++info->bundled;
    }
    return info;
}

std::vector<ModInfo> readModsFolder(const std::filesystem::path& folder) {
    std::vector<ModInfo> mods;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || lowered(it->path().extension().string()) != ".jar") continue;
        if (auto mod = readModJar(it->path())) {
            mods.push_back(std::move(*mod));
            continue;
        }
        // A jar with no mod information (a library, or a file that is not a jar): shown by its file name, so that nothing in the folder is missing from the list.
        ModInfo unknown;
        unknown.loader = "Unknown";
        unknown.name = it->path().stem().string();
        unknown.id = unknown.name;
        unknown.description = "This file has no mod information (no fabric.mod.json, quilt.mod.json, mods.toml or mcmod.info), so there is nothing to show about it.";
        unknown.file = it->path();
        const auto size = it->file_size(ec);
        unknown.size = ec ? 0 : static_cast<std::uint64_t>(size);
        mods.push_back(std::move(unknown));
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        const std::string x = lowered(a.name), y = lowered(b.name);
        return x != y ? x < y : a.file < b.file;
    });
    return mods;
}

}  // namespace falcon
