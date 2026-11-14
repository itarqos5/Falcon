#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace falcon {

// One flat face of a model part: four corners (x, y, z in 1/16 blocks of the part's own space, then u, v across the texture, 0..1) and
// the face's outward normal. The corners run counter-clockwise seen from outside.
struct EntityPolygon {
    std::array<std::array<float, 5>, 4> vertex{};
    std::array<float, 3> normal{};
};

// A part of an entity model (the game's ModelPart): where it sits against its parent (offset in 1/16 blocks, rotation in radians,
// scale), the faces of its boxes and its child parts.
struct EntityPart {
    std::string name;
    std::array<float, 3> offset{};
    std::array<float, 3> rotation{};  // x, y, z
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::vector<EntityPolygon> polygons;
    std::vector<EntityPart> children;

    [[nodiscard]] EntityPart* find(const std::string& childName);
};

// Reads one model out of the game's `entity-models.json` (the models as baked by the game: "minecraft:player#main",
// "minecraft:chest#main", ...), straight from the data folder. The file is 2.4 MB; only the one model is parsed.
[[nodiscard]] std::optional<EntityPart> loadEntityModel(const std::filesystem::path& gameDataDir, const std::string& key, std::string& error);

}  // namespace falcon
