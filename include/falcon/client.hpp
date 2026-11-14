#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace falcon {

// A left click injected at a window position (pixels) once the client has run for `seconds`.
struct ScriptedClick {
    double x = 0.0;
    double y = 0.0;
    double seconds = 0.0;
};

// Scripted mouse-wheel turn / typed text / key press, injected once the client has run for `seconds`.
struct ScriptedScroll {
    double x = 0.0;
    double y = 0.0;
    double dy = 0.0;  // > 0 scrolls up
    double seconds = 0.0;
};
// A mouse button pressed or released, or the pointer moved, at a window position once the client has run for `seconds`.
struct ScriptedMouse {
    double x = 0.0;
    double y = 0.0;
    int button = 0;       // GLFW button (0 left, 1 right, 2 middle); -1: only move the pointer
    bool press = true;
    double seconds = 0.0;
};
struct ScriptedText {
    std::string text;
    double seconds = 0.0;
};
struct ScriptedKey {
    int key = 0;  // GLFW key code
    double seconds = 0.0;
    // --key CODE@SECONDS taps the key (press, then release); CODE@SECONDS+ only presses it and CODE@SECONDS- only lets it go,
    // so a chord like F3 + F4 can be scripted: 292@1+ 293@1.5 292@2-.
    bool press = true;
    bool release = true;
};
// Selects resource packs (ids, lowest priority first) once the client has run for `seconds`,
// exactly as closing the Resource Packs screen with that selection would.
struct ScriptedPacks {
    std::vector<std::string> ids;
    double seconds = 0.0;
};

struct ClientOptions {
    std::optional<std::filesystem::path> assetsDir;  // --assets <dir>
    std::optional<std::string> language;             // --language <code>: start in this language (e.g. de_de)
    std::optional<std::filesystem::path> gameDir;    // --game-dir <dir>: the ".minecraft" folder (resourcepacks/ lives in it)
    std::vector<ScriptedPacks> packs;                // --packs ID,ID@S, for tests
    int width = 854;                                  // --size WxH (the game's default window)
    int height = 480;
    std::optional<std::filesystem::path> capturePath;  // --capture <png>: save a frame and exit
    double captureAfterSeconds = 4.0;                  // --capture-after <s>
    std::optional<unsigned> seed;                      // --seed <n>: fixes the splash text
    std::optional<std::pair<double, double>> mouse;    // --mouse X,Y: fake cursor (window pixels)
    std::vector<ScriptedClick> clicks;                 // --click X,Y@S (repeatable), for tests
    std::vector<ScriptedMouse> mouseEvents;            // --press/--release X,Y,BUTTON@S and --move X,Y@S
    bool holdShift = false;                            // --shift: shift counts as held (for a shift-click)
    std::vector<std::string> give;                     // --give ITEM[:COUNT] (repeatable): put items in the inventory
    std::optional<int> gameType;                       // --gamemode survival|creative: overrides the world's game mode
    std::vector<ScriptedScroll> scrolls;               // --scroll X,Y,DY@S
    std::vector<ScriptedText> typed;                   // --type TEXT@S
    std::vector<ScriptedKey> keys;                     // --key CODE@S (256 = Escape)
    std::vector<std::pair<int, double>> renderDistances;  // --render-distance N@S: set the Render Distance option to N after S seconds (for tests)
    bool keepJavaEdition = false;                      // --java-edition: keep the original badge
    bool audioTest = false;                            // --audio-test: play a click + music, report, exit
    bool auditLighting = false;                        // --audit-lighting: mesh every block, report which are not smooth-lit, exit
    std::optional<std::filesystem::path> compareVanilla;  // --compare-vanilla <world folder>: diff the terrain against the game's own, exit
    bool audioDevices = false;                         // --audio-devices: list outputs, try each silently, exit
    std::optional<std::string> play;                   // --play <folder>: open this world of saves/ at start-up
    std::optional<std::array<double, 5>> camera;       // --cam X,Y,Z,YAW,PITCH: put the player there (and make them fly)
    std::optional<double> benchmark;                   // --bench <s>: fly a fixed path for s seconds, print frame times, exit
    std::optional<double> holdLoading;                 // --hold-loading <s>: keep the "Loading terrain..." screen up at least this long
    bool captureLoading = false;                       // --capture-loading: capture without waiting for the world (the loading screen)
    std::vector<std::string> edits;                    // --edit X,Y,Z,BLOCK (repeatable): change a block once the world is ready
    bool background = false;                           // --background: hidden, unfocused window
    bool memReport = false;                            // --mem-report: print RAM use after each loading step
    bool musicToast = false;                           // --music-toast: show the "now playing" toast
    bool mute = false;                                 // --mute: no sound output
    bool forceAudio = false;                           // --audio: sound output even when capturing
    bool showHelp = false;                             // --help
};

// Parses command line arguments. Throws std::invalid_argument on bad input.
[[nodiscard]] ClientOptions parseOptions(int argc, const char* const* argv);
[[nodiscard]] std::string usageText();

class Client {
public:
    explicit Client(ClientOptions options);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;

    // Opens the window, loads the game assets and runs the main menu until it is closed.
    // Returns a process exit code.
    int run();

private:
    ClientOptions options_;
};

}  // namespace falcon
