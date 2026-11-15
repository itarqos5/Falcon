#include "falcon/client.hpp"

#include "falcon/audio/audio_engine.hpp"
#include "falcon/audio/music_manager.hpp"
#include "falcon/audio/sound_system.hpp"
#include "falcon/assets/asset_locator.hpp"
#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_manager.hpp"
#include "falcon/gfx/gl.hpp"
#include "falcon/gfx/gui_renderer.hpp"
#include "falcon/gfx/panorama_renderer.hpp"
#include "falcon/gfx/texture_manager.hpp"
#include "falcon/platform/dialog.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/platform/window.hpp"
#include "falcon/resource_reloader.hpp"
#include "falcon/text/font.hpp"
#include "falcon/text/language.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/game/game_options.hpp"
#include "falcon/ui/gui_scale.hpp"
#include "falcon/game/now_playing_toast.hpp"
#include "falcon/game/game_session.hpp"
#include "falcon/game/game_ui.hpp"
#include "falcon/game/death_screen.hpp"
#include "falcon/gfx/frame_queue.hpp"
#include "falcon/game/in_game_screen.hpp"
#include "falcon/game/lighting_audit.hpp"
#include "falcon/game/vanilla_compare.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/util/options_file.hpp"
#include "falcon/util/task_pool.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/game/edition_badge.hpp"
#include "falcon/game/panorama.hpp"
#include "falcon/game/splash_manager.hpp"
#include "falcon/ui/sprite_manager.hpp"
#include "falcon/game/title_screen.hpp"
#include "falcon/ui/confirm_screen.hpp"
#include "falcon/world/level_info.hpp"
#include "falcon/version.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

// The command line of the client: the options, and the usage text --help shows.

namespace falcon {

static std::string nextValue(int& i, int argc, const char* const* argv) {
    if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + argv[i]);
    return argv[++i];
}

std::string usageText() {
    return "Usage: Falcon [options]\n"
           "  --assets <dir>          Unpacked game assets (the folder that contains 'minecraft/').\n"
           "                          Default: $FALCON_ASSETS_DIR, then the newest horizon-launcher 26.3 assets (development).\n"
           "  --size <W>x<H>          Initial window size (default 854x480)\n"
           "  --capture <file.png>    Save one frame as a PNG and exit\n"
           "  --capture-after <s>     Seconds to run before capturing (default 4)\n"
           "  --hold-loading <s>      Keep the \"Loading terrain...\" screen up for at least this many seconds (for screenshots)\n"
           "  --capture-loading       With --play: capture without waiting for the world to be ready (the loading screen)\n"
           "  --audit-lighting        Mesh every block beside stone pillars, report which are not smooth-lit, and exit\n"
           "  --seed <n>              Fix the random splash text\n"
           "  --mouse <X>,<Y>         Pretend the cursor is at this window position (for captures)\n"
           "  --audio-test            Decode and play a click and a music track, report, and exit\n"
           "  --audio-devices         List the output devices, switch to each one silently, report, and exit\n"
           "  --language <code>       Start in this language (e.g. de_de, fr_fr, ja_jp)\n"
           "  --borderless            No title bar or frame; the top edge of the window moves it (F11 still toggles fullscreen)\n"
           "  --data <dir>            The folder with Falcon's data tables (registries.json, gameplay-facts.json, ...) and the game's data/;\n"
           "                          by default the folder that holds the assets folder\n"
           "  --username <name>       The player's name (shown in chat); the launcher passes the account's\n"
           "  --uuid <id>             The player's account id\n"
           "  --game-dir <dir>        The .minecraft folder to use (its resourcepacks/ folder lists the packs)\n"
           "  --packs <ids>@<s>       Select resource packs (comma-separated ids, lowest priority first, e.g.\n"
           "                          high_contrast or file/MyPack.zip) after <s> seconds, as the packs screen would\n"
           "  --background            Hidden, unfocused window (implied by --capture): never pops up over other apps\n"
           "  --graphics-api <api>    opengl or vulkan, for this run (instead of the Graphics API setting; OpenGL is used when Vulkan cannot be)\n"
           "  --mem-report            Print the process's RAM use after each loading step\n"
           "  --play <folder>         Open this world of <game dir>/saves right away\n"
           "  --cam X,Y,Z,YAW,PITCH   Put the player there and let them fly (for screenshots)\n"
           "  --bench <seconds>       Fly a fixed path, print frame times and memory, and exit (needs --play)\n"
           "  --music-toast           Show the \"now playing\" toast when a track starts (game default: off)\n"
           "  --mute                  Do not output any sound\n"
           "  --audio                 Output sound even with --capture (captures are silent by default)\n"
           "  --java-edition          Keep the original \"JAVA EDITION\" title badge (default: C++ EDITION)\n"
           "  --scroll <X>,<Y>,<DY>@<s>  Inject a mouse-wheel turn (DY > 0 = up) at that position\n"
           "  --type <text>@<s>       Inject typed characters\n"
           "  --key <code>@<s>        Inject a key press (GLFW code, e.g. 256 = Escape)\n"
           "  --press <X>,<Y>,<B>@<s>   Press mouse button B (0 left, 1 right, 2 middle) at that window position\n"
           "  --release <X>,<Y>,<B>@<s> Release it (repeatable; with --press, for scripted drags and right clicks)\n"
           "  --move <X>,<Y>@<s>      Move the pointer there (repeatable)\n"
           "  --shift                 Treat shift as held (a shift-click)\n"
           "  --give <item>[:<n>]     Put items in the inventory once a world is open (repeatable)\n"
           "  --gamemode <mode>       survival, creative, adventure or spectator, whatever the world says\n"
           "  --cheats                allow commands whatever the world says\n"
           "  --connect <host[:port]> join a server (one that does not check accounts) at start-up; --username <name> says who as\n"
           "  --drop-from <blocks>    Lift the player that far once the world is ready and let them fall (to test fall damage)\n"
           "  --vitals <h>,<f>,<l>,<p>  What the survival bars show: health (20 = full), food (20), level, progress 0..1 (for screenshots)\n"
           "  --click <X>,<Y>@<s>     Inject a left click at that window position after <s> seconds\n"
           "                          (repeatable; for scripted tests)\n"
           "  --help                  Show this message\n";
}

ClientOptions parseOptions(int argc, const char* const* argv) {
    ClientOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            o.showHelp = true;
        } else if (arg == "--assets") {
            o.assetsDir = std::filesystem::path(nextValue(i, argc, argv));
        } else if (arg == "--size") {
            const std::string v = nextValue(i, argc, argv);
            const auto x = v.find('x');
            if (x == std::string::npos) throw std::invalid_argument("--size expects WxH, got " + v);
            o.width = std::stoi(v.substr(0, x));
            o.height = std::stoi(v.substr(x + 1));
            if (o.width < 1 || o.height < 1) throw std::invalid_argument("--size must be positive");
        } else if (arg == "--capture") {
            o.capturePath = std::filesystem::path(nextValue(i, argc, argv));
        } else if (arg == "--capture-after") {
            o.captureAfterSeconds = std::stod(nextValue(i, argc, argv));
        } else if (arg == "--seed") {
            o.seed = static_cast<unsigned>(std::stoul(nextValue(i, argc, argv)));
        } else if (arg == "--mouse") {
            const std::string v = nextValue(i, argc, argv);
            const auto comma = v.find(',');
            if (comma == std::string::npos) throw std::invalid_argument("--mouse expects X,Y");
            o.mouse = std::make_pair(std::stod(v.substr(0, comma)), std::stod(v.substr(comma + 1)));
        } else if (arg == "--scroll") {
            // X,Y,DY@SECONDS
            const std::string v = nextValue(i, argc, argv);
            const auto c1 = v.find(',');
            const auto c2 = v.find(',', c1 == std::string::npos ? 0 : c1 + 1);
            const auto at = v.find('@');
            if (c1 == std::string::npos || c2 == std::string::npos || at == std::string::npos) {
                throw std::invalid_argument("--scroll expects X,Y,DY@SECONDS, got " + v);
            }
            o.scrolls.push_back({std::stod(v.substr(0, c1)), std::stod(v.substr(c1 + 1, c2 - c1 - 1)),
                                 std::stod(v.substr(c2 + 1, at - c2 - 1)), std::stod(v.substr(at + 1))});
        } else if (arg == "--type") {
            const std::string v = nextValue(i, argc, argv);
            const auto at = v.rfind('@');
            if (at == std::string::npos) throw std::invalid_argument("--type expects TEXT@SECONDS, got " + v);
            o.typed.push_back({v.substr(0, at), std::stod(v.substr(at + 1))});
        } else if (arg == "--key") {
            const std::string v = nextValue(i, argc, argv);
            const auto at = v.find('@');
            if (at == std::string::npos) throw std::invalid_argument("--key expects CODE@SECONDS, got " + v);
            std::string when = v.substr(at + 1);
            ScriptedKey scripted;
            scripted.key = std::stoi(v.substr(0, at));
            if (!when.empty() && (when.back() == '+' || when.back() == '-')) {
                scripted.press = when.back() == '+';
                scripted.release = when.back() == '-';
                when.pop_back();
            }
            scripted.seconds = std::stod(when);
            o.keys.push_back(scripted);
        } else if (arg == "--java-edition") {
            o.keepJavaEdition = true;
        } else if (arg == "--audio-test") {
            o.audioTest = true;
        } else if (arg == "--audio-devices") {
            o.audioDevices = true;
        } else if (arg == "--connect") {
            o.connect = nextValue(i, argc, argv);
        } else if (arg == "--play") {
            o.play = nextValue(i, argc, argv);
        } else if (arg == "--cam") {
            const std::string v = nextValue(i, argc, argv);
            std::array<double, 5> cam{};
            std::size_t pos = 0;
            for (std::size_t k = 0; k < cam.size(); ++k) {
                const auto comma = v.find(',', pos);
                if (comma == std::string::npos && k + 1 < cam.size()) throw std::invalid_argument("--cam expects X,Y,Z,YAW,PITCH, got " + v);
                cam[k] = std::stod(v.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
                pos = comma == std::string::npos ? v.size() : comma + 1;
            }
            o.camera = cam;
        } else if (arg == "--cheats") {
            o.cheats = true;
        } else if (arg == "--edit") {
            o.edits.push_back(nextValue(i, argc, argv));
        } else if (arg == "--bench") {
            o.benchmark = std::stod(nextValue(i, argc, argv));
        } else if (arg == "--compare-vanilla") {
            o.compareVanilla = nextValue(i, argc, argv);
        } else if (arg == "--audit-lighting") {
            o.auditLighting = true;
        } else if (arg == "--render-distance") {
            const std::string v = nextValue(i, argc, argv);
            const auto at = v.find('@');
            if (at == std::string::npos) throw std::invalid_argument("--render-distance expects N@SECONDS, got " + v);
            o.renderDistances.push_back({std::stoi(v.substr(0, at)), std::stod(v.substr(at + 1))});
        } else if (arg == "--hold-loading") {
            o.holdLoading = std::stod(nextValue(i, argc, argv));
        } else if (arg == "--capture-loading") {
            o.captureLoading = true;
        } else if (arg == "--language") {
            o.language = nextValue(i, argc, argv);
        } else if (arg == "--borderless") {
            o.borderless = true;
        } else if (arg == "--data") {
            o.dataDir = std::filesystem::path(nextValue(i, argc, argv));
        } else if (arg == "--username") {
            o.username = nextValue(i, argc, argv);
        } else if (arg == "--uuid") {
            o.uuid = nextValue(i, argc, argv);
        } else if (arg == "--game-dir") {
            o.gameDir = std::filesystem::path(nextValue(i, argc, argv));
        } else if (arg == "--packs") {
            // ID,ID,...@SECONDS  (the list may be empty: "@5" deselects everything)
            const std::string v = nextValue(i, argc, argv);
            const auto at = v.rfind('@');
            if (at == std::string::npos) throw std::invalid_argument("--packs expects IDS@SECONDS, got " + v);
            ScriptedPacks selection;
            selection.seconds = std::stod(v.substr(at + 1));
            std::stringstream list(v.substr(0, at));
            for (std::string id; std::getline(list, id, ',');) {
                if (!id.empty()) selection.ids.push_back(id);
            }
            o.packs.push_back(std::move(selection));
        } else if (arg == "--background") {
            o.background = true;
        } else if (arg == "--graphics-api") {
            const std::string v = nextValue(i, argc, argv);
            if (v != "opengl" && v != "vulkan") throw std::invalid_argument("--graphics-api expects opengl or vulkan");
            o.graphicsApi = parseGraphicsApi(v);
        } else if (arg == "--mem-report") {
            o.memReport = true;
        } else if (arg == "--music-toast") {
            o.musicToast = true;
        } else if (arg == "--mute") {
            o.mute = true;
        } else if (arg == "--audio") {
            o.forceAudio = true;
        } else if (arg == "--press" || arg == "--release") {
            // X,Y,BUTTON@SECONDS
            const std::string v = nextValue(i, argc, argv);
            const auto c1 = v.find(',');
            const auto c2 = v.find(',', c1 == std::string::npos ? 0 : c1 + 1);
            const auto at = v.find('@');
            if (c1 == std::string::npos || c2 == std::string::npos || at == std::string::npos) {
                throw std::invalid_argument(arg + " expects X,Y,BUTTON@SECONDS, got " + v);
            }
            o.mouseEvents.push_back({std::stod(v.substr(0, c1)), std::stod(v.substr(c1 + 1, c2 - c1 - 1)), std::stoi(v.substr(c2 + 1, at - c2 - 1)),
                                     arg == "--press", std::stod(v.substr(at + 1))});
        } else if (arg == "--move") {
            const std::string v = nextValue(i, argc, argv);
            const auto comma = v.find(',');
            const auto at = v.find('@');
            if (comma == std::string::npos || at == std::string::npos || at < comma) throw std::invalid_argument("--move expects X,Y@SECONDS, got " + v);
            o.mouseEvents.push_back({std::stod(v.substr(0, comma)), std::stod(v.substr(comma + 1, at - comma - 1)), -1, true, std::stod(v.substr(at + 1))});
        } else if (arg == "--shift") {
            o.holdShift = true;
        } else if (arg == "--give") {
            o.give.push_back(nextValue(i, argc, argv));
        } else if (arg == "--drop-from") {
            o.dropFrom = std::stod(nextValue(i, argc, argv));
        } else if (arg == "--vitals") {  // for screenshots of the survival bars: 13,7,5,0.4 = 6.5 hearts, 3.5 food, level 5, 40% of the bar
            const std::string v = nextValue(i, argc, argv);
            std::array<float, 4> values{};
            std::size_t from = 0;
            for (std::size_t k = 0; k < values.size(); ++k) {
                const std::size_t comma = v.find(',', from);
                if (comma == std::string::npos && k + 1 < values.size()) throw std::invalid_argument("--vitals expects HEALTH,FOOD,LEVEL,PROGRESS, got " + v);
                values[k] = std::stof(v.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
                from = comma + 1;
            }
            o.vitals = values;
        } else if (arg == "--gamemode") {
            const std::string v = nextValue(i, argc, argv);
            if (v == "survival") {
                o.gameType = 0;
            } else if (v == "creative") {
                o.gameType = 1;
            } else if (v == "adventure") {
                o.gameType = 2;
            } else if (v == "spectator") {
                o.gameType = 3;
            } else {
                throw std::invalid_argument("--gamemode expects survival, creative, adventure or spectator, got " + v);
            }
        } else if (arg == "--click") {
            const std::string v = nextValue(i, argc, argv);
            const auto comma = v.find(',');
            const auto at = v.find('@');
            if (comma == std::string::npos || at == std::string::npos || at < comma) {
                throw std::invalid_argument("--click expects X,Y@SECONDS, got " + v);
            }
            o.clicks.push_back({std::stod(v.substr(0, comma)),
                                std::stod(v.substr(comma + 1, at - comma - 1)),
                                std::stod(v.substr(at + 1))});
        } else {
            throw std::invalid_argument("unknown option " + arg);
        }
    }
    return o;
}

}  // namespace falcon
