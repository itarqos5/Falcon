#pragma once

#include "falcon/game/game_options_screen.hpp"

#include <functional>
#include <vector>

namespace falcon {

// "Video Settings": tabs down the left (Presets, Display, Quality, LODs, Preferences) and the options of the one that is open beside them. The Presets tab is three cards (Fast,
// Fancy, Fabulous!), the LODs tab the levels of detail beyond the loaded chunks: on or off, how far out (64 to 8192 chunks) and how fine (Low, Medium or Full Detail).
class VideoSettingsScreen : public GameOptionsScreen {
public:
    VideoSettingsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.videoTitle") {}

    // Sets the options a graphics preset controls (Fast, Fancy or Fabulous!), the LODs among them.
    static void applyPreset(GameOptions& options, GraphicsPreset preset);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void addOptions() override;
    void addFooter(const ListScreenLayout& layout) override;
    [[nodiscard]] ListScreenLayout computeLayout(int titleWidth) const override;

private:
    void fill();               // the rows of the open tab
    void selectTab(int tab);
    void markCustom();         // a quality option was changed by hand: the preset reads "Custom"

    int tab_ = 0;
    std::vector<Widget*> tabButtons_;
    SliderWidget* anisotropySlider_ = nullptr;
    SliderWidget* lodRadiusSlider_ = nullptr;
    CycleButton* lodDetailButton_ = nullptr;
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

}  // namespace falcon
