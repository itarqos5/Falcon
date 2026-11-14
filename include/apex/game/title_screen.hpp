#pragma once

#include "apex/ui/rect.hpp"
#include "apex/ui/screen.hpp"
#include "apex/game/screen_services.hpp"

#include <optional>
#include <string>

namespace apex {

// Where every title screen widget sits for a given GUI-scaled window size.
struct TitleScreenLayout {
    Rect singleplayer, multiplayer, realms;
    Rect friends, language, accessibility;  // the three 20x20 icon buttons
    Rect options, quit;
    int copyrightX = 0;
    int copyrightY = 0;
};

[[nodiscard]] TitleScreenLayout computeTitleScreenLayout(int width, int height, int copyrightWidth);

struct TitleScreenOptions {
    std::optional<std::string> splash;
    bool fadeIn = false;         // true on the first screen shown after launch
    bool easterEggLogo = false;  // 1-in-10000 "Minceraft" logo
    std::string versionText = "Minecraft 26.3";
};

// The Java Edition main menu.
class TitleScreen : public Screen {
public:
    TitleScreen(ScreenNavigator& navigator, ScreenServices& services, TitleScreenOptions options);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    [[nodiscard]] bool shouldCloseOnEsc() const override { return false; }

protected:
    void init() override;

private:
    void renderLogo(GuiGraphics& graphics, float alpha);
    void renderSplash(GuiGraphics& graphics, float alpha, double millis);

    ScreenServices& services_;
    const Language& language_;
    const Font& font_;
    Panorama& panorama_;
    TitleScreenOptions options_;
    bool fading_;
    std::optional<double> fadeInStart_;
    // The bottom-right line. It is this client's own wording (not a translated game string) and is
    // plain text: nothing happens when it is clicked.
    std::string footerText_;
    int footerX_ = 0;
    int footerY_ = 0;
};

}  // namespace apex
