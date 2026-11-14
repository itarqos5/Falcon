#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace apex {

// "namespace:path" identifier, the same shape Minecraft uses for assets.
struct ResourceLocation {
    std::string ns = "minecraft";
    std::string path;

    ResourceLocation() = default;
    ResourceLocation(std::string ns_, std::string path_)
        : ns(std::move(ns_)), path(std::move(path_)) {}

    // Accepts "ns:path" or just "path" (which defaults to the minecraft namespace).
    [[nodiscard]] static ResourceLocation parse(std::string_view text);

    [[nodiscard]] std::string toString() const { return ns + ':' + path; }

    // Same namespace, path replaced by prefix + path + suffix.
    [[nodiscard]] ResourceLocation decorated(std::string_view prefix,
                                             std::string_view suffix) const;

    bool operator==(const ResourceLocation&) const = default;
};

}  // namespace apex
