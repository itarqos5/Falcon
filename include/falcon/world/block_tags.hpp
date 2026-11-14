#pragma once

#include "falcon/world/block_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace falcon {

// The game's block tags (data/minecraft/tags/block/**/*.json), read when first asked for and kept: "which blocks does #minecraft:mineable/pickaxe hold?".
// A tag may name blocks and other tags; both are followed. Main thread only.
class BlockTags {
public:
    BlockTags(std::filesystem::path tagDirectory, const BlockRegistry& registry) : dir_(std::move(tagDirectory)), registry_(registry) {}

    // `tag` is "#minecraft:mineable/pickaxe" (or without the # or the namespace); `blockIndex` an index into BlockRegistry::blocks().
    [[nodiscard]] bool contains(std::string_view tag, std::size_t blockIndex) const;

private:
    const std::vector<std::uint8_t>& members(const std::string& tag) const;

    std::filesystem::path dir_;
    const BlockRegistry& registry_;
    mutable std::unordered_map<std::string, std::vector<std::uint8_t>> cache_;
};

}  // namespace falcon
