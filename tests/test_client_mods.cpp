// The Mods screen's data: what a mod's jar says about itself (mods/mod_info.hpp) and which mods are optimization mods (mods/optimization_mods.hpp).
#include "test_support.hpp"

#include "falcon/game/mods_screen.hpp"
#include "falcon/mods/mod_info.hpp"
#include "falcon/mods/optimization_mods.hpp"

namespace fs = std::filesystem;

void test_mod_metadata() {
    // Fabric: authors as names and as objects, a licence list, icons by size, a dependency on the game.
    const auto fabric = falcon::parseFabricModJson(R"({
        "schemaVersion": 1, "id": "examplemod", "version": "1.4.0", "name": "Example Mod",
        "description": "Does examples.", "authors": ["Alice", {"name": "Bob", "contact": {"email": "b@x"}}],
        "contributors": ["Carol"], "license": ["MIT", "CC0-1.0"], "environment": "client",
        "icon": {"16": "a16.png", "128": "assets/examplemod/icon.png"},
        "contact": {"homepage": "https://example.org", "sources": "https://git.example.org", "issues": "https://git.example.org/issues"},
        "depends": {"fabricloader": ">=0.15.0", "minecraft": "~26.3", "java": ">=21", "fabric-api": "*"}
    })");
    CHECK(fabric.has_value());
    if (fabric) {
        CHECK(fabric->loader == "Fabric" && fabric->id == "examplemod" && fabric->name == "Example Mod" && fabric->version == "1.4.0");
        CHECK(fabric->authors == (std::vector<std::string>{"Alice", "Bob"}) && fabric->contributors == (std::vector<std::string>{"Carol"}));
        CHECK(fabric->license == "MIT, CC0-1.0" && fabric->environment == "client");
        CHECK(fabric->iconPath == "assets/examplemod/icon.png");  // the biggest
        CHECK(fabric->homepage == "https://example.org" && fabric->sources == "https://git.example.org" && fabric->issues == "https://git.example.org/issues");
        CHECK(fabric->minecraft == "~26.3");
        CHECK((fabric->depends == std::vector<std::string>{"fabric-api", "fabricloader >=0.15.0"}));  // (the game and Java are not in the list; "*" asks for no version)
    }
    CHECK(!falcon::parseFabricModJson("not json").has_value() && !falcon::parseFabricModJson(R"({"name": "no id"})").has_value());

    // Quilt: the metadata is inside quilt_loader; contributors are name to role.
    const auto quilt = falcon::parseQuiltModJson(R"({
        "schema_version": 1, "quilt_loader": {"id": "qmod", "version": "2.0", "metadata": {"name": "Q Mod", "description": "Quilted.",
        "contributors": {"Dana": "Owner", "Eve": "Artist"}, "license": {"name": "Apache License 2.0", "id": "Apache-2.0"}, "icon": "q.png",
        "contact": {"homepage": "https://q.example"}}, "depends": ["quilt_loader", {"id": "minecraft", "versions": ">=26.3"}, {"id": "lib", "versions": ["1.x", "2.x"]}]},
        "minecraft": {"environment": "dedicated_server"}})");
    CHECK(quilt.has_value());
    if (quilt) {
        CHECK(quilt->loader == "Quilt" && quilt->name == "Q Mod" && quilt->version == "2.0" && quilt->environment == "server");
        CHECK((quilt->authors == std::vector<std::string>{"Dana (Owner)", "Eve (Artist)"}) && quilt->license == "Apache License 2.0" && quilt->homepage == "https://q.example");
        CHECK(quilt->minecraft == ">=26.3" && (quilt->depends == std::vector<std::string>{"quilt_loader", "lib 1.x or 2.x"}));
    }

    // Forge and NeoForge: mods.toml, the version from the jar's manifest, a multi-line description, the dependencies of this mod only.
    const std::string toml = R"TOML(modLoader="javafml"
loaderVersion="[47,)"
license="LGPL-3.0"
issueTrackerURL="https://issues.example"
[[mods]]
modId="forgemod"
version="${file.jarVersion}"
displayName="Forge Mod"
displayURL="https://forge.example"
logoFile="logo.png"
authors="Fay, Gus"
credits="Thanks to Hal"
description='''
Line one.
Line two.
'''
[[dependencies.forgemod]]
    modId="forge"
    mandatory=true
    versionRange="[47,)"
[[dependencies.forgemod]]
    modId="minecraft"
    mandatory=true
    versionRange="[26.3]"
[[dependencies.forgemod]]
    modId="optionalthing"
    mandatory=false
    versionRange="*"
[[dependencies.othermod]]
    modId="unrelated"
    mandatory=true
)TOML";
    const auto forge = falcon::parseModsToml(toml, "3.1.4", "Forge");
    CHECK(forge.has_value());
    if (forge) {
        CHECK(forge->loader == "Forge" && forge->id == "forgemod" && forge->name == "Forge Mod" && forge->version == "3.1.4");
        CHECK(forge->description == "Line one.\nLine two." && forge->license == "LGPL-3.0" && forge->issues == "https://issues.example");
        CHECK(forge->authors == (std::vector<std::string>{"Fay", "Gus"}) && forge->contributors == (std::vector<std::string>{"Thanks to Hal"}));
        CHECK(forge->homepage == "https://forge.example" && forge->iconPath == "logo.png" && forge->minecraft == "[26.3]");
        CHECK((forge->depends == std::vector<std::string>{"forge [47,)"}));  // required ones of this mod: not the optional one, not another mod's
    }
    CHECK(!falcon::parseModsToml("modLoader=\"javafml\"\n", "", "Forge").has_value());

    const auto old = falcon::parseMcmodInfo(R"([{"modid": "oldmod", "name": "Old Mod", "version": "0.9", "description": "Ancient.", "authorList": ["Ivy"], "url": "https://old.example", "mcversion": "1.12.2", "logoFile": "old.png"}])");
    CHECK(old.has_value() && old->loader == "Forge (old)" && old->id == "oldmod" && old->authors == std::vector<std::string>{"Ivy"} && old->minecraft == "1.12.2");
    const auto wrapped = falcon::parseMcmodInfo(R"({"modListVersion": 2, "modList": [{"modid": "m2", "name": "M2"}]})");
    CHECK(wrapped.has_value() && wrapped->id == "m2");
}

void test_mods_folder() {
    const fs::path folder = fs::temp_directory_path() / "falcon_mods_test";
    std::error_code ec;
    fs::remove_all(folder, ec);
    // A Fabric jar with a picture and a bundled library, a Forge jar, a jar with no mod information, a file that is no jar at all, and a text file.
    writeBytes(folder / "zeta.jar", makeZip({{"fabric.mod.json", R"({"schemaVersion":1,"id":"zeta","version":"1","name":"Zeta","icon":"icon.png"})", false},
                                             {"icon.png", "not really a png", false},
                                             {"META-INF/jars/lib.jar", "x", false}}));
    writeBytes(folder / "Alpha.JAR", makeZip({{"META-INF/mods.toml", "[[mods]]\nmodId=\"alpha\"\nversion=\"${file.jarVersion}\"\n", false}, {"META-INF/MANIFEST.MF", "Manifest-Version: 1.0\r\nImplementation-Version: 9.9\r\n", false}}));
    writeBytes(folder / "library.jar", makeZip({{"some/Class.class", "x", false}}));
    writeBytes(folder / "broken.jar", {1, 2, 3, 4});
    writeText(folder / "readme.txt", "hello");
    const std::vector<falcon::ModInfo> mods = falcon::readModsFolder(folder);
    CHECK(mods.size() == 4);  // (the text file is not a jar)
    if (mods.size() == 4) {
        CHECK(mods[0].name == "alpha" && mods[0].loader == "Forge" && mods[0].version == "9.9");  // sorted by name, case apart
        CHECK(mods[1].name == "broken" && mods[1].loader == "Unknown" && mods[2].name == "library" && mods[2].loader == "Unknown");
        CHECK(mods[3].name == "Zeta" && mods[3].icon.size() == 16 && mods[3].bundled == 1 && mods[3].size > 0 && mods[3].file.filename() == "zeta.jar");
    }
    CHECK(falcon::readModsFolder(folder / "missing").empty());
    CHECK(!falcon::readModJar(folder / "library.jar").has_value());

    // The facts the screen shows under the description: what is empty is left out.
    falcon::ModInfo info;
    info.id = "x";
    info.loader = "Fabric";
    info.authors = {"A", "B"};
    info.file = "x.jar";
    info.size = 1536;
    info.bundled = 2;
    bool hasAuthors = false, hasLicense = false, hasFile = false, hasBundled = false;
    for (const auto& [label, value] : falcon::ModsScreen::facts(info)) {
        hasAuthors = hasAuthors || (label == "Authors" && value == "A, B");
        hasLicense = hasLicense || label == "License";
        hasFile = hasFile || (label == "File" && value == "x.jar (1.5 KB)");
        hasBundled = hasBundled || (label == "Bundled" && value == "2 libraries");
    }
    CHECK(hasAuthors && !hasLicense && hasFile && hasBundled);
    fs::remove_all(folder, ec);
}

void test_optimization_mods() {
    // Known ones, whatever the spelling of the id or slug.
    for (const char* name : {"sodium", "Sodium", "lithium", "ferrite-core", "ferritecore", "Ferrite_Core", "iris", "immediatelyfast", "entityculling", "c2me-fabric", "dynamic_fps", "optifine", "embeddium"}) CHECK(falcon::isKnownOptimizationMod(name));
    for (const char* name : {"fabric-api", "modmenu", "journeymap", "appleskin", "", "sodiumlike-but-not"}) CHECK(!falcon::isKnownOptimizationMod(name));

    // The verdict: known, by Modrinth's category (a mod not on the list), by what it needs, or none.
    const auto known = falcon::judgeOptimization("sodium", "Sodium", {}, {});
    CHECK(known.blocked && known.reason.find("Sodium") != std::string::npos && known.reason.find("never will be") != std::string::npos);
    const auto filed = falcon::judgeOptimization("brand-new-speedup", "Brand New Speedup", {"utility", "Optimization"}, {});
    CHECK(filed.blocked && filed.reason.find("Optimization on Modrinth") != std::string::npos);
    const auto needs = falcon::judgeOptimization("shiny-shaders", "Shiny Shaders", {"decoration"}, {"fabric-api", "sodium"});
    CHECK(needs.blocked && needs.reason.find("needs sodium") != std::string::npos);
    CHECK(!falcon::judgeOptimization("modmenu", "Mod Menu", {"utility"}, {"fabric-api"}).blocked);
    CHECK(!falcon::judgeOptimization("modmenu", "Mod Menu", {}, {}).reason.size());
}
