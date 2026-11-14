#pragma once

#include "falcon/text/font.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace falcon {

// A run of text with one style: what a server's message of the day is made of.
struct TextSpan {
    std::string text;
    std::uint32_t rgb = 0;     // 0xRRGGBB, meaningful when `hasColor`
    bool hasColor = false;     // otherwise the caller's default colour applies
    bool bold = false;
    bool italic = false;
    bool underlined = false;
    bool strikethrough = false;
    bool operator==(const TextSpan&) const = default;
};
using StyledText = std::vector<TextSpan>;

// The text of `styled` with the styling dropped.
[[nodiscard]] std::string plainText(const StyledText& styled);

// Parses "section sign" formatting codes (0-9, a-f colours; l bold, m strikethrough, n underline, o italic,
// r reset; k "obfuscated" is accepted and ignored). A colour code also clears the other styles, like the game.
[[nodiscard]] StyledText parseLegacyFormatting(std::string_view text);

// Parses a chat component: a string, an object ({"text", "color", "bold", "italic", "underlined",
// "strikethrough", "extra": [...]}) or an array of components. Children inherit the style of their parent.
// `translate` shows its key, since there is no translation to look up. Nested deeper than 16 levels,
// or anything else that is not a component, is dropped.
[[nodiscard]] StyledText parseChatComponent(const nlohmann::json& component);
[[nodiscard]] StyledText parseChatComponentJson(std::string_view jsonText);  // empty if the JSON is malformed

// Breaks `styled` into at most `maxLines` lines no wider than `maxWidth`: at "\n", at spaces, and inside
// a word that is wider than a line. Text beyond the last line is dropped (no ellipsis).
using TextMeasure = std::function<int(std::string_view text, bool bold)>;  // the width of text in pixels
[[nodiscard]] std::vector<StyledText> wrapStyledText(const TextMeasure& measure, const StyledText& styled, int maxWidth,
                                                     std::size_t maxLines);
// The same, measuring with the game's font.
[[nodiscard]] inline std::vector<StyledText> wrapStyledText(const Font& font, const StyledText& styled, int maxWidth,
                                                            std::size_t maxLines) {
    return wrapStyledText(TextMeasure([&font](std::string_view t, bool bold) { return font.width(t, bold); }), styled, maxWidth, maxLines);
}

}  // namespace falcon
