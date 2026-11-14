#pragma once

#include "apex/game/screen_services.hpp"
#include "apex/ui/screen.hpp"

#include <array>

namespace apex {

// The game's GameModeSwitcherScreen (F3 + F4): four slots in a row, Creative, Survival, Adventure and Spectator, each with the item
// that stands for it, in a 125 x 75 panel with the mode's name above and "F4 Next" below. Pressing F4 (while F3 is held) goes on to
// the next mode, moving the mouse over a slot picks that one, and letting go of F3 switches to the picked mode and closes the screen.
// Escape closes it without switching. The game goes on behind it (it is not a pause screen).
class GameModeSwitcherScreen : public Screen {
public:
    GameModeSwitcherScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool keyPressed(int key, int mods) override;
    void keyReleased(int key, int mods) override;
    [[nodiscard]] bool keepsWorldTicking() const override { return true; }

    // The geometry of the slots (the game's constants), for the tests: the left edge of slot `index` and the common top.
    static constexpr int kSlotArea = 26;
    static constexpr int kSlotAreaPadded = 31;
    static constexpr int kModeCount = 4;
    static constexpr int kAllSlotsWidth = kModeCount * kSlotAreaPadded - 5;
    [[nodiscard]] static int slotLeft(int screenWidth, int index) { return screenWidth / 2 - kAllSlotsWidth / 2 + index * kSlotAreaPadded; }
    [[nodiscard]] static int slotTop(int screenHeight) { return screenHeight / 2 - kSlotAreaPadded; }

    // The slot after `index`, wrapping round: Creative -> Survival -> Adventure -> Spectator -> Creative.
    [[nodiscard]] static int nextMode(int index) { return (index + 1) % kModeCount; }
    // Slot order is the game's GameModeIcon order; the game type numbers are 0 survival, 1 creative, 2 adventure, 3 spectator.
    [[nodiscard]] static int gameTypeOfSlot(int index);
    [[nodiscard]] static int slotOfGameType(int gameType);
    // The slot that is picked when the screen opens (getDefaultSelected): the previous mode if there is one, else Survival while
    // in Creative and Creative otherwise.
    [[nodiscard]] static int defaultSlot(int currentGameType, int previousGameType);

protected:
    void init() override {}

private:
    void switchToPicked();

    ScreenServices& services_;
    int picked_ = 0;
    int firstMouseX_ = 0;
    int firstMouseY_ = 0;
    bool haveFirstMouse_ = false;
};

}  // namespace apex
