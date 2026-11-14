#pragma once

#include "falcon/util/options_file.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace falcon {

class Language;

// One key or mouse button that an action can be bound to. Keyboard codes are GLFW key codes (what this
// client's window delivers) and mouse codes are GLFW button numbers (0 left, 1 right, 2 middle, 3...).
// In the options file they are written the way the game writes them: key.keyboard.space, key.mouse.left.
struct KeyId {
    enum class Type { Keyboard, Mouse };
    Type type = Type::Keyboard;
    int code = -1;  // GLFW_KEY_UNKNOWN: nothing is bound

    static constexpr KeyId keyboard(int glfwKey) { return {Type::Keyboard, glfwKey}; }
    static constexpr KeyId mouse(int glfwButton) { return {Type::Mouse, glfwButton}; }
    static constexpr KeyId unbound() { return {Type::Keyboard, -1}; }

    [[nodiscard]] constexpr bool isUnbound() const { return type == Type::Keyboard && code < 0; }
    constexpr bool operator==(const KeyId&) const = default;

    // "key.keyboard.left.shift", "key.mouse.middle", "key.mouse.4", or "key.keyboard.unknown" for unbound.
    [[nodiscard]] std::string optionName() const;
    // The reverse; nullopt for a name this client does not know.
    [[nodiscard]] static std::optional<KeyId> fromOptionName(std::string_view name);
    // Whether a key press can be bound: every key that has a name. (GLFW reports a few keys as unknown.)
    [[nodiscard]] static bool isBindableKey(int glfwKey);
};

// The sections of the Key Binds screen, in the order it lists them.
enum class KeyCategory { Movement, Misc, Multiplayer, Gameplay, Inventory, Creative, Spectator, Debug };

// One action that can be bound. `name` is its translation key ("key.jump"); it is also the options-file
// key (key_key.jump). `order` puts a few debug keys first inside their category.
struct KeyMappingInfo {
    const char* name;
    KeyCategory category;
    KeyId defaultKey;
    int order = 0;
};

inline constexpr std::size_t kKeyMappingCount = 62;
// Every action of the 26.3 client, grouped by category (the screen sorts them again by translated name).
[[nodiscard]] const std::array<KeyMappingInfo, kKeyMappingCount>& keyMappings();
// The index of the action called `name` ("key.jump") in keyMappings(); kKeyMappingCount if there is none.
[[nodiscard]] std::size_t keyMappingIndex(std::string_view name);
// "key.category.minecraft.movement" and so on.
[[nodiscard]] const char* keyCategoryLabelKey(KeyCategory category);

// The indices into keyMappings() in the order the Key Binds screen lists them: by category, then by `order`,
// then by the name `translate` gives (Java's String.compareTo, so upper case sorts before lower case).
[[nodiscard]] std::vector<std::size_t> sortedKeyMappingOrder(const std::function<std::string(const std::string& nameKey)>& translate);

// What the player has bound each action to (KeyMapping.setKey and friends).
class KeyBindings {
public:
    KeyBindings();

    [[nodiscard]] const KeyId& key(std::size_t index) const { return keys_[index]; }
    void set(std::size_t index, KeyId key) { keys_[index] = key; }
    [[nodiscard]] bool isDefault(std::size_t index) const;
    void reset(std::size_t index);
    void resetAll();
    [[nodiscard]] bool anyChanged() const;  // some action is not on its default key

    // The other actions on the same key, which is a conflict unless both are still on their defaults
    // (two actions that ship on one key, like F3, never flag each other). Unbound actions never conflict.
    [[nodiscard]] std::vector<std::size_t> conflicts(std::size_t index) const;

    // key_<name>:<key name> lines, as the game writes them. Loading keeps the default for a missing or
    // unrecognised value.
    void save(OptionsFile& file) const;
    void load(const OptionsFile& file);

private:
    std::array<KeyId, kKeyMappingCount> keys_;
};

// The text on a key button: "Not Bound", "Left Shift", "Left Button", "Button 4", or the key's printed
// letter or symbol on the player's keyboard layout (a single character is shown upper case).
[[nodiscard]] std::string keyDisplayName(const KeyId& key, const Language& language);

}  // namespace falcon
