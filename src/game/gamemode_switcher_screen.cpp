#include "falcon/game/gamemode_switcher_screen.hpp"

#include "falcon/game/game_session.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/game/key_mappings.hpp"
#include "falcon/world/item_registry.hpp"
#include "falcon/world/item_stack.hpp"

#include <string>

namespace falcon {

namespace {

constexpr int kF4 = 293, kF3 = 292;

// GameModeIcon: the order of the slots, the translation key of the name and the item that stands for it.
struct ModeIcon {
    int gameType;
    const char* nameKey;
    const char* item;
};
constexpr ModeIcon kIcons[GameModeSwitcherScreen::kModeCount] = {
    {1, "gameMode.creative", "grass_block"},
    {0, "gameMode.survival", "iron_sword"},
    {2, "gameMode.adventure", "buried_treasure_map"},
    {3, "gameMode.spectator", "ender_eye"},
};

}  // namespace

int GameModeSwitcherScreen::gameTypeOfSlot(int index) { return kIcons[(index % kModeCount + kModeCount) % kModeCount].gameType; }

int GameModeSwitcherScreen::slotOfGameType(int gameType) {
    for (int i = 0; i < kModeCount; ++i) {
        if (kIcons[i].gameType == gameType) return i;
    }
    return 0;
}

int GameModeSwitcherScreen::defaultSlot(int currentGameType, int previousGameType) {
    if (previousGameType >= 0) return slotOfGameType(previousGameType);
    return slotOfGameType(currentGameType == 1 ? 0 : 1);
}

GameModeSwitcherScreen::GameModeSwitcherScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {
    if (services_.session != nullptr) picked_ = defaultSlot(services_.session->gameType(), services_.session->previousGameType());
}

void GameModeSwitcherScreen::render(GuiGraphics& g, int mouseX, int mouseY, const FrameTime&) {
    if (services_.session != nullptr) services_.session->renderHud(g, width_, height_);

    // The panel (125 x 75 of the 128 x 128 sheet) behind the slots: its top left is (width / 2 - 62, height / 2 - 31 - 27).
    g.blit(g.textures().get("gui/container/gamemode_switcher.png"), width_ / 2 - 62, height_ / 2 - kSlotAreaPadded - 27, 0.0f, 0.0f, 125, 75, 128, 128);

    // The name of the picked mode above the slots, and "F4 Next" below them (the key's name in aqua).
    const std::string name = text(kIcons[picked_].nameKey);
    g.text(name, static_cast<float>(width_ / 2 - g.textWidth(name) / 2), static_cast<float>(height_ / 2 - kSlotAreaPadded - 20), 0xFFFFFFFFu);
    {
        const std::size_t index = keyMappingIndex("key.debug.switchGameMode");
        const std::string key = index < kKeyMappingCount ? keyDisplayName(services_.options.keyBindings.key(index), services_.language) : "F4";
        const std::string rest = text("debug.gamemodes.select_next");  // "%s Next"
        const std::size_t at = rest.find("%s");
        const std::string before = at == std::string::npos ? std::string() : rest.substr(0, at);
        const std::string after = at == std::string::npos ? rest : rest.substr(at + 2);
        const int total = g.textWidth(before) + g.textWidth(key) + g.textWidth(after);
        float x = static_cast<float>(width_ / 2 - total / 2);
        const float y = static_cast<float>(height_ / 2 + 5);
        g.text(before, x, y, 0xFFFFFFFFu);
        x += static_cast<float>(g.textWidth(before));
        g.text(key, x, y, 0xFF55FFFFu);  // ChatFormatting.AQUA
        x += static_cast<float>(g.textWidth(key));
        g.text(after, x, y, 0xFFFFFFFFu);
    }

    // The mouse only picks a slot once it has moved from where it was when the screen opened (or after F4 was pressed).
    if (!haveFirstMouse_) {
        firstMouseX_ = mouseX;
        firstMouseY_ = mouseY;
        haveFirstMouse_ = true;
    }
    const bool mouseStill = firstMouseX_ == mouseX && firstMouseY_ == mouseY;
    for (int i = 0; i < kModeCount; ++i) {
        const int x = slotLeft(width_, i);
        const int y = slotTop(height_);
        g.blitSprite("gamemode_switcher/slot", x, y, kSlotArea, kSlotArea);
        if (i == picked_) g.blitSprite("gamemode_switcher/selection", x, y, kSlotArea, kSlotArea);
        if (services_.session != nullptr) {
            if (const auto item = currentItemRegistry() ? currentItemRegistry()->find(kIcons[i].item) : std::nullopt) {
                drawItem(g, services_.session->itemIcons(), ItemStack(*item, 1), x + 5, y + 5);
            }
        }
        const bool hovered = mouseX >= x && mouseX < x + kSlotArea && mouseY >= y && mouseY < y + kSlotArea;
        if (!mouseStill && hovered) picked_ = i;
    }
}

bool GameModeSwitcherScreen::keyPressed(int key, int mods) {
    // The key is checked against its binding (the game's keyDebugSwitchGameMode.matches).
    const std::size_t index = keyMappingIndex("key.debug.switchGameMode");
    const bool isF4 = index < kKeyMappingCount ? services_.options.keyBindings.key(index) == KeyId::keyboard(key) : key == kF4;
    if (isF4) {
        haveFirstMouse_ = false;  // the mouse has to move again to pick a slot
        picked_ = nextMode(picked_);
        return true;
    }
    return Screen::keyPressed(key, mods);
}

void GameModeSwitcherScreen::keyReleased(int key, int mods) {
    const std::size_t index = keyMappingIndex("key.debug.modifier");
    const bool isModifier = index < kKeyMappingCount ? services_.options.keyBindings.key(index) == KeyId::keyboard(key) : key == kF3;
    if (isModifier) {
        switchToPicked();
        navigator_.pop();
        return;
    }
    Screen::keyReleased(key, mods);
}

void GameModeSwitcherScreen::switchToPicked() {
    if (services_.session != nullptr) services_.session->requestGameType(gameTypeOfSlot(picked_));
}

}  // namespace falcon
