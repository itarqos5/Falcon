#pragma once

#include "apex/world/terrain.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace apex {

// What Play needs to know about a world folder, read from its level.dat.
struct LevelInfo {
    std::string folder;       // the folder's name inside saves/
    std::string name;         // the name the player gave it
    std::int64_t seed = 0;
    TerrainStyle style = TerrainStyle::Hills;
    int gameType = 0;         // 0 survival, 1 creative, 2 adventure, 3 spectator
    bool hardcore = false;
    bool allowCommands = false;  // cheats: the game mode switcher and the commands need them (Create New World: "Allow Cheats")
    // Whether Falcon made this world (Create New World leaves its own note in level.dat). A world made by
    // Minecraft has real terrain in region files that Falcon cannot read: it can only show a generated stand-in.
    bool madeByFalcon = false;
};

// Reads <worldDir>/level.dat; nullopt if it is missing or damaged.
[[nodiscard]] std::optional<LevelInfo> readLevelInfo(const std::filesystem::path& worldDir);

}  // namespace apex
