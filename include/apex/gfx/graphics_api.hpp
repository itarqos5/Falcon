#pragma once

#include <string>
#include <string_view>

namespace apex {

// The graphics APIs the game can be told to draw with (Video Settings -> Graphics API). There is no "Default": the player picks one.
// Metal is meant to join them later (macOS); add it here and in graphicsApiId() / graphicsApiRenders() and nowhere else needs to learn
// about the list - the setting screen, the options file and the start-up choice all go through these.
enum class GraphicsApi : unsigned char { OpenGL, Vulkan };
inline constexpr int kGraphicsApiCount = 2;

// "opengl", "vulkan": how the choice is kept in the options file.
[[nodiscard]] std::string_view graphicsApiId(GraphicsApi api);
// The reverse; `fallback` for anything that is not an id.
[[nodiscard]] GraphicsApi parseGraphicsApi(std::string_view id, GraphicsApi fallback = GraphicsApi::OpenGL);
// The game's translation key of its name ("options.graphicsApi.opengl"), as the setting button shows it.
[[nodiscard]] std::string_view graphicsApiTranslationKey(GraphicsApi api);

// Whether this build can actually draw the game with the API. OpenGL can. Vulkan cannot yet: its renderer is the next piece of work
// (see docs/RENDERING.md), so choosing it is remembered but the game still draws with OpenGL.
[[nodiscard]] bool graphicsApiRenders(GraphicsApi api);
// What the game really draws with for a choice: the API itself when it can render, otherwise OpenGL. `note` (may be null) is set to a
// one-line explanation when the two differ, empty otherwise.
[[nodiscard]] GraphicsApi effectiveGraphicsApi(GraphicsApi requested, std::string* note = nullptr);
// The wording of the setting's tooltip for an API, including (for one that cannot render yet) that OpenGL is used instead.
[[nodiscard]] std::string graphicsApiTooltip(GraphicsApi api);

}  // namespace apex
