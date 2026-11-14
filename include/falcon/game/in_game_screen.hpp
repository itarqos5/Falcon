#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/ui/rect.hpp"
#include "falcon/ui/screen.hpp"

namespace falcon {

// The screen that is on top while a world is being played and no menu is open: it draws the HUD, passes keys, mouse buttons and
// the mouse wheel to the session, opens the inventory on its key and the pause menu on Escape. The mouse is grabbed for looking around
// while this is the top screen.
class InGameScreen : public Screen {
public:
    InGameScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool keyPressed(int key, int mods) override;
    bool mouseClicked(double x, double y, int button) override;
    bool mouseScrolled(double x, double y, double dy) override;
    // Escape opens the pause menu (keyPressed), it does not leave the world.
    [[nodiscard]] bool shouldCloseOnEsc() const override { return false; }

protected:
    void init() override {}

private:
    ScreenServices& services_;
};

// PauseScreen's geometry: a grid of buttons (cells padded by 4) placed with half the free width on the left and a
// quarter of the free height above, under the title at y = 40.
struct PauseLayout {
    int titleY = 40;
    Rect back, advancements, statistics, options, worldOptions, quit;
};
[[nodiscard]] PauseLayout computePauseLayout(int width, int height);

// "Game Menu": Back to Game, Options, and Save and Quit to Title. (Advancements, Statistics and World Options
// are shown locked: they do not exist here yet.)
class PauseScreen : public Screen {
public:
    PauseScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void init() override;

private:
    ScreenServices& services_;
};

}  // namespace falcon
