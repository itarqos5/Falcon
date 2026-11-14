#include "falcon/game/options_video_screens.hpp"

#include "falcon/game/key_binds_screen.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace falcon {
namespace {

std::string fixed(double v, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
    return buffer;
}


}  // namespace

// ---- Video Settings -------------------------------------------------------------------------

void VideoSettingsScreen::applyPreset(GameOptions& o, GraphicsPreset preset) {
    if (preset == GraphicsPreset::Custom) return;
    struct Row {
        int biome, render;
        ChunkUpdates chunks;
        int simulation;
        bool ao;
        CloudStatus clouds;
        ParticleStatus particles;
        int mipmaps;
        bool shadows;
        double entityDistance;
        int blur, cloudRange;
        bool leaves, transparency;
        int weather, anisotropy;
        TextureFiltering filtering;
    };
    static constexpr Row kFast = {1, 8, ChunkUpdates::None, 6, false, CloudStatus::Fast, ParticleStatus::Decreased, 2,
                                  false, 0.75, 2, 32, false, false, 5, 1, TextureFiltering::None};
    static constexpr Row kFancy = {2, 16, ChunkUpdates::PlayerAffected, 12, true, CloudStatus::Fancy, ParticleStatus::All, 4,
                                   true, 1.0, 5, 64, true, false, 10, 1, TextureFiltering::Rgss};
    static constexpr Row kFabulous = {2, 32, ChunkUpdates::PlayerAffected, 12, true, CloudStatus::Fancy, ParticleStatus::All, 4,
                                      true, 1.25, 5, 128, true, true, 10, 2, TextureFiltering::Anisotropic};
    const Row& r = preset == GraphicsPreset::Fast ? kFast : preset == GraphicsPreset::Fancy ? kFancy : kFabulous;
    o.biomeBlendRadius = r.biome;
    o.renderDistance = r.render;
    o.prioritizeChunkUpdates = r.chunks;
    o.simulationDistance = r.simulation;
    o.ambientOcclusion = r.ao;
    o.cloudStatus = r.clouds;
    o.particles = r.particles;
    o.mipmapLevels = r.mipmaps;
    o.entityShadows = r.shadows;
    o.entityDistanceScaling = r.entityDistance;
    o.menuBackgroundBlurriness = r.blur;
    o.cloudRange = r.cloudRange;
    o.cutoutLeaves = r.leaves;
    o.improvedTransparency = r.transparency;
    o.weatherRadius = r.weather;
    o.maxAnisotropyBit = r.anisotropy;
    o.textureFiltering = r.filtering;
    o.graphicsPreset = preset;
}

void VideoSettingsScreen::markCustom() {
    services_.options.graphicsPreset = GraphicsPreset::Custom;
    if (presetSlider_ != nullptr) presetSlider_->syncValue(intToSlider(0, 3, 3));
}

void VideoSettingsScreen::syncAll() {
    for (const auto& sync : syncers_) sync();
    if (anisotropySlider_ != nullptr) {
        anisotropySlider_->active = services_.options.textureFiltering == TextureFiltering::Anisotropic;
    }
}

void VideoSettingsScreen::addOptions() {
    GameOptions& o = services_.options;
    syncers_.clear();
    presetSlider_ = nullptr;
    anisotropySlider_ = nullptr;
    const auto onSync = [this](std::function<void()> f) { syncers_.push_back(std::move(f)); };

    // An on/off button bound to a bool field; `quality` ones switch the Preset to Custom when changed.
    const auto toggle = [&](const char* caption, bool GameOptions::*field, const std::string& tip, bool quality) {
        auto button = makeToggle(caption, o.*field, kSmallWidth, [this, field, quality](bool on) {
            services_.options.*field = on;
            if (quality) markCustom();
        });
        button->tooltip = tip;
        CycleButton* raw = button.get();
        onSync([this, raw, field] { raw->setValues(2, services_.options.*field ? 0 : 1); });
        return button;
    };
    // A button cycling through the values of an enum field, with an optional tooltip per value.
    const auto choice = [&](const char* caption, std::vector<std::string> keys, auto GameOptions::*field,
                            std::vector<std::string> tips, bool quality) {
        using Value = std::remove_reference_t<decltype(o.*field)>;
        const int count = static_cast<int>(keys.size());
        auto button = makeChoice(caption, std::move(keys), static_cast<int>(o.*field), kSmallWidth, nullptr);
        CycleButton* raw = button.get();
        const auto setTip = [raw, tips](int i) {
            if (!tips.empty()) raw->tooltip = tips[static_cast<std::size_t>(i)];
        };
        setTip(static_cast<int>(o.*field));
        button->onChange = [this, field, quality, setTip](int i) {
            services_.options.*field = static_cast<Value>(i);
            setTip(i);
            if (quality) markCustom();
        };
        onSync([this, raw, field, count, setTip] {
            const int i = static_cast<int>(services_.options.*field);
            raw->setValues(count, i);
            setTip(i);
        });
        return button;
    };
    // A slider over the whole numbers min..max, read and written through `get`/`set`.
    const auto intSlider = [&](int min, int max, std::function<int()> get, std::function<void(int)> set,
                               std::function<std::string(int)> label, const std::string& tip, bool quality) {
        auto slider = makeIntSlider(min, max, get(), kSmallWidth, std::move(label), [this, set, quality](int v) {
            set(v);
            if (quality) markCustom();
        });
        slider->tooltip = tip;
        SliderWidget* raw = slider.get();
        onSync([raw, min, max, get] { raw->syncValue(intToSlider(min, max, get())); });
        return slider;
    };
    const auto chunksLabel = [this](const char* caption) {
        return [this, caption](int n) { return optionLabel(caption, formatLanguageString(text("options.chunks"), {std::to_string(n)})); };
    };
    const auto intField = [this](int GameOptions::*member) {  // get/set pair for a plain int field
        return std::make_pair(std::function<int()>([this, member] { return services_.options.*member; }),
                              std::function<void(int)>([this, member](int v) { services_.options.*member = v; }));
    };

    // ---- Display ----
    addHeader(text("options.video.display.header"));

    // Exclusive Fullscreen Mode: -1 is the current mode, then each mode the monitor offers.
    const std::vector<std::string> modes = services_.videoModes ? services_.videoModes() : std::vector<std::string>();
    auto exclusive = makeIntSlider(
        -1, static_cast<int>(modes.size()) - 1, std::min(o.exclusiveFullscreenMode, static_cast<int>(modes.size()) - 1), kBigWidth,
        [this, modes](int v) {
            if (modes.empty()) return text("options.fullscreen.unavailable");
            return optionLabel("options.fullscreen.exclusive.mode",
                               v < 0 ? text("options.fullscreen.current") : modes[static_cast<std::size_t>(v)]);
        },
        [this](int v) { services_.options.exclusiveFullscreenMode = v; });
    exclusive->tooltip = "This setting only takes effect when Exclusive Fullscreen is enabled.";
    addBig(std::move(exclusive));

    auto framerate = intSlider(
        1, 26, [this] { return services_.options.framerateLimit / 10; },
        [this](int i) { services_.options.framerateLimit = i * 10; },
        [this](int i) {
            return i * 10 >= 260 ? optionLabel("options.framerateLimit", text("options.framerateLimit.max"))
                                 : optionLabel("options.framerateLimit",
                                               formatLanguageString(text("options.framerate"), {std::to_string(i * 10)}));
        },
        {}, false);

    // GUI Scale cycles Auto (0) up to the largest scale the window allows.
    const int maxScale = services_.maxGuiScale ? std::max(1, services_.maxGuiScale()) : 4;
    auto guiScale = std::make_unique<CycleButton>(
        0, 0, kSmallWidth, kWidgetHeight, maxScale + 1, std::min(o.guiScale, maxScale),
        [this](int i) { return optionLabel("options.guiScale", i == 0 ? text("options.guiScale.auto") : std::to_string(i)); },
        shiftDown());
    guiScale->onChange = [this](int i) { services_.options.guiScale = i; };
    CycleButton* guiScaleRaw = guiScale.get();
    onSync([this, guiScaleRaw, maxScale] { guiScaleRaw->setValues(maxScale + 1, std::min(services_.options.guiScale, maxScale)); });

    auto exclusiveFullscreen = makeToggle("options.exclusiveFullscreen", o.exclusiveFullscreen, kSmallWidth, nullptr);
    {
        const auto tip = [](bool on) {
            return on ? "This fullscreen mode might prevent some input methods from working, but adaptive sync "
                        "(Nvidia G-Sync, AMD FreeSync) should work."
                      : "All input methods are expected to work in this fullscreen mode, but adaptive sync "
                        "(Nvidia G-Sync, AMD FreeSync) will not work.";
        };
        CycleButton* raw = exclusiveFullscreen.get();
        raw->tooltip = tip(o.exclusiveFullscreen);
        raw->onChange = [this, raw, tip](int i) {
            services_.options.exclusiveFullscreen = i == 0;
            raw->tooltip = tip(i == 0);
        };
    }

    auto brightness = makeSlider(
        o.gamma, kSmallWidth,
        [this](double v) {
            const int p = static_cast<int>(v * 100.0);
            return optionLabel("options.gamma", p == 0 ? text("options.gamma.min")
                                                     : p == 50 ? text("options.gamma.default")
                                                               : p == 100 ? text("options.gamma.max") : std::to_string(p));
        },
        [this](double v) { services_.options.gamma = v; });

    addPair(std::move(framerate), toggle("options.vsync", &GameOptions::enableVsync, {}, false));
    addPair(choice("options.inactivityFpsLimit", {"options.inactivityFpsLimit.minimized", "options.inactivityFpsLimit.afk"},
                   &GameOptions::inactivityFpsLimit,
                   {"Limits framerate only when the game window is minimized.",
                    "Limits framerate to 30 when the game is not getting any player input for more than a minute. "
                    "Further limits it to 10 after 9 more minutes."},
                   false),
            std::move(guiScale));
    addPair(toggle("options.fullscreen", &GameOptions::fullscreen, {}, false), std::move(exclusiveFullscreen));
    addPair(std::move(brightness),
            // (No "Default" choice: OpenGL or Vulkan. The list comes from graphics_api.hpp, so a new API only has to be added there.)
            choice("options.graphicsApi", {std::string(graphicsApiTranslationKey(GraphicsApi::OpenGL)), std::string(graphicsApiTranslationKey(GraphicsApi::Vulkan))},
                   &GameOptions::graphicsApi, {graphicsApiTooltip(GraphicsApi::OpenGL), graphicsApiTooltip(GraphicsApi::Vulkan)}, false));

    // ---- Quality & Performance ----
    addHeader(text("options.video.quality.header"));
    auto preset = makeIntSlider(
        0, 3, static_cast<int>(o.graphicsPreset), kBigWidth,
        [this](int i) {
            static constexpr const char* kKeys[] = {"options.graphics.fast", "options.graphics.fancy",
                                                    "options.graphics.fabulous", "options.graphics.custom"};
            return optionLabel("options.graphics.preset", text(kKeys[i]));
        },
        [this](int i) {
            services_.options.graphicsPreset = static_cast<GraphicsPreset>(i);
            if (i < 3) {
                applyPreset(services_.options, static_cast<GraphicsPreset>(i));
                syncAll();
            }
        });
    preset->tooltip = "Sets \"Quality & Performance\" settings to reasonable defaults corresponding to the desired quality.";
    presetSlider_ = preset.get();
    addBig(std::move(preset));

    auto biomeBlend = intSlider(
        0, 7, intField(&GameOptions::biomeBlendRadius).first, intField(&GameOptions::biomeBlendRadius).second,
        [this](int n) { return optionLabel("options.biomeBlendRadius", text("options.biomeBlendRadius." + std::to_string(2 * n + 1))); },
        {}, true);
    auto renderDistance = intSlider(2, 32, intField(&GameOptions::renderDistance).first, intField(&GameOptions::renderDistance).second,
                                    chunksLabel("options.renderDistance"), {}, true);
    auto simulationDistance = intSlider(5, 32, intField(&GameOptions::simulationDistance).first,
                                        intField(&GameOptions::simulationDistance).second,
                                        chunksLabel("options.simulationDistance"), {}, true);
    auto mipmaps = intSlider(0, 4, intField(&GameOptions::mipmapLevels).first, intField(&GameOptions::mipmapLevels).second,
                             [this](int n) { return numberOrOffLabel("options.mipmapLevels", n); }, {}, true);
    auto entityDistance = intSlider(
        2, 20, [this] { return static_cast<int>(std::lround(services_.options.entityDistanceScaling * 4.0)); },
        [this](int i) { services_.options.entityDistanceScaling = i / 4.0; },
        [this](int i) { return percentLabel("options.entityDistanceScaling", i / 4.0); }, {}, true);
    auto blur = intSlider(0, 10, intField(&GameOptions::menuBackgroundBlurriness).first,
                          intField(&GameOptions::menuBackgroundBlurriness).second,
                          [this](int n) { return numberOrOffLabel("options.accessibility.menu_background_blurriness", n); },
                          text("options.accessibility.menu_background_blurriness.tooltip"), true);
    auto cloudDistance = intSlider(2, 128, intField(&GameOptions::cloudRange).first, intField(&GameOptions::cloudRange).second,
                                   chunksLabel("options.renderCloudsDistance"), {}, true);
    auto anisotropy = intSlider(
        1, 3, intField(&GameOptions::maxAnisotropyBit).first, intField(&GameOptions::maxAnisotropyBit).second,
        [this](int n) { return optionLabel("options.maxAnisotropy", std::to_string(1 << n) + "x"); },
        "Each level significantly improves how smooth textures look, but impacts performance and significantly impacts "
        "video memory usage. Requires Texture Filtering to be set to Anisotropic.",
        true);
    anisotropySlider_ = anisotropy.get();
    anisotropy->active = o.textureFiltering == TextureFiltering::Anisotropic;
    auto weather = intSlider(3, 10, intField(&GameOptions::weatherRadius).first, intField(&GameOptions::weatherRadius).second,
                             [this](int n) { return optionLabel("options.weatherRadius", formatLanguageString(text("options.blocks"), {std::to_string(n)})); },
                             "Radius of the area where rain and snow effects are visible. Very low performance impact.", true);

    auto textureFiltering = choice(
        "options.textureFiltering", {"options.textureFiltering.none", "options.textureFiltering.rgss", "options.textureFiltering.anisotropic"},
        &GameOptions::textureFiltering,
        {"Textures are displayed without any filtering. Blocks may look blurry when viewed at an angle.",
         "(Rotated Grid Super Sampling)\nA shader-based filtering method that improves texture quality with a moderate performance impact.",
         "A hardware-based filtering method, but impacts performance and significantly impacts video memory usage. May not be "
         "supported on all hardware."},
        true);
    // Anisotropic Filtering only works with that filtering mode.
    {
        auto previous = std::move(textureFiltering->onChange);
        textureFiltering->onChange = [this, previous](int i) {
            if (previous) previous(i);
            if (anisotropySlider_ != nullptr) anisotropySlider_->active = i == static_cast<int>(TextureFiltering::Anisotropic);
        };
    }

    addPair(std::move(biomeBlend), std::move(renderDistance));
    addPair(choice("options.prioritizeChunkUpdates",
                   {"options.prioritizeChunkUpdates.none", "options.prioritizeChunkUpdates.byPlayer", "options.prioritizeChunkUpdates.nearby"},
                   &GameOptions::prioritizeChunkUpdates,
                   {"Nearby chunks are compiled in parallel threads. This may result in brief visual holes when blocks are destroyed.",
                    "Some actions within a chunk will recompile the chunk immediately. This includes block placing and destroying.",
                    "Nearby chunks are always compiled immediately. This may impact game performance when blocks are placed or destroyed."},
                   true),
            std::move(simulationDistance));
    addPair(toggle("options.ao", &GameOptions::ambientOcclusion, {}, true),
            choice("options.renderClouds", {"options.off", "options.clouds.fast", "options.clouds.fancy"}, &GameOptions::cloudStatus, {}, true));
    addPair(choice("options.particles", {"options.particles.all", "options.particles.decreased", "options.particles.minimal"},
                   &GameOptions::particles, {}, true),
            std::move(mipmaps));
    addPair(toggle("options.entityShadows", &GameOptions::entityShadows, {}, true), std::move(entityDistance));
    addPair(std::move(blur), std::move(cloudDistance));
    addPair(toggle("options.cutoutLeaves", &GameOptions::cutoutLeaves,
                   "Allows you to see through gaps in leaves. Disabling improves performance.", true),
            toggle("options.improvedTransparency", &GameOptions::improvedTransparency,
                   "An experimental approach that uses an order-independent transparency algorithm to avoid graphical issues "
                   "normally present when looking through multiple layers of translucent objects.\nThis will impact performance.",
                   true));
    addPair(std::move(textureFiltering), std::move(anisotropy));
    addPair(std::move(weather));

    // ---- Preferences ----
    addHeader(text("options.video.preferences.header"));
    auto chunkFade = intSlider(
        0, 40, [this] { return static_cast<int>(std::lround(services_.options.chunkSectionFadeInTime * 20.0)); },
        [this](int i) { services_.options.chunkSectionFadeInTime = i / 20.0; },
        [this](int i) {
            const double seconds = i / 20.0;
            return seconds <= 0.0 ? text("options.chunkFade.none")
                                  : formatLanguageString(text("options.chunkFade.seconds"), {fixed(seconds, 2)});
        },
        "How long in seconds chunks should fade in when they're first rendered, if at all.", false);
    addPair(toggle("options.autosaveIndicator", &GameOptions::showAutosaveIndicator, {}, false),
            toggle("options.vignette", &GameOptions::vignette,
                   "This is a subtle texture over the game screen used for reducing brightness towards the edges of the screen "
                   "and warning about the world border.",
                   false));
    addPair(choice("options.attackIndicator", {"options.off", "options.attack.crosshair", "options.attack.hotbar"},
                   &GameOptions::attackIndicator, {}, false),
            std::move(chunkFade));
}

// ---- Controls -------------------------------------------------------------------------------

void ControlsScreen::addOptions() {
    GameOptions& o = services_.options;

    auto mouse = makeLink("options.mouse_settings", kSmallWidth,
                          [this] { return std::make_unique<MouseSettingsScreen>(navigator_, services_); });
    auto keyBinds = makeLink("controls.keybinds", kSmallWidth,
                             [this] { return std::make_unique<KeyBindsScreen>(navigator_, services_); });
    addPair(std::move(mouse), std::move(keyBinds));

    // Hold/Toggle options: the list is [Toggle, Hold], so a click simply flips the value.
    const auto holdToggle = [&](const char* captionKey, bool GameOptions::*field) {
        return makeChoice(captionKey, {"options.key.toggle", "options.key.hold"}, o.*field ? 0 : 1, kSmallWidth,
                          [this, field](int i) { services_.options.*field = i == 0; });
    };
    addPair(holdToggle("key.sneak", &GameOptions::toggleCrouch), holdToggle("key.sprint", &GameOptions::toggleSprint));
    addPair(holdToggle("key.attack", &GameOptions::toggleAttack), holdToggle("key.use", &GameOptions::toggleUse));
    addPair(optionToggle("options.autoJump", &GameOptions::autoJump),
            withTooltip(makeIntSlider(
                            0, 10, o.sprintWindow, kSmallWidth,
                            [this](int n) { return numberOrOffLabel("options.sprintWindow", n); },
                            [this](int n) { services_.options.sprintWindow = n; }),
                        "options.sprintWindow.tooltip"));
    addPair(optionToggle("options.quitShortcuts", &GameOptions::quitShortcuts, "options.quitShortcuts.tooltip"),
            optionToggle("options.operatorItemsTab", &GameOptions::operatorItemsTab));
}

// ---- Mouse Settings -------------------------------------------------------------------------

void MouseSettingsScreen::addOptions() {
    GameOptions& o = services_.options;

    auto sensitivity = makeSlider(
        o.mouseSensitivity, kSmallWidth,
        [this](double v) {
            if (v == 0.0) return optionLabel("options.sensitivity", text("options.sensitivity.min"));
            if (v == 1.0) return optionLabel("options.sensitivity", text("options.sensitivity.max"));
            return percentLabel("options.sensitivity", 2.0 * v);
        },
        [this](double v) { services_.options.mouseSensitivity = v; });

    // Scroll Sensitivity is logarithmic: the slider steps i = -200..100 mean 10^(i/100) = 0.01..10.
    const int wheelStep = std::clamp(static_cast<int>(std::floor(std::log10(std::max(o.mouseWheelSensitivity, 0.01)) * 100.0)), -200, 100);
    auto wheel = makeIntSlider(
        -200, 100, wheelStep, kSmallWidth,
        [this](int i) { return text("options.mouseWheelSensitivity") + ": " + fixed(std::pow(10.0, i / 100.0), 2); },
        [this](int i) { services_.options.mouseWheelSensitivity = std::pow(10.0, i / 100.0); });

    addPair(std::move(sensitivity), std::move(wheel));
    addPair(optionToggle("options.discrete_mouse_scroll", &GameOptions::discreteMouseScroll),
            optionToggle("options.invertMouseX", &GameOptions::invertXMouse));
    addPair(optionToggle("options.invertMouseY", &GameOptions::invertYMouse),
            optionToggle("options.allowCursorChanges", &GameOptions::allowCursorChanges, "options.allowCursorChanges.tooltip"));
}

}  // namespace falcon
