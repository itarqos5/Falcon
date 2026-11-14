#pragma once

#include "apex/game/game_options_screen.hpp"

namespace apex {

// What the Device button cycles through, as GameOptions::audioDevice values: "" (System Default)
// first, then each device by name. With one device (or none) there is nothing to choose between,
// so only "System Default" is offered and the button does nothing when pressed.
[[nodiscard]] std::vector<std::string> buildDeviceChoices(const std::vector<AudioDevice>& devices);

// "Music & Sound Options": a volume slider per sound category, then Device, Closed Captions,
// Directional Audio, Music Frequency and Music Toast. The rows scroll when they do not all fit.
class SoundOptionsScreen : public GameOptionsScreen {
public:
    SoundOptionsScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void addOptions() override;

private:
    [[nodiscard]] std::string volumeLabel(SoundSource source, double value) const;
    [[nodiscard]] std::string deviceLabel(int choice) const;
    // Re-reads the engine's device list into the Device button (it arrives in the background).
    void syncDeviceButton();

    CycleButton* deviceButton_ = nullptr;
    std::vector<std::string> deviceChoices_ = {""};
    unsigned deviceListVersion_ = 0;
};

}  // namespace apex
