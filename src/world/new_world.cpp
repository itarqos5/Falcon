#include "apex/world/new_world.hpp"

#include "apex/util/text_util.hpp"
#include "apex/world/level_summary.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

namespace apex {
namespace {

namespace fs = std::filesystem;

// FileUtil.findAvailableName's own limit on a file name.
constexpr std::size_t kMaxNameLength = 255;

bool isIllegalFileChar(char32_t c) {
    // SharedConstants.ILLEGAL_FILE_CHARACTERS, plus the other control characters (Windows refuses
    // them in a file name; the game would fall back to the name "World" for such a world).
    switch (c) {
        case U'/': case U'\\': case U'`': case U'?': case U'*': case U'<': case U'>': case U'|': case U'"': case U':':
            return true;
        default:
            return c < 0x20;
    }
}

// Names Windows reserves for devices (the game's "not portable" check): COM, CON, PRN, AUX, NUL,
// CLOCK$, COM1-9 and LPT1-9, in any case.
bool isReservedName(const std::string& name) {
    std::string upper = name;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper == "COM" || upper == "CLOCK$" || upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") {
        return true;
    }
    return upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) && upper[3] >= '1' && upper[3] <= '9';
}

// FileUtil.findAvailableName(saves, name, ""). Nullopt where the game would throw (and fall back to
// the name "World").
std::optional<std::string> findAvailableName(const fs::path& savesDir, const std::string& name) {
    std::u32string safe;
    for (const char32_t c : decodeUtf8(name)) {
        safe.push_back(isIllegalFileChar(c) || c == U'.' ? U'_' : c);  // "." too: no extension tricks
    }
    std::string base = encodeUtf8(safe);
    if (isReservedName(base)) base = "_" + base + "_";

    // "World (3)" carries on counting from 3.
    int count = 0;
    if (!base.empty() && base.back() == ')') {
        const std::size_t open = base.rfind('(');
        if (open != std::string::npos && open >= 1 && base[open - 1] == ' ') {
            const std::string digits = base.substr(open + 1, base.size() - open - 2);
            if (!std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                // not a counter: part of the name
            } else if (digits.empty() || digits.size() > 9) {
                return std::nullopt;  // Integer.parseInt fails
            } else {
                count = std::stoi(digits);
                base.resize(open - 1);
            }
        }
    }
    std::u32string units = [&] {
        std::u32string u;
        for (const char32_t c : decodeUtf8(base)) u.push_back(c);
        return u;
    }();
    if (units.size() > kMaxNameLength) base = encodeUtf8(units.substr(0, kMaxNameLength));

    std::error_code ec;
    for (int tries = 0; tries < 100000; ++tries, ++count) {
        const std::string candidate = count == 0 ? base : base + " (" + std::to_string(count) + ")";
        if (!fs::exists(fs::symlink_status(savesDir / pathFromUtf8(candidate), ec))) return candidate;
    }
    return std::nullopt;
}

bool writeFile(const fs::path& file, const std::vector<unsigned char>& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return static_cast<bool>(out);
}

const char* difficultyName(NewWorldDifficulty d) {
    switch (d) {
        case NewWorldDifficulty::Peaceful: return "peaceful";
        case NewWorldDifficulty::Easy: return "easy";
        case NewWorldDifficulty::Normal: return "normal";
        case NewWorldDifficulty::Hard: return "hard";
    }
    return "normal";
}

const char* typeName(NewWorldType t) {
    switch (t) {
        case NewWorldType::Default: return "minecraft:normal";
        case NewWorldType::Superflat: return "minecraft:flat";
        case NewWorldType::LargeBiomes: return "minecraft:large_biomes";
        case NewWorldType::Amplified: return "minecraft:amplified";
        case NewWorldType::SingleBiome: return "minecraft:single_biome_surface";
    }
    return "minecraft:normal";
}

}  // namespace

std::string findAvailableWorldFolder(const fs::path& savesDir, const std::string& worldName, const std::string& defaultName) {
    std::string base = javaTrim(worldName);
    if (base.empty()) base = defaultName;
    if (auto found = findAvailableName(savesDir, base)) return *found;
    if (auto fallback = findAvailableName(savesDir, "World")) return *fallback;
    return "World";
}

std::int64_t parseWorldSeed(const std::string& text, std::mt19937_64& rng) {
    const std::string trimmed = javaTrim(text);
    if (trimmed.empty()) return static_cast<std::int64_t>(rng());

    // Long.parseLong: an optional sign and decimal digits that fit in 64 bits.
    std::size_t i = 0;
    bool negative = false;
    if (trimmed[0] == '-' || trimmed[0] == '+') {
        negative = trimmed[0] == '-';
        i = 1;
    }
    bool valid = i < trimmed.size();
    std::uint64_t magnitude = 0;
    const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
    for (; valid && i < trimmed.size(); ++i) {
        const char c = trimmed[i];
        if (c < '0' || c > '9') {
            valid = false;
        } else if (magnitude > (limit - static_cast<std::uint64_t>(c - '0')) / 10) {
            valid = false;
        } else {
            magnitude = magnitude * 10 + static_cast<std::uint64_t>(c - '0');
        }
    }
    if (valid) return static_cast<std::int64_t>(negative ? 0 - magnitude : magnitude);
    return javaStringHash(trimmed);  // (long) String.hashCode()
}

NbtTag buildLevelDat(const NewWorldSettings& settings, std::int64_t seed, std::int64_t nowMillis) {
    using Type = NbtTag::Type;
    NbtTag difficulty = NbtTag::compound();
    difficulty.put("difficulty", NbtTag::ofString(difficultyName(settings.effectiveDifficulty())))
        .put("hardcore", NbtTag::ofBool(settings.hardcore()))
        .put("locked", NbtTag::ofBool(false));

    NbtTag version = NbtTag::compound();
    version.put("Id", NbtTag::ofInteger(Type::Int, kCurrentDataVersion))
        .put("Name", NbtTag::ofString(kCurrentVersionName))
        .put("Series", NbtTag::ofString(kCurrentSeries))
        .put("Snapshot", NbtTag::ofBool(false));

    NbtTag generation = NbtTag::compound();
    generation.put("seed", NbtTag::ofInteger(Type::Long, seed))
        .put("generate_features", NbtTag::ofBool(settings.generateStructures))
        .put("bonus_chest", NbtTag::ofBool(settings.effectiveBonusChest()));

    // This client's own note of the chosen world preset; nothing generates a world from it yet.
    NbtTag apex = NbtTag::compound();
    apex.put("world_type", NbtTag::ofString(typeName(settings.type)));

    NbtTag data = NbtTag::compound();
    data.put("version", NbtTag::ofInteger(Type::Int, 19133))
        .put("DataVersion", NbtTag::ofInteger(Type::Int, kCurrentDataVersion))
        .put("LevelName", NbtTag::ofString(javaTrim(settings.name)))
        .put("GameType", NbtTag::ofInteger(Type::Int, settings.mode == NewWorldMode::Creative ? 1 : 0))
        .put("allowCommands", NbtTag::ofBool(settings.effectiveAllowCommands()))
        .put("LastPlayed", NbtTag::ofInteger(Type::Long, nowMillis))
        .put("difficulty_settings", std::move(difficulty))
        .put("Version", std::move(version))
        .put("WorldGenSettings", std::move(generation))
        .put("Falcon", std::move(apex));

    NbtTag root = NbtTag::compound();
    root.put("Data", std::move(data));
    return root;
}

bool createWorldFolder(const fs::path& savesDir, const std::string& folder, const NewWorldSettings& settings,
                       std::int64_t seed, std::int64_t nowMillis, std::string& error) {
    std::error_code ec;
    fs::create_directories(savesDir, ec);
    if (ec || !fs::is_directory(savesDir)) {
        error = "could not create the saves folder " + utf8FromPath(savesDir);
        return false;
    }
    const fs::path dir = savesDir / pathFromUtf8(folder);
    if (!fs::create_directory(dir, ec) || ec) {
        error = ec ? "could not create the world folder: " + ec.message() : "the world folder already exists";
        return false;  // an existing folder is never touched
    }

    const auto fail = [&](const char* what) {
        error = std::string("could not write ") + what;
        std::error_code ignored;
        fs::remove_all(dir, ignored);  // only the folder created just above
        return false;
    };
    const std::vector<unsigned char> levelDat = gzipStored(serializeNbt(buildLevelDat(settings, seed, nowMillis)));
    if (!writeFile(dir / "level.dat", levelDat)) return fail("level.dat");
    // session.lock holds writeUTF("☃") (the snowman), as the game writes it.
    if (!writeFile(dir / "session.lock", {0x00, 0x03, 0xE2, 0x98, 0x83})) return fail("session.lock");
    return true;
}

}  // namespace apex
