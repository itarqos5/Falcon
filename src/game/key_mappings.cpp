#include "apex/game/key_mappings.hpp"

#include "apex/platform/key_names.hpp"
#include "apex/text/language.hpp"
#include "apex/util/text_util.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <map>
#include <numeric>

namespace apex {
namespace {

struct NamedKey {
    int glfwKey;
    const char* id;  // the part after "key.keyboard."
};

// Keys with a fixed name. Letters, digits and function keys are generated (key.keyboard.a, .1, .f5).
constexpr NamedKey kNamedKeys[] = {
    {GLFW_KEY_SPACE, "space"},           {GLFW_KEY_APOSTROPHE, "apostrophe"},   {GLFW_KEY_COMMA, "comma"},
    {GLFW_KEY_MINUS, "minus"},           {GLFW_KEY_PERIOD, "period"},           {GLFW_KEY_SLASH, "slash"},
    {GLFW_KEY_SEMICOLON, "semicolon"},   {GLFW_KEY_EQUAL, "equal"},             {GLFW_KEY_LEFT_BRACKET, "left.bracket"},
    {GLFW_KEY_BACKSLASH, "backslash"},   {GLFW_KEY_RIGHT_BRACKET, "right.bracket"}, {GLFW_KEY_GRAVE_ACCENT, "grave.accent"},
    {GLFW_KEY_WORLD_1, "world.1"},       {GLFW_KEY_WORLD_2, "world.2"},         {GLFW_KEY_ESCAPE, "escape"},
    {GLFW_KEY_ENTER, "enter"},           {GLFW_KEY_TAB, "tab"},                 {GLFW_KEY_BACKSPACE, "backspace"},
    {GLFW_KEY_INSERT, "insert"},         {GLFW_KEY_DELETE, "delete"},           {GLFW_KEY_RIGHT, "right"},
    {GLFW_KEY_LEFT, "left"},             {GLFW_KEY_DOWN, "down"},               {GLFW_KEY_UP, "up"},
    {GLFW_KEY_PAGE_UP, "page.up"},       {GLFW_KEY_PAGE_DOWN, "page.down"},     {GLFW_KEY_HOME, "home"},
    {GLFW_KEY_END, "end"},               {GLFW_KEY_CAPS_LOCK, "caps.lock"},     {GLFW_KEY_SCROLL_LOCK, "scroll.lock"},
    {GLFW_KEY_NUM_LOCK, "num.lock"},     {GLFW_KEY_PRINT_SCREEN, "print.screen"}, {GLFW_KEY_PAUSE, "pause"},
    {GLFW_KEY_KP_DECIMAL, "keypad.decimal"}, {GLFW_KEY_KP_DIVIDE, "keypad.divide"}, {GLFW_KEY_KP_MULTIPLY, "keypad.multiply"},
    {GLFW_KEY_KP_SUBTRACT, "keypad.subtract"}, {GLFW_KEY_KP_ADD, "keypad.add"}, {GLFW_KEY_KP_ENTER, "keypad.enter"},
    {GLFW_KEY_KP_EQUAL, "keypad.equal"}, {GLFW_KEY_LEFT_SHIFT, "left.shift"},   {GLFW_KEY_LEFT_CONTROL, "left.control"},
    {GLFW_KEY_LEFT_ALT, "left.alt"},     {GLFW_KEY_LEFT_SUPER, "left.win"},     {GLFW_KEY_RIGHT_SHIFT, "right.shift"},
    {GLFW_KEY_RIGHT_CONTROL, "right.control"}, {GLFW_KEY_RIGHT_ALT, "right.alt"}, {GLFW_KEY_RIGHT_SUPER, "right.win"},
    {GLFW_KEY_MENU, "menu"},
};

// "key.keyboard." + the id of a key; empty for a key with no name.
std::string keyboardId(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return std::string(1, static_cast<char>('a' + (key - GLFW_KEY_A)));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) return "f" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return "keypad." + std::to_string(key - GLFW_KEY_KP_0);
    for (const NamedKey& named : kNamedKeys) {
        if (named.glfwKey == key) return named.id;
    }
    return {};
}

constexpr KeyId K(int glfwKey) { return KeyId::keyboard(glfwKey); }
constexpr KeyId M(int glfwButton) { return KeyId::mouse(glfwButton); }

using Cat = KeyCategory;

// The defaults of the 26.3 client (KeyMapping registrations), grouped by category.
const std::array<KeyMappingInfo, kKeyMappingCount> kMappings = {{
    {"key.jump", Cat::Movement, K(GLFW_KEY_SPACE)},
    {"key.sneak", Cat::Movement, K(GLFW_KEY_LEFT_SHIFT)},
    {"key.sprint", Cat::Movement, K(GLFW_KEY_LEFT_CONTROL)},
    {"key.left", Cat::Movement, K(GLFW_KEY_A)},
    {"key.right", Cat::Movement, K(GLFW_KEY_D)},
    {"key.back", Cat::Movement, K(GLFW_KEY_S)},
    {"key.forward", Cat::Movement, K(GLFW_KEY_W)},

    {"key.advancements", Cat::Misc, K(GLFW_KEY_L)},
    {"key.quickActions", Cat::Misc, K(GLFW_KEY_G)},
    {"key.screenshot", Cat::Misc, K(GLFW_KEY_F2)},
    {"key.smoothCamera", Cat::Misc, KeyId::unbound()},
    {"key.fullscreen", Cat::Misc, K(GLFW_KEY_F11)},
    {"key.toggleGui", Cat::Misc, K(GLFW_KEY_F1)},
    {"key.togglePerspective", Cat::Misc, K(GLFW_KEY_F5)},
    {"key.toggleSpectatorShaderEffects", Cat::Misc, K(GLFW_KEY_F4)},

    {"key.friends", Cat::Multiplayer, K(GLFW_KEY_O)},
    {"key.playerlist", Cat::Multiplayer, K(GLFW_KEY_TAB)},
    {"key.chat", Cat::Multiplayer, K(GLFW_KEY_T)},
    {"key.command", Cat::Multiplayer, K(GLFW_KEY_SLASH)},
    {"key.socialInteractions", Cat::Multiplayer, K(GLFW_KEY_P)},

    {"key.attack", Cat::Gameplay, M(GLFW_MOUSE_BUTTON_LEFT)},
    {"key.pickItem", Cat::Gameplay, M(GLFW_MOUSE_BUTTON_MIDDLE)},
    {"key.use", Cat::Gameplay, M(GLFW_MOUSE_BUTTON_RIGHT)},

    {"key.drop", Cat::Inventory, K(GLFW_KEY_Q)},
    {"key.hotbar.1", Cat::Inventory, K(GLFW_KEY_1)},
    {"key.hotbar.2", Cat::Inventory, K(GLFW_KEY_2)},
    {"key.hotbar.3", Cat::Inventory, K(GLFW_KEY_3)},
    {"key.hotbar.4", Cat::Inventory, K(GLFW_KEY_4)},
    {"key.hotbar.5", Cat::Inventory, K(GLFW_KEY_5)},
    {"key.hotbar.6", Cat::Inventory, K(GLFW_KEY_6)},
    {"key.hotbar.7", Cat::Inventory, K(GLFW_KEY_7)},
    {"key.hotbar.8", Cat::Inventory, K(GLFW_KEY_8)},
    {"key.hotbar.9", Cat::Inventory, K(GLFW_KEY_9)},
    {"key.inventory", Cat::Inventory, K(GLFW_KEY_E)},
    {"key.swapOffhand", Cat::Inventory, K(GLFW_KEY_F)},

    {"key.loadToolbarActivator", Cat::Creative, K(GLFW_KEY_X)},
    {"key.saveToolbarActivator", Cat::Creative, K(GLFW_KEY_C)},

    {"key.spectatorOutlines", Cat::Spectator, KeyId::unbound()},
    {"key.spectatorHotbar", Cat::Spectator, M(GLFW_MOUSE_BUTTON_MIDDLE)},

    {"key.debug.overlay", Cat::Debug, K(GLFW_KEY_F3), -2},
    {"key.debug.modifier", Cat::Debug, K(GLFW_KEY_F3), -1},
    {"key.debug.clearChat", Cat::Debug, K(GLFW_KEY_D)},
    {"key.debug.copyRecreateCommand", Cat::Debug, K(GLFW_KEY_I)},
    {"key.debug.copyLocation", Cat::Debug, K(GLFW_KEY_C)},
    {"key.debug.spectate", Cat::Debug, K(GLFW_KEY_N)},
    {"key.debug.crash", Cat::Debug, K(GLFW_KEY_C)},
    {"key.debug.debugOptions", Cat::Debug, K(GLFW_KEY_F6)},
    {"key.debug.dumpDynamicTextures", Cat::Debug, K(GLFW_KEY_S)},
    {"key.debug.dumpVersion", Cat::Debug, K(GLFW_KEY_V)},
    {"key.debug.switchGameMode", Cat::Debug, K(GLFW_KEY_F4)},
    {"key.debug.reloadChunk", Cat::Debug, K(GLFW_KEY_A)},
    {"key.debug.reloadResourcePacks", Cat::Debug, K(GLFW_KEY_T)},
    {"key.debug.showAdvancedTooltips", Cat::Debug, K(GLFW_KEY_H)},
    {"key.debug.showChunkBorders", Cat::Debug, K(GLFW_KEY_G)},
    {"key.debug.showHitboxes", Cat::Debug, K(GLFW_KEY_B)},
    {"key.debug.profiling", Cat::Debug, K(GLFW_KEY_L)},
    {"key.debug.improvedTransparency", Cat::Debug, K(GLFW_KEY_X)},
    {"key.debug.focusPause", Cat::Debug, K(GLFW_KEY_P)},
    {"key.debug.profilingChart", Cat::Debug, K(GLFW_KEY_1), 1},
    {"key.debug.fpsCharts", Cat::Debug, K(GLFW_KEY_2), 2},
    {"key.debug.networkCharts", Cat::Debug, K(GLFW_KEY_3), 3},
    {"key.debug.lightmapTexture", Cat::Debug, K(GLFW_KEY_4), 4},
}};

}  // namespace

// ---- KeyId -------------------------------------------------------------------------------

std::string KeyId::optionName() const {
    if (type == Type::Mouse) {
        switch (code) {
            case GLFW_MOUSE_BUTTON_LEFT: return "key.mouse.left";
            case GLFW_MOUSE_BUTTON_RIGHT: return "key.mouse.right";
            case GLFW_MOUSE_BUTTON_MIDDLE: return "key.mouse.middle";
            default: return "key.mouse." + std::to_string(code + 1);  // the game counts buttons from 1
        }
    }
    const std::string id = keyboardId(code);
    return id.empty() ? "key.keyboard.unknown" : "key.keyboard." + id;
}

std::optional<KeyId> KeyId::fromOptionName(std::string_view name) {
    constexpr std::string_view kKeyboard = "key.keyboard.";
    constexpr std::string_view kMouse = "key.mouse.";
    if (name == "key.keyboard.unknown") return unbound();
    if (name.substr(0, kKeyboard.size()) == kKeyboard) {
        // Built once: every key GLFW has (about 120) by its id.
        static const std::map<std::string, int, std::less<>> byId = [] {
            std::map<std::string, int, std::less<>> table;
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
                if (const std::string known = keyboardId(key); !known.empty()) table.emplace(known, key);
            }
            return table;
        }();
        const auto found = byId.find(name.substr(kKeyboard.size()));
        return found != byId.end() ? std::optional<KeyId>(keyboard(found->second)) : std::nullopt;
    }
    if (name.substr(0, kMouse.size()) == kMouse) {
        const std::string_view id = name.substr(kMouse.size());
        if (id == "left") return mouse(GLFW_MOUSE_BUTTON_LEFT);
        if (id == "right") return mouse(GLFW_MOUSE_BUTTON_RIGHT);
        if (id == "middle") return mouse(GLFW_MOUSE_BUTTON_MIDDLE);
        int button = 0;
        for (const char c : id) {
            if (c < '0' || c > '9' || button > 100) return std::nullopt;
            button = button * 10 + (c - '0');
        }
        if (button >= 4 && button <= GLFW_MOUSE_BUTTON_LAST + 1) return mouse(button - 1);
    }
    return std::nullopt;
}

bool KeyId::isBindableKey(int glfwKey) { return glfwKey >= 0 && !keyboardId(glfwKey).empty(); }

// ---- the registry ------------------------------------------------------------------------

const std::array<KeyMappingInfo, kKeyMappingCount>& keyMappings() { return kMappings; }

std::size_t keyMappingIndex(std::string_view name) {
    for (std::size_t i = 0; i < kKeyMappingCount; ++i) {
        if (name == kMappings[i].name) return i;
    }
    return kKeyMappingCount;
}

const char* keyCategoryLabelKey(KeyCategory category) {
    switch (category) {
        case KeyCategory::Movement: return "key.category.minecraft.movement";
        case KeyCategory::Misc: return "key.category.minecraft.misc";
        case KeyCategory::Multiplayer: return "key.category.minecraft.multiplayer";
        case KeyCategory::Gameplay: return "key.category.minecraft.gameplay";
        case KeyCategory::Inventory: return "key.category.minecraft.inventory";
        case KeyCategory::Creative: return "key.category.minecraft.creative";
        case KeyCategory::Spectator: return "key.category.minecraft.spectator";
        case KeyCategory::Debug: return "key.category.minecraft.debug";
    }
    return "";
}

std::vector<std::size_t> sortedKeyMappingOrder(const std::function<std::string(const std::string&)>& translate) {
    std::vector<std::u32string> names;
    for (const KeyMappingInfo& info : kMappings) {
        const auto cps = decodeUtf8(translate(info.name));
        names.emplace_back(cps.begin(), cps.end());
    }
    std::vector<std::size_t> order(kKeyMappingCount);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const KeyMappingInfo& x = kMappings[a];
        const KeyMappingInfo& y = kMappings[b];
        if (x.category != y.category) return static_cast<int>(x.category) < static_cast<int>(y.category);
        if (x.order != y.order) return x.order < y.order;
        return names[a] < names[b];
    });
    return order;
}

// ---- KeyBindings -------------------------------------------------------------------------

KeyBindings::KeyBindings() {
    for (std::size_t i = 0; i < kKeyMappingCount; ++i) keys_[i] = kMappings[i].defaultKey;
}

bool KeyBindings::isDefault(std::size_t index) const { return keys_[index] == kMappings[index].defaultKey; }

void KeyBindings::reset(std::size_t index) { keys_[index] = kMappings[index].defaultKey; }

void KeyBindings::resetAll() {
    for (std::size_t i = 0; i < kKeyMappingCount; ++i) reset(i);
}

bool KeyBindings::anyChanged() const {
    for (std::size_t i = 0; i < kKeyMappingCount; ++i) {
        if (!isDefault(i)) return true;
    }
    return false;
}

std::vector<std::size_t> KeyBindings::conflicts(std::size_t index) const {
    std::vector<std::size_t> found;
    if (keys_[index].isUnbound()) return found;
    for (std::size_t other = 0; other < kKeyMappingCount; ++other) {
        if (other != index && keys_[other] == keys_[index] && (!isDefault(other) || !isDefault(index))) found.push_back(other);
    }
    return found;
}

void KeyBindings::save(OptionsFile& file) const {
    for (std::size_t i = 0; i < kKeyMappingCount; ++i) file.set(std::string("key_") + kMappings[i].name, keys_[i].optionName());
}

void KeyBindings::load(const OptionsFile& file) {
    for (std::size_t i = 0; i < kKeyMappingCount; ++i) {
        const std::string name = std::string("key_") + kMappings[i].name;
        if (!file.contains(name)) continue;
        if (const auto key = KeyId::fromOptionName(file.getString(name))) keys_[i] = *key;
    }
}

// ---- display -----------------------------------------------------------------------------

std::string keyDisplayName(const KeyId& key, const Language& language) {
    const std::string id = key.optionName();
    if (key.type == KeyId::Type::Mouse) {
        const std::string translated = language.get(id);
        if (translated != id) return translated;  // Left Button, Middle Button, Right Button
        return formatLanguageString(language.get("key.mouse"), {std::to_string(key.code + 1)});  // Button 4
    }
    if (key.isUnbound()) return language.get("key.keyboard.unknown");  // "Not Bound"
    if (const std::string printed = layoutKeyName(key.code); !printed.empty()) return printed;
    const std::string translated = language.get(id);
    if (translated != id) return translated;
    // A letter or digit key on a system with no layout name for it: the character GLFW's code stands for.
    if ((key.code >= GLFW_KEY_A && key.code <= GLFW_KEY_Z) || (key.code >= GLFW_KEY_0 && key.code <= GLFW_KEY_9)) {
        return std::string(1, static_cast<char>(key.code));
    }
    return id.substr(std::string_view("key.keyboard.").size());
}

}  // namespace apex
