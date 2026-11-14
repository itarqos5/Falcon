#pragma once

#include "apex/game/screen_services.hpp"
#include "apex/ui/screen.hpp"

#include <string>
#include <vector>

namespace apex {

// "Caution: Third-Party Online Play": the notice shown before the server list, until the player ticks
// "Do not show this screen again". Proceed opens the server list; Back and Escape leave.
class SafetyScreen : public Screen {
public:
    SafetyScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void init() override;

private:
    ScreenServices& services_;
    bool dontShowAgain_ = false;
    std::vector<std::string> lines_;  // the wrapped message
    int titleY_ = 0;
    int messageY_ = 0;
};

}  // namespace apex
