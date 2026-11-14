#include "falcon/world/level_info.hpp"

#include "falcon/assets/nbt.hpp"
#include "falcon/util/text_util.hpp"

#include <fstream>
#include <iterator>

namespace falcon {

std::optional<LevelInfo> readLevelInfo(const std::filesystem::path& worldDir) {
    std::ifstream in(worldDir / "level.dat", std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto root = parseNbt(bytes);
    if (!root) return std::nullopt;
    const NbtTag* data = root->get("Data");
    if (data == nullptr || data->type != NbtTag::Type::Compound) return std::nullopt;

    LevelInfo info;
    info.folder = utf8FromPath(worldDir.filename());
    info.name = data->getString("LevelName");
    if (info.name.empty()) info.name = info.folder;
    info.gameType = static_cast<int>(data->getInt("GameType", 0));
    if (const NbtTag* difficulty = data->get("difficulty_settings")) info.hardcore = difficulty->getBool("hardcore");
    info.allowCommands = data->get("allowCommands") != nullptr ? data->getBool("allowCommands") : info.gameType == 1;  // (as the world list reads it)

    if (const NbtTag* generation = data->get("WorldGenSettings")) info.seed = generation->getInt("seed", 0);
    if (info.seed == 0) info.seed = data->getInt("RandomSeed", 0);

    // Falcon's own note: the world type the player picked.
    const NbtTag* own = data->get("Falcon");
    if (own != nullptr) {
        info.madeByFalcon = true;
        const std::string type = own->getString("world_type");
        if (type == "minecraft:flat") {
            info.style = TerrainStyle::Flat;
        } else if (type == "minecraft:amplified") {
            info.style = TerrainStyle::Amplified;
        }
    }
    return info;
}

}  // namespace falcon
