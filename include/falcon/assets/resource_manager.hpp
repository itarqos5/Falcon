#pragma once

#include "falcon/assets/resource_location.hpp"
#include "falcon/assets/resource_source.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace falcon {

// Read-only view of the game's resources: an unpacked "assets" directory (the Default pack),
// with the selected resource packs layered on top.
// A location like minecraft:textures/gui/title/minecraft.png maps to
// <root>/minecraft/textures/gui/title/minecraft.png in the base assets; a pack that has the same
// file overrides it, and a pack later in the list overrides an earlier one.
//
// The pack list can be replaced at any time from any thread (setPacks): readers always see either
// the old list or the new one, never a mix, and swapping is instant.
class ResourceManager {
public:
    using PackList = std::vector<std::shared_ptr<const ResourceSource>>;  // lowest priority first

    explicit ResourceManager(std::filesystem::path assetsRoot, PackList packs = {});
    ResourceManager(const ResourceManager& other);
    ResourceManager& operator=(const ResourceManager&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    // The folder with the game's exported data (registries.json, gameplay-facts.json, data/minecraft/..., worldgen, ...). By default the
    // folder that holds the assets folder; --data points it somewhere else (the launcher downloads the assets itself).
    [[nodiscard]] const std::filesystem::path& dataRoot() const { return dataRoot_; }
    void setDataRoot(std::filesystem::path dir) { dataRoot_ = std::move(dir); }
    // Where the file would be in the base assets (ignores packs).
    [[nodiscard]] std::filesystem::path resolve(const ResourceLocation& loc) const;

    [[nodiscard]] bool exists(const ResourceLocation& loc) const;
    // The highest-priority copy of the file: from the last pack that has it, else the base assets.
    [[nodiscard]] std::optional<std::vector<unsigned char>> readBytes(const ResourceLocation& loc) const;
    [[nodiscard]] std::optional<std::string> readText(const ResourceLocation& loc) const;
    // Every copy of a text file, lowest priority first (base assets, then each pack that has it),
    // for files the game merges instead of replacing (language files, sounds.json).
    [[nodiscard]] std::vector<std::string> readAllText(const ResourceLocation& loc) const;

    // Who supplies a file: one entry per copy, lowest priority first, nullptr standing for the
    // base assets. Two managers with equal answers give the same file, so a reload can skip it.
    [[nodiscard]] std::vector<const void*> providers(const ResourceLocation& loc) const;

    [[nodiscard]] PackList packs() const;
    void setPacks(PackList packs);

    // The same base assets with a different pack list.
    [[nodiscard]] ResourceManager withPacks(PackList packs) const {
        ResourceManager other(root_, std::move(packs));
        other.dataRoot_ = dataRoot_;
        return other;
    }

private:
    using Snapshot = std::shared_ptr<const PackList>;
    [[nodiscard]] Snapshot snapshot() const;

    std::filesystem::path root_;
    std::filesystem::path dataRoot_;
    mutable std::mutex mutex_;
    Snapshot packs_;
};

}  // namespace falcon
