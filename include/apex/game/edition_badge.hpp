#pragma once

#include "apex/assets/image.hpp"
#include "apex/assets/resource_manager.hpp"
#include "apex/gfx/texture_manager.hpp"

#include <optional>

namespace apex {

// Builds a "C++ EDITION" version of the game's "JAVA EDITION" title badge (the 512x64
// textures/gui/title/edition.png) out of the badge's own pixels, so it keeps the same lettering:
//   * EDITION is copied unchanged,
//   * C is the letter E with its middle arm removed (that is how the logo font draws a C),
//   * + is drawn with the font's outline / extrusion rules and colours sampled from the E.
// The whole text is re-centred. Returns nullopt if `javaEdition` does not look like the
// expected badge (wrong size or lettering), so a resource pack with a different badge is left alone.
[[nodiscard]] std::optional<Image> makeCppEditionBadge(const Image& javaEdition);

// The badge's texture path, as the texture manager knows it.
inline constexpr const char* kEditionTexturePath = "gui/title/edition.png";

// The C++ badge made from whichever edition.png the resources currently supply (a resource pack
// may replace it); nullopt if that cannot be done. Touches no GL, so it is safe on any thread.
[[nodiscard]] std::optional<Image> buildCppEditionBadge(const ResourceManager& resources);

// Reads edition.png from the assets and registers the C++ version as "gui/title/edition.png" in
// the texture manager. Returns false (keeping the original badge) if that is not possible.
bool installCppEditionBadge(const ResourceManager& resources, TextureManager& textures);

}  // namespace apex
