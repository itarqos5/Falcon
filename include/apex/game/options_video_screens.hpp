#pragma once

#include "apex/game/game_options_screen.hpp"

#include <functional>
#include <vector>

namespace apex {

// "Video Settings": Display, Quality & Performance (with the Preset slider) and Preferences.
class VideoSettingsScreen : public GameOptionsScreen {
public:
    VideoSettingsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.videoTitle") {}

    // Sets the 17 options a graphics preset controls (Fast, Fancy or Fabulous!).
    static void applyPreset(GameOptions& options, GraphicsPreset preset);

protected:
    void addOptions() override;

private:
    void markCustom();   // a quality option was changed by hand: the Preset reads "Custom"
    void syncAll();      // re-reads every widget from the options (after a preset rewrote them)

    std::vector<std::function<void()>> syncers_;
    SliderWidget* presetSlider_ = nullptr;
    SliderWidget* anisotropySlider_ = nullptr;
};

// "Controls": Mouse Settings, Key Binds and the hold/toggle and movement options.
class ControlsScreen : public GameOptionsScreen {
public:
    ControlsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "controls.title") {}

protected:
    void addOptions() override;
};

// "Mouse Settings": sensitivity, scrolling, inversion and cursor shape.
class MouseSettingsScreen : public GameOptionsScreen {
public:
    MouseSettingsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.mouse_settings.title") {}

protected:
    void addOptions() override;
};

}  // namespace apex
