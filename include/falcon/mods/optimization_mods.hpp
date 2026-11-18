#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace falcon {

// Optimization mods (Sodium, Lithium, Iris, FerriteCore ...) replace parts of the game that Falcon has written itself (the renderer, the lighting, the chunk loading), so they cannot
// work with it, and never will: Falcon does not support them. The launcher does not install them, and the Mods screen says so when it finds one in the folder.
//
// Not one fixed list, so that a new one is caught too: a project is an optimization mod when it is a known one (by its id or slug), when Modrinth files it under the "optimization"
// category, or when it needs one (Iris and Sodium Extra need Sodium).
struct OptimizationVerdict {
    bool blocked = false;
    std::string reason;  // for the person: why it is not installed, in a sentence
};

// A mod known by its id or its Modrinth slug (case, dashes and underscores do not matter).
[[nodiscard]] bool isKnownOptimizationMod(std::string_view idOrSlug);

// `idOrSlug` and `title` are the mod's; `categories` are Modrinth's (or empty); `needs` are the ids or slugs of the mods it needs (or empty).
[[nodiscard]] OptimizationVerdict judgeOptimization(std::string_view idOrSlug, std::string_view title, const std::vector<std::string>& categories, const std::vector<std::string>& needs);

}  // namespace falcon
