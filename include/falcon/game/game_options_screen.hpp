#pragma once

#include "falcon/game/game_options.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/ui/list_screen.hpp"

#include <string>

namespace falcon {

// A ListScreen with the game's settings and services at hand: the base of Music & Sounds, Chat
// Settings, Video Settings and the other Options screens. Adds widgets bound straight to a field of
// GameOptions on top of the library's generic ones.
class GameOptionsScreen : public ListScreen {
public:
    GameOptionsScreen(ScreenNavigator& navigator, ScreenServices& services, std::string titleKey)
        : ListScreen(navigator, std::move(titleKey)), services_(services) {}

protected:
    // On/Off button bound to a GameOptions field ("Caption: ON"), with an optional tooltip.
    [[nodiscard]] std::unique_ptr<CycleButton> optionToggle(const std::string& captionKey, bool GameOptions::*field,
                                                            const std::string& tooltipKey = {}, int width = kSmallWidth);
    // 0..100% slider bound to a GameOptions field ("Caption: 50%"; `zeroIsOff` shows OFF at exactly 0).
    [[nodiscard]] std::unique_ptr<SliderWidget> percentSlider(const std::string& captionKey, double GameOptions::*field,
                                                              const std::string& tooltipKey = {}, bool zeroIsOff = false,
                                                              int width = kSmallWidth);
    // "Narrator: Not Available": this client has no narrator, so the button is shown but inactive.
    [[nodiscard]] std::unique_ptr<CycleButton> makeNarratorButton();

    ScreenServices& services_;
};

}  // namespace falcon
