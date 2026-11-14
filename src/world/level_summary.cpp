#include "falcon/world/level_summary.hpp"

#include "falcon/assets/nbt.hpp"
#include "falcon/platform/file_lock.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

namespace falcon {
namespace fs = std::filesystem;
namespace {

constexpr std::uint32_t kRed = 0xFFFF5555u;         // ChatFormatting.RED
constexpr std::uint32_t kYellow = 0xFFFFFF55u;      // ChatFormatting.YELLOW
constexpr std::uint32_t kHardRed = 0xFFFF0000u;     // hardcore, corrupted and symlink warnings
constexpr int kLegacyDifficultyDataVersion = 4771;  // before this, hardcore lives at the top of Data

std::optional<std::vector<unsigned char>> readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::optional<Image> readIcon(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    const auto bytes = readFile(file);
    if (!bytes) return std::nullopt;
    auto image = decodePng(*bytes);
    if (!image || image->width != 64 || image->height != 64) return std::nullopt;  // the game requires exactly 64x64
    return image;
}

std::int64_t modifiedMillis(const fs::path& file) {
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) return -1;
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::duration_cast<std::chrono::milliseconds>(system.time_since_epoch()).count();
}

bool hasLevelFile(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "level.dat", ec) || fs::is_regular_file(dir / "level.dat_old", ec);
}

LevelSummary corrupted(const fs::path& dir, bool locked) {
    LevelSummary s;
    s.kind = WorldKind::Corrupted;
    s.id = utf8FromPath(dir.filename());
    s.locked = locked;
    std::int64_t modified = modifiedMillis(dir / "level.dat");
    if (modified < 0) modified = modifiedMillis(dir / "level.dat_old");
    s.lastPlayed = modified;
    s.icon = readIcon(dir / "icon.png");
    return s;
}

bool isExperimentalFeature(const std::string& id) {
    return id == "minecraft:trade_rebalance" || id == "minecraft:redstone_experiments" ||
           id == "minecraft:minecart_improvements";
}

}  // namespace

const std::string& LevelSummary::displayName() const {
    return kind == WorldKind::Normal && !levelName.empty() ? levelName : id;
}

std::string LevelSummary::primaryActionKey() const {
    if (kind == WorldKind::Corrupted) return "recover_world.button";
    if (kind == WorldKind::Symlink) return "symlink_warning.more_info";
    return requiresFileFixing ? "selectWorld.upgrade_and_play" : "selectWorld.select";
}

bool levelSummaryLess(const LevelSummary& a, const LevelSummary& b) {
    if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed;
    return a.id < b.id;
}

std::string levelIdLine(const LevelSummary& s) {
    if (s.lastPlayed == -1) return s.id;
    // The game's short localized date and time; for en_US that is "M/d/yy, h:mm a".
    const std::time_t seconds = static_cast<std::time_t>(s.lastPlayed / 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    char date[64];
    std::snprintf(date, sizeof(date), "%d/%d/%02d, %d:%02d %s", local.tm_mon + 1, local.tm_mday, local.tm_year % 100,
                  hour12, local.tm_min, local.tm_hour < 12 ? "AM" : "PM");
    return s.id + " (" + date + ")";
}

std::vector<InfoSegment> levelInfoSegments(const LevelSummary& s, const Language& language) {
    if (s.kind == WorldKind::Corrupted) return {{language.get("recover_world.warning"), kHardRed, false}};
    if (s.kind == WorldKind::Symlink) return {{language.get("symlink_warning.title"), kHardRed, false}};
    if (s.locked) return {{language.get("selectWorld.locked"), kRed, false}};
    if (s.requiresManualConversion) return {{language.get("selectWorld.conversion"), kRed, false}};
    if (!s.compatible) {
        return {{formatLanguageString(language.get("selectWorld.incompatible.info"), {s.versionName}), kRed, false}};
    }

    static constexpr const char* kModes[] = {"survival", "creative", "adventure", "spectator"};
    std::vector<InfoSegment> out;
    if (s.hardcore) {
        out.push_back({language.get("gameMode.hardcore"), kHardRed, false});
    } else {
        const int mode = s.gameType >= 0 && s.gameType <= 3 ? s.gameType : 0;
        out.push_back({language.get(std::string("gameMode.") + kModes[mode]), 0, false});
    }
    if (s.allowCommands) out.push_back({"," + language.get("selectWorld.commands"), 0, false});
    if (s.experimental) {
        out.push_back({",", 0, false});  // no space: that is what the game's strings give
        out.push_back({language.get("selectWorld.experimental"), kYellow, false});
    }
    out.push_back({"," + language.get("selectWorld.version") + " ", 0, false});
    const std::string version = s.versionName.empty() ? language.get("selectWorld.versionUnknown") : s.versionName;
    const bool downgrade = s.backup == BackupStatus::Downgrade;
    out.push_back({version, s.shouldBackup() && downgrade ? kRed : 0, s.shouldBackup() && !downgrade});
    return out;
}

std::optional<LevelSummary> readLevelSummary(const fs::path& dir) {
    const LockProbe probe = probeSessionLock(dir / "session.lock");
    if (probe == LockProbe::Missing) return std::nullopt;  // the game silently drops such folders
    const bool locked = probe == LockProbe::Locked;

    const fs::path levelDat = dir / "level.dat";
    std::error_code ec;
    if (!fs::is_regular_file(levelDat, ec)) return corrupted(dir, locked);  // only level.dat_old, or neither

    if (fs::is_symlink(levelDat, ec)) {  // symlinks are only allowed through allowed_symlinks.txt, which we do not read
        LevelSummary s;
        s.kind = WorldKind::Symlink;
        s.id = utf8FromPath(dir.filename());
        s.lastPlayed = -1;
        s.icon = readIcon(dir / "icon.png");
        return s;
    }

    const auto bytes = readFile(levelDat);
    const auto root = bytes ? parseNbt(*bytes) : std::nullopt;
    if (!root || root->type != NbtTag::Type::Compound) return corrupted(dir, locked);
    static const NbtTag kEmpty = [] {
        NbtTag t;
        t.type = NbtTag::Type::Compound;
        return t;
    }();
    const NbtTag* dataTag = root->get("Data");
    const NbtTag& data = dataTag != nullptr && dataTag->type == NbtTag::Type::Compound ? *dataTag : kEmpty;

    const int worldFormat = static_cast<int>(data.getInt("version", 0));
    if (worldFormat != 19132 && worldFormat != 19133) return corrupted(dir, locked);  // "Unknown level data version"

    LevelSummary s;
    s.id = utf8FromPath(dir.filename());
    s.locked = locked;
    s.levelName = data.getString("LevelName");
    s.lastPlayed = data.getInt("LastPlayed", 0);
    const auto gameType = data.getInt("GameType", 0);
    s.gameType = gameType >= 0 && gameType <= 3 ? static_cast<int>(gameType) : 0;
    s.allowCommands = data.contains("allowCommands") ? data.getBool("allowCommands") : s.gameType == 1;
    s.requiresManualConversion = worldFormat != 19133;

    const auto dataVersion = static_cast<int>(data.getInt("DataVersion", -1));
    s.requiresFileFixing = (dataVersion < 4772 ? 0 : dataVersion) < kFileFixThreshold;

    // Hardcore: in `difficulty_settings` when it is complete, else the pre-26.1 top-level field.
    const NbtTag* difficulty = data.get("difficulty_settings");
    const bool difficultyValid =
        difficulty != nullptr && difficulty->type == NbtTag::Type::Compound && difficulty->contains("hardcore") &&
        difficulty->contains("locked") && difficulty->get("difficulty") != nullptr &&
        difficulty->get("difficulty")->type == NbtTag::Type::String;
    if (difficultyValid) {
        s.hardcore = difficulty->getBool("hardcore");
    } else if (dataVersion < kLegacyDifficultyDataVersion) {
        s.hardcore = data.getBool("hardcore");
    }

    const NbtTag* version = data.get("Version");
    std::string series = kCurrentSeries;
    if (version != nullptr && version->type == NbtTag::Type::Compound) {
        s.versionName = version->getString("Name", kCurrentVersionName);
        s.versionId = static_cast<int>(version->getInt("Id", kCurrentDataVersion));
        series = version->getString("Series", kCurrentSeries);
    }  // without a Version compound: no name, id 0
    s.compatible = series == kCurrentSeries;

    if ((s.versionId < 4772 ? 0 : s.versionId) < kFileFixThreshold) {
        s.backup = BackupStatus::FileFixingRequired;
    } else if (s.versionId > kCurrentDataVersion) {
        s.backup = BackupStatus::Downgrade;
    }  // UpgradeToSnapshot only applies to a snapshot client; 26.3 is stable

    if (const NbtTag* features = data.get("enabled_features"); features != nullptr && features->type == NbtTag::Type::List) {
        for (const NbtTag& feature : features->items) {
            if (feature.type == NbtTag::Type::String && isExperimentalFeature(feature.text)) s.experimental = true;
        }
    }

    s.icon = readIcon(dir / "icon.png");
    return s;
}

std::optional<std::vector<LevelSummary>> scanWorlds(const fs::path& savesDir) {
    std::error_code ec;
    if (!fs::exists(savesDir, ec)) fs::create_directories(savesDir, ec);
    if (!fs::is_directory(savesDir, ec)) return std::nullopt;

    std::vector<LevelSummary> worlds;
    for (fs::directory_iterator it(savesDir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_directory(entryEc) || !hasLevelFile(it->path())) continue;
        if (auto summary = readLevelSummary(it->path())) worlds.push_back(std::move(*summary));
    }
    std::sort(worlds.begin(), worlds.end(), levelSummaryLess);
    return worlds;
}

bool deleteWorld(const fs::path& savesDir, const std::string& worldId) {
    if (worldId.empty() || worldId == "." || worldId == ".." ||
        worldId.find_first_of("/\\:") != std::string::npos) {
        return false;
    }
    const fs::path target = savesDir / pathFromUtf8(worldId);
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (ec || status.type() != fs::file_type::directory) return false;  // missing, a file, or a symlink
    fs::remove_all(target, ec);
    return !ec && !fs::exists(target, ec);
}

}  // namespace falcon
