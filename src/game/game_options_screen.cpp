#include "falcon/game/game_options_screen.hpp"

namespace falcon {

std::unique_ptr<CycleButton> GameOptionsScreen::optionToggle(const std::string& captionKey, bool GameOptions::*field,
                                                             const std::string& tooltipKey, int width) {
    auto button = makeToggle(captionKey, services_.options.*field, width,
                             [this, field](bool on) { services_.options.*field = on; });
    if (!tooltipKey.empty()) button->tooltip = text(tooltipKey);
    return button;
}

std::unique_ptr<SliderWidget> GameOptionsScreen::percentSlider(const std::string& captionKey, double GameOptions::*field,
                                                               const std::string& tooltipKey, bool zeroIsOff, int width) {
    auto slider = makeSlider(
        services_.options.*field, width,
        [this, captionKey, zeroIsOff](double v) {
            return zeroIsOff ? percentOrOffLabel(captionKey, v) : percentLabel(captionKey, v);
        },
        [this, field](double v) { services_.options.*field = v; });
    if (!tooltipKey.empty()) slider->tooltip = text(tooltipKey);
    return slider;
}

std::unique_ptr<CycleButton> GameOptionsScreen::makeNarratorButton() {
    auto button = makeChoice("options.narrator", {"options.narrator.notavailable"}, 0, kSmallWidth, nullptr);
    button->active = false;
    return button;
}

}  // namespace falcon
