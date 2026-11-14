#include "falcon/text/text_layout.hpp"

#include "falcon/util/text_util.hpp"

namespace falcon {

std::vector<std::string> wrapText(const Font& font, const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    std::string line;

    const auto flush = [&] {
        if (!line.empty()) lines.push_back(line);
        line.clear();
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t space = text.find(' ', pos);
        const std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
        pos = space == std::string::npos ? text.size() + 1 : space + 1;
        if (word.empty()) continue;

        const std::string candidate = line.empty() ? word : line + " " + word;
        if (font.width(candidate) <= maxWidth) {
            line = candidate;
            continue;
        }
        flush();
        if (font.width(word) <= maxWidth) {
            line = word;
            continue;
        }
        // A single word wider than the line: break it by characters.
        std::u32string piece;
        for (const char32_t cp : decodeUtf8(word)) {
            piece.push_back(cp);
            if (font.width(encodeUtf8(piece)) > maxWidth && piece.size() > 1) {
                piece.pop_back();
                lines.push_back(encodeUtf8(piece));
                piece.assign(1, cp);
            }
        }
        line = encodeUtf8(piece);
    }
    flush();
    return lines;
}

std::string ellipsize(const Font& font, const std::string& text, int maxWidth) {
    if (font.width(text) <= maxWidth) return text;
    std::u32string chars;
    for (const char32_t cp : decodeUtf8(text)) chars.push_back(cp);
    while (!chars.empty() && font.width(encodeUtf8(chars) + "...") > maxWidth) chars.pop_back();
    return encodeUtf8(chars) + "...";
}

}  // namespace falcon
