#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/ui/screen.hpp"

#include <string>
#include <vector>

namespace falcon {

// "Connection Lost" / "Failed to connect to the server" (the game's DisconnectedScreen): the title, the reason the server (or Falcon) gave, word-wrapped, and a button that goes
// back to the server list underneath.
class DisconnectedScreen : public Screen {
public:
    // `titleKey` is a language key ("disconnect.lost", "connect.failed"); `reason` is shown as it is.
    DisconnectedScreen(ScreenNavigator& navigator, ScreenServices& services, std::string titleKey, std::string reason)
        : Screen(navigator), services_(services), titleKey_(std::move(titleKey)), reason_(std::move(reason)) {}

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void init() override;

private:
    ScreenServices& services_;
    std::string titleKey_, reason_;
    std::vector<std::string> lines_;
    int titleY_ = 0, messageY_ = 0;
};

}  // namespace falcon
