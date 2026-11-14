#pragma once

#include "apex/text/font.hpp"

#include <string>
#include <vector>

namespace apex {

// Word-wraps `text` to lines no wider than `maxWidth` pixels (a word wider than a line is split).
[[nodiscard]] std::vector<std::string> wrapText(const Font& font, const std::string& text, int maxWidth);

// `text` unchanged if it fits, otherwise cut so that it plus "..." fits (ComponentRenderUtils.clipText).
[[nodiscard]] std::string ellipsize(const Font& font, const std::string& text, int maxWidth);

}  // namespace apex
