#pragma once

#include "falcon/ui/rect.hpp"
#include "falcon/ui/screen.hpp"
#include "falcon/game/screen_services.hpp"

#include <array>

namespace falcon {

// Where every Options screen widget sits for a given GUI-scaled window size, following the
// game's header (61) / content / footer (33) layout including its rounding rules.
struct OptionsScreenLayout {
    Rect title;
    Rect fov;
    Rect online;
    std::array<Rect, 10> grid;  // 2 columns x 5 rows, row-major
    Rect done;
};

[[nodiscard]] OptionsScreenLayout computeOptionsScreenLayout(int width, int height, int titleWidth);

// The FOV option (30..110) and its 0..1 slider value.
inline constexpr int kFovMin = 30;
inline constexpr int kFovMax = 110;
[[nodiscard]] double fovToSliderValue(int fov);
[[nodiscard]] int sliderValueToFov(double sliderValue);

// "Options" as opened from the main menu: field-of-view slider, the settings sub-screens and Done.
// The sub-screen buttons are inert for now.
class OptionsScreen : public Screen {
public:
    OptionsScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void init() override;

private:
    [[nodiscard]] std::string fovLabel(int fov) const;

    ScreenServices& services_;
    const Language& language_;
    const Font& font_;
    GameOptions& options_;
};

}  // namespace falcon
