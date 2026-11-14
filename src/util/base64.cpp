#include "apex/util/base64.hpp"

namespace apex {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int valueOf(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::vector<unsigned char>& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t left = bytes.size() - i;
        const unsigned v = (static_cast<unsigned>(bytes[i]) << 16) | (left > 1 ? static_cast<unsigned>(bytes[i + 1]) << 8 : 0u) |
                           (left > 2 ? static_cast<unsigned>(bytes[i + 2]) : 0u);
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(left > 1 ? kAlphabet[(v >> 6) & 63] : '=');
        out.push_back(left > 2 ? kAlphabet[v & 63] : '=');
    }
    return out;
}

std::optional<std::vector<unsigned char>> base64Decode(std::string_view text) {
    std::vector<unsigned char> out;
    unsigned buffer = 0;
    int bits = 0;
    std::size_t padding = 0;
    std::size_t symbols = 0;
    for (const char c : text) {
        if (c == '\n' || c == '\r') continue;
        if (c == '=') {
            ++padding;
            continue;
        }
        const int v = valueOf(c);
        if (v < 0 || padding > 0) return std::nullopt;  // junk, or data after the padding
        ++symbols;
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    if (padding > 2 || symbols % 4 == 1) return std::nullopt;  // one symbol never makes a whole byte
    if (padding != 0 && (symbols + padding) % 4 != 0) return std::nullopt;
    return out;
}

}  // namespace apex
