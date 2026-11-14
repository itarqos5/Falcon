#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace apex {

// Decode UTF-8 into code points. Invalid bytes become U+FFFD.
[[nodiscard]] std::vector<char32_t> decodeUtf8(std::string_view utf8);

// Encodes a code point (or a whole string of them) as UTF-8.
[[nodiscard]] std::string encodeUtf8(char32_t codePoint);
[[nodiscard]] std::string encodeUtf8(const std::u32string& codePoints);

// java.lang.String.hashCode() of a UTF-8 string (computed over its UTF-16 code units).
[[nodiscard]] std::int32_t javaStringHash(std::string_view utf8);

// A filesystem path from a UTF-8 string, and back. (std::filesystem::path(std::string) reads narrow text in the
// ANSI code page on Windows, which garbles every non-ASCII name; these always use UTF-8.)
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8);
[[nodiscard]] std::string utf8FromPath(const std::filesystem::path& path);

// java.lang.String.trim(): strips every leading/trailing char <= U+0020.
[[nodiscard]] std::string javaTrim(std::string_view s);

// Fills "%s" (and positional "%1$s") placeholders of a language string, like String.format.
// "%%" is a literal percent sign. Missing arguments leave the placeholder empty.
[[nodiscard]] std::string formatLanguageString(std::string_view pattern,
                                               const std::vector<std::string>& args);

}  // namespace apex
