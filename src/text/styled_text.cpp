#include "falcon/text/styled_text.hpp"

#include "falcon/util/text_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace falcon {
namespace {

constexpr int kMaxComponentDepth = 16;

struct Style {
    bool hasColor = false;
    std::uint32_t rgb = 0;
    bool bold = false;
    bool italic = false;
    bool underlined = false;
    bool strikethrough = false;
};

constexpr char kHexDigits[] = "0123456789abcdef";
constexpr std::uint32_t kLegacyColors[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
                                             0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};

struct NamedColor {
    const char* name;
    std::uint32_t rgb;
};
constexpr NamedColor kNamedColors[] = {
    {"black", 0x000000},      {"dark_blue", 0x0000AA}, {"dark_green", 0x00AA00}, {"dark_aqua", 0x00AAAA},
    {"dark_red", 0xAA0000},   {"dark_purple", 0xAA00AA}, {"gold", 0xFFAA00},     {"gray", 0xAAAAAA},
    {"dark_gray", 0x555555},  {"blue", 0x5555FF},      {"green", 0x55FF55},      {"aqua", 0x55FFFF},
    {"red", 0xFF5555},        {"light_purple", 0xFF55FF}, {"yellow", 0xFFFF55},  {"white", 0xFFFFFF}};

void append(StyledText& out, std::string text, const Style& style) {
    if (text.empty()) return;
    TextSpan span{std::move(text), style.rgb, style.hasColor, style.bold, style.italic, style.underlined, style.strikethrough};
    if (!out.empty()) {  // merge with the previous run when the style is the same
        TextSpan& last = out.back();
        if (last.hasColor == span.hasColor && (!span.hasColor || last.rgb == span.rgb) && last.bold == span.bold &&
            last.italic == span.italic && last.underlined == span.underlined && last.strikethrough == span.strikethrough) {
            last.text += span.text;
            return;
        }
    }
    out.push_back(std::move(span));
}

// Text that may carry formatting codes, starting from `base` (the parent's style).
void appendWithCodes(StyledText& out, std::string_view text, const Style& base) {
    Style style = base;
    std::string run;
    for (std::size_t i = 0; i < text.size();) {
        // The section sign is U+00A7: C2 A7 in UTF-8.
        if (i + 2 < text.size() + 0 && static_cast<unsigned char>(text[i]) == 0xC2 && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
            const char* hex = std::strchr(kHexDigits, code);
            const bool known = code != '\0' && (hex != nullptr || std::strchr("klmnor", code) != nullptr);
            if (known) {
                append(out, std::move(run), style);
                run.clear();
                if (hex != nullptr) {
                    style = Style{true, kLegacyColors[hex - kHexDigits], false, false, false, false};  // clears styles
                } else if (code == 'l') {
                    style.bold = true;
                } else if (code == 'm') {
                    style.strikethrough = true;
                } else if (code == 'n') {
                    style.underlined = true;
                } else if (code == 'o') {
                    style.italic = true;
                } else if (code == 'r') {
                    style = base;  // back to what the surrounding component says
                }  // 'k' (obfuscated) changes nothing here
                i += 3;
                continue;
            }
        }
        run.push_back(text[i++]);
    }
    append(out, std::move(run), style);
}

bool boolField(const nlohmann::json& obj, const char* key, bool fallback) {
    const auto it = obj.find(key);
    return it != obj.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void walk(const nlohmann::json& node, const Style& parent, int depth, StyledText& out) {
    if (depth > kMaxComponentDepth) return;
    if (node.is_string()) {
        appendWithCodes(out, node.get_ref<const std::string&>(), parent);
    } else if (node.is_number() || node.is_boolean()) {
        appendWithCodes(out, node.dump(), parent);
    } else if (node.is_array()) {
        for (const auto& child : node) walk(child, parent, depth + 1, out);
    } else if (node.is_object()) {
        Style style = parent;
        style.bold = boolField(node, "bold", style.bold);
        style.italic = boolField(node, "italic", style.italic);
        style.underlined = boolField(node, "underlined", style.underlined);
        style.strikethrough = boolField(node, "strikethrough", style.strikethrough);
        if (const auto color = node.find("color"); color != node.end() && color->is_string()) {
            const std::string& name = color->get_ref<const std::string&>();
            if (name.size() == 7 && name[0] == '#') {
                char* end = nullptr;
                const unsigned long v = std::strtoul(name.c_str() + 1, &end, 16);
                if (end == name.c_str() + 7) {
                    style.hasColor = true;
                    style.rgb = static_cast<std::uint32_t>(v);
                }
            } else {
                for (const NamedColor& c : kNamedColors) {
                    if (name == c.name) {
                        style.hasColor = true;
                        style.rgb = c.rgb;
                    }
                }
            }
        }
        if (const auto text = node.find("text"); text != node.end()) {
            walk(*text, style, depth + 1, out);
        } else if (const auto translate = node.find("translate"); translate != node.end() && translate->is_string()) {
            appendWithCodes(out, translate->get_ref<const std::string&>(), style);  // no translations here: the key
        }
        if (const auto extra = node.find("extra"); extra != node.end() && extra->is_array()) {
            for (const auto& child : *extra) walk(child, style, depth + 1, out);
        }
    }
}

}  // namespace

std::string plainText(const StyledText& styled) {
    std::string out;
    for (const TextSpan& span : styled) out += span.text;
    return out;
}

StyledText parseLegacyFormatting(std::string_view text) {
    StyledText out;
    appendWithCodes(out, text, Style{});
    return out;
}

StyledText parseChatComponent(const nlohmann::json& component) {
    StyledText out;
    walk(component, Style{}, 0, out);
    return out;
}

StyledText parseChatComponentJson(std::string_view jsonText) {
    const nlohmann::json parsed = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) return {};
    return parseChatComponent(parsed);
}

std::vector<StyledText> wrapStyledText(const TextMeasure& measure, const StyledText& styled, int maxWidth, std::size_t maxLines) {
    std::vector<StyledText> lines(1);
    int lineWidth = 0;

    const auto newLine = [&] {
        lines.emplace_back();
        lineWidth = 0;
    };
    const auto put = [&](const TextSpan& style, const std::string& piece) {
        if (piece.empty()) return;
        TextSpan span = style;
        span.text = piece;
        StyledText& line = lines.back();
        if (!line.empty() && line.back().hasColor == span.hasColor && line.back().rgb == span.rgb && line.back().bold == span.bold &&
            line.back().italic == span.italic && line.back().underlined == span.underlined &&
            line.back().strikethrough == span.strikethrough) {
            line.back().text += span.text;
        } else {
            line.push_back(std::move(span));
        }
        lineWidth += measure(piece, span.bold);
    };

    for (const TextSpan& span : styled) {
        std::string word;  // the characters since the last space (a word plus its trailing spaces)
        const auto flushWord = [&] {
            if (word.empty()) return;
            int w = measure(word, span.bold);
            // A word that does not fit goes to the next line; one that never fits is split by character.
            if (lineWidth > 0 && lineWidth + w > maxWidth && w <= maxWidth) newLine();
            if (w > maxWidth - lineWidth) {
                std::string piece;
                for (const char32_t cp : decodeUtf8(word)) {
                    const std::string ch = encodeUtf8(cp);
                    // Start a new line when this character no longer fits (never before the first one of a line).
                    if (lineWidth + measure(piece + ch, span.bold) > maxWidth && (lineWidth > 0 || !piece.empty())) {
                        put(span, piece);
                        piece.clear();
                        newLine();
                    }
                    piece += ch;
                }
                put(span, piece);
            } else {
                put(span, word);
            }
            word.clear();
        };
        for (const char c : span.text) {
            if (c == '\n') {
                flushWord();
                newLine();
            } else {
                word.push_back(c);
                if (c == ' ') flushWord();
            }
        }
        flushWord();
    }
    if (lines.size() > maxLines) lines.resize(maxLines);
    // A trailing empty line (the text ended with a newline) is not a line.
    while (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    return lines;
}

}  // namespace falcon
