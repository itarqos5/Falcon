#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/text/language.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace falcon {

// What the game version this client targets says about worlds (26.3).
inline constexpr int kCurrentDataVersion = 5023;      // SharedConstants world version
inline constexpr int kFileFixThreshold = 4899;         // older worlds need upgrading before they can be played
inline constexpr const char* kCurrentSeries = "main";
inline constexpr const char* kCurrentVersionName = "26.3";

enum class WorldKind { Normal, Corrupted, Symlink };
enum class BackupStatus { None, FileFixingRequired, UpgradeToSnapshot, Downgrade };

// What the Singleplayer list shows about one world folder: the part of level.dat the game's
// LevelSummary reads, plus the rules derived from it.
struct LevelSummary {
    WorldKind kind = WorldKind::Normal;
    std::string id;                  // the folder name
    std::string levelName;           // `LevelName`, may be empty
    std::int64_t lastPlayed = 0;     // ms since the epoch; -1 for a symlinked world (sorts last)
    int gameType = 0;                // 0 survival, 1 creative, 2 adventure, 3 spectator
    bool hardcore = false;
    bool allowCommands = false;
    bool experimental = false;
    bool locked = false;             // another running game holds the world's session.lock
    bool requiresManualConversion = false;  // an old McRegion world
    bool compatible = true;          // same version series as this client
    bool requiresFileFixing = false;
    std::string versionName;         // may be empty ("unknown")
    int versionId = 0;
    BackupStatus backup = BackupStatus::None;
    std::optional<Image> icon;       // the world's own 64x64 icon.png, if it has a valid one

    // The name the list shows: the level name, or the folder name when there is none.
    [[nodiscard]] const std::string& displayName() const;
    [[nodiscard]] bool isDisabled() const { return locked || requiresManualConversion || !compatible; }
    [[nodiscard]] bool primaryActionActive() const { return kind != WorldKind::Normal || !isDisabled(); }
    [[nodiscard]] bool canEdit() const { return kind == WorldKind::Normal && !isDisabled() && !requiresFileFixing; }
    [[nodiscard]] bool canRecreate() const { return canEdit(); }
    [[nodiscard]] bool canDelete() const { return true; }  // always, even when locked
    [[nodiscard]] bool shouldBackup() const { return backup != BackupStatus::None; }
    // Translation key of the Play button's label for this world.
    [[nodiscard]] std::string primaryActionKey() const;
};

// Orders worlds like the game: most recently played first, ties by folder name (case-sensitive).
[[nodiscard]] bool levelSummaryLess(const LevelSummary& a, const LevelSummary& b);

// One piece of the info line (game mode, version, ...): its text and ARGB colour (0 = the line's
// own grey) and whether it is italic.
struct InfoSegment {
    std::string text;
    std::uint32_t color = 0;
    bool italic = false;
};
// The third line of a list entry ("Survival Mode,Commands,Version: 26.3"), exactly composed as the game does.
[[nodiscard]] std::vector<InfoSegment> levelInfoSegments(const LevelSummary& summary, const Language& language);
// The second line: "<folder> (<last played>)", or just the folder for a symlinked world.
[[nodiscard]] std::string levelIdLine(const LevelSummary& summary);

// Reads one world folder. Null when the folder has no session.lock (the game drops those). A damaged
// level.dat gives a Corrupted summary instead of failing.
[[nodiscard]] std::optional<LevelSummary> readLevelSummary(const std::filesystem::path& worldDir);

// Scans <savesDir> for worlds (a folder with level.dat or level.dat_old) and returns them sorted. A
// missing saves folder is created; nullopt means the folder cannot be used at all.
[[nodiscard]] std::optional<std::vector<LevelSummary>> scanWorlds(const std::filesystem::path& savesDir);

// Deletes a world folder. Only a real folder directly inside `savesDir` is removed (never a
// symlink, never a path with separators or "..").
[[nodiscard]] bool deleteWorld(const std::filesystem::path& savesDir, const std::string& worldId);

}  // namespace falcon
