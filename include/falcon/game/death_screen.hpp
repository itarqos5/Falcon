#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/ui/screen.hpp"

namespace falcon {

// "You Died!" (the game's DeathScreen): the world behind it goes red, the title is drawn at double size with the score under it, and two buttons:
// Respawn (back at the spawn with full health) and Title Screen (leave the world). Escape does nothing here, as in the game.
class DeathScreen : public Screen {
public:
    DeathScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    [[nodiscard]] bool shouldCloseOnEsc() const override { return false; }
    // The buttons wake up one second after the screen opens (a click made while dying must not respawn at once).
    bool mouseClicked(double x, double y, int button) override;

protected:
    void init() override;

private:
    ScreenServices& services_;
    double ticks_ = 0.0;  // game ticks since it opened
};

}  // namespace falcon
