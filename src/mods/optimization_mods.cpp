#include "falcon/mods/optimization_mods.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace falcon {
namespace {

// Lower case, letters and digits only: "Ferrite-Core", "ferrite_core" and "ferritecore" are one name.
std::string normalized(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// Ids and slugs of the mods that make the game faster by replacing its parts, and of the add-ons and ports that only work on top of one of them. Written normalized.
constexpr std::array kKnown = {
    // the renderer
    "sodium", "sodiumextra", "reesessodiumoptions", "sodiumoptions", "sodiumdynamiclights", "sodiumshadowypathblocks", "embeddium", "embeddiumextra", "embeddiumplus", "rubidium",
    "rubidiumextra", "rubidiumplus", "magnesium", "indium", "iris", "irisshaders", "oculus", "nvidium", "immediatelyfast", "entityculling", "moreculling", "cullleaves",
    "enhancedblockentities", "fastquit", "betterfps", "optifine", "optifabric", "optifabricplus", "vulkanmod", "dashloader",
    // the lighting and the chunks
    "phosphor", "starlight", "lithium", "canary", "radium", "ferritecore", "krypton", "c2me", "c2mefabric", "c2meneoforge", "vmp", "vmpfabric", "modernfix", "memoryleakfix", "lazydfu",
    "smoothboot", "smoothbootfabric", "smoothbootreloaded", "dynamicfps", "fastload", "noisium", "threadtweak", "clumps", "saturn", "ksyxis", "scalablelux", "moonrise", "paperium",
};

}  // namespace

bool isKnownOptimizationMod(std::string_view idOrSlug) {
    const std::string name = normalized(idOrSlug);
    if (name.empty()) return false;
    return std::any_of(kKnown.begin(), kKnown.end(), [&](const char* known) { return name == known; });
}

OptimizationVerdict judgeOptimization(std::string_view idOrSlug, std::string_view title, const std::vector<std::string>& categories, const std::vector<std::string>& needs) {
    const std::string label = title.empty() ? std::string(idOrSlug) : std::string(title);
    OptimizationVerdict verdict;
    if (isKnownOptimizationMod(idOrSlug)) {
        verdict.blocked = true;
        verdict.reason = label + " is an optimization mod (or only works with one). It replaces parts of the game that Falcon has written itself, so it is not compatible with Falcon and never will be.";
        return verdict;
    }
    for (const std::string& category : categories) {
        if (normalized(category) == "optimization") {
            verdict.blocked = true;
            verdict.reason = label + " is filed under Optimization on Modrinth. Optimization mods replace parts of the game that Falcon has written itself, so they are not compatible with Falcon and never will be.";
            return verdict;
        }
    }
    for (const std::string& needed : needs) {
        if (isKnownOptimizationMod(needed)) {
            verdict.blocked = true;
            verdict.reason = label + " needs " + needed + ", an optimization mod. Optimization mods are not compatible with Falcon and never will be.";
            return verdict;
        }
    }
    return verdict;
}

}  // namespace falcon
