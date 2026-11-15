#pragma once

#include <filesystem>
#include <optional>

namespace falcon {

// The folders Falcon looks in, by the rules of the system it runs on.
//
//   Windows   game folder  %APPDATA%\.minecraft              Falcon's own  %LOCALAPPDATA%\Falcon
//   Linux     game folder  ~/.minecraft                      Falcon's own  $XDG_DATA_HOME/Falcon (~/.local/share/Falcon)
//
// Both are empty when the system gives no place for them (no home folder, no environment).

// The folder of the Minecraft Java Edition game (".minecraft").
[[nodiscard]] std::optional<std::filesystem::path> minecraftDir();

// Falcon's own folder for what it keeps: the launcher's downloads, resource packs, the chat keys.
[[nodiscard]] std::optional<std::filesystem::path> falconDataDir();

}  // namespace falcon
