#include "falcon/client.hpp"
#include "falcon/util/log.hpp"
#include "client_internal.hpp"

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
#include "falcon/platform/dirs.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/platform/window.hpp"
#include "falcon/resource_reloader.hpp"
#include "falcon/text/font.hpp"
#include "falcon/text/language.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/game/game_options.hpp"
#include "falcon/ui/gui_scale.hpp"
#include "falcon/game/now_playing_toast.hpp"
#include "falcon/game/fps_test.hpp"
#include "falcon/game/game_session.hpp"
#include "falcon/game/game_ui.hpp"
#include "falcon/game/death_screen.hpp"
#include "falcon/game/disconnected_screen.hpp"
#include "falcon/game/join_multiplayer_screen.hpp"
#include "falcon/game/remote_container_screen.hpp"
#include "falcon/gfx/frame_queue.hpp"
#include "falcon/game/in_game_screen.hpp"
#include "falcon/game/mod_screen.hpp"
#include "falcon/game/lighting_audit.hpp"
#include "falcon/game/vanilla_compare.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/util/options_file.hpp"
#include "falcon/util/task_pool.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/new_world.hpp"
#include "falcon/game/edition_badge.hpp"
#include "falcon/game/panorama.hpp"
#include "falcon/game/splash_manager.hpp"
#include "falcon/ui/sprite_manager.hpp"
#include "falcon/game/title_screen.hpp"
#include "falcon/ui/confirm_screen.hpp"
#include "falcon/world/level_info.hpp"
#include "falcon/version.hpp"
#include "falcon/game/mod_content_apply.hpp"
#include "falcon/jvm/mod_runtime.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <typeinfo>
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

// The game's class of a screen, from the C++ class (Itanium names: "N6falcon11TitleScreenE" is falcon::TitleScreen): Falcon's screens carry the game's names, which is what a mod tests the
// screen against (screen instanceof TitleScreen).
std::string minecraftScreenClass(const char* mangled) {
    std::string name;
    const char* p = mangled;
    if (*p == 'N') ++p;
    while (*p >= '0' && *p <= '9') {
        int length = 0;
        while (*p >= '0' && *p <= '9') length = length * 10 + (*p++ - '0');
        name.assign(p, static_cast<std::size_t>(length));
        p += length;
    }
    if (name.empty()) return "net/minecraft/client/gui/screens/Screen";
    static const char* const kOptions[] = {"OptionsScreen", "VideoSettingsScreen", "SoundOptionsScreen", "ControlsScreen", "KeyBindsScreen", "LanguageSelectScreen", "SkinCustomizationScreen", "AccessibilityOptionsScreen", "OnlineOptionsScreen", "ChatOptionsScreen", "MouseSettingsScreen"};
    for (const char* option : kOptions) {
        if (name == option) return "net/minecraft/client/gui/screens/options/" + name;
    }
    if (name == "SelectWorldScreen" || name == "CreateWorldScreen" || name == "EditWorldScreen") return "net/minecraft/client/gui/screens/worldselection/" + name;
    if (name == "JoinMultiplayerScreen" || name == "ConnectScreen") return "net/minecraft/client/gui/screens/multiplayer/" + name;
    if (name == "InventoryScreen" || name == "CreativeModeInventoryScreen" || name == "CraftingScreen" || name == "FurnaceScreen") return "net/minecraft/client/gui/screens/inventory/" + name;
    return "net/minecraft/client/gui/screens/" + name;
}

}  // namespace

namespace {

constexpr const char* kGameVersion = "26.3";

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
    falcon::log::line("assets: %s (%s)\n", search.location->root.generic_string().c_str(),
                search.location->source.c_str());
    if (options_.audioDevices) return runAudioDevices();
    if (options_.audioTest) return runAudioTest(ResourceManager(search.location->root));
    if (options_.auditLighting) {
        const std::filesystem::path auditGameDir = options_.gameDir ? *options_.gameDir : minecraftDir().value_or(".minecraft");
        return runLightingAudit(ResourceManager(search.location->root), search.location->root.parent_path(), auditGameDir / "falcon_cache");
    }

    if (options_.compareVanilla) {
        const std::filesystem::path compareGameDir = options_.gameDir ? *options_.gameDir : minecraftDir().value_or(".minecraft");
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
    const std::filesystem::path gameDir = options_.gameDir ? *options_.gameDir : minecraftDir().value_or(".minecraft");
    // The mods start first, beside everything else: what they add (blocks, items, keys, assets) has to be in the game's tables before the world opens.
    const std::filesystem::path modsDir = options_.modsDir ? *options_.modsDir : options_.gameDir ? gameDir / "mods" : falconDataDir().value_or(std::filesystem::path(".")) / "mods";
    std::future<std::shared_ptr<jvm::ModHost>> modHostStart = std::async(std::launch::async, [modsDir, gameDir] {
        if (std::getenv("FALCON_NO_MODS") != nullptr) return std::shared_ptr<jvm::ModHost>();
        return std::make_shared<jvm::ModHost>(modsDir, gameDir);
    });
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
            falcon::log::line("could not save %s\n", optionsPath.string().c_str());
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
    Window window(options_.width, options_.height, "Falcon", background, /*startHidden=*/true, options_.borderless, options_.graphicsApi.value_or(gameOptions.graphicsApi));
    memStage("window + GL context");
    window.setIconFromExecutable();  // the Falcon icon linked into the exe
    memStage("window icon");
    // The Graphics API setting: what was chosen, and what the game really draws with (only OpenGL can draw it so far).
    {
        const std::string& apiNote = window.graphicsApiNote();
        falcon::log::line("graphics API: %s%s%s\n", std::string(graphicsApiId(window.graphicsApi())).c_str(), apiNote.empty() ? "" : " - ", apiNote.c_str());
    }
    falcon::log::line("%s%s on %s\n", window.graphicsApi() == GraphicsApi::Vulkan ? "" : "OpenGL ", reinterpret_cast<const char*>(gl::GetString(gl::VERSION)),
                reinterpret_cast<const char*>(gl::GetString(gl::RENDERER)));

    {
        int ww = 0;
        int wh = 0;
        int fw = 0;
        int fh = 0;
        window.windowSize(ww, wh);
        window.framebufferSize(fw, fh);
        falcon::log::line("window %dx%d, framebuffer %dx%d\n", ww, wh, fw, fh);
        const auto glText = [](gl::GLenum name) {
            const auto* text = reinterpret_cast<const char*>(gl::GetString(name));
            return text != nullptr ? std::string(text) : std::string("unknown");
        };
        falcon::log::info("gpu", "vendor %s; renderer %s; version %s; shading language %s", glText(gl::VENDOR).c_str(), glText(gl::RENDERER).c_str(), glText(gl::VERSION).c_str(),
                          glText(0x8B8C).c_str());  // (GL_SHADING_LANGUAGE_VERSION)
        falcon::log::info("options", "render distance %d, simulation %d, LODs %s (%d chunks, detail %d), vsync %s, max framerate %d, GUI scale %d, language %s", gameOptions.renderDistance,
                          gameOptions.simulationDistance, gameOptions.lodsEnabled ? "on" : "off", gameOptions.lodChunkRadius, gameOptions.lodDetail, gameOptions.enableVsync ? "on" : "off",
                          gameOptions.framerateLimit, gameOptions.guiScale, gameOptions.language.c_str());
    }

    // ---- turn the loaded data into GL objects and screens ---------------------------------
    LoadedLanguage loadedLanguage = languageLoad.get();
    if (!loadedLanguage.warning.empty()) {
        falcon::log::line("language %s: %s; using English\n", gameOptions.language.c_str(), loadedLanguage.warning.c_str());
        gameOptions.language = loadedLanguage.code;
    }
    Language language = std::move(loadedLanguage.language);
    std::shared_ptr<jvm::ModHost> modHost;
    ResourceManager::PackList modPacks;
    try {
        modHost = modHostStart.get();
    } catch (const std::exception& e) {
        falcon::log::line("mods: %s\n", e.what());
    }
    if (modHost && !modHost->mods().empty()) {
        for (const auto& mod : modHost->mods()) {
            const jvm::ModReport& r = mod->report();
            falcon::log::line("mods: %s: %.1f ms, %zu item(s), %zu block(s), %zu key(s), %zu handler(s)%s%s\n", r.modId.c_str(), r.milliseconds, mod->content().items.size(), mod->content().blocks.size(), mod->content().keys.size(),
                              r.events.size(), r.error.empty() ? "" : ", stopped: ", r.error.c_str());
        }
        modPacks = modHost->resourcePacks();
        Language::setModNamespaces(modHost->namespaces());
        resources.setPacks(modPacks);                       // (their assets, under the packs the player selects)
        language.mergeMods(resources, gameOptions.language);  // (their translations)
        registerModKeyMappings(modKeyMappings(modHost.get()));
        gameOptions.keyBindings.load(optionsFile);          // (the keys they asked for, on what the player saved for them)
    }
    memStage("language");
    Font font = Font::fromData(fontLoad.get());
    memStage("font");
    for (const std::string& w : font.warnings()) falcon::log::line("font: %s\n", w.c_str());
    falcon::log::line("loaded %zu language strings\n", language.size());

    TextureManager textures(resources);
    if (!options_.keepJavaEdition) {
        const std::optional<Image> badge = badgeLoad.get();
        if (badge) textures.add(kEditionTexturePath, *badge);
        falcon::log::line("title badge: %s\n",
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
        falcon::log::line("audio: muted\n");
    } else {
        audio.selectDevice(gameOptions.audioDevice);
        audio.refreshDevices();  // so the Device button already knows the outputs when it is opened
    }
    memStage("audio (opening)");
    const unsigned soundSeed = options_.seed ? *options_.seed : std::random_device{}();
    SoundSystem sounds(resources, audio, gameOptions, soundSeed, soundsLoad.get());
    MusicManager music(sounds, gameOptions, soundSeed + 1);
    falcon::log::line("sounds.json: %zu events\n", sounds.registry().size());
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
    // (and in Falcon's own folder, %LOCALAPPDATA%\\Falcon\\resourcepacks, which is made here like the game makes its own at startup so that there is a place to put them)
    std::vector<std::filesystem::path> extraPackFolders;
    if (const auto falconDir = falconDataDir(); falconDir && !options_.gameDir) {
        extraPackFolders.push_back(*falconDir / "resourcepacks");
        std::error_code ignored;
        std::filesystem::create_directories(extraPackFolders.back(), ignored);
    }
    if (!options_.gameDir) {
        std::error_code ignored;
        std::filesystem::create_directories(gameDir / "resourcepacks", ignored);
    }
    PackRepository packs = PackRepository::load(language, search.location->root, gameDir, std::move(extraPackFolders));
    falcon::log::line("resource packs: %zu available\n", packs.packs().size());
    memStage("resource pack list");

    // Applies a new pack selection in the background; update() below swaps it in between frames.
    ResourceReloader reloader(ReloadTargets{resources, packs, language, font, textures, sprites,
                                            panoramaRenderer, splashes, sounds, !options_.keepJavaEdition,
                                            gameOptions.language, modPacks});
    int framebufferNowW = options_.width;  // the window's current size, for the GUI Scale button's range
    int framebufferNowH = options_.height;
    ScreenServices services{language, font, panorama, gameOptions, sounds, music, packs, {}, gameDir, {}, {},
                            loadLanguageList(resources), nullptr, {}, {}, "Player", {}, {}, {}, {}, {}};
    services.modsDir = modsDir;
    services.mods = modHost;
    if (modHost && !modHost->mods().empty()) {
        jvm::HostState hostState;  // (what the mods may ask the client before a world is open: the language and the font)
        hostState.textWidth = [&font](const std::string& text) { return font.width(text); };
        hostState.translate = [&language](const std::string& key) { return language.get(key); };
        modHost->setHost(hostState);
        modHost->clientStarted();  // (ClientLifecycleEvents.CLIENT_STARTED)
        // The buttons the mods put on screens (ScreenEvents.AFTER_INIT): asked for every screen once it has built its own widgets; a press runs the mod's handler in the VM.
        Screen::setPostInitHook([modHost](Screen& screen) {
            const std::string name = minecraftScreenClass(typeid(screen).name());
            for (const jvm::ModHost::HostWidget& w : modHost->screenOpened(name, screen.width(), screen.height())) {
                jvm::ModInstance* mod = w.mod;
                const int id = w.widget.id;
                screen.addExternalButton(w.widget.label, Rect{w.widget.x, w.widget.y, w.widget.width, w.widget.height}, [mod, id] { mod->pressWidget(id); });
            }
        });
    }
    if (options_.username && !options_.username->empty()) services.playerName = *options_.username;
    if (options_.uuid) services.playerUuid = *options_.uuid;
    services.accessToken = options_.accessToken;
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
    std::optional<std::string> serverToJoin = options_.connect;
    bool quitWorldRequested = false;
    std::vector<std::string> pendingEdits = options_.edits;
    std::vector<std::string> pendingGives = options_.give;
    std::optional<std::array<float, 4>> pendingVitals = options_.vitals;
    std::optional<long long> pendingTime = options_.dayTime;
    std::optional<double> pendingDropFrom = options_.dropFrom;
    navigator.setHoldShift(options_.holdShift);
    bool leftWorld = false;  // a world was played and then quit (a --capture run then no longer waits for one)
    services.startWorld = [&worldToStart](const std::string& folder) { worldToStart = folder; };
    services.quitWorld = [&quitWorldRequested] { quitWorldRequested = true; };
    services.joinServer = [&serverToJoin](const std::string& address) { serverToJoin = address; };
    std::optional<std::pair<std::string, std::string>> disconnected;  // a server session that ended: the title (a language key) and the reason, shown once the world is gone
    std::vector<std::unique_ptr<Screen>> screens;  // [0] is the main menu; others open on top
    screens.push_back(std::make_unique<TitleScreen>(navigator, services, titleOptions));
    // What a crash report says the client was doing (asked when the program crashes, so they only read): the screen, the window and the world. The guard takes them out again before what
    // they look at is gone.
    struct ContextGuard {
        ~ContextGuard() {
            for (const char* name : {"screen", "window", "options", "world"}) falcon::log::removeContext(name);
        }
    } contextGuard;
    falcon::log::addContext("screen", [&screens] { return screens.empty() ? std::string("(none)") : falcon::log::typeName(typeid(*screens.back())); });
    falcon::log::addContext("window", [&window] {
        int w = 0, h = 0;
        window.framebufferSize(w, h);
        return std::to_string(w) + "x" + std::to_string(h) + " framebuffer, " + std::string(graphicsApiId(window.graphicsApi())) + (window.focused() ? ", focused" : ", not focused");
    });
    falcon::log::addContext("options", [&gameOptions] {
        return "render distance " + std::to_string(gameOptions.renderDistance) + ", LODs " + (gameOptions.lodsEnabled ? std::to_string(gameOptions.lodChunkRadius) + " chunks" : std::string("off")) +
               ", language " + gameOptions.language;
    });
    falcon::log::addContext("world", [&session] { return session ? session->describe() : std::string("no world is open"); });

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
        // The Highest FPS Test's label is the corner of the screen it draws itself in: a click there stops the test and puts every option back (nothing in the world is pressed).
        if (action == kPress && button == 0 && falcon::fps_test::running() && x >= 2.0 && x < 192.0 && y >= 2.0 && y < 34.0) {
            falcon::fps_test::stop(gameOptions);
            return;
        }
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
        // bind gets the key itself, so F11 can be rebound; the chat box gets every key but these two).
        if (action == kPress && !screens.back()->bindingKey()) {
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
            if (bound("key.toggleGui") && !screens.back()->wantsAllKeys()) {
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
        const double frameMillis = deltaTicks * 50.0;
        last = now;
        // A frame that took more than a quarter of a second while a world is open (not one that waited for the window to come back): the log says what the game was doing.
        if (session != nullptr && session->ready() && frameMillis > 250.0 && frameMillis < 5000.0 && !window.background()) {
            static double lastHitch = -1.0e9;
            if (millis - lastHitch > 2000.0) {
                lastHitch = millis;
                falcon::log::warn("perf", "a frame took %.0f ms (%s)", frameMillis, session->describe().c_str());
            }
        }

        nowMillis = millis;
        // (A test aid for the crash report: FALCON_TEST_CRASH=null|abort|throw|thread makes the client fail four seconds after it starts, the way the kind says.)
        if (static const char* const crashKind = std::getenv("FALCON_TEST_CRASH"); crashKind != nullptr && millis > 4000.0) {
            falcon::log::warn("test", "FALCON_TEST_CRASH=%s: failing now", crashKind);
            const std::string kind = crashKind;
            if (kind == "null") *static_cast<volatile int*>(nullptr) = 1;
            if (kind == "abort") std::abort();
            if (kind == "throw") throw std::runtime_error("a test error (FALCON_TEST_CRASH=throw)");
            if (kind == "thread") std::thread([] { throw std::logic_error("a test exception on a worker thread"); }).join();
        }
        music.update(deltaTicks);
        if (!silent && !audioReported && (audio.available() || !audio.error().empty())) {
            audioReported = true;
            if (audio.available()) {
                falcon::log::line("audio: '%s' at %d Hz (ready %.0f ms after launch)\n", audio.deviceName().c_str(),
                            audio.sampleRate(), millisSinceProcessStart());
            } else {
                falcon::log::line("audio: unavailable (%s); continuing without sound\n", audio.error().c_str());
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
            if (disconnected) {  // back to the server list, with the reason on top
                screens.push_back(std::make_unique<JoinMultiplayerScreen>(navigator, services));
                screens.back()->resize(guiWidth, guiHeight);
                screens.push_back(std::make_unique<DisconnectedScreen>(navigator, services, disconnected->first, disconnected->second));
                disconnected.reset();
            }
            screens.back()->resize(guiWidth, guiHeight);
        }
        // Joining a server (--connect, or Join Server in the list): a session that plays on a server.
        if (serverToJoin && session == nullptr) {
            std::string address = *serverToJoin;
            serverToJoin.reset();
            int port = 25565;
            if (const auto colon = address.rfind(':'); colon != std::string::npos) {
                port = std::atoi(address.c_str() + colon + 1);
                address.erase(colon);
            }
            LevelInfo info;
            info.name = address;
            info.style = TerrainStyle::Flat;
            info.remote = true;
            try {
                session = std::make_unique<GameSession>(services, resources, window, info);
                std::string error;
                if (!session->connectRemote(address, port, options_.username.value_or(services.playerName), error)) {
                    falcon::log::line("cannot join %s:%d: %s\n", address.c_str(), port, error.c_str());
                    session.reset();
                    leftWorld = true;  // (a --capture run does not wait for a world that is not coming)
                    screens.push_back(std::make_unique<DisconnectedScreen>(navigator, services, "connect.failed", error));
                    screens.back()->resize(guiWidth, guiHeight);
                } else {
                    services.session = session.get();
                    session->setSavingEnabled(false);
                    screens.push_back(std::make_unique<InGameScreen>(navigator, services));
                    screens.back()->resize(guiWidth, guiHeight);
                }
            } catch (const std::exception& e) {
                falcon::log::line("could not start the session: %s\n", e.what());
                session.reset();
            }
        }
        // Opening a world (Play, or --play): the session starts loading and the in-game screen goes on top.
        // A screen that asked to be closed and to start a world at once (Create New World) has a pop waiting: it is done before the world's screen goes on, or it would take that one off.
        bool appliedEarly = false;
        if (worldToStart) {
            const std::string folder = *worldToStart;
            worldToStart.reset();
            appliedEarly = navigator.apply(screens);
            if (options_.newWorld && !std::filesystem::exists(gameDir / "saves" / pathFromUtf8(folder) / "level.dat")) {
                // A scripted run (--capture) has no menu to make a world in, so it makes one here, the same way Create New World does.
                NewWorldSettings settings;
                settings.name = folder;
                std::string error;
                const std::int64_t madeAtMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                if (!createWorldFolder(gameDir / "saves", folder, settings, 0, madeAtMillis, error)) {
                    falcon::log::line("could not create the world '%s': %s\n", folder.c_str(), error.c_str());
                } else {
                    falcon::log::line("made the world '%s' for this run\n", folder.c_str());
                }
            }
            auto levelInfo = readLevelInfo(gameDir / "saves" / pathFromUtf8(folder));
            if (levelInfo && options_.gameType) levelInfo->gameType = *options_.gameType;  // --gamemode
            if (levelInfo && options_.cheats) levelInfo->allowCommands = true;               // --cheats
            if (!levelInfo) {
                falcon::log::line("cannot open world '%s': no readable level.dat\n", folder.c_str());
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
                    falcon::log::line("playing world '%s' (%s, seed %lld)\n", levelInfo->name.c_str(),
                                levelInfo->style == TerrainStyle::Flat ? "flat" : "hills", static_cast<long long>(levelInfo->seed));
                } catch (const std::exception& e) {
                    falcon::log::line("could not start the world: %s\n", e.what());
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
        const bool screenChanged = navigator.apply(screens) || appliedEarly;
        if (screenChanged) {
            saveOptions();  // leaving a settings screen saves the settings
            falcon::log::debug("ui", "screen: %s (%d open)", falcon::log::typeName(typeid(*screens.back())).c_str(), static_cast<int>(screens.size()));
        }
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
        if (services.mods && services.mods->hasActiveScreen() && dynamic_cast<ModScreen*>(screens.back().get()) == nullptr) {
            navigator.push(std::make_unique<ModScreen>(navigator, services));
        }
        const bool activeSteering = session != nullptr && dynamic_cast<InGameScreen*>(screens.back().get()) != nullptr;
        window.setCursorCaptured(activeSteering);
        // How the frame is spent, for the Developer page of the video settings: the world's update, its drawing, the HUD and the menus; whatever is left of the frame
        // is the display (vsync) or the frame rate cap.
        FramePhaseTimes framePhases{};
        auto phaseMark = Clock::now();
        const auto markPhase = [&framePhases, &phaseMark](FramePhase phase) {
            const auto at = Clock::now();
            framePhases[static_cast<std::size_t>(phase)] += std::chrono::duration<double, std::milli>(at - phaseMark).count();
            phaseMark = at;
        };
        if (session != nullptr) {
            // The inventory leaves the world running behind it (singleplayer does not pause for it): only the player cannot steer.
            const bool keepTicking = !activeSteering && screens.back()->keepsWorldTicking();
            session->update(static_cast<double>(deltaTicks) * 0.05, activeSteering, fbW, fbH, keepTicking);
            if (session->consumeDeathRequest()) navigator.push(std::make_unique<DeathScreen>(navigator, services));  // "You Died!"
            if (session->consumeContainerOpen() && session->remoteMenu() != nullptr) {  // a chest, a furnace ... that the server opened
                auto containerScreen = std::make_unique<RemoteContainerScreen>(navigator, services, *session->remoteMenu(), session->remoteMenuTitle());
                containerScreen->enchantmentName = [&session](int id, int level) { return session ? session->enchantmentName(id, level) : std::string(); };
                screens.push_back(std::move(containerScreen));
                screens.back()->resize(guiWidth, guiHeight);
            }
            if (session->consumeContainerClose() && dynamic_cast<RemoteContainerScreen*>(screens.back().get()) != nullptr) navigator.pop();  // the server closed it
            if (session->isRemote() && !session->remoteLost().empty() && !quitWorldRequested) {  // the server is gone (or never let the player in)
                disconnected = std::make_pair(std::string(session->ready() ? "disconnect.lost" : "connect.failed"), session->remoteLost());
                quitWorldRequested = true;
            }
        }
        markPhase(kFrameUpdate);
        if (session != nullptr && session->ready() && !pendingGives.empty()) {  // --give
            for (const std::string& spec : pendingGives) {
                if (!session->giveItem(spec)) falcon::log::line("--give: no item called '%s'\n", spec.c_str());
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
        if (session != nullptr && session->ready() && pendingTime) {  // --time
            session->setDayTime(*pendingTime, true);
            pendingTime.reset();
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
        markPhase(kFrameDraw);
        guiRenderer.begin(fbW, fbH, guiScale);
        graphics.beginFrame(guiWidth, guiHeight, fbW, fbH);
        if (gameOptions.vignette) {  // the game's vignette: the edges of the screen darkened a little more than the middle (Video Settings > Other)
            constexpr int steps = 20;
            for (int i = 0; i < steps; ++i) {
                const double fade = 1.0 - static_cast<double>(i) / steps;
                const auto alpha = static_cast<std::uint32_t>(std::lround(70.0 * fade * fade));
                if (alpha == 0) continue;
                const std::uint32_t colour = alpha << 24;
                const int up = i * guiHeight / (2 * steps), side = i * guiWidth / (2 * steps);
                graphics.fill(0, up, guiWidth, up + 1, colour);
                graphics.fill(0, guiHeight - up - 1, guiWidth, guiHeight - up, colour);
                graphics.fill(side, 0, side + 1, guiHeight, colour);
                graphics.fill(guiWidth - side - 1, 0, guiWidth - side, guiHeight, colour);
            }
        }
        screens.back()->render(graphics, static_cast<int>(std::floor(mouseX)),
                               static_cast<int>(std::floor(mouseY)),
                               FrameTime{millis, deltaTicks});
        nowPlayingToast.render(graphics, millis);  // toasts draw above every screen
        if (falcon::fps_test::running()) {  // the Highest FPS Test's frame rate, in the corner (it stands in for the F3 screen, which costs frames)
            char rate[96];
            std::snprintf(rate, sizeof(rate), "Highest FPS Test  %.0f fps (%.2f ms)", falcon::fps_test::fps(), falcon::fps_test::millis());
            graphics.fill(2, 2, 192, 34, 0xB4000000u);
            graphics.text(rate, 6.0f, 6.0f, 0xFFFFFFFFu);
            graphics.text("click here to stop and restore", 6.0f, 19.0f, 0xFFA8A8A8u);
        }
        if (!overlayMessage.empty() && millis < overlayUntilMillis) {  // the screenshot message (a stand-in for the chat line)
            const int w = graphics.textWidth(overlayMessage);
            const double left = overlayUntilMillis - millis;
            const std::uint32_t fade = left < 1000.0 ? static_cast<std::uint32_t>(left / 1000.0 * 255.0) : 255u;
            graphics.fill(1, guiHeight - 49, 2 + w + 1, guiHeight - 38, (fade * 128u / 255u) << 24);
            graphics.text(overlayMessage, 2.0f, static_cast<float>(guiHeight - 47), (fade << 24) | 0xFFFFFFu);
        }
        guiRenderer.end();
        markPhase(kFrameGui);
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
            falcon::log::line("%s\n", pattern.c_str());
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

        const bool worldReadyForCapture = (!options_.play && !options_.connect) || leftWorld || options_.captureLoading ||
                                          (session != nullptr && session->ready() && !options_.benchmark);
        if (options_.capturePath && millis >= options_.captureAfterSeconds * 1000.0 && worldReadyForCapture) {
            if (!silent && audio.available()) {
                falcon::log::line("audio summary: %llu sounds started, music playing: %s, peak %.3f\n",
                            static_cast<unsigned long long>(audio.stats().soundsStarted),
                            audio.musicPlaying() ? "yes" : "no", static_cast<double>(audio.takePeak()));
            }
            const bool ok = saveFrame(*options_.capturePath, fbW, fbH);
            falcon::log::line("%s %dx%d frame %s %s\n", ok ? "saved" : "FAILED to save", fbW, fbH,
                        ok ? "to" : "at", options_.capturePath->generic_string().c_str());
            saveOptions();
            return ok ? 0 : 3;
        }
        if (session != nullptr) {  // what this frame cost the CPU (before the swap, which may wait for vsync)
            session->noteCpuMillis(std::chrono::duration<double, std::milli>(Clock::now() - now).count());
        }
        window.swapBuffers();
        markPhase(kFramePresent);
        if (session != nullptr) {
            session->noteFrame(frameMillis, framePhases);
            falcon::fps_test::setFrame(session->fps(), session->frameMillis());
        }
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
            for (const std::string& line : session->benchmarkReport()) falcon::log::line("%s\n", line.c_str());
            std::fflush(stdout);
            return 0;
        }
        // A background window's swaps do not wait for vsync: pace it ourselves (~60 fps) rather
        // than spinning a core while someone is busy doing something else. A window that is not the one being
        // used (another program has the focus) is held to 20 fps, so a game running next to it keeps the machine.
        static const bool noPacing = std::getenv("FALCON_NO_PACING") != nullptr;  // (to measure a hidden window running flat out, as an uncapped one does)
        // Inactivity FPS Limit: a window nobody is looking at is not drawn at full speed. Minimized means only a minimized window is held back (to 10 frames a second);
        // AFK also holds one that is merely not focused (to 20), the way the game does when nothing has been touched for a while.
        if (window.background()) {
            if (!noPacing) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else if (!window.focused() && gameOptions.inactivityFpsLimit == InactivityFpsLimit::Afk) {
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
