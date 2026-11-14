#include "falcon/platform/key_names.hpp"

#include "falcon/util/text_util.hpp"

#include <GLFW/glfw3.h>

namespace falcon {
namespace {

// The keys whose name is the character they print (SDL names them that way; the rest have names like "Space").
bool isMainBlockKey(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return true;
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return true;
    switch (key) {
        case GLFW_KEY_APOSTROPHE: case GLFW_KEY_COMMA: case GLFW_KEY_MINUS: case GLFW_KEY_PERIOD:
        case GLFW_KEY_SLASH: case GLFW_KEY_SEMICOLON: case GLFW_KEY_EQUAL: case GLFW_KEY_LEFT_BRACKET:
        case GLFW_KEY_BACKSLASH: case GLFW_KEY_RIGHT_BRACKET: case GLFW_KEY_GRAVE_ACCENT:
            return true;
        default:
            return false;
    }
}

// toUpperCase(Locale.ROOT) for the single characters keyboards print: ASCII, Latin-1, Greek and Cyrillic.
char32_t upperCase(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - 0x20;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;           // à..þ
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 0x20;         // Greek alpha..omega
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;                       // Cyrillic а..я
    if (c >= 0x450 && c <= 0x45F) return c - 0x50;                       // Cyrillic ѐ..џ
    return c;
}

}  // namespace

std::string layoutKeyName(int glfwKey) {
    if (!isMainBlockKey(glfwKey)) return {};
    const char* name = glfwGetKeyName(glfwKey, 0);
    if (name == nullptr) return {};
    const std::vector<char32_t> chars = decodeUtf8(name);
    if (chars.size() != 1) return {};  // only a single character counts as the key's name
    return encodeUtf8(upperCase(chars[0]));
}

}  // namespace falcon
