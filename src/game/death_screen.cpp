#include "falcon/game/death_screen.hpp"

#include "falcon/game/game_session.hpp"

#include <algorithm>

namespace falcon {

void DeathScreen::init() {
    // DeathScreen: the buttons are 200 wide, a quarter of the height plus 72 and 96 down.
    addButton(text("deathScreen.respawn"), {width_ / 2 - 100, height_ / 4 + 72, 200, 20}, [this] {
        if (services_.session != nullptr) services_.session->respawn();
        navigator_.pop();
    });
    addButton(text("deathScreen.titleScreen"), {width_ / 2 - 100, height_ / 4 + 96, 200, 20}, [this] {
        if (services_.quitWorld) services_.quitWorld();
    });
}

bool DeathScreen::mouseClicked(double x, double y, int button) {
    if (ticks_ < 20.0) return true;
    return Screen::mouseClicked(x, y, button);
}

void DeathScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ticks_ += static_cast<double>(time.deltaTicks);
    // The red wash: fillGradient(0, 0, w, h, 0x60500000, 0xA0803030), drawn as thin strips.
    const auto channel = [](std::uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFFu); };
    constexpr std::uint32_t kTop = 0x60500000u, kBottom = 0xA0803030u;
    for (int y = 0; y < height_; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(std::max(1, height_ - 1));
        std::uint32_t color = 0;
        for (const int shift : {24, 16, 8, 0}) {
            const int v = channel(kTop, shift) + static_cast<int>(static_cast<float>(channel(kBottom, shift) - channel(kTop, shift)) * t);
            color |= static_cast<std::uint32_t>(std::clamp(v, 0, 255)) << shift;
        }
        graphics.fill(0, y, width_, y + 1, color);
    }
    // The title at double size, centred at a quarter... y = 30 in the doubled space, then the score.
    graphics.pushPose();
    graphics.pose().scale(2.0f);
    const std::string title = text("deathScreen.title");
    graphics.text(title, static_cast<float>(width_ / 2 / 2 - graphics.textWidth(title) / 2), 30.0f, kWhite);
    graphics.popPose();
    const std::string label = text("deathScreen.score") + ": ";  // "Score: " in white, the score itself in yellow
    const int x = width_ / 2 - (graphics.textWidth(label) + graphics.textWidth("0")) / 2;
    graphics.text(label, static_cast<float>(x), 85.0f, kWhite);
    graphics.text("0", static_cast<float>(x + graphics.textWidth(label)), 85.0f, 0xFFFFFF55u);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
