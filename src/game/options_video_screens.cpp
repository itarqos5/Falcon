#include "falcon/game/options_video_screens.hpp"

#include "falcon/game/key_binds_screen.hpp"
#include "falcon/render/lod_renderer.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <array>
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

namespace {

constexpr std::uint32_t kAccent = 0xFF5BD18A;  // the green of what is selected

// How far out the LODs reach, for a place on the slider: 64 to 8192 chunks on a scale where every doubling is the same distance, in steps that grow with the number.
int lodChunksFromSlider(double t) {
    const double raw = 64.0 * std::pow(128.0, std::clamp(t, 0.0, 1.0));
    const int step = raw < 128 ? 8 : raw < 256 ? 16 : raw < 512 ? 32 : raw < 1024 ? 64 : raw < 2048 ? 128 : raw < 4096 ? 256 : 512;
    return std::clamp(static_cast<int>(std::lround(raw / step)) * step, kLodMinChunks, kLodMaxChunks);
}
double sliderFromLodChunks(int chunks) { return std::clamp(std::log(std::max(chunks, 1) / 64.0) / std::log(128.0), 0.0, 1.0); }

// One of the tabs down the left of the screen.
class TabButton final : public Widget {
public:
    TabButton(std::string label, int px, int py, int w, int h) : Widget(px, py, w, h), label_(std::move(label)) {}
    bool selected = false;

protected:
    void renderContents(GuiGraphics& g) override {
        const bool hot = isHovered();
        g.fill(x, y, x + width, y + height, selected ? 0xE0262C30u : hot ? 0x70FFFFFFu : 0x50000000u);
        if (selected) g.fill(x, y, x + 3, y + height, kAccent);
        g.text(label_, static_cast<float>(x + 11), static_cast<float>(y + (height - 8) / 2), selected || hot ? 0xFFFFFFFFu : 0xFFB8B8B8u, true, false, selected);
    }

private:
    std::string label_;
};

// The three presets as cards: what each sets, and a frame round the one that is on.
class PresetRow final : public ScrollList::Entry {
public:
    static constexpr int kHeight = 98;
    struct Card {
        std::string name;
        std::array<std::string, 4> lines;
    };
    PresetRow(const GameOptions& options, int rowsWidth, std::array<Card, 3> cards, std::function<void(int)> pick)
        : Entry(kHeight), options_(options), rowsWidth_(rowsWidth), cards_(std::move(cards)), pick_(std::move(pick)) {}

    void render(GuiGraphics& g, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState&) override {
        for (int i = 0; i < 3; ++i) {
            const Rect c = card(r, i);
            const bool hot = mouseX >= c.x && mouseX < c.x + c.width && mouseY >= c.y && mouseY < c.y + c.height;
            const bool on = static_cast<int>(options_.graphicsPreset) == i;
            g.fill(c.x, c.y, c.x + c.width, c.y + c.height, on ? 0xC01E3A2Au : hot ? 0xA03A3A3Au : 0x80202020u);
            const std::uint32_t border = on ? kAccent : hot ? 0xFFAAAAAAu : 0xFF505050u;
            g.fill(c.x, c.y, c.x + c.width, c.y + 1, border);
            g.fill(c.x, c.y + c.height - 1, c.x + c.width, c.y + c.height, border);
            g.fill(c.x, c.y, c.x + 1, c.y + c.height, border);
            g.fill(c.x + c.width - 1, c.y, c.x + c.width, c.y + c.height, border);
            const Card& card = cards_[static_cast<std::size_t>(i)];
            g.text(card.name, static_cast<float>(c.x + (c.width - g.textWidth(card.name, true)) / 2), static_cast<float>(c.y + 8), kWhite, true, false, true);
            for (std::size_t line = 0; line < card.lines.size(); ++line) {
                g.text(card.lines[line], static_cast<float>(c.x + 8), static_cast<float>(c.y + 28 + 13 * static_cast<int>(line)), 0xFFC4C4C4u);
            }
            if (on) g.text("Selected", static_cast<float>(c.x + (c.width - g.textWidth("Selected")) / 2), static_cast<float>(c.y + c.height - 13), kAccent);
        }
    }
    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int button) override {
        if (button != 0) return {};
        for (int i = 0; i < 3; ++i) {
            const Rect c = card(r, i);
            if (px >= c.x && px < c.x + c.width && py >= c.y && py < c.y + c.height) {
                pick_(i);
                return {true, true};
            }
        }
        return {};
    }

private:
    [[nodiscard]] Rect card(const Rect& r, int index) const {
        constexpr int kGap = 6, kRowWidth = 310;
        const int width = (kRowWidth - 2 * kGap) / 3;
        return {rowsWidth_ / 2 - kRowWidth / 2 + index * (width + kGap), r.y + 2, width, kHeight - 4};
    }
    const GameOptions& options_;
    int rowsWidth_;
    std::array<Card, 3> cards_;
    std::function<void(int)> pick_;
};

// Lines of grey text in a row (a note under the options); the lines are asked for every frame, so they can follow the options.
class NoteRow final : public ScrollList::Entry {
public:
    NoteRow(int lines, int rowsWidth, std::function<std::vector<std::string>()> text) : Entry(lines * 11 + 8), rowsWidth_(rowsWidth), text_(std::move(text)) {}
    void render(GuiGraphics& g, const Rect& r, int, int, const ScrollList::RowState&) override {
        int y = r.y + 4;
        for (const std::string& line : text_()) {
            g.text(line, static_cast<float>(rowsWidth_ / 2 - 155), static_cast<float>(y), 0xFFA0A0A0u);
            y += 11;
        }
    }

private:
    int rowsWidth_;
    std::function<std::vector<std::string>()> text_;
};

constexpr const char* kTabNames[] = {"Presets", "Display", "Quality", "LODs", "Preferences"};
constexpr int kTabCount = 5;
constexpr int kTabWidth = 84;
constexpr int kTabMargin = 8;

}  // namespace

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
        bool lods;
        int lodRadius, lodDetail;
    };
    static constexpr Row kFast = {1, 8, ChunkUpdates::None, 6, false, CloudStatus::Fast, ParticleStatus::Decreased, 2,
                                  false, 0.75, 2, 32, false, false, 5, 1, TextureFiltering::None, false, 64, kLodDetailLow};
    static constexpr Row kFancy = {2, 16, ChunkUpdates::PlayerAffected, 12, true, CloudStatus::Fancy, ParticleStatus::All, 4,
                                   true, 1.0, 5, 64, true, false, 10, 1, TextureFiltering::Rgss, true, 128, kLodDetailMedium};
    static constexpr Row kFabulous = {2, 32, ChunkUpdates::PlayerAffected, 12, true, CloudStatus::Fancy, ParticleStatus::All, 4,
                                      true, 1.25, 5, 128, true, true, 10, 2, TextureFiltering::Anisotropic, true, 256, kLodDetailFull};
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
    o.lodsEnabled = r.lods;
    o.lodChunkRadius = r.lodRadius;
    o.lodDetail = r.lodDetail;
    o.graphicsPreset = preset;
}

void VideoSettingsScreen::markCustom() { services_.options.graphicsPreset = GraphicsPreset::Custom; }

ListScreenLayout VideoSettingsScreen::computeLayout(int titleWidth) const {
    ListScreenLayout l = ListScreen::computeLayout(titleWidth);
    l.list.x = kTabMargin + kTabWidth + kTabMargin;
    l.list.width = std::max(320, width_ - l.list.x - kTabMargin);
    return l;
}

void VideoSettingsScreen::addFooter(const ListScreenLayout& layout) {
    ListScreen::addFooter(layout);  // Done
    tabButtons_.clear();
    for (int i = 0; i < kTabCount; ++i) {
        auto& tab = addWidget<TabButton>(kTabNames[i], kTabMargin, layout.list.y + 4 + i * 25, kTabWidth, 22);
        tab.selected = i == tab_;
        tab.onPress = [this, i] { selectTab(i); };
        tabButtons_.push_back(&tab);
    }
}

void VideoSettingsScreen::selectTab(int tab) {
    if (tab == tab_) return;
    tab_ = tab;
    for (int i = 0; i < static_cast<int>(tabButtons_.size()); ++i) static_cast<TabButton*>(tabButtons_[static_cast<std::size_t>(i)])->selected = i == tab_;
    list().clearEntries();
    list().setScrollAmount(0.0);
    fill();
}

void VideoSettingsScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    graphics.fill(kTabMargin - 3, metrics::kHeaderHeight, kTabMargin + kTabWidth + 3, height_ - metrics::kFooterHeight, 0x70000000u);  // behind the tabs
    Screen::render(graphics, mouseX, mouseY, time);
}

void VideoSettingsScreen::addOptions() { fill(); }

void VideoSettingsScreen::fill() {
    GameOptions& o = services_.options;
    anisotropySlider_ = nullptr;
    lodRadiusSlider_ = nullptr;
    lodDetailButton_ = nullptr;

    // An on/off button bound to a bool field; `quality` ones switch the Preset to Custom when changed.
    const auto toggle = [&](const char* caption, bool GameOptions::*field, const std::string& tip, bool quality) {
        auto button = makeToggle(caption, o.*field, kSmallWidth, [this, field, quality](bool on) {
            services_.options.*field = on;
            if (quality) markCustom();
        });
        button->tooltip = tip;
        return button;
    };
    // A button cycling through the values of an enum field, with an optional tooltip per value.
    const auto choice = [&](const char* caption, std::vector<std::string> keys, auto GameOptions::*field,
                            std::vector<std::string> tips, bool quality) {
        using Value = std::remove_reference_t<decltype(o.*field)>;
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
        return slider;
    };
    const auto chunksLabel = [this](const char* caption) {
        return [this, caption](int n) { return optionLabel(caption, formatLanguageString(text("options.chunks"), {std::to_string(n)})); };
    };
    const auto intField = [this](int GameOptions::*member) {  // get/set pair for a plain int field
        return std::make_pair(std::function<int()>([this, member] { return services_.options.*member; }),
                              std::function<void(int)>([this, member](int v) { services_.options.*member = v; }));
    };

    switch (tab_) {
        case 0: {  // ---- Presets ----
            addHeader("Graphics Preset");
            std::array<PresetRow::Card, 3> cards;
            static constexpr const char* kNames[3] = {"Fast", "Fancy", "Fabulous!"};
            for (int i = 0; i < 3; ++i) {
                GameOptions sample;
                applyPreset(sample, static_cast<GraphicsPreset>(i));
                PresetRow::Card& card = cards[static_cast<std::size_t>(i)];
                card.name = text(i == 0 ? "options.graphics.fast" : i == 1 ? "options.graphics.fancy" : "options.graphics.fabulous");
                if (card.name.empty() || card.name[0] == 'o') card.name = kNames[i];
                card.lines = {std::to_string(sample.renderDistance) + " chunks", sample.ambientOcclusion ? "Smooth lighting" : "Flat lighting",
                              sample.cloudStatus == CloudStatus::Fancy ? "Fancy clouds" : "Fast clouds",
                              sample.lodsEnabled ? "LODs to " + std::to_string(sample.lodChunkRadius) : std::string("No LODs")};
            }
            list().addEntry<PresetRow>(services_.options, rowsWidth(), cards, [this](int i) { applyPreset(services_.options, static_cast<GraphicsPreset>(i)); });
            list().addEntry<NoteRow>(1, rowsWidth(), [this] {
                const GraphicsPreset p = services_.options.graphicsPreset;
                return std::vector<std::string>{p == GraphicsPreset::Custom ? "Preset: Custom (you changed something a preset sets)"
                                                                           : std::string("Preset: ") + (p == GraphicsPreset::Fast ? "Fast" : p == GraphicsPreset::Fancy ? "Fancy" : "Fabulous!")};
            });
            list().addEntry<NoteRow>(2, rowsWidth(), [] {
                return std::vector<std::string>{"A preset sets distance, lighting, clouds, textures and LODs together.", "Change any of them in the other tabs and it reads Custom."};
            });
            break;
        }
        case 1: {  // ---- Display ----
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
            break;
        }
        case 2: {  // ---- Quality & Performance ----
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
            break;
        }
        case 3: {  // ---- LODs ----
            addHeader("LODs");
            // The checkbox: the whole feature on or off.
            auto box = std::make_unique<CheckboxWidget>("LODs", 0, 0, font().width("LODs"), o.lodsEnabled);
            box->tooltip = "Draws simplified terrain, as blocky columns, beyond the loaded chunks, out to the LOD Distance. It is worked out from the world's seed: no chunks are "
                           "generated for it.";
            box->onChange = [this](bool on) {
                services_.options.lodsEnabled = on;
                markCustom();
                if (lodRadiusSlider_ != nullptr) lodRadiusSlider_->active = on;
                if (lodDetailButton_ != nullptr) lodDetailButton_->active = on;
            };
            addBig(std::move(box));

            // The distance: 64 to 8192 chunks, every doubling the same width on the slider.
            auto radius = makeSlider(
                sliderFromLodChunks(o.lodChunkRadius), kBigWidth,
                [this](double t) { return optionLabel("LOD Distance", formatLanguageString(text("options.chunks"), {std::to_string(lodChunksFromSlider(t))})); },
                [this](double t) {
                    services_.options.lodChunkRadius = lodChunksFromSlider(t);
                    markCustom();
                });
            radius->tooltip = "How far out LODs reach, 64 to 8192 chunks from you. The further, the more of the world is drawn and the more the LODs cost.";
            radius->active = o.lodsEnabled;
            lodRadiusSlider_ = radius.get();
            addBig(std::move(radius));

            // The strength: how fine the columns are.
            static const std::vector<std::string> kStrengthTips = {
                "Low Detail: big columns, the cheapest. For the longest distances and for slower computers; the terrain is simple.",
                "Medium Detail: a balance. Columns get smaller close to the loaded chunks.",
                "Full Detail: the finest columns (down to 4 blocks), the closest to the real terrain. Needs a fast processor to fill in."};
            auto strength = makeChoice("LOD View Strength", {"Low Detail", "Medium Detail", "Full Detail"}, o.lodDetail, kBigWidth, nullptr);
            CycleButton* strengthRaw = strength.get();
            strengthRaw->tooltip = kStrengthTips[static_cast<std::size_t>(std::clamp(o.lodDetail, 0, 2))];
            strength->onChange = [this, strengthRaw](int i) {
                services_.options.lodDetail = i;
                strengthRaw->tooltip = kStrengthTips[static_cast<std::size_t>(i)];
                markCustom();
            };
            strength->active = o.lodsEnabled;
            lodDetailButton_ = strength.get();
            addBig(std::move(strength));

            list().addEntry<NoteRow>(4, rowsWidth(), [this] {
                if (services_.options.lodsEnabled) {
                    return std::vector<std::string>{"LODs only fill in the distance: the loaded chunks are always the real world.", "Turn the LOD Distance up for a farther horizon, the strength",
                                                    "down if it takes long to fill in. They need a world made on this computer", "(not a server)."};
                }
                return std::vector<std::string>{"LODs are off: the world ends where the loaded chunks do.", "", "", ""};
            });
            break;
        }
        default: {  // ---- Preferences ----
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
            break;
        }
    }
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
