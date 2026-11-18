#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace falcon {

// What a mod's jar says about itself, read from the metadata file the mod loaders look for: fabric.mod.json (Fabric), quilt.mod.json (Quilt), META-INF/neoforge.mods.toml (NeoForge),
// META-INF/mods.toml (Forge) or mcmod.info (old Forge). Nothing of the mod is loaded or run: only the text and the icon are read. The client's Mods screen shows this, and the launcher
// reads a jar it has downloaded with it before it keeps it.
struct ModInfo {
    std::string loader;  // "Fabric", "Quilt", "NeoForge", "Forge" or "Forge (old)": what the jar was made for
    std::string id, name, version, description, license;
    std::vector<std::string> authors, contributors;  // a Quilt contributor reads "Name (Role)"
    std::string homepage, sources, issues;
    std::string environment;           // "client", "server" or empty: both
    std::string minecraft;             // the game version it asks for, when it says
    std::vector<std::string> depends;  // "fabricloader >=0.15.0": what else it needs (the game and Java left out)
    std::string iconPath;              // where the icon is in the jar
    std::vector<unsigned char> icon;   // the icon, a PNG (empty when it has none)
    std::filesystem::path file;
    std::uint64_t size = 0;            // the jar's size in bytes
    int bundled = 0;                   // jars inside it (Fabric's jar-in-jar)
};

// The mod in a .jar, or nothing when the file is not a zip or has none of those metadata files.
[[nodiscard]] std::optional<ModInfo> readModJar(const std::filesystem::path& jar);
// Every .jar in the folder (not below it), by name. A jar that is no mod is left out.
[[nodiscard]] std::vector<ModInfo> readModsFolder(const std::filesystem::path& folder);

// The parts, for tests: each reads the text of one metadata file. `manifestVersion` is the jar's Implementation-Version, which stands for ${file.jarVersion} in a Forge file.
[[nodiscard]] std::optional<ModInfo> parseFabricModJson(const std::string& text);
[[nodiscard]] std::optional<ModInfo> parseQuiltModJson(const std::string& text);
[[nodiscard]] std::optional<ModInfo> parseModsToml(const std::string& text, std::string_view manifestVersion, std::string_view loader);
[[nodiscard]] std::optional<ModInfo> parseMcmodInfo(const std::string& text);

}  // namespace falcon
