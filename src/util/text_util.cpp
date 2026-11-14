#include "falcon/util/text_util.hpp"

namespace falcon {

std::vector<char32_t> decodeUtf8(std::string_view s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = (static_cast<char32_t>(b0 & 0x1F) << 6) |
                 (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = (static_cast<char32_t>(b0 & 0x0F) << 12) |
                 (static_cast<char32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3F);
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = (static_cast<char32_t>(b0 & 0x07) << 18) |
                 (static_cast<char32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                 (static_cast<char32_t>(static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3F);
            len = 4;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

std::string encodeUtf8(const std::u32string& codePoints) {
    std::string out;
    for (const char32_t cp : codePoints) out += encodeUtf8(cp);
    return out;
}

std::int32_t javaStringHash(std::string_view utf8) {
    std::uint32_t h = 0;
    const auto add = [&h](std::uint32_t unit) { h = h * 31u + unit; };
    for (const char32_t cp : decodeUtf8(utf8)) {
        if (cp >= 0x10000) {
            const char32_t v = cp - 0x10000;
            add(0xD800u + static_cast<std::uint32_t>(v >> 10));
            add(0xDC00u + static_cast<std::uint32_t>(v & 0x3FF));
        } else {
            add(static_cast<std::uint32_t>(cp));
        }
    }
    return static_cast<std::int32_t>(h);
}

std::string formatLanguageString(std::string_view pattern, const std::vector<std::string>& args) {
    std::string out;
    std::size_t next = 0;  // index for the next plain "%s"
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            out += pattern[i];
            continue;
        }
        const char c = pattern[i + 1];
        if (c == '%') {
            out += '%';
            ++i;
        } else if (c == 's') {
            if (next < args.size()) out += args[next];
            ++next;
            ++i;
        } else if (c >= '1' && c <= '9' && i + 3 < pattern.size() && pattern[i + 2] == '$' &&
                   pattern[i + 3] == 's') {
            const auto index = static_cast<std::size_t>(c - '1');
            if (index < args.size()) out += args[index];
            i += 3;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string utf8FromPath(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string javaTrim(std::string_view s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && static_cast<unsigned char>(s[begin]) <= 0x20) ++begin;
    while (end > begin && static_cast<unsigned char>(s[end - 1]) <= 0x20) --end;
    return std::string(s.substr(begin, end - begin));
}

}  // namespace falcon
