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

namespace falcon {
namespace {

constexpr const char* kGameVersion = "26.3";

std::string nextValue(int& i, int argc, const char* const* argv) {
    if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + argv[i]);
    return argv[++i];
}

bool saveFrame(const std::filesystem::path& file, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 4);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, width, height, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());

    // OpenGL rows run bottom to top.
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        auto* top = pixels.data() + static_cast<std::size_t>(y) * stride;
        auto* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * stride;
        std::swap_ranges(top, top + stride, bottom);
    }
    // The framebuffer has no meaningful alpha.
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    return writePng(file, width, height, pixels.data());
}

// Diagnostic for --audio-devices: lists the outputs and switches to each in turn, then back to the
// system default, the way the Device button does. Silent: nothing is played.
int runAudioDevices() {
    using Clock = std::chrono::steady_clock;
    AudioEngine engine;
    if (!engine.available()) {
        std::printf("[audio-devices] FAILED: %s\n", engine.error().c_str());
        return 2;
    }
    std::printf("[audio-devices] opened '%s' at %d Hz\n", engine.deviceName().c_str(), engine.sampleRate());

    const auto waitFor = [](auto&& done) {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!done() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return done();
    };

    const auto listStart = Clock::now();
    const unsigned before = engine.deviceListVersion();
    engine.refreshDevices();
    if (!waitFor([&] { return engine.deviceListVersion() != before; })) {
        std::printf("[audio-devices] FAILED: no device list within 10 s\n");
        return 3;
    }
    const auto listMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - listStart).count();
    const std::vector<AudioDevice> devices = engine.devices();
    std::printf("[audio-devices] %zu output device(s), listed in %lld ms:\n", devices.size(),
                static_cast<long long>(listMs));
    for (const AudioDevice& d : devices) std::printf("    %s%s\n", d.name.c_str(), d.isDefault ? "  (default)" : "");

    std::string defaultName;
    for (const AudioDevice& d : devices) {
        if (d.isDefault) defaultName = d.name;
    }
    int failures = 0;
    std::vector<std::string> targets;
    for (const AudioDevice& d : devices) targets.push_back(d.name);
    targets.emplace_back("");  // finish back on the system default
    for (const std::string& target : targets) {
        const auto start = Clock::now();
        engine.selectDevice(target);
        const auto returnedUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
        // Done when the engine reports the wanted device and its callback is delivering buffers.
        const std::string& expected = target.empty() ? defaultName : target;
        const std::uint64_t framesBefore = engine.stats().framesRendered;
        const bool ok = waitFor([&] {
            return engine.available() && engine.deviceName() == expected &&
                   engine.stats().framesRendered > framesBefore + 480;
        });
        const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::printf("[audio-devices] select '%s': call returned in %lld us, playing '%s' at %d Hz after %lld ms  %s\n",
                    target.empty() ? "(system default)" : target.c_str(), static_cast<long long>(returnedUs),
                    engine.deviceName().c_str(), engine.sampleRate(), static_cast<long long>(totalMs),
                    ok ? "OK" : "FAILED");
        if (!ok) ++failures;
    }
    std::printf("[audio-devices] %s\n", failures == 0 ? "all switches worked" : "some switches FAILED");
    return failures == 0 ? 0 : 4;
}

// Diagnostic for --audio-test: proves decoding and the output device work with real game files.
int runAudioTest(const ResourceManager& resources) {
    const auto clickBytes = resources.readBytes(ResourceLocation("minecraft", "sounds/random/click.ogg"));
    const auto click = clickBytes ? decodeOgg(*clickBytes) : std::nullopt;
    if (!click) {
        std::printf("[audio-test] FAILED: could not decode sounds/random/click.ogg\n");
        return 1;
    }
    float clickPeak = 0.0f;
    for (const float s : click->samples) clickPeak = std::max(clickPeak, std::fabs(s));
    std::printf("[audio-test] click.ogg: %d Hz, %d ch, %.3f s, peak sample %.3f\n", click->sampleRate,
                click->channels, click->durationSeconds(), static_cast<double>(clickPeak));

    AudioEngine engine;
    if (!engine.available()) {
        std::printf("[audio-test] FAILED: %s\n", engine.error().c_str());
        return 2;
    }
    std::printf("[audio-test] device '%s' at %d Hz\n", engine.deviceName().c_str(), engine.sampleRate());

    engine.playSound(std::make_shared<PcmSound>(*click), 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const float soundPeak = engine.takePeak();
    std::printf("[audio-test] click mixed: peak %.3f (device callbacks rendered %llu frames)\n",
                static_cast<double>(soundPeak),
                static_cast<unsigned long long>(engine.stats().framesRendered));

    auto bytes = resources.readBytes(ResourceLocation("minecraft", "sounds/music/game/clark.ogg"));
    auto stream = bytes ? OggStream::open(std::move(*bytes)) : nullptr;
    if (!stream) {
        std::printf("[audio-test] FAILED: could not open music/game/clark.ogg\n");
        return 3;
    }
    std::printf("[audio-test] clark.ogg: %d Hz, %d ch, %.1f s\n", stream->sampleRate(),
                stream->channels(), stream->durationSeconds());
    engine.playMusic(std::move(stream), 0.5f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const float musicPeak = engine.takePeak();
    std::printf("[audio-test] music mixed: peak %.3f, still playing: %s\n",
                static_cast<double>(musicPeak), engine.musicPlaying() ? "yes" : "no");
    engine.stopMusic();

    // Music often fades in from silence, so also measure the loudest sample in its first 40 s.
    auto again = resources.readBytes(ResourceLocation("minecraft", "sounds/music/game/clark.ogg"));
    auto scan = again ? OggStream::open(std::move(*again)) : nullptr;
    float trackPeak = 0.0f;
    if (scan) {
        std::vector<float> block(4096 * static_cast<std::size_t>(scan->channels()));
        for (int decoded = 0; decoded < scan->sampleRate() * 40;) {
            const int got = scan->read(block.data(), 4096);
            if (got <= 0) break;
            decoded += got;
            for (int i = 0; i < got * scan->channels(); ++i) {
                trackPeak = std::max(trackPeak, std::fabs(block[static_cast<std::size_t>(i)]));
            }
        }
    }
    std::printf("[audio-test] clark.ogg loudest sample in the first 40 s: %.3f\n",
                static_cast<double>(trackPeak));
    return (soundPeak > 0.0f && trackPeak > 0.05f) ? 0 : 4;
}

// Screen changes requested by buttons are applied between frames.
class ClientNavigator final : public ScreenNavigator {
public:
    ClientNavigator(Window& window, std::function<void()> buttonClick)
        : window_(window), buttonClick_(std::move(buttonClick)) {}

    void push(std::unique_ptr<Screen> screen) override { pendingPush_ = std::move(screen); }
    void pop() override { pendingPop_ = true; }
    void quit() override { window_.requestClose(); }
    void playButtonClick() override {
        if (buttonClick_) buttonClick_();
    }
    [[nodiscard]] bool isShiftDown() const override { return holdShift_ || window_.shiftDown(); }
    [[nodiscard]] bool isControlDown() const override { return window_.keyDown(341) || window_.keyDown(345); }  // GLFW left / right control
    void setHoldShift(bool hold) { holdShift_ = hold; }  // --shift
    [[nodiscard]] UiContext& ui() override { return *ui_; }
    void setUi(UiContext& context) { ui_ = &context; }  // set before the first screen is built
    void setButtonClick(std::function<void()> callback) { buttonClick_ = std::move(callback); }

    // Applies pending changes to `stack` (root screen at index 0). Returns true if the top
    // screen changed and therefore needs its widgets rebuilt.
    bool apply(std::vector<std::unique_ptr<Screen>>& stack) {
        bool changed = false;
        if (pendingPop_ && stack.size() > 1) {
            stack.pop_back();
            changed = true;
        }
        pendingPop_ = false;
        if (pendingPush_) {
            stack.push_back(std::move(pendingPush_));
            changed = true;
        }
        return changed;
    }

private:
    Window& window_;
    std::function<void()> buttonClick_;
    UiContext* ui_ = nullptr;
    std::unique_ptr<Screen> pendingPush_;
    bool pendingPop_ = false;
    bool holdShift_ = false;
};

}  // namespace

std::string usageText() {
    return "Usage: Falcon [options]\n"
           "  --assets <dir>          Unpacked game assets (the folder that contains 'minecraft/').\n"
           "                          Default: $FALCON_ASSETS_DIR, then the newest horizon-launcher 26.3 assets.\n"
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
           "  --data <dir>            The folder with the game's exported data (registries.json, gameplay-facts.json, ...);\n"
           "                          by default the folder that holds the assets folder\n"
           "  --username <name>       The player's name (shown in chat); the launcher passes the account's\n"
           "  --uuid <id>             The player's account id\n"
           "  --game-dir <dir>        The .minecraft folder to use (its resourcepacks/ folder lists the packs)\n"
           "  --packs <ids>@<s>       Select resource packs (comma-separated ids, lowest priority first, e.g.\n"
           "                          high_contrast or file/MyPack.zip) after <s> seconds, as the packs screen would\n"
           "  --background            Hidden, unfocused window (implied by --capture): never pops up over other apps\n"
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

Client::Client(ClientOptions options) : options_(std::move(options)) {}

std::string_view Client::name() const noexcept { return "Falcon"; }
std::string_view Client::version() const noexcept { return FALCON_VERSION_STRING; }

int Client::run() {
    std::printf("%s v%s (targeting Minecraft Java %s)\n", name().data(), version().data(),
                kGameVersion);

    // ---- find the game assets -------------------------------------------------------------
    const AssetSearchResult search = locateAssets(options_.assetsDir);
    if (!search.location) {
        std::string message = std::string("Could not find the Minecraft ") + kGameVersion +
                              " assets.\n\n";
        for (const std::string& note : search.notes) message += "- " + note + "\n";
        message +=
            "\nPass --assets <dir> (or set FALCON_ASSETS_DIR) pointing at an unpacked 'assets' "
            "folder.";
        if (options_.capturePath || options_.background) {  // scripted runs never open a dialog
            std::fprintf(stderr, "[falcon] fatal: %s\n", message.c_str());
        } else {
            showErrorDialog("Falcon - assets not found", message);
        }
        return 2;
    }
    std::printf("[falcon] assets: %s (%s)\n", search.location->root.generic_string().c_str(),
                search.location->source.c_str());
    if (options_.audioDevices) return runAudioDevices();
    if (options_.audioTest) return runAudioTest(ResourceManager(search.location->root));
    if (options_.auditLighting) {
        const char* appDataDir = std::getenv("APPDATA");
        const std::filesystem::path auditGameDir = options_.gameDir ? *options_.gameDir
                                                   : appDataDir != nullptr ? std::filesystem::path(appDataDir) / ".minecraft" : std::filesystem::path(".minecraft");
        return runLightingAudit(ResourceManager(search.location->root), search.location->root.parent_path(), auditGameDir / "falcon_cache");
    }

    if (options_.compareVanilla) {
        const char* appDataDir = std::getenv("APPDATA");
        const std::filesystem::path compareGameDir = options_.gameDir ? *options_.gameDir
                                                     : appDataDir != nullptr ? std::filesystem::path(appDataDir) / ".minecraft" : std::filesystem::path(".minecraft");
        return runVanillaCompare(ResourceManager(search.location->root), search.location->root.parent_path(), compareGameDir / "falcon_cache", *options_.compareVanilla);
    }
    // --mem-report: RAM after each loading step, to see what costs what.
    std::size_t lastMem = processWorkingSetBytes();
    const auto processStart = std::chrono::steady_clock::now() - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::milli>(millisSinceProcessStart()));  // the OS-side start, not main()
    auto lastStageTime = processStart;
    const auto memStage = [&](const char* what) {
        if (!options_.memReport) return;
        const std::size_t now = processWorkingSetBytes();
        const auto stageNow = std::chrono::steady_clock::now();
        std::printf("[mem] %-26s %7.1f MB  (%+.1f)   t=%5.0f ms  (+%.0f)\n", what, static_cast<double>(now) / 1048576.0,
                    (static_cast<double>(now) - static_cast<double>(lastMem)) / 1048576.0,
                    std::chrono::duration<double, std::milli>(stageNow - processStart).count(),
                    std::chrono::duration<double, std::milli>(stageNow - lastStageTime).count());
        lastStageTime = stageNow;
        lastMem = now;
    };
    memStage("process start");

    // ---- settings and resources ------------------------------------------------------------
    // The game folder and the settings saved in it (read first: they include the language).
    // Settings go to falcon_options.txt (never the real options.txt) whenever a screen is left and
    // when the game closes; automated captures only save when given their own --game-dir, so
    // they never touch the real folder.
    const char* appData = std::getenv("APPDATA");
    const std::filesystem::path gameDir =
        options_.gameDir ? *options_.gameDir
        : appData != nullptr ? std::filesystem::path(appData) / ".minecraft"
                             : std::filesystem::path(".minecraft");
    const std::filesystem::path optionsPath = gameDir / "falcon_options.txt";
    const bool persistOptions = !options_.capturePath || options_.gameDir.has_value();
    OptionsFile optionsFile = OptionsFile::load(optionsPath);
    GameOptions gameOptions;
    gameOptions.load(optionsFile);
    if (options_.language) gameOptions.language = *options_.language;
    std::string savedOptionsText = optionsFile.toString();
    const auto saveOptions = [&] {
        if (!persistOptions) return;
        gameOptions.save(optionsFile);
        const std::string text = optionsFile.toString();
        if (text == savedOptionsText) return;  // nothing changed since the last save
        if (optionsFile.save(optionsPath)) {
            savedOptionsText = text;
        } else {
            std::printf("[falcon] could not save %s\n", optionsPath.string().c_str());
        }
    };

    // Resource packs change what the loaded resources hold while the game runs (see
    // ResourceReloader), so none of them is const; everything else only ever reads them through
    // const references.
    ResourceManager resources(search.location->root);
    if (options_.dataDir) resources.setDataRoot(*options_.dataDir);

    // ---- start-up loading that needs no OpenGL, on other threads ---------------------------
    // Creating the window and the GL context is the slowest single step (mostly the graphics
    // driver starting up), so everything that only reads and decodes files runs beside it.
    struct LoadedLanguage {
        Language language;
        std::string code;
        std::string warning;  // set when the chosen language could not be loaded and English was used
    };
    TaskPool loader(std::clamp(std::thread::hardware_concurrency() / 3, 2u, 4u));  // leaves the cores the main thread needs
    // The six panorama pictures are the longest jobs, so they go first.
    std::array<std::future<std::optional<Image>>, 6> faceLoad;
    for (unsigned face = 0; face < faceLoad.size(); ++face) {
        faceLoad[face] = loader.submit([&resources, face] { return PanoramaRenderer::decodeFace(resources, face); });
    }
    auto languageLoad = loader.submit([&]() -> LoadedLanguage {
        try {
            return {Language::load(resources, gameOptions.language), gameOptions.language, {}};
        } catch (const std::exception& e) {  // a missing translation falls back to English
            return {Language::load(resources, "en_us"), "en_us", e.what()};
        }
    });
    auto fontLoad = loader.submit([&] { return Font::prepare(resources, "default"); });
    auto soundsLoad = loader.submit([&] { return SoundRegistry::load(resources); });
    auto splashLoad = loader.submit([&] { return SplashManager(resources); });
    // This is the C++ edition: the "JAVA EDITION" badge under the logo is regenerated from the
    // game's own lettering (the original texture is never modified on disk).
    auto badgeLoad = loader.submit([&]() -> std::optional<Image> {
        return options_.keepJavaEdition ? std::nullopt : buildCppEditionBadge(resources);
    });

    // ---- window and GL (declared first, so it is destroyed last) --------------------------
    // Captures are automated runs: keep their window hidden and unfocused so they never appear over
    // whatever the person is doing. --background does the same for any other run. A normal run starts
    // hidden too and is shown with its first finished frame, so nothing blank is ever on screen.
    const bool background = options_.background || options_.capturePath.has_value();
    Window window(options_.width, options_.height, "Falcon", background, /*startHidden=*/true, options_.borderless);
    memStage("window + GL context");
    window.setIconFromExecutable();  // the Falcon icon linked into the exe
    memStage("window icon");
    // The Graphics API setting: what was chosen, and what the game really draws with (only OpenGL can draw it so far).
    {
        std::string apiNote;
        const GraphicsApi activeApi = effectiveGraphicsApi(gameOptions.graphicsApi, &apiNote);
        std::printf("[falcon] graphics API: %s%s%s\n", std::string(graphicsApiId(activeApi)).c_str(), apiNote.empty() ? "" : " - ", apiNote.c_str());
    }
    std::printf("[falcon] OpenGL %s on %s\n", reinterpret_cast<const char*>(gl::GetString(gl::VERSION)),
                reinterpret_cast<const char*>(gl::GetString(gl::RENDERER)));

    {
        int ww = 0;
        int wh = 0;
        int fw = 0;
        int fh = 0;
        window.windowSize(ww, wh);
        window.framebufferSize(fw, fh);
        std::printf("[falcon] window %dx%d, framebuffer %dx%d\n", ww, wh, fw, fh);
    }

    // ---- turn the loaded data into GL objects and screens ---------------------------------
    LoadedLanguage loadedLanguage = languageLoad.get();
    if (!loadedLanguage.warning.empty()) {
        std::printf("[falcon] language %s: %s; using English\n", gameOptions.language.c_str(), loadedLanguage.warning.c_str());
        gameOptions.language = loadedLanguage.code;
    }
    Language language = std::move(loadedLanguage.language);
    memStage("language");
    Font font = Font::fromData(fontLoad.get());
    memStage("font");
    for (const std::string& w : font.warnings()) std::printf("[falcon] font: %s\n", w.c_str());
    std::printf("[falcon] loaded %zu language strings\n", language.size());

    TextureManager textures(resources);
    if (!options_.keepJavaEdition) {
        const std::optional<Image> badge = badgeLoad.get();
        if (badge) textures.add(kEditionTexturePath, *badge);
        std::printf("[falcon] title badge: %s\n",
                    badge ? "C++ EDITION" : "kept the original (unexpected edition.png layout)");
    }
    memStage("textures + badge");
    SpriteManager sprites(resources);
    memStage("sprites");
    GuiRenderer guiRenderer;
    memStage("gui renderer");
    PanoramaRenderer panoramaRenderer([&faceLoad](unsigned face) -> Image {
        auto image = faceLoad[face].get();
        if (!image) throw std::runtime_error("missing or broken panorama face " + PanoramaRenderer::faceLocation(face).toString());
        return std::move(*image);
    });
    memStage("panorama renderer");
    GuiGraphics graphics(guiRenderer, sprites, textures, font);
    Panorama panorama(panoramaRenderer);
    SplashManager splashes = splashLoad.get();
    memStage("splashes");

    // ---- title screen ---------------------------------------------------------------------
    std::mt19937 rng(options_.seed ? *options_.seed : std::random_device{}());
    const std::time_t nowTime = std::time(nullptr);
    const std::tm* today = std::localtime(&nowTime);

    TitleScreenOptions titleOptions;
    titleOptions.splash = splashes.pick(today->tm_mon + 1, today->tm_mday, rng);
    titleOptions.fadeIn = true;
    titleOptions.easterEggLogo = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 1.0e-4f;
    titleOptions.versionText = std::string("Minecraft ") + kGameVersion;

    // ---- sound -----------------------------------------------------------------------------
    // Captures are silent by default so automated runs never play through your speakers. The output
    // device is opened on a background thread (the saved device, else the system default), so
    // start-up never waits for the OS audio stack; sounds are no-ops until it is ready.
    const bool silent = options_.mute || (options_.capturePath && !options_.forceAudio);
    AudioEngine audio(silent ? AudioEngine::Mode::Offline : AudioEngine::Mode::Deferred);
    if (silent) {
        std::printf("[falcon] audio: muted\n");
    } else {
        audio.selectDevice(gameOptions.audioDevice);
        audio.refreshDevices();  // so the Device button already knows the outputs when it is opened
    }
    memStage("audio (opening)");
    const unsigned soundSeed = options_.seed ? *options_.seed : std::random_device{}();
    SoundSystem sounds(resources, audio, gameOptions, soundSeed, soundsLoad.get());
    MusicManager music(sounds, gameOptions, soundSeed + 1);
    std::printf("[falcon] sounds.json: %zu events\n", sounds.registry().size());
    memStage("sounds.json");

    ClientNavigator navigator(window, [&sounds] { sounds.playButtonClick(); });

    // "Now playing" toast: shown when a menu track starts, if the Music Toast option allows it.
    if (options_.musicToast) gameOptions.musicToast = MusicToast::PauseAndToast;
    NowPlayingToast nowPlayingToast;
    double nowMillis = 0.0;  // the loop's clock, for callbacks that fire inside music.update()
    music.onTrackStarted = [&](const std::string& track) {
        if (gameOptions.musicToast != MusicToast::PauseAndToast) return;
        std::string key = track;
        std::replace(key.begin(), key.end(), '/', '.');  // music/game/ebb -> music.game.ebb
        nowPlayingToast.show(language.get(key), nowMillis);
    };
    // The resource packs the Resource Packs screen can list: the built-ins plus whatever is in
    // <.minecraft>/resourcepacks.
    PackRepository packs = PackRepository::load(language, search.location->root, gameDir);
    std::printf("[falcon] resource packs: %zu available\n", packs.packs().size());
    memStage("resource pack list");

    // Applies a new pack selection in the background; update() below swaps it in between frames.
    ResourceReloader reloader(ReloadTargets{resources, packs, language, font, textures, sprites,
                                            panoramaRenderer, splashes, sounds, !options_.keepJavaEdition,
                                            gameOptions.language});
    int framebufferNowW = options_.width;  // the window's current size, for the GUI Scale button's range
    int framebufferNowH = options_.height;
    ScreenServices services{language, font, panorama, gameOptions, sounds, music, packs, {}, gameDir, {}, {},
                            loadLanguageList(resources), nullptr, {}, {}, "Player", {}};
    if (options_.username && !options_.username->empty()) services.playerName = *options_.username;
    if (options_.uuid) services.playerUuid = *options_.uuid;
    services.maxGuiScale = [&] { return calculateGuiScale(framebufferNowW, framebufferNowH, /*requested=*/0, false); };
    services.videoModes = [&window] { return window.videoModes(); };
    GameUiContext gameUi(services);  // what the UI library needs from the game
    navigator.setUi(gameUi);
    services.packsChanged = [&] { reloader.request(gameOptions.resourcePacks, gameOptions.language); };
    if (!gameOptions.resourcePacks.empty()) reloader.request(gameOptions.resourcePacks, gameOptions.language);  // the saved selection
    // The world being played (null in the menus). Declared before the screens: they hold a pointer to it, so they
    // must be destroyed first. The renderer inside owns GL objects, so it also has to go before the window.
    std::unique_ptr<GameSession> session;
    std::optional<std::string> worldToStart = options_.play;
    bool quitWorldRequested = false;
    std::vector<std::string> pendingEdits = options_.edits;
    std::vector<std::string> pendingGives = options_.give;
    std::optional<std::array<float, 4>> pendingVitals = options_.vitals;
    std::optional<double> pendingDropFrom = options_.dropFrom;
    navigator.setHoldShift(options_.holdShift);
    bool leftWorld = false;  // a world was played and then quit (a --capture run then no longer waits for one)
    services.startWorld = [&worldToStart](const std::string& folder) { worldToStart = folder; };
    services.quitWorld = [&quitWorldRequested] { quitWorldRequested = true; };
    std::vector<std::unique_ptr<Screen>> screens;  // [0] is the main menu; others open on top
    screens.push_back(std::make_unique<TitleScreen>(navigator, services, titleOptions));

    // ---- input ----------------------------------------------------------------------------
    int guiScale = 1;
    int guiWidth = 0;
    int guiHeight = 0;
    std::optional<std::pair<double, double>> fakeMouse = options_.mouse;  // debug override
    const auto guiMouse = [&](double& x, double& y) {
        double cx = 0.0;
        double cy = 0.0;
        int ww = 1;
        int wh = 1;
        window.cursorPosition(cx, cy);
        if (fakeMouse) {
            cx = fakeMouse->first;
            cy = fakeMouse->second;
        }
        window.windowSize(ww, wh);
        x = cx * static_cast<double>(guiWidth) / static_cast<double>(std::max(ww, 1));
        y = cy * static_cast<double>(guiHeight) / static_cast<double>(std::max(wh, 1));
    };
    bool leftDown = false;  // drives slider dragging
    window.onMouseButton = [&](int button, int action) {
        constexpr int kRelease = 0;  // GLFW_RELEASE
        constexpr int kPress = 1;    // GLFW_PRESS
        double x = 0.0;
        double y = 0.0;
        guiMouse(x, y);
        if (action == kPress) {
            if (button == 0) leftDown = true;
            screens.back()->mouseClicked(x, y, button);
        } else if (action == kRelease) {
            if (button == 0) leftDown = false;
            screens.back()->mouseReleased(x, y, button);
        }
    };
    // F2: a screenshot is taken at the end of the frame; a line at the bottom left says where it went (there is no chat yet).
    bool screenshotRequested = false;
    std::string overlayMessage;
    double overlayUntilMillis = 0.0;
    window.onKey = [&](int key, int action, int mods) {
        constexpr int kEscape = 256;  // GLFW_KEY_ESCAPE
        constexpr int kPress = 1;     // GLFW_PRESS
        constexpr int kRepeat = 2;    // GLFW_REPEAT
        if (action == 0) {            // GLFW_RELEASE: the top screen hears it (the game mode switcher acts when F3 is let go), and so does the world
            screens.back()->keyReleased(key, mods);
            if (session != nullptr) session->onKeyReleased(key);
            return;
        }
        if (action != kPress && action != kRepeat) return;
        // Keys that work on every screen, through whatever key they are bound to (a screen that is waiting for a key to
        // bind gets the key itself, so F11 can be rebound).
        if (action == kPress && !screens.back()->wantsAllKeys()) {
            const auto bound = [&](const char* name) {
                const std::size_t index = keyMappingIndex(name);
                return index < kKeyMappingCount && gameOptions.keyBindings.key(index) == KeyId::keyboard(key);
            };
            if (bound("key.fullscreen")) {
                gameOptions.fullscreen = !window.isFullscreen();  // the main loop applies it
                saveOptions();
                return;
            }
            if (bound("key.screenshot")) {
                screenshotRequested = true;  // taken once the frame is drawn
                return;
            }
            if (bound("key.toggleGui")) {
                if (session != nullptr) session->toggleHud();
                return;
            }
        }
        if (key == kEscape) {
            // Escape leaves the screen (Done does the same); the main menu ignores it. A screen that does
            // not close on Escape gets the key itself (Key Binds uses it to unbind the waiting action).
            if (action == kPress) {
                if (screens.back()->shouldCloseOnEsc()) {
                    screens.back()->onClose();
                } else {
                    screens.back()->keyPressed(key, mods);
                }
            }
            return;
        }
        screens.back()->keyPressed(key, mods);
    };
    window.onChar = [&](unsigned int codePoint) { screens.back()->charTyped(codePoint); };
    window.onScroll = [&](double, double yOffset) {
        double x = 0.0;
        double y = 0.0;
        guiMouse(x, y);
        screens.back()->mouseScrolled(x, y, yOffset);
    };

    // ---- main loop ------------------------------------------------------------------------
    std::vector<ScriptedClick> scriptedClicks = options_.clicks;
    std::vector<ScriptedMouse> scriptedMouse = options_.mouseEvents;
    std::vector<ScriptedScroll> scriptedScrolls = options_.scrolls;
    std::vector<ScriptedText> scriptedText = options_.typed;
    std::vector<ScriptedKey> scriptedKeys = options_.keys;
    std::vector<ScriptedPacks> scriptedPacks = options_.packs;
    std::vector<std::pair<int, double>> scriptedDistances = options_.renderDistances;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    bool lastVsync = true;
    int lastGuiScaleOption = gameOptions.guiScale;
    int lastFbW = -1;
    int lastFbH = -1;
    bool memFirstFrame = false;
    bool windowShown = false;       // the window opens with the first finished frame
    bool trimAfterShow = false;     // hand back start-up memory once the window is up
    bool audioReported = false;     // the output device opens in the background: say when it is ready
    bool memSteady = false;
    FrameQueue frameQueue;          // keeps an uncapped window from queueing frames far ahead of the graphics card

    while (!window.shouldClose()) {
        window.pollEvents();

        const auto now = Clock::now();
        const double millis = std::chrono::duration<double, std::milli>(now - start).count();
        const auto deltaTicks = static_cast<float>(
            std::chrono::duration<double, std::milli>(now - last).count() / 50.0);
        last = now;

        nowMillis = millis;
        music.update(deltaTicks);
        if (!silent && !audioReported && (audio.available() || !audio.error().empty())) {
            audioReported = true;
            if (audio.available()) {
                std::printf("[falcon] audio: '%s' at %d Hz (ready %.0f ms after launch)\n", audio.deviceName().c_str(),
                            audio.sampleRate(), millisSinceProcessStart());
            } else {
                std::printf("[falcon] audio: unavailable (%s); continuing without sound\n", audio.error().c_str());
            }
        }

        int fbW = 0;
        int fbH = 0;
        window.framebufferSize(fbW, fbH);
        if (fbW <= 0 || fbH <= 0) {  // minimised
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        framebufferNowW = fbW;
        framebufferNowH = fbH;
        // Display options act on the window as soon as they change.
        if (gameOptions.enableVsync != lastVsync) {
            lastVsync = gameOptions.enableVsync;
            window.setVsync(lastVsync);
        }
        if (gameOptions.fullscreen != window.isFullscreen()) window.setFullscreen(gameOptions.fullscreen);
        const bool guiScaleChanged = gameOptions.guiScale != lastGuiScaleOption;
        lastGuiScaleOption = gameOptions.guiScale;
        const bool windowResized = fbW != lastFbW || fbH != lastFbH || guiScaleChanged;
        if (windowResized) {
            guiScale = calculateGuiScale(fbW, fbH, gameOptions.guiScale, /*forceUnicode=*/false);
            guiWidth = scaledSize(fbW, guiScale);
            guiHeight = scaledSize(fbH, guiScale);
            lastFbW = fbW;
            lastFbH = fbH;
        }
        // Resource packs that finished loading in the background go live here, between frames.
        // Screens are rebuilt so they pick up new text, and memory the old resources used is returned.
        const bool resourcesReloaded = reloader.update();
        if (resourcesReloaded) trimWorkingSet();

        // Leaving a world: close every screen above the title, then let the world go (its GL objects with it).
        if (quitWorldRequested) {
            quitWorldRequested = false;
            leftWorld = true;
            while (screens.size() > 1) screens.pop_back();
            services.session = nullptr;
            session.reset();
            music.setSituation(MusicManager::Situation::Menu);
            window.setCursorCaptured(false);
            trimWorkingSet();
            screens.back()->resize(guiWidth, guiHeight);
        }
        // Opening a world (Play, or --play): the session starts loading and the in-game screen goes on top.
        if (worldToStart) {
            const std::string folder = *worldToStart;
            worldToStart.reset();
            auto levelInfo = readLevelInfo(gameDir / "saves" / pathFromUtf8(folder));
            if (levelInfo && options_.gameType) levelInfo->gameType = *options_.gameType;  // --gamemode
            if (!levelInfo) {
                std::printf("[falcon] cannot open world '%s': no readable level.dat\n", folder.c_str());
            } else if (session == nullptr) {
                try {
                    session = std::make_unique<GameSession>(services, resources, window, *levelInfo);
                    services.session = session.get();
                    session->setSavingEnabled(!options_.capturePath && !options_.benchmark);  // (a test run leaves the world as it found it)
                    music.setSituation(levelInfo->gameType == 1 ? MusicManager::Situation::Creative : MusicManager::Situation::Survival);
                    if (options_.camera) {
                        const auto& c = *options_.camera;
                        session->teleport(c[0], c[1], c[2], static_cast<float>(c[3]), static_cast<float>(c[4]));
                    }
                    if (options_.benchmark) session->startBenchmark(*options_.benchmark);
                    if (options_.holdLoading) session->setMinLoadSeconds(*options_.holdLoading);
                    screens.push_back(std::make_unique<InGameScreen>(navigator, services));
                    screens.back()->resize(guiWidth, guiHeight);
                    std::printf("[falcon] playing world '%s' (%s, seed %lld)\n", levelInfo->name.c_str(),
                                levelInfo->style == TerrainStyle::Flat ? "flat" : "hills", static_cast<long long>(levelInfo->seed));
                } catch (const std::exception& e) {
                    std::printf("[falcon] could not start the world: %s\n", e.what());
                    services.session = nullptr;
                    session.reset();
                    // (Not only on the console: someone who started Falcon from the launcher has none.)
                    std::string why = e.what();
                    if (!why.empty() && why[0] >= 'a' && why[0] <= 'z') why[0] = static_cast<char>(why[0] - 'a' + 'A');
                    navigator.push(std::make_unique<ConfirmScreen>(navigator, "Could not open the world", why, language.get("gui.done"),
                                                                    language.get("gui.cancel"), [](bool) {}));
                }
            }
        }
        // The mouse belongs to the game while a world is open and no menu is: it steers. Losing the keyboard focus
        // opens the pause menu, so a game running next to this one is never fought over.
        const bool steering = session != nullptr && dynamic_cast<InGameScreen*>(screens.back().get()) != nullptr;
        if (steering && !window.background() && !window.focused()) screens.back()->keyPressed(256, 0);

        // Screen changes requested by buttons / Escape; a new top screen is laid out first.
        const bool screenChanged = navigator.apply(screens);
        if (screenChanged) saveOptions();  // leaving a settings screen saves the settings
        panorama.setSpeed(static_cast<float>(gameOptions.panoramaSpeed));
        if (windowResized || screenChanged || resourcesReloaded) screens.back()->resize(guiWidth, guiHeight);

        for (auto& d : scriptedDistances) {  // --render-distance
            if (d.second >= 0.0 && millis >= d.second * 1000.0) {
                d.second = -1.0;
                gameOptions.renderDistance = d.first;
            }
        }
        for (ScriptedPacks& selection : scriptedPacks) {  // --packs: like closing the packs screen
            if (selection.seconds >= 0.0 && millis >= selection.seconds * 1000.0) {
                selection.seconds = -1.0;
                gameOptions.resourcePacks = selection.ids;
                reloader.request(gameOptions.resourcePacks, gameOptions.language);
            }
        }

        // Scripted clicks go through the same handler a real GLFW mouse press does.
        for (ScriptedClick& click : scriptedClicks) {
            if (click.seconds >= 0.0 && millis >= click.seconds * 1000.0) {
                click.seconds = -1.0;  // fired
                fakeMouse = std::make_pair(click.x, click.y);
                window.onMouseButton(0, 1);
            }
        }
        for (ScriptedMouse& e : scriptedMouse) {  // --press / --release / --move
            if (e.seconds >= 0.0 && millis >= e.seconds * 1000.0) {
                e.seconds = -1.0;
                fakeMouse = std::make_pair(e.x, e.y);
                if (e.button >= 0) {
                    window.setSimulatedButton(e.button, e.press);
                    window.onMouseButton(e.button, e.press ? 1 : 0);
                }
            }
        }
        for (ScriptedScroll& s : scriptedScrolls) {
            if (s.seconds >= 0.0 && millis >= s.seconds * 1000.0) {
                s.seconds = -1.0;
                fakeMouse = std::make_pair(s.x, s.y);
                window.onScroll(0.0, s.dy);
            }
        }
        for (ScriptedText& t : scriptedText) {
            if (t.seconds >= 0.0 && millis >= t.seconds * 1000.0) {
                t.seconds = -1.0;
                for (const char32_t cp : decodeUtf8(t.text)) window.onChar(static_cast<unsigned int>(cp));
            }
        }
        for (ScriptedKey& k : scriptedKeys) {
            if (k.seconds >= 0.0 && millis >= k.seconds * 1000.0) {
                k.seconds = -1.0;
                if (k.press) {
                    window.setSimulatedKey(k.key, !k.release);  // (a held key stays down until its release)
                    window.onKey(k.key, 1, 0);
                }
                if (k.release) {
                    window.setSimulatedKey(k.key, false);
                    window.onKey(k.key, 0, 0);
                }
            }
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        guiMouse(mouseX, mouseY);
        if (leftDown) screens.back()->mouseDragged(mouseX, mouseY);

        // The world first (3D, with depth), then everything else on top of it as 2D.
        window.setCursorCaptured(steering);
        if (session != nullptr) {
            // The inventory leaves the world running behind it (singleplayer does not pause for it): only the player cannot steer.
            const bool keepTicking = !steering && screens.back()->keepsWorldTicking();
            session->update(static_cast<double>(deltaTicks) * 0.05, steering, fbW, fbH, keepTicking);
            if (session->consumeDeathRequest()) navigator.push(std::make_unique<DeathScreen>(navigator, services));  // "You Died!"
        }
        if (session != nullptr && session->ready() && !pendingGives.empty()) {  // --give
            for (const std::string& spec : pendingGives) {
                if (!session->giveItem(spec)) std::printf("[falcon] --give: no item called '%s'\n", spec.c_str());
            }
            pendingGives.clear();
        }
        if (session != nullptr && session->ready() && pendingDropFrom) {  // --drop-from
            Player& p = session->player();
            p.setPosition(p.x, p.y + *pendingDropFrom, p.z);
            p.fallDistance = 0.0;
            pendingDropFrom.reset();
        }
        if (session != nullptr && session->ready() && pendingVitals) {  // --vitals
            Vitals vitals;
            vitals.health = (*pendingVitals)[0];
            vitals.food = static_cast<int>((*pendingVitals)[1]);
            vitals.level = static_cast<int>((*pendingVitals)[2]);
            vitals.progress = (*pendingVitals)[3];
            session->setVitals(vitals);
            pendingVitals.reset();
        }
        if (session != nullptr && session->ready() && !pendingEdits.empty()) {  // --edit
            for (const std::string& edit : pendingEdits) {
                int x = 0, y = 0, z = 0;
                char name[64] = {};
                if (std::sscanf(edit.c_str(), "%d,%d,%d,%63s", &x, &y, &z, name) == 4) {
                    if (const auto block = blockRegistry().parse(name)) session->setBlock(x, y, z, *block);
                }
            }
            pendingEdits.clear();
        }
        gl::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
        if (session != nullptr) session->renderWorld(fbW, fbH);
        guiRenderer.begin(fbW, fbH, guiScale);
        graphics.beginFrame(guiWidth, guiHeight, fbW, fbH);
        screens.back()->render(graphics, static_cast<int>(std::floor(mouseX)),
                               static_cast<int>(std::floor(mouseY)),
                               FrameTime{millis, deltaTicks});
        nowPlayingToast.render(graphics, millis);  // toasts draw above every screen
        if (!overlayMessage.empty() && millis < overlayUntilMillis) {  // the screenshot message (a stand-in for the chat line)
            const int w = graphics.textWidth(overlayMessage);
            const double left = overlayUntilMillis - millis;
            const std::uint32_t fade = left < 1000.0 ? static_cast<std::uint32_t>(left / 1000.0 * 255.0) : 255u;
            graphics.fill(1, guiHeight - 49, 2 + w + 1, guiHeight - 38, (fade * 128u / 255u) << 24);
            graphics.text(overlayMessage, 2.0f, static_cast<float>(guiHeight - 47), (fade << 24) | 0xFFFFFFu);
        }
        guiRenderer.end();
        if (screenshotRequested) {
            screenshotRequested = false;
            const std::filesystem::path folder = gameDir / "screenshots";
            std::error_code ignoredError;
            std::filesystem::create_directories(folder, ignoredError);
            const std::time_t stamp = std::time(nullptr);
            char name[48] = {};
            std::strftime(name, sizeof(name), "%Y-%m-%d_%H.%M.%S", std::localtime(&stamp));  // the game's own file names
            std::filesystem::path file = folder / (std::string(name) + ".png");
            for (int n = 1; std::filesystem::exists(file, ignoredError); ++n) file = folder / (std::string(name) + "_" + std::to_string(n) + ".png");
            const bool saved = saveFrame(file, fbW, fbH);
            std::string pattern = language.get(saved ? "screenshot.success" : "screenshot.failure");
            if (pattern.empty() || pattern == "screenshot.success" || pattern == "screenshot.failure") pattern = saved ? "Saved screenshot as %s" : "Couldn't save screenshot: %s";
            const std::string shown = saved ? file.filename().string() : std::string("could not write the file");
            if (const std::size_t at = pattern.find("%s"); at != std::string::npos) pattern.replace(at, 2, shown);
            overlayMessage = pattern;
            overlayUntilMillis = millis + 5000.0;
            std::printf("[falcon] %s\n", pattern.c_str());
        }
        window.setCursor(graphics.requestedCursor());

        if (!memFirstFrame) {
            // Loading is over: return everything it used up and no longer needs.
            memFirstFrame = true;
            memStage("first frame");
            trimAfterShow = true;
        } else if (!memSteady && millis > 3000.0) {
            memSteady = true;
            memStage("after 3 s");
        }

        const bool worldReadyForCapture = !options_.play || leftWorld || options_.captureLoading ||
                                          (session != nullptr && session->ready() && !options_.benchmark);
        if (options_.capturePath && millis >= options_.captureAfterSeconds * 1000.0 && worldReadyForCapture) {
            if (!silent && audio.available()) {
                std::printf("[falcon] audio summary: %llu sounds started, music playing: %s, peak %.3f\n",
                            static_cast<unsigned long long>(audio.stats().soundsStarted),
                            audio.musicPlaying() ? "yes" : "no", static_cast<double>(audio.takePeak()));
            }
            const bool ok = saveFrame(*options_.capturePath, fbW, fbH);
            std::printf("[falcon] %s %dx%d frame %s %s\n", ok ? "saved" : "FAILED to save", fbW, fbH,
                        ok ? "to" : "at", options_.capturePath->generic_string().c_str());
            saveOptions();
            return ok ? 0 : 3;
        }
        if (session != nullptr) {  // what this frame cost the CPU (before the swap, which may wait for vsync)
            session->noteCpuMillis(std::chrono::duration<double, std::milli>(Clock::now() - now).count());
        }
        window.swapBuffers();
        {   // Without vsync and without a frame cap the CPU would queue frames far ahead of the graphics card: allow two (see FrameQueue).
            static const char* forcedAhead = std::getenv("FALCON_FRAME_AHEAD");  // (for tests: a number of frames, 0 = no limit, even in a hidden window)
            const bool uncapped = !window.background() && window.focused() && !gameOptions.enableVsync && gameOptions.framerateLimit >= 260;
            frameQueue.limit(forcedAhead != nullptr ? std::atoi(forcedAhead) : (uncapped ? 2 : 0));
        }
        if (!windowShown) {  // the first frame is on screen: now the window can appear
            windowShown = true;
            window.show();
        }
        if (trimAfterShow) {
            trimAfterShow = false;
            trimWorkingSet();
            memStage("after trimming");
        }
        // The benchmark: report and leave once the flight is over.
        if (session != nullptr && session->benchmarkDone()) {
            for (const std::string& line : session->benchmarkReport()) std::printf("[falcon] %s\n", line.c_str());
            std::fflush(stdout);
            return 0;
        }
        // A background window's swaps do not wait for vsync: pace it ourselves (~60 fps) rather
        // than spinning a core while someone is busy doing something else. A window that is not the one being
        // used (another program has the focus) is held to 20 fps, so a game running next to it keeps the machine.
        static const bool noPacing = std::getenv("FALCON_NO_PACING") != nullptr;  // (to measure a hidden window running flat out, as an uncapped one does)
        if (window.background()) {
            if (!noPacing) std::this_thread::sleep_for(std::chrono::milliseconds(16));
        } else if (!window.focused()) {
            const auto spent = Clock::now() - now;
            const auto budget = std::chrono::milliseconds(50);
            if (spent < budget) std::this_thread::sleep_for(budget - spent);
        } else if (gameOptions.framerateLimit < 260) {  // Max Framerate (260 means unlimited)
            const auto frameBudget = std::chrono::duration<double, std::milli>(1000.0 / gameOptions.framerateLimit);
            const auto spent = Clock::now() - now;
            if (spent < frameBudget) std::this_thread::sleep_for(frameBudget - spent);
        }
    }
    saveOptions();  // the window was closed: keep whatever was changed
    return 0;
}

}  // namespace falcon
