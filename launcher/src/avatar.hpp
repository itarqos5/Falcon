#pragma once

#include <string>

namespace launcher {

// A player's head for the UI, from the skin texture: the face (8x8 pixels at 8,8 of the skin) with the hat layer (at 40,8) on top, enlarged
// with hard pixel edges, as a "data:image/png;base64,..." address a page can show. Empty when the picture is not a usable skin.
[[nodiscard]] std::string avatarFromSkin(const std::string& skinPng, int scale = 12);

// The same for 8x8 RGBA pixels (row by row): the skin's face already cut out. Used for the demo mode's pretend player.
[[nodiscard]] std::string avatarFromFace(const unsigned char* rgba8x8, int scale = 12);

// Only addresses on Mojang's texture server are fetched for a skin.
[[nodiscard]] bool isSkinUrl(const std::string& url);

[[nodiscard]] std::string base64(const std::string& bytes);

}  // namespace launcher
