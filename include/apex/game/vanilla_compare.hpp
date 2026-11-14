#pragma once

#include <filesystem>

namespace apex {

class ResourceManager;

// `Falcon --compare-vanilla <world folder>`: a developer check of the terrain against the game's own. The folder is a world the real
// game made (its level.dat for the seed and its region files); Falcon generates the same chunks from the same seed and compares them
// block by block, first the generator alone and then with Falcon's own decoration. It prints how many blocks agree, which blocks the
// game has where Falcon has something else (the biggest differences first, with the height range they sit at), and the spawn of
// both. Returns 0 (1 when the data or the world cannot be read).
int runVanillaCompare(const ResourceManager& resources, const std::filesystem::path& gameDataDir, const std::filesystem::path& cacheDir,
                      const std::filesystem::path& worldDir);

}  // namespace apex
