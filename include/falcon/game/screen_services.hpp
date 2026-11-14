#pragma once

#include "falcon/audio/music_manager.hpp"
#include "falcon/audio/sound_system.hpp"
#include "falcon/packs/pack_repository.hpp"
#include "falcon/text/font.hpp"
#include "falcon/text/language.hpp"
#include "falcon/game/game_options.hpp"
#include "falcon/game/panorama.hpp"
#include "falcon/ui/screen.hpp"

#include <filesystem>
#include <functional>

namespace falcon {

class GameSession;

// Everything the screens share. Screens hold a reference and pass it on to the screens they open.
struct ScreenServices {
    const Language& language;
    const Font& font;
    Panorama& panorama;
    GameOptions& options;
    SoundSystem& sounds;
    MusicManager& music;
    PackRepository& packs;
    // The selected resource packs (GameOptions::resourcePacks) changed: load them. Returns at once;
    // the loading happens in the background.
    std::function<void()> packsChanged;
    // The ".minecraft" folder: worlds are in saves/, packs in resourcepacks/.
    std::filesystem::path gameDir;
    // The largest GUI scale the current window can use (the GUI Scale button cycles 0 = Auto .. this).
    std::function<int()> maxGuiScale;
    // The monitor's video modes as "1920x1080@144 (24bit)" (empty when there is no monitor).
    std::function<std::vector<std::string>()> videoModes;
    // The languages the game ships (code, name, region), ordered by code.
    std::vector<LanguageInfo> languages;
    // The world being played, or null in the menus. Menus opened while it is set draw over the world.
    GameSession* session = nullptr;
    // Starts playing the world in this folder of saves/ (the main loop does it between frames).
    std::function<void(const std::string& folder)> startWorld;
    // Leaves the world and goes back to the title screen.
    std::function<void()> quitWorld;
};

// A button handler that opens screen T on top of the current one (T's constructor takes the
// navigator and the services): addButton(text("menu.options"), rect, openScreen<OptionsScreen>(navigator_, services_)).
template <class T>
[[nodiscard]] std::function<void()> openScreen(ScreenNavigator& navigator, ScreenServices& services) {
    return [&navigator, &services] { navigator.push(std::make_unique<T>(navigator, services)); };
}

}  // namespace falcon
