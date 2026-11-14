#pragma once

#include "apex/assets/nbt.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>

namespace apex {

// The Game Mode button's values, in its order. Hardcore is a game mode of its own in the Create
// New World screen; it is saved as Survival + hardcore + Hard difficulty.
enum class NewWorldMode { Survival, Hardcore, Creative };
enum class NewWorldDifficulty { Peaceful, Easy, Normal, Hard };
// The World Type button's values (the game's world presets), in its order.
enum class NewWorldType { Default, Superflat, LargeBiomes, Amplified, SingleBiome };

// Everything the Create New World screen lets the player choose.
struct NewWorldSettings {
    std::string name;      // as typed (not trimmed)
    std::string seedText;  // as typed; blank = a random seed
    NewWorldMode mode = NewWorldMode::Survival;
    NewWorldDifficulty difficulty = NewWorldDifficulty::Normal;
    std::optional<bool> allowCommands;  // unset: on only in Creative, until the player touches the button
    bool generateStructures = true;
    bool bonusChest = false;
    NewWorldType type = NewWorldType::Default;

    // The values the game really uses (WorldCreationUiState): Hardcore forces Hard difficulty, no
    // cheats and no bonus chest.
    [[nodiscard]] bool hardcore() const { return mode == NewWorldMode::Hardcore; }
    [[nodiscard]] NewWorldDifficulty effectiveDifficulty() const {
        return hardcore() ? NewWorldDifficulty::Hard : difficulty;
    }
    [[nodiscard]] bool effectiveAllowCommands() const {
        return hardcore() ? false : allowCommands.value_or(mode == NewWorldMode::Creative);
    }
    [[nodiscard]] bool effectiveBonusChest() const { return hardcore() ? false : bonusChest; }
};

// The folder a new world called `worldName` would be saved in: the trimmed name (or `defaultName`
// when that is empty) made safe for the file system, with " (1)", " (2)", ... added until no folder in
// `savesDir` has that name (FileUtil.findAvailableName). Only looks; creates nothing.
[[nodiscard]] std::string findAvailableWorldFolder(const std::filesystem::path& savesDir, const std::string& worldName,
                                                   const std::string& defaultName);

// What the Seed box means: blank gives a random seed, a number is that number, anything else is the
// Java hash of the text (WorldOptions.parseSeed).
[[nodiscard]] std::int64_t parseWorldSeed(const std::string& text, std::mt19937_64& rng);

// The contents of level.dat for a world that was just created (a root compound holding `Data`).
[[nodiscard]] NbtTag buildLevelDat(const NewWorldSettings& settings, std::int64_t seed, std::int64_t nowMillis);

// Creates <savesDir>/<folder> with a level.dat and a session.lock, so the world shows up in the
// Singleplayer list. level.dat is written first and session.lock last: the list ignores a folder
// without a lock, so it never shows a half-written world.
//
// This is only the world's description (name, game mode, difficulty, cheats, seed, version). There is no
// world generation or save data in it yet, and it is not a complete save as the game itself writes
// one. Returns false and sets `error` if the folder cannot be created or written; a folder it created is
// removed again in that case.
[[nodiscard]] bool createWorldFolder(const std::filesystem::path& savesDir, const std::string& folder,
                                     const NewWorldSettings& settings, std::int64_t seed, std::int64_t nowMillis,
                                     std::string& error);

}  // namespace apex
