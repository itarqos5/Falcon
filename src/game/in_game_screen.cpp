#include "falcon/game/in_game_screen.hpp"

#include "falcon/game/game_session.hpp"
#include "falcon/game/gamemode_switcher_screen.hpp"
#include "falcon/game/chat_screen.hpp"
#include "falcon/game/inventory_screen.hpp"
#include "falcon/game/options_screen.hpp"

namespace falcon {

void InGameScreen::render(GuiGraphics& graphics, int, int, const FrameTime& time) {
    if (services_.session == nullptr) return;
    if (!services_.session->ready()) {
        services_.session->renderLoading(graphics, width_, height_, time.deltaTicks);  // "Loading terrain..." until the world is ready
        return;
    }
    services_.session->renderHud(graphics, width_, height_);
}

bool InGameScreen::keyPressed(int key, int mods) {
    constexpr int kEscape = 256;
    if (key == kEscape) {
        navigator_.push(std::make_unique<PauseScreen>(navigator_, services_));
        return true;
    }
    if (services_.session != nullptr) {
        // The inventory key opens the "E" screen of the game mode (not for a spectator, who has none).
        if (services_.session->ready() && services_.session->info().gameType != 3 && services_.session->keyIs("key.inventory", key)) {
            if (services_.session->creative()) {
                navigator_.push(std::make_unique<CreativeScreen>(navigator_, services_));
            } else {
                navigator_.push(std::make_unique<InventoryScreen>(navigator_, services_));
            }
            return true;
        }
        // The chat opens on its key (empty) or the command key (starting with "/").
        if (services_.session->ready() && services_.session->keyIs("key.chat", key)) {
            navigator_.push(std::make_unique<ChatScreen>(navigator_, services_, ""));
            return true;
        }
        if (services_.session->ready() && services_.session->keyIs("key.command", key)) {
            navigator_.push(std::make_unique<ChatScreen>(navigator_, services_, "/"));
            return true;
        }
        services_.session->onKey(key, mods);
        if (services_.session->consumeGameModeSwitcherRequest()) navigator_.push(std::make_unique<GameModeSwitcherScreen>(navigator_, services_));  // F3 + F4
    }
    return Screen::keyPressed(key, mods);
}

bool InGameScreen::mouseClicked(double, double, int button) {
    if (services_.session != nullptr) services_.session->onMouseButton(button);
    return true;
}

bool InGameScreen::mouseScrolled(double, double, double dy) {
    if (services_.session != nullptr) services_.session->onScroll(dy);
    return true;
}

PauseLayout computePauseLayout(int width, int height) {
    // A GridLayout of two columns (106 wide: a 98 button and 4 of padding each side), rows 24 tall, the first one
    // 70 (50 more padding above Back to Game). Buttons sit 4 in from the cell's left and 4 down from its top.
    constexpr int kGridWidth = 212;
    constexpr int kGridHeight = 70 + 24 + 24 + 24;
    PauseLayout l;
    const int x0 = static_cast<int>(0.5f * static_cast<float>(width - kGridWidth));
    const int y0 = static_cast<int>(0.25f * static_cast<float>(height - kGridHeight));
    l.back = {x0 + 4, y0 + 50, 204, 20};
    l.advancements = {x0 + 4, y0 + 74, 98, 20};
    l.statistics = {x0 + 110, y0 + 74, 98, 20};
    l.options = {x0 + 4, y0 + 98, 98, 20};
    l.worldOptions = {x0 + 110, y0 + 98, 98, 20};
    l.quit = {x0 + 4, y0 + 122, 204, 20};
    return l;
}

void PauseScreen::init() {
    const PauseLayout l = computePauseLayout(width_, height_);
    addButton(text("menu.returnToGame"), l.back, [this] { onClose(); });
    addLockedButton(text("gui.advancements"), l.advancements);
    addLockedButton(text("gui.stats"), l.statistics);
    addButton(text("menu.options"), l.options, openScreen<OptionsScreen>(navigator_, services_));
    addLockedButton(text("options.worldOptions.button"), l.worldOptions);
    addButton(text("menu.returnToMenu"), l.quit, [this] {
        if (services_.quitWorld) services_.quitWorld();
    });
}

void PauseScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    const std::string title = text("menu.game");
    graphics.text(title, static_cast<float>(width_ / 2 - graphics.textWidth(title) / 2), 40.0f, kWhite);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
