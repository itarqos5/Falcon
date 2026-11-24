#include "falcon/game/options_video_screens.hpp"

#include "falcon/game/fps_test.hpp"
#include "falcon/game/game_session.hpp"
#include "falcon/game/key_binds_screen.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/render/lod_renderer.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace falcon {
namespace {

std::string fixed(double v, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
    return buffer;
}

}  // namespace

// ---- Video Settings -------------------------------------------------------------------------

// The pages and options of the screen. An option reads and writes one whole number (a switch is 0 or 1, a row of values the index of the shown one, a slider the step it is at),
// whatever the field it stands for is: `read` and `write` go to the field, `set` is what a change made in the screen does to the pending options (the quality options also turn
// the preset to Custom).
struct VideoSettingsModel {
    enum class Kind { Switch, Cycle, Range };
    enum class Impact { None, Low, Medium, High };
    struct Option {
        Kind kind = Kind::Switch;
        std::string name, tooltip;
        Impact impact = Impact::None;
        std::vector<std::string> values;               // Switch (Off, On) and Cycle: the name of each value
        int min = 0, max = 0;                          // Range
        std::function<std::string(int)> label;         // Range: the text of a step
        std::function<int(const GameOptions&)> read;
        std::function<void(GameOptions&, int)> write;
        std::function<void(GameOptions&, int)> set;
        std::function<bool(const GameOptions&)> enabled;  // (empty: always)
        float hover = 0.0f;                            // 0..1, eased: the red of the hovered row
    };
    struct Page {
        std::string name;
        std::vector<std::vector<std::unique_ptr<Option>>> blocks;  // groups of options, a gap between them
    };
    struct Block {
        std::string title;  // (only the search results have one: the page it came from)
        std::vector<Option*> options;
    };
    struct RowInfo {
        Option* option;
        int y;
    };
    // A button of the Developer page's panel (drawn and hit-tested by the screen: it is not an option of the list).
    struct DevButton {
        std::string label;
        int action = 0;  // 0: restart the counts, 1: the Highest FPS Test (its confirmation panel), 2: put the settings back, 3: OK and 4: Cancel in that panel
        int x = 0, y = 0, w = 0, h = 0;
        bool enabled = true;
    };
    std::vector<DevButton> devButtons;
    std::vector<DevButton> confirmButtons;  // (the "Are you sure?" panel's own, while it is up)

    GameOptions pending;  // the options as the screen has them, until Apply
    std::vector<Page> pages;
    int page = 0;
    std::string query;
    std::vector<Block> found;  // the search results
    float scroll = 0.0f;
    int totalLength = 0;
    Option* dragging = nullptr;
    bool draggingScroll = false;
    std::vector<RowInfo> rows;  // what was drawn last frame, for the mouse
    std::array<float, 8> buttonHover{};
    std::array<float, 3> actionHover{};

    [[nodiscard]] bool searching() const { return !query.empty(); }
    [[nodiscard]] std::vector<Block> blocksOf() const {
        if (searching()) return found;
        std::vector<Block> out;
        for (const auto& block : pages[static_cast<std::size_t>(page)].blocks) {
            Block b;
            for (const auto& option : block) b.options.push_back(option.get());
            out.push_back(std::move(b));
        }
        return out;
    }
    template <class F>
    void forEachOption(F&& f) {
        for (Page& p : pages) {
            for (auto& block : p.blocks) {
                for (auto& option : block) f(*option);
            }
        }
    }
};

namespace {

using Model = VideoSettingsModel;
using Option = VideoSettingsModel::Option;
using Kind = VideoSettingsModel::Kind;
using Impact = VideoSettingsModel::Impact;

// ---- VulkanMod's colours and sizes (config/gui/util/VGuiConstants.java and the widgets) ----

constexpr std::uint32_t pack(float r, float g, float b, float a) {
    const auto c = [](float v) { return static_cast<std::uint32_t>(v * 255.0f + 0.5f); };
    return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
}
std::uint32_t withAlpha(std::uint32_t argb, float multiplier) {
    const auto a = static_cast<std::uint32_t>(std::clamp(static_cast<float>(argb >> 24) * multiplier, 0.0f, 255.0f) + 0.5f);
    return (a << 24) | (argb & 0xFFFFFFu);
}
constexpr std::uint32_t kRed = pack(0.4f, 0.05f, 0.05f, 0.8f);
constexpr std::uint32_t kBlack = pack(0.0f, 0.0f, 0.0f, 1.0f);
constexpr std::uint32_t kGrey = pack(0.6f, 0.6f, 0.6f, 1.0f);
constexpr int kMargin = 10, kPageWidth = 100, kWidgetHeight = 20, kWidgetMargin = 5;
constexpr int kListTop = 32, kListBottom = 60, kItemMargin = 3, kIconHeight = 28;

// How far out the LODs reach, for a place on the slider: 64 to 8192 chunks on a scale where every doubling is the same distance, in steps that grow with the number.
int lodChunksFromSlider(double t) {
    const double raw = 64.0 * std::pow(128.0, std::clamp(t, 0.0, 1.0));
    const int step = raw < 128 ? 8 : raw < 256 ? 16 : raw < 512 ? 32 : raw < 1024 ? 64 : raw < 2048 ? 128 : raw < 4096 ? 256 : 512;
    return std::clamp(static_cast<int>(std::lround(raw / step)) * step, kLodMinChunks, kLodMaxChunks);
}
double sliderFromLodChunks(int chunks) { return std::clamp(std::log(std::max(chunks, 1) / 64.0) / std::log(128.0), 0.0, 1.0); }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

[[nodiscard]] std::string shown(const Option& o, int v) {
    if (o.kind == Kind::Range) return o.label ? o.label(v) : std::to_string(v);
    if (o.values.empty()) return {};
    return o.values[static_cast<std::size_t>(std::clamp(v, 0, static_cast<int>(o.values.size()) - 1))];
}

// ---- the options ----

// A page's options are made with these; `quality` ones turn the preset to Custom when they change.
struct Builder {
    VideoSettingsModel& model;
    ScreenServices& services;
    std::function<std::string(const std::string&)> tr;

    Option& add(std::size_t page, std::size_t block, Option&& option, bool quality) {
        if (!option.set) {
            option.set = quality ? std::function<void(GameOptions&, int)>([write = option.write](GameOptions& g, int v) {
                write(g, v);
                g.graphicsPreset = GraphicsPreset::Custom;
            })
                                 : option.write;
        }
        auto& p = model.pages[page];
        if (p.blocks.size() <= block) p.blocks.resize(block + 1);
        p.blocks[block].push_back(std::make_unique<Option>(std::move(option)));
        return *p.blocks[block].back();
    }
    template <class T>
    Option& toggle(std::size_t page, std::size_t block, const std::string& name, const std::string& tip, Impact impact, T GameOptions::*field, bool quality) {
        Option o;
        o.kind = Kind::Switch;
        o.name = name;
        o.tooltip = tip;
        o.impact = impact;
        o.values = {tr("options.off"), tr("options.on")};
        o.read = [field](const GameOptions& g) { return static_cast<int>(g.*field); };
        o.write = [field](GameOptions& g, int v) { g.*field = static_cast<T>(v != 0); };
        return add(page, block, std::move(o), quality);
    }
    template <class T>
    Option& cycle(std::size_t page, std::size_t block, const std::string& name, const std::string& tip, Impact impact, std::vector<std::string> values, T GameOptions::*field,
                  bool quality) {
        Option o;
        o.kind = Kind::Cycle;
        o.name = name;
        o.tooltip = tip;
        o.impact = impact;
        o.values = std::move(values);
        o.read = [field](const GameOptions& g) { return static_cast<int>(g.*field); };
        o.write = [field](GameOptions& g, int v) { g.*field = static_cast<T>(v); };
        return add(page, block, std::move(o), quality);
    }
    Option& range(std::size_t page, std::size_t block, const std::string& name, const std::string& tip, Impact impact, int min, int max, std::function<int(const GameOptions&)> read,
                  std::function<void(GameOptions&, int)> write, std::function<std::string(int)> label, bool quality) {
        Option o;
        o.kind = Kind::Range;
        o.name = name;
        o.tooltip = tip;
        o.impact = impact;
        o.min = min;
        o.max = max;
        o.read = std::move(read);
        o.write = std::move(write);
        o.label = std::move(label);
        return add(page, block, std::move(o), quality);
    }
    template <class T>
    Option& intRange(std::size_t page, std::size_t block, const std::string& name, const std::string& tip, Impact impact, int min, int max, T GameOptions::*field,
                     std::function<std::string(int)> label, bool quality) {
        return range(page, block, name, tip, impact, min, max, [field](const GameOptions& g) { return static_cast<int>(g.*field); },
                     [field](GameOptions& g, int v) { g.*field = static_cast<T>(v); }, std::move(label), quality);
    }
};

void buildPages(VideoSettingsModel& m, ScreenServices& services, const std::function<std::string(const std::string&)>& tr) {
    using falcon::formatLanguageString;
    Builder b{m, services, tr};
    m.pages.resize(6);
    m.pages[0].name = "Video";
    m.pages[1].name = "Graphics";
    m.pages[2].name = "LODs";
    m.pages[3].name = "Sky";
    m.pages[4].name = "Other";
    m.pages[5].name = "Developer";  // (no options: the screen draws a panel of what the frames cost, see drawDeveloper)
    const auto chunks = [tr](int n) { return formatLanguageString(tr("options.chunks"), {std::to_string(n)}); };
    const auto offOr = [tr](int n) { return n == 0 ? tr("options.off") : std::to_string(n); };

    // ---- Video: the window and the frame rate ----
    b.range(0, 0, tr("options.framerateLimit"), "How many frames a second the game draws at most.", Impact::None, 1, 26,
            [](const GameOptions& g) { return g.framerateLimit / 10; }, [](GameOptions& g, int i) { g.framerateLimit = i * 10; },
            [tr](int i) { return i * 10 >= 260 ? tr("options.framerateLimit.max") : formatLanguageString(tr("options.framerate"), {std::to_string(i * 10)}); }, false);
    b.toggle(0, 0, tr("options.vsync"), "Waits for the screen to refresh before showing a frame: no tearing, and the frame rate follows the monitor's.", Impact::None,
             &GameOptions::enableVsync, false);
    b.cycle(0, 0, tr("options.inactivityFpsLimit"),
            "Minimized: limits the frame rate only when the game window is minimized.\nAFK: limits it to 30 when the game is not getting any player input for more than a minute, "
            "and to 10 after 9 more minutes.",
            Impact::None, {tr("options.inactivityFpsLimit.minimized"), tr("options.inactivityFpsLimit.afk")}, &GameOptions::inactivityFpsLimit, false);
    {
        const int maxScale = services.maxGuiScale ? std::max(1, services.maxGuiScale()) : 4;
        std::vector<std::string> scales{tr("options.guiScale.auto")};
        for (int i = 1; i <= maxScale; ++i) scales.push_back(std::to_string(i));
        Option o;
        o.kind = Kind::Cycle;
        o.name = tr("options.guiScale");
        o.tooltip = "How big the menus and the HUD are. Auto picks the largest size the window allows.";
        o.values = std::move(scales);
        o.read = [maxScale](const GameOptions& g) { return std::min(g.guiScale, maxScale); };
        o.write = [](GameOptions& g, int v) { g.guiScale = v; };
        b.add(0, 0, std::move(o), false);
    }
    b.range(0, 0, tr("options.gamma"), "How bright the dark is. Moody is the game's own darkness, Bright lights up caves and nights.", Impact::None, 0, 100,
            [](const GameOptions& g) { return static_cast<int>(std::lround(g.gamma * 100.0)); }, [](GameOptions& g, int v) { g.gamma = v / 100.0; },
            [tr](int p) { return p == 0 ? tr("options.gamma.min") : p == 50 ? tr("options.gamma.default") : p == 100 ? tr("options.gamma.max") : std::to_string(p); }, false);

    b.toggle(0, 1, tr("options.fullscreen"), {}, Impact::None, &GameOptions::fullscreen, false);
    b.toggle(0, 1, tr("options.exclusiveFullscreen"),
             "Exclusive fullscreen might prevent some input methods from working, but adaptive sync (Nvidia G-Sync, AMD FreeSync) should work.\nOtherwise all input methods are "
             "expected to work, but adaptive sync will not.",
             Impact::None, &GameOptions::exclusiveFullscreen, false);
    {
        const std::vector<std::string> modes = services.videoModes ? services.videoModes() : std::vector<std::string>();
        const int last = static_cast<int>(modes.size()) - 1;
        Option& mode = b.range(0, 1, tr("options.fullscreen.exclusive.mode"), "This setting only takes effect when Exclusive Fullscreen is enabled.", Impact::None, -1, std::max(-1, last),
                               [last](const GameOptions& g) { return std::min(g.exclusiveFullscreenMode, last); }, [](GameOptions& g, int v) { g.exclusiveFullscreenMode = v; },
                               [tr, modes](int v) { return modes.empty() ? tr("options.fullscreen.unavailable") : v < 0 ? tr("options.fullscreen.current") : modes[static_cast<std::size_t>(v)]; },
                               false);
        mode.enabled = [last](const GameOptions&) { return last >= 0; };
    }
    b.cycle(0, 1, tr("options.graphicsApi"),
            graphicsApiTooltip(GraphicsApi::OpenGL) + "\n" + graphicsApiTooltip(GraphicsApi::Vulkan) + "\n" + tr("options.graphicsApi.restart"), Impact::None,
            {tr(std::string(graphicsApiTranslationKey(GraphicsApi::OpenGL))), tr(std::string(graphicsApiTranslationKey(GraphicsApi::Vulkan)))}, &GameOptions::graphicsApi, false);

    // ---- Graphics: the preset, then what it sets ----
    {
        const auto presetName = [tr](const char* key, const char* fallback) {
            const std::string s = tr(key);
            return s.empty() || s[0] == 'o' ? std::string(fallback) : s;
        };
        Option o;
        o.kind = Kind::Cycle;
        o.name = "Graphics Preset";
        o.tooltip = "A preset sets distance, lighting, clouds, textures and LODs together. Change any of them below and it reads Custom.";
        o.values = {presetName("options.graphics.fast", "Fast"), presetName("options.graphics.fancy", "Fancy"), presetName("options.graphics.fabulous", "Fabulous!"), "Custom"};
        o.read = [](const GameOptions& g) { return static_cast<int>(g.graphicsPreset); };
        o.write = [](GameOptions& g, int v) { g.graphicsPreset = static_cast<GraphicsPreset>(v); };
        o.set = [](GameOptions& g, int v) {
            if (v < 3) VideoSettingsScreen::applyPreset(g, static_cast<GraphicsPreset>(v));
            else g.graphicsPreset = GraphicsPreset::Custom;
        };
        b.add(1, 0, std::move(o), false);
    }
    b.intRange(1, 1, tr("options.renderDistance"), "How far the world is drawn as real chunks. Every step costs a good deal of memory and processing.", Impact::High, 2, 32,
               &GameOptions::renderDistance, chunks, true);
    b.intRange(1, 1, tr("options.simulationDistance"), "How far from you things move and grow: mobs, crops, fire. Chunks beyond it are still drawn but stand still.", Impact::Medium, 5, 32,
               &GameOptions::simulationDistance, chunks, true);
    b.intRange(1, 1, tr("options.biomeBlendRadius"), "How softly the colours of grass, leaves and water change from biome to biome.", Impact::Low, 0, 7, &GameOptions::biomeBlendRadius,
               [tr](int n) { return tr("options.biomeBlendRadius." + std::to_string(2 * n + 1)); }, true);
    b.toggle(1, 2, tr("options.ao"), "Smooth lighting: the soft shade in corners and under overhangs.", Impact::Medium, &GameOptions::ambientOcclusion, true);
    b.cycle(1, 2, tr("options.renderClouds"), "Fast clouds are flat, fancy ones have a thickness.", Impact::Medium, {tr("options.off"), tr("options.clouds.fast"), tr("options.clouds.fancy")},
            &GameOptions::cloudStatus, true);
    b.intRange(1, 2, tr("options.renderCloudsDistance"), "How far away the clouds are drawn.", Impact::Medium, 2, 128, &GameOptions::cloudRange, chunks, true);
    b.cycle(1, 2, tr("options.particles"), "How many particles the world makes: smoke, rain splashes, breaking blocks.", Impact::Medium,
            {tr("options.particles.all"), tr("options.particles.decreased"), tr("options.particles.minimal")}, &GameOptions::particles, true);
    b.intRange(1, 2, tr("options.weatherRadius"), "Radius of the area where rain and snow effects are visible. Very low performance impact.", Impact::Low, 3, 10,
               &GameOptions::weatherRadius, [tr](int n) { return formatLanguageString(tr("options.blocks"), {std::to_string(n)}); }, true);
    b.toggle(1, 2, tr("options.entityShadows"), "Blobs of shadow under mobs and items.", Impact::Low, &GameOptions::entityShadows, true);
    b.range(1, 2, tr("options.entityDistanceScaling"), "How far away mobs and items are still drawn.", Impact::Medium, 2, 20,
            [](const GameOptions& g) { return static_cast<int>(std::lround(g.entityDistanceScaling * 4.0)); }, [](GameOptions& g, int i) { g.entityDistanceScaling = i / 4.0; },
            [](int i) { return std::to_string(i * 25) + "%"; }, true);

    b.intRange(1, 3, tr("options.mipmapLevels"), "How far textures shrink into smaller copies of themselves with distance: smoother far away, a little blurrier.", Impact::Low, 0, 4,
               &GameOptions::mipmapLevels, offOr, true);
    b.cycle(1, 3, tr("options.textureFiltering"),
            "None: textures are displayed without any filtering. Blocks may look blurry when viewed at an angle.\nRGSS (Rotated Grid Super Sampling): a shader-based filtering method "
            "that improves texture quality with a moderate performance impact.\nAnisotropic: a hardware-based filtering method, but impacts performance and significantly impacts "
            "video memory usage. May not be supported on all hardware.",
            Impact::Medium, {tr("options.textureFiltering.none"), tr("options.textureFiltering.rgss"), tr("options.textureFiltering.anisotropic")}, &GameOptions::textureFiltering, true);
    {
        Option& anisotropy = b.intRange(1, 3, tr("options.maxAnisotropy"),
                                        "Each level significantly improves how smooth textures look, but impacts performance and significantly impacts video memory usage. Requires "
                                        "Texture Filtering to be set to Anisotropic.",
                                        Impact::Medium, 1, 3, &GameOptions::maxAnisotropyBit, [](int n) { return std::to_string(1 << n) + "x"; }, true);
        anisotropy.enabled = [](const GameOptions& g) { return g.textureFiltering == TextureFiltering::Anisotropic; };
    }
    b.toggle(1, 3, tr("options.cutoutLeaves"), "Allows you to see through gaps in leaves. Disabling improves performance.", Impact::Medium, &GameOptions::cutoutLeaves, true);
    b.toggle(1, 3, tr("options.improvedTransparency"),
             "An experimental approach that uses an order-independent transparency algorithm to avoid graphical issues normally present when looking through multiple layers of "
             "translucent objects.\nThis will impact performance.",
             Impact::High, &GameOptions::improvedTransparency, true);
    b.intRange(1, 3, tr("options.accessibility.menu_background_blurriness"), tr("options.accessibility.menu_background_blurriness.tooltip"), Impact::Low, 0, 10,
               &GameOptions::menuBackgroundBlurriness, offOr, true);

    // ---- LODs: the terrain beyond the loaded chunks ----
    b.toggle(2, 0, "LODs",
             "Draws simplified terrain, as blocky columns with the block textures, beyond the loaded chunks, out to the LOD Distance. It is worked out from the world's seed: no chunks "
             "are generated for it. Only the surface is drawn, never caves.",
             Impact::High, &GameOptions::lodsEnabled, true);
    {
        const auto lodsOn = [](const GameOptions& g) { return g.lodsEnabled; };
        Option& radius = b.range(2, 0, "LOD Distance", "How far out LODs reach, 64 to 8192 chunks from you. The further, the more of the world is drawn and the more the LODs cost.",
                                 Impact::High, 0, 100, [](const GameOptions& g) { return static_cast<int>(std::lround(sliderFromLodChunks(g.lodChunkRadius) * 100.0)); },
                                 [](GameOptions& g, int v) { g.lodChunkRadius = lodChunksFromSlider(v / 100.0); }, [chunks](int v) { return chunks(lodChunksFromSlider(v / 100.0)); },
                                 true);
        radius.enabled = lodsOn;
        Option& strength = b.cycle(2, 0, "LOD View Strength",
                                   "Low Detail: big columns, the cheapest. For the longest distances and for slower computers.\nMedium Detail: a balance. Columns get smaller close to the "
                                   "loaded chunks.\nFull Detail: the finest columns (down to a block), the closest to the real terrain. Needs a fast processor to fill in.",
                                   Impact::High, {"Low Detail", "Medium Detail", "Full Detail"}, &GameOptions::lodDetail, true);
        strength.enabled = lodsOn;
    }

    // ---- Sky: what is drawn above the horizon ----
    b.toggle(3, 0, "Sky",
             "The sky itself: the gradient from the horizon to overhead, and the fog takes its colour. Off draws the world over the clear colour instead: nothing above the horizon, "
             "which is the least the game can draw.",
             Impact::Low, &GameOptions::skyEnabled, true);
    {
        const auto skyOn = [](const GameOptions& g) { return g.skyEnabled; };
        Option& sun = b.toggle(3, 0, "Sun", "The sun, and the glow it throws along the horizon at sunrise and sunset.", Impact::Low, &GameOptions::sunEnabled, true);
        Option& moon = b.toggle(3, 0, "Moon", "The moon, in the phase of the day it is.", Impact::Low, &GameOptions::moonEnabled, true);
        Option& stars = b.toggle(3, 0, "Stars", "The stars of the night sky.", Impact::Low, &GameOptions::starsEnabled, true);
        sun.enabled = skyOn;
        moon.enabled = skyOn;
        stars.enabled = skyOn;
    }

    // ---- Other ----
    b.toggle(4, 0, tr("options.autosaveIndicator"), {}, Impact::None, &GameOptions::showAutosaveIndicator, false);
    b.toggle(4, 0, tr("options.vignette"),
             "This is a subtle texture over the game screen used for reducing brightness towards the edges of the screen and warning about the world border.", Impact::None,
             &GameOptions::vignette, false);
    b.cycle(4, 0, tr("options.attackIndicator"), {}, Impact::None, {tr("options.off"), tr("options.attack.crosshair"), tr("options.attack.hotbar")}, &GameOptions::attackIndicator, false);
    b.range(4, 0, tr("options.chunkFade"), "How long in seconds chunks should fade in when they're first rendered, if at all.", Impact::None, 0, 40,
            [](const GameOptions& g) { return static_cast<int>(std::lround(g.chunkSectionFadeInTime * 20.0)); }, [](GameOptions& g, int i) { g.chunkSectionFadeInTime = i / 20.0; },
            [](int i) { return i <= 0 ? std::string("None") : fixed(i / 20.0, 2) + " s"; }, false);
}

// ---- drawing ----

// A triangle pointing left or right, as VulkanMod's arrows are (a column of fills for each step across it).
void arrow(GuiGraphics& g, float cx, float cy, float f, bool left, std::uint32_t color) {
    const float w = f - 1.0f;
    const int steps = std::max(1, static_cast<int>(std::lround(2.0f * w)));
    for (int i = 0; i < steps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
        const float half = f * (left ? t : 1.0f - t);
        const int x0 = static_cast<int>(std::floor(cx - w + static_cast<float>(i)));
        g.fill(x0, static_cast<int>(std::lround(cy - half)), x0 + 1, static_cast<int>(std::lround(cy + half)), color);
    }
}

void border(GuiGraphics& g, int x0, int y0, int x1, int y1, int width, std::uint32_t color) {
    g.fill(x0, y0, x1, y0 + width, color);
    g.fill(x0, y1 - width, x1, y1, color);
    g.fill(x0, y0 + width, x0 + width, y1 - width, color);
    g.fill(x1 - width, y0 + width, x1, y1 - width, color);
}

// Text centred on cx; one that does not fit slides back and forth (VulkanMod's drawScrollingString).
void scrollingText(GuiGraphics& g, const std::string& text, int cx, int y, int maxWidth, std::uint32_t color, double timeMs) {
    const int width = g.textWidth(text);
    if (width <= maxWidth) {
        g.text(text, static_cast<float>(cx - width / 2), static_cast<float>(y), color);
        return;
    }
    const int x0 = cx - maxWidth / 2, x1 = cx + maxWidth / 2;
    const double scrollAmount = width - maxWidth;
    const double seconds = timeMs / 1000.0;
    const double speed = std::max(scrollAmount * 0.5, 3.0);
    const double offset = std::sin((3.14159265358979 / 2.0) * std::cos(6.28318530717959 * seconds / speed)) / 2.0 + 0.5;
    g.pushScissor(x0 - 1, 0, x1, g.guiHeight());
    g.text(text, static_cast<float>(x0) - static_cast<float>(offset * scrollAmount), static_cast<float>(y), color);
    g.popScissor();
}

void ease(float& hover, bool on, float dtMs) {
    const float target = on ? 1.0f : 0.0f;
    hover += (target - hover) * std::min(1.0f, dtMs / 90.0f);
    if (std::abs(target - hover) < 0.01f) hover = target;
}

// The hover of a VulkanMod widget: a red wash and a red frame, both fading in.
void hovering(GuiGraphics& g, int x, int y, int w, int h, float hover) {
    if (hover <= 0.0f) return;
    g.fill(x, y, x + w, y + h, withAlpha(kRed, 0.3f * hover));
    border(g, x, y, x + w, y + h, 1, withAlpha(kRed, hover));
}

void drawButton(GuiGraphics& g, int x, int y, int w, int h, const std::string& label, bool active, bool selected, bool centered, float hover) {
    g.fill(x, y, x + w, y + h, withAlpha(kBlack, active ? 0.45f : 0.3f));
    if (selected) {
        g.fill(x, y, x + 2, y + h, withAlpha(kRed, 0.8f));
        g.fill(x, y, x + w, y + h, withAlpha(kRed, 0.2f));
    }
    if (active) hovering(g, x, y, w, h, hover);
    const std::uint32_t color = active ? kWhite : kGrey;
    const int ty = y + h / 2 - 4;
    if (centered) g.text(label, static_cast<float>(x + (w - g.textWidth(label)) / 2), static_cast<float>(ty), color);
    else g.text(label, static_cast<float>(x + 12), static_cast<float>(ty), color);
}

// Wraps `text` (with '\n' for new paragraphs) to `width`.
std::vector<std::string> wrap(const GuiGraphics& g, const std::string& text, int width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string paragraph = text.substr(start, end - start);
        std::string line;
        std::size_t i = 0;
        while (i < paragraph.size()) {
            std::size_t j = paragraph.find(' ', i);
            if (j == std::string::npos) j = paragraph.size();
            const std::string word = paragraph.substr(i, j - i);
            const std::string next = line.empty() ? word : line + " " + word;
            if (!line.empty() && g.textWidth(next) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = next;
            }
            i = j + 1;
        }
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

// The page that is a panel rather than a list of options.
constexpr int kDeveloperPage = 5;

}  // namespace

VideoSettingsScreen::VideoSettingsScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator), services_(services), model_(std::make_unique<VideoSettingsModel>()) {
    model_->pending = services.options;
    buildPages(*model_, services_, [this](const std::string& key) { return text(key); });
}

VideoSettingsScreen::~VideoSettingsScreen() = default;

void VideoSettingsScreen::applyPreset(GameOptions& o, GraphicsPreset preset) {
    if (preset == GraphicsPreset::Custom) return;
    struct Row {
        int biome, render;
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
    static constexpr Row kFast = {1, 8, 6, false, CloudStatus::Fast, ParticleStatus::Decreased, 2,
                                  false, 0.75, 2, 32, false, false, 5, 1, TextureFiltering::None, false, 64, kLodDetailLow};
    static constexpr Row kFancy = {2, 16, 12, true, CloudStatus::Fancy, ParticleStatus::All, 4,
                                   true, 1.0, 5, 64, true, false, 10, 1, TextureFiltering::Rgss, true, 128, kLodDetailMedium};
    static constexpr Row kFabulous = {2, 32, 12, true, CloudStatus::Fancy, ParticleStatus::All, 4,
                                      true, 1.25, 5, 128, true, true, 10, 2, TextureFiltering::Anisotropic, true, 256, kLodDetailFull};
    const Row& r = preset == GraphicsPreset::Fast ? kFast : preset == GraphicsPreset::Fancy ? kFancy : kFabulous;
    o.biomeBlendRadius = r.biome;
    o.renderDistance = r.render;
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

void VideoSettingsScreen::applyHighestFps(GameOptions& o) { fps_test::applyLowest(o); }

// ---- layout and actions ----

void VideoSettingsScreen::computeLayout() {
    Layout& l = layout_;
    const int leftMargin = kMargin + kPageWidth + 6;
    l.listX = leftMargin;
    l.listY = kListTop;
    l.listWidth = std::min(width_ - leftMargin - kMargin, 420);
    // (VulkanMod puts the box under the list when there is no room of 200 beside it; a list that can give up some width keeps it beside, where it can be read.)
    if (width_ - (leftMargin + l.listWidth + 6) - 10 < 200 && width_ - leftMargin - kMargin - 6 - 200 >= 300) l.listWidth = width_ - leftMargin - kMargin - 6 - 200;
    l.listHeight = std::max(40, height_ - kListTop - kListBottom);
    int x = leftMargin + l.listWidth + 6;
    int tipWidth = std::min(width_ - x - 10, 420);
    int y = kListTop + kWidgetHeight + 6;
    if (tipWidth < 200) {  // no room beside the list: under it (which takes some of the list's height, so that a few lines of text fit)
        l.listHeight = std::max(40, height_ - kListTop - kListBottom - 28);
        x = leftMargin + 3;
        tipWidth = l.listWidth;
        y = kListTop + l.listHeight + 10;
    }
    l.tipX = x;
    l.tipY = y;
    l.tipWidth = tipWidth;
    l.searchX = leftMargin;
    l.searchWidth = std::min(width_ - kMargin - leftMargin - 4, 413);
    const int padding = 10;
    l.doneWidth = font().width(text("gui.done")) + 2 * padding;
    l.applyWidth = font().width("Apply") + 2 * padding;
    l.undoWidth = font().width("Undo") + 2 * padding;
    l.doneX = width_ - l.doneWidth - kMargin;
    l.applyX = l.doneX - l.applyWidth - kWidgetMargin;
    l.undoX = l.applyX - l.undoWidth - kWidgetMargin;
    l.buttonY = height_ - kWidgetHeight - 7;
}

void VideoSettingsScreen::init() {
    computeLayout();
    // (An unframed box draws its text from its top edge: it is a line of text high, in the middle of the bar.)
    auto& box = addWidget<EditBox>(font(), layout_.searchX + 6, 4 + (kWidgetHeight - 8) / 2, std::max(20, layout_.searchWidth - 12), 10, "Search options (Ctrl+L)", 48);
    box.bordered = false;
    box.setText(model_->query);
    box.onChange = [this](const std::string& query) { searchChanged(query); };
    search_ = &box;
}

void VideoSettingsScreen::searchChanged(const std::string& query) {
    VideoSettingsModel& m = *model_;
    m.query = query;
    m.found.clear();
    m.scroll = 0.0f;
    if (query.empty()) return;
    const std::string term = lower(query);
    for (const auto& page : m.pages) {
        VideoSettingsModel::Block block;
        block.title = page.name;
        for (const auto& group : page.blocks) {
            for (const auto& option : group) {
                std::string hay = lower(option->name + " " + option->tooltip + " " + shown(*option, option->read(m.pending)));
                for (const std::string& value : option->values) hay += " " + lower(value);
                if (hay.find(term) != std::string::npos) block.options.push_back(option.get());
            }
        }
        if (!block.options.empty()) m.found.push_back(std::move(block));
    }
}

void VideoSettingsScreen::selectPage(int page) {
    VideoSettingsModel& m = *model_;
    m.page = page;
    m.scroll = 0.0f;
    m.query.clear();
    m.found.clear();
    if (search_ != nullptr) search_->setText("");
    setFocus(nullptr);
}

void VideoSettingsScreen::apply() {
    VideoSettingsModel& m = *model_;
    m.forEachOption([&](Option& o) {
        const int value = o.read(m.pending);
        if (value != o.read(services_.options)) o.write(services_.options, value);
    });
}

void VideoSettingsScreen::undo() {
    if (fps_test::stop(services_.options)) {  // the Highest FPS Test is taken back first: it is what the settings were before anything else
        developerNote_ = "Settings restored.";
        if (services_.session != nullptr) services_.session->resetFrameStats();
    }
    model_->pending = services_.options;
}

void VideoSettingsScreen::onClose() {
    if (model_->searching()) {  // Escape clears the search first
        if (search_ != nullptr) search_->setText("");
        searchChanged("");
        setFocus(nullptr);
        return;
    }
    navigator_.pop();
}

// ---- drawing the screen ----

void VideoSettingsScreen::drawDeveloper(GuiGraphics& g) {
    VideoSettingsModel& m = *model_;
    const Layout& l = layout_;
    const int left = l.listX + 4, right = l.listX + l.listWidth - 8;
    const int bottom = l.listY + l.listHeight;
    const int buttonsY = bottom - kWidgetHeight - 4;
    const int textBottom = buttonsY - 8;
    int y = l.listY + 4;
    const auto line = [&](const std::string& s, std::uint32_t colour) {
        if (y + 10 <= textBottom) g.text(s, static_cast<float>(left), static_cast<float>(y), colour);
        y += 10;
    };
    const auto heading = [&](const char* s) {
        y += 5;
        line(s, pack(1.0f, 0.62f, 0.5f, 1.0f));
    };
    const auto row = [&](const std::string& name, const std::string& value, std::uint32_t colour) {
        if (y + 10 <= textBottom) {
            g.text(name, static_cast<float>(left), static_cast<float>(y), colour);
            g.text(value, static_cast<float>(right - g.textWidth(value)), static_cast<float>(y), colour);
        }
        y += 10;
    };

    g.enableScissor(l.listX, l.listY, l.listX + l.listWidth, bottom);
    GameSession* session = services_.session;
    const bool lodsOn = services_.options.lodsEnabled;
    LodStats lod;
    if (session == nullptr) {
        line("No world is open: the counts begin when one is.", kGrey);
    } else {
        const FrameStats& frames = session->frameStats();
        WorldRenderer& world = session->renderer();
        lod = world.lodStats();
        heading("Frame");
        row("Average", fixed(frames.averageFps(), 1) + " fps (" + fixed(frames.averageMs(), 2) + " ms)", kWhite);
        row("1% low", fixed(frames.percentileFps(0.01), 1) + " fps (" + fixed(frames.percentileMs(0.01), 2) + " ms)", kWhite);
        row("0.1% low", fixed(frames.percentileFps(0.001), 1) + " fps (" + fixed(frames.percentileMs(0.001), 2) + " ms)", kWhite);
        row("Cost", "cpu " + fixed(session->cpuMillis(), 2) + " ms, gpu " + fixed(world.gpuMillis(), 2) + " ms", kWhite);
        row("Samples", std::to_string(frames.count()) + " frames", kGrey);
        heading("What the average frame is spent on");
        const FramePhaseTimes phases = frames.averagePhases();
        const int heaviest = frames.count() > 0 ? frames.heaviestPhase() : -1;
        const double total = std::max(frames.averageMs(), 1.0e-3);
        for (int p = 0; p < kFramePhaseCount; ++p) {
            const double ms = phases[static_cast<std::size_t>(p)];
            const int share = static_cast<int>(std::lround(100.0 * ms / total));
            row(kFramePhaseNames[p], fixed(ms, 2) + " ms  " + std::to_string(share) + "%" + (p == heaviest ? "  most" : ""), p == heaviest ? 0xFFFFD27Fu : kWhite);
        }
        heading("LODs");
        if (!lodsOn) {
            line("Off: no tile is made or drawn.", kGrey);
        } else {
            row("Tiles", std::to_string(lod.tilesDrawn) + " drawn of " + std::to_string(lod.tilesLoaded) + " loaded (" + std::to_string(lod.tilesBuilding) + " building)", kWhite);
            row("Cost", std::to_string(lod.levels) + " levels, " + fixed(static_cast<double>(lod.trianglesDrawn) / 1.0e6, 2) + " M triangles, " + fixed(lod.megabytes, 1) + " MB", kWhite);
            row("Main thread", fixed(lod.updateMs, 2) + " ms update, " + fixed(lod.drawMs, 2) + " ms draw", kWhite);
            row("Workers", fixed(lod.jobMs, 2) + " ms a tile job, " + std::to_string(lod.workers) + " threads", kWhite);
        }
        heading("Chunks");
        const RenderStats chunks = world.stats();
        row("Loaded", std::to_string(chunks.chunksLoaded) + " chunks, " + std::to_string(chunks.sectionsDrawn) + " of " + std::to_string(chunks.sectionsLoaded) + " sections drawn", kWhite);
        row("Jobs", std::to_string(chunks.jobsWaiting) + " waiting, " + std::to_string(chunks.jobsInFlight) + " in flight, " + std::to_string(chunks.drawCalls) + " draw calls", kWhite);
        const auto mib = [](std::size_t bytes) { return fixed(static_cast<double>(bytes) / 1048576.0, 1); };
        row("Memory", "process " + mib(processWorkingSetBytes()) + " MB, blocks " + mib(chunks.levelBytes) + " MB", kWhite);
        row("", "meshes " + mib(chunks.meshBytes) + " MB, pictures " + mib(chunks.textureBytes) + " MB" + (lodsOn ? ", LODs " + fixed(lod.megabytes, 1) + " MB" : ""), kWhite);
        heading("Settings");
        const GameOptions& o = services_.options;
        line(std::to_string(o.renderDistance) + " chunks, LODs " + (o.lodsEnabled ? std::to_string(o.lodChunkRadius) + " chunks" : std::string("off")) + ", clouds " +
                 (o.cloudStatus == CloudStatus::Off ? "off" : o.cloudStatus == CloudStatus::Fast ? "fast" : "fancy") + ", lighting " + (o.ambientOcclusion ? "smooth" : "flat"),
             kGrey);
        line(std::string("vsync ") + (o.enableVsync ? "on" : "off") + ", cap " + (o.framerateLimit >= 260 ? std::string("off") : std::to_string(o.framerateLimit) + " fps") + ", leaves " +
                 (o.cutoutLeaves ? "cutout" : "solid"),
             kGrey);
    }
    // The Highest FPS Test: its name and the button that starts it on one line (the button sits beside what it is named after), then what it does.
    y += 5;
    const int titleTop = y;
    if (titleTop + 10 <= textBottom) g.text("Highest FPS Test", static_cast<float>(left), static_cast<float>(titleTop), pack(1.0f, 0.62f, 0.5f, 1.0f));
    y = titleTop + kWidgetHeight;
    line("Every setting to its least (2 chunks, no LODs, no clouds, no sky, sun, moon or stars, flat lighting), every core on every job, and the frame rate in the corner instead of "
         "the F3 screen. Clicking that label, or Restore, puts every setting back exactly as it was.",
         kGrey);
    g.disableScissor();

    // The panel's own buttons: a fresh count, and what puts the settings back afterwards (the test's own sits on the title).
    m.devButtons.clear();
    const auto addButton = [&](const std::string& label, int action, bool enabled) {
        VideoSettingsModel::DevButton b;
        b.label = label;
        b.action = action;
        b.enabled = enabled;
        b.w = g.textWidth(label) + 16;
        b.h = kWidgetHeight;
        b.y = buttonsY;
        m.devButtons.push_back(b);
    };
    addButton("Reset counts", 0, session != nullptr);
    addButton("Restore", 2, fps_test::running());
    {
        VideoSettingsModel::DevButton test;
        test.label = "Test";
        test.action = 1;
        test.w = g.textWidth(test.label) + 16;
        test.h = kWidgetHeight;
        test.x = left + g.textWidth("Highest FPS Test") + 8;
        test.y = titleTop - (kWidgetHeight - 10) / 2;
        m.devButtons.push_back(test);
    }
    int bx = left;
    for (auto& b : m.devButtons) {
        if (b.action == 1) continue;  // (it is placed already)
        b.x = bx;
        bx += b.w + 6;
    }
    for (const auto& b : m.devButtons) drawButton(g, b.x, b.y, b.w, b.h, b.label, b.enabled, false, true, 0.0f);
    if (!developerNote_.empty()) g.text(developerNote_, static_cast<float>(left), static_cast<float>(buttonsY - 11), kGrey);

    // The "Are you sure?" panel of the test, in VulkanMod's style: the screen dimmed, a black panel with a red frame, the question, and OK and Cancel under it.
    m.confirmButtons.clear();
    if (!confirmHighestFps_) return;
    const int panelWidth = std::min(width_ - 40, 340), panelHeight = 128;
    const int px = (width_ - panelWidth) / 2, py = (height_ - panelHeight) / 2;
    g.fill(0, 0, width_, height_, withAlpha(kBlack, 0.55f));
    g.fill(px, py, px + panelWidth, py + panelHeight, withAlpha(kBlack, 0.9f));
    border(g, px, py, px + panelWidth, py + panelHeight, 1, kRed);
    g.fill(px, py, px + panelWidth, py + 20, withAlpha(kRed, 0.25f));
    g.text("Highest FPS Test", static_cast<float>(px + 10), static_cast<float>(py + 6), kWhite, false, false, true);
    int ty = py + 30;
    for (const std::string& row : wrap(g, "Are you sure? This will overwrite all your current settings.\nThey are put back, exactly as they are now, when the test is stopped.", panelWidth - 20)) {
        g.text(row, static_cast<float>(px + 10), static_cast<float>(ty), kWhite);
        ty += 10;
    }
    for (int i = 0; i < 2; ++i) {
        VideoSettingsModel::DevButton b;
        b.label = i == 0 ? "OK" : "Cancel";
        b.action = 3 + i;
        b.w = 64;
        b.h = kWidgetHeight;
        b.x = px + panelWidth / 2 - 68 + i * 72;
        b.y = py + panelHeight - kWidgetHeight - 10;
        m.confirmButtons.push_back(b);
    }
    for (const auto& b : m.confirmButtons) drawButton(g, b.x, b.y, b.w, b.h, b.label, true, false, true, 0.0f);
}

void VideoSettingsScreen::render(GuiGraphics& g, int mouseX, int mouseY, const FrameTime& time) {
    VideoSettingsModel& m = *model_;
    const Layout& l = layout_;
    const float dtMs = std::min(100.0f, time.deltaTicks * 50.0f);
    g.setTimeMillis(time.millis);
    ui().drawBackdrop(g, width_, height_, time.deltaTicks);

    // Which options differ from the real ones (Apply and Undo wake up, and a changed option's name is marked).
    bool modified = false;
    m.forEachOption([&](Option& o) { modified |= o.read(m.pending) != o.read(services_.options); });

    // ---- the pages ----
    g.fill(kMargin, 4, kMargin + kPageWidth, 4 + kIconHeight, withAlpha(kBlack, 0.6f));
    g.text("Falcon", static_cast<float>(kMargin + 8), static_cast<float>(4 + (kIconHeight - 8) / 2), kWhite, true, false, true);
    for (int i = 0; i < static_cast<int>(m.pages.size()); ++i) {
        const int y = 4 + kIconHeight + i * kWidgetHeight;
        const bool over = mouseX >= kMargin && mouseX < kMargin + kPageWidth && mouseY >= y && mouseY < y + kWidgetHeight;
        ease(m.buttonHover[static_cast<std::size_t>(i)], over, dtMs);
        drawButton(g, kMargin, y, kPageWidth, kWidgetHeight, m.pages[static_cast<std::size_t>(i)].name, true, !m.searching() && i == m.page, false, m.buttonHover[static_cast<std::size_t>(i)]);
    }

    // ---- the list (or the Developer page's own panel) ----
    Option* hovered = nullptr;
    const bool developer = !m.searching() && m.page == kDeveloperPage;
    if (developer) {
        m.rows.clear();  // (the panel's rows are not options: a click there is not a click on one)
        drawDeveloper(g);
    } else {
    const std::vector<VideoSettingsModel::Block> blocks = m.blocksOf();
    const int itemWidth = l.listWidth - 7;
    int length = 0;
    for (const auto& block : blocks) {
        if (!block.title.empty()) length += 9 + 8;
        length += static_cast<int>(block.options.size()) * (kWidgetHeight + kItemMargin) + 12;
    }
    m.totalLength = length;
    const int maxScroll = std::max(0, length - l.listHeight);
    m.scroll = std::clamp(m.scroll, 0.0f, static_cast<float>(maxScroll));
    m.rows.clear();
    g.enableScissor(l.listX, l.listY, l.listX + l.listWidth, l.listY + l.listHeight);
    int rowTop = l.listY - static_cast<int>(m.scroll);
    const bool listHot = mouseX >= l.listX && mouseX < l.listX + itemWidth && mouseY >= l.listY && mouseY < l.listY + l.listHeight;
    hovered = m.dragging;
    for (const auto& block : blocks) {
        if (!block.title.empty()) {
            g.text(block.title, static_cast<float>(l.listX + 8), static_cast<float>(rowTop + 4), kWhite, true, false, true);
            rowTop += 9 + 8;
        }
        for (Option* option : block.options) {
            const int y = rowTop;
            rowTop += kWidgetHeight + kItemMargin;
            if (y + kWidgetHeight < l.listY || y > l.listY + l.listHeight) continue;
            m.rows.push_back({option, y});
            const bool enabled = !option->enabled || option->enabled(m.pending);
            const bool over = listHot && mouseY >= y && mouseY < y + kWidgetHeight;
            if (over && hovered == nullptr) hovered = option;
            ease(option->hover, over && enabled, dtMs);
            const int controlWidth = std::min(static_cast<int>(static_cast<float>(itemWidth) * 0.5f) - 8, 120);
            const int controlX = l.listX + itemWidth - controlWidth - 8;
            const bool changed = option->read(m.pending) != option->read(services_.options);

            g.fill(l.listX, y, l.listX + itemWidth, y + kWidgetHeight, withAlpha(kBlack, 0.45f));
            hovering(g, l.listX, y, itemWidth, kWidgetHeight, option->hover);
            g.text(option->name, static_cast<float>(l.listX + 8), static_cast<float>(y + (kWidgetHeight - 8) / 2), !enabled ? 0xFFA0A0A0u : changed ? 0xFFFFD27Fu : kWhite);

            const int value = option->read(m.pending);
            const std::uint32_t textColor = enabled ? kWhite : 0xFFA0A0A0u;
            const bool controlHot = over && mouseX >= controlX;
            switch (option->kind) {
                case Kind::Switch: {
                    const int center = controlX + controlWidth / 2, half = 12;
                    const int x0 = center - half, y0 = y + 4, h = kWidgetHeight - 8;
                    if (value != 0) {
                        const int x1 = x0 + half + 2;
                        g.fill(x0 + 2, y0 + 2, x1 - 1, y0 + 2 + h - 4, pack(0.4f, 0.4f, 0.4f, 1.0f));
                        g.fill(x1, y0 + 2, x1 + half - 4, y0 + 2 + h - 4, pack(1.0f, 1.0f, 1.0f, 1.0f));
                    } else {
                        g.fill(x0 + 2, y0 + 2, x0 + 2 + half - 4, y0 + 2 + h - 4, pack(1.0f, 1.0f, 1.0f, 0.4f));
                    }
                    border(g, x0, y0, x0 + half * 2, y0 + h, 1, kGrey);
                    const int margin = std::max(g.textWidth(option->values[1]), g.textWidth(option->values[0])) / 3;
                    const int tx = center - static_cast<int>(static_cast<float>(half) * 1.5f) - 4 - margin;
                    const std::string& label = option->values[static_cast<std::size_t>(value != 0 ? 1 : 0)];
                    g.text(label, static_cast<float>(tx - g.textWidth(label) / 2), static_cast<float>(y + (kWidgetHeight - 8) / 2), textColor);
                    break;
                }
                case Kind::Cycle: {
                    const int count = static_cast<int>(option->values.size());
                    const int barMargin = 30, padding = 4;
                    const int barWidth = (controlWidth - 2 * barMargin - padding * count) / std::max(1, count);
                    if (barWidth > 0) {
                        for (int i = 0; i < count; ++i) {
                            const int x0 = controlX + barMargin + i * (barWidth + padding), y0 = y + kWidgetHeight - 5;
                            g.fill(x0, y0, x0 + barWidth, y0 + 1, i == value ? pack(1, 1, 1, 1.0f) : pack(1, 1, 1, 0.4f));
                        }
                    }
                    const bool canLeft = enabled && value > 0, canRight = enabled && value < count - 1;
                    const bool leftHot = controlHot && mouseX < controlX + 16, rightHot = over && mouseX >= controlX + controlWidth - 16;
                    scrollingText(g, shown(*option, value), controlX + controlWidth / 2, y + (kWidgetHeight - 9) / 2, controlWidth - 32 - 12, textColor, time.millis);
                    const auto arrowColor = [&](bool can, bool hot) { return can ? (hot ? pack(1, 1, 1, 1.0f) : pack(1, 1, 1, 0.8f)) : pack(0.3f, 0.3f, 0.3f, 0.8f); };
                    arrow(g, static_cast<float>(controlX) + 8.0f, static_cast<float>(y) + kWidgetHeight * 0.5f, leftHot && canLeft ? 5.0f : 4.5f, true, arrowColor(canLeft, leftHot));
                    arrow(g, static_cast<float>(controlX + controlWidth) - 8.0f, static_cast<float>(y) + kWidgetHeight * 0.5f, rightHot && canRight ? 5.0f : 4.5f, false,
                          arrowColor(canRight, rightHot));
                    break;
                }
                case Kind::Range: {
                    const float scaled = option->max > option->min ? static_cast<float>(value - option->min) / static_cast<float>(option->max - option->min) : 0.0f;
                    const int valueX = controlX + static_cast<int>(scaled * static_cast<float>(controlWidth));
                    if ((controlHot || m.dragging == option) && enabled) {
                        const int y0 = y + kWidgetHeight / 2 - 1, y1 = y0 + 2;
                        g.fill(controlX, y0, controlX + controlWidth, y1, pack(1, 1, 1, 0.1f));
                        g.fill(controlX, y0, valueX - 2, y1, pack(1, 1, 1, 0.3f));
                        border(g, valueX - 2, y0 - 4, valueX + 2, y1 + 4, 1, pack(1, 1, 1, 0.3f));
                    } else {
                        const int y0 = y + kWidgetHeight - 5;
                        g.fill(controlX, y0, controlX + controlWidth, y0 + 1, pack(1, 1, 1, 0.3f));
                        g.fill(controlX, y0, valueX, y0 + 1, pack(1, 1, 1, enabled ? 0.8f : 0.3f));
                    }
                    const std::string label = shown(*option, value);
                    g.text(label, static_cast<float>(controlX + controlWidth / 2 - g.textWidth(label) / 2), static_cast<float>(y + (kWidgetHeight - 9) / 2), textColor);
                    break;
                }
            }
        }
        rowTop += 12;
    }
    g.disableScissor();
    if (blocks.empty() || (m.searching() && m.found.empty())) {
        g.text(m.searching() ? "No option matches that." : "Nothing here.", static_cast<float>(l.listX + 8), static_cast<float>(l.listY + 8), kGrey);
    }
    if (maxScroll > 0) {  // the scroll bar
        const int bar = std::clamp(l.listHeight * l.listHeight / std::max(1, length), 32, l.listHeight - 8);
        const int barY = std::max(l.listY, static_cast<int>(m.scroll) * (l.listHeight - bar) / maxScroll + l.listY);
        const int x = l.listX + l.listWidth;
        g.fill(x, l.listY, x + 3, l.listY + l.listHeight, pack(0.8f, 0.8f, 0.8f, 0.2f));
        g.fill(x, barY, x + 3, barY + bar, pack(0.3f, 0.0f, 0.0f, 0.6f));
    }

    }

    // ---- the search bar and the buttons ----
    {
        const bool focused = search_ != nullptr && search_->focused;
        g.fill(l.searchX, 4, l.searchX + l.searchWidth, 4 + kWidgetHeight, withAlpha(kBlack, 0.45f));
        if (focused) border(g, l.searchX, 4, l.searchX + l.searchWidth, 4 + kWidgetHeight, 1, withAlpha(kRed, 1.0f));
        else if (mouseX >= l.searchX && mouseX < l.searchX + l.searchWidth && mouseY >= 4 && mouseY < 4 + kWidgetHeight) border(g, l.searchX, 4, l.searchX + l.searchWidth, 4 + kWidgetHeight, 1, withAlpha(kRed, 0.6f));
    }
    struct Action {
        const char* label;
        int x, width;
        bool active, visible;
    };
    const std::array<Action, 3> actions = {{{"Undo", l.undoX, l.undoWidth, true, modified},
                                            {"Apply", l.applyX, l.applyWidth, modified, true},
                                            {nullptr, l.doneX, l.doneWidth, true, true}}};
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const Action& a = actions[i];
        if (!a.visible) continue;
        const bool over = mouseX >= a.x && mouseX < a.x + a.width && mouseY >= l.buttonY && mouseY < l.buttonY + kWidgetHeight;
        ease(m.actionHover[i], over && a.active, dtMs);
        drawButton(g, a.x, l.buttonY, a.width, kWidgetHeight, a.label != nullptr ? std::string(a.label) : text("gui.done"), a.active, false, true, m.actionHover[i]);
    }

    Screen::render(g, mouseX, mouseY, time);  // (the search box's text)

    // ---- what the hovered option does ----
    if (hovered != nullptr) {
        const std::vector<std::string> lines = wrap(g, hovered->tooltip, l.tipWidth);
        const bool tipText = !hovered->tooltip.empty();
        const bool impact = hovered->impact != Impact::None;
        if (tipText || impact) {
            const std::string impactWord = hovered->impact == Impact::Low ? "Low" : hovered->impact == Impact::Medium ? "Medium" : "High";
            const std::uint32_t impactColor = hovered->impact == Impact::Low ? 0xFF00AA00u : hovered->impact == Impact::Medium ? 0xFFFFFF55u : 0xFFFF5555u;
            const std::string impactLabel = "Performance Impact: ";
            int width = 0;
            if (tipText) {
                for (const std::string& line : lines) width = std::max(width, g.textWidth(line));
            }
            if (impact) width = std::max(width, g.textWidth(impactLabel + impactWord));
            // Under the list there is room for a few lines only (the buttons are below): the text is cut to what fits.
            const int room = std::max(1, (l.buttonY - 6 - l.tipY) / 10);
            const int reserved = impact ? 1 : 0;
            const int shownLines = tipText ? std::min(static_cast<int>(lines.size()), std::max(1, room - reserved)) : 0;
            const bool cut = tipText && shownLines < static_cast<int>(lines.size()) && l.tipY > l.listY + l.listHeight - 1;
            const int count = shownLines + reserved;
            const int x = l.tipX, y = l.tipY, pad = 3, height = count * 10;
            g.fill(x - pad, y - pad, x + width + pad, y + height + pad, pack(0.05f, 0.05f, 0.05f, 0.6f));
            border(g, x - pad, y - pad, x + width + pad, y + height + pad, 1, kRed);
            int ty = y;
            if (tipText) {
                for (int i = 0; i < shownLines; ++i) {
                    const bool last = cut && i == shownLines - 1;
                    g.text(last ? lines[static_cast<std::size_t>(i)] + " ..." : lines[static_cast<std::size_t>(i)], static_cast<float>(x), static_cast<float>(ty), kWhite);
                    ty += 10;
                }
            }
            if (impact) {
                g.text(impactLabel, static_cast<float>(x), static_cast<float>(ty), kWhite);
                g.text(impactWord, static_cast<float>(x + g.textWidth(impactLabel)), static_cast<float>(ty), impactColor);
            }
        }
    }
}

// ---- the mouse and the keys ----

bool VideoSettingsScreen::mouseClicked(double x, double y, int button) {
    VideoSettingsModel& m = *model_;
    const Layout& l = layout_;
    if (button != 0) return false;
    // The search bar: the text box inside it takes the click.
    if (x >= l.searchX && x < l.searchX + l.searchWidth && y >= 4 && y < 4 + kWidgetHeight && search_ != nullptr) {
        return Screen::mouseClicked(std::clamp(x, static_cast<double>(search_->x), static_cast<double>(search_->x + search_->width - 1)), y, button);
    }
    setFocus(nullptr);

    // The Highest FPS Test's confirmation panel takes every click while it is up (Cancel, or the panel's glass, closes it and changes nothing).
    if (confirmHighestFps_) {
        for (const VideoSettingsModel::DevButton& b : m.confirmButtons) {
            if (x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
            navigator_.playButtonClick();
            confirmHighestFps_ = false;
            if (b.action == 3) {  // OK: every option is overwritten now, and the label in the corner shows the frame rate until it is stopped
                fps_test::start(services_.options, services_.options);
                m.pending = services_.options;
                if (services_.session != nullptr) services_.session->resetFrameStats();
                developerNote_ = "Test running: click the frame rate in the corner, or Restore, to put the settings back.";
            }
            return true;
        }
        return true;
    }

    for (int i = 0; i < static_cast<int>(m.pages.size()); ++i) {
        const int py = 4 + kIconHeight + i * kWidgetHeight;
        if (x >= kMargin && x < kMargin + kPageWidth && y >= py && y < py + kWidgetHeight) {
            navigator_.playButtonClick();
            selectPage(i);
            return true;
        }
    }
    // The Developer page's panel: its own buttons, and nothing else in the list area.
    if (!m.searching() && m.page == kDeveloperPage) {
        for (const VideoSettingsModel::DevButton& b : m.devButtons) {
            if (!b.enabled || x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
            navigator_.playButtonClick();
            if (b.action == 0) {
                if (services_.session != nullptr) services_.session->resetFrameStats();
                developerNote_ = "Counts restarted.";
            } else if (b.action == 1) {
                confirmHighestFps_ = true;
                developerNote_.clear();
            } else if (fps_test::stop(services_.options)) {
                m.pending = services_.options;
                if (services_.session != nullptr) services_.session->resetFrameStats();
                developerNote_ = "Settings restored.";
            }
            return true;
        }
        if (x >= l.listX && x < l.listX + l.listWidth && y >= l.listY && y < l.listY + l.listHeight) return true;
    }
    bool modified = false;
    m.forEachOption([&](Option& o) { modified |= o.read(m.pending) != o.read(services_.options); });
    if (y >= l.buttonY && y < l.buttonY + kWidgetHeight) {
        if (x >= l.doneX && x < l.doneX + l.doneWidth) {
            navigator_.playButtonClick();
            navigator_.pop();  // (what was not applied is dropped, as in VulkanMod)
            return true;
        }
        if (modified && x >= l.applyX && x < l.applyX + l.applyWidth) {
            navigator_.playButtonClick();
            apply();
            return true;
        }
        if (modified && x >= l.undoX && x < l.undoX + l.undoWidth) {
            navigator_.playButtonClick();
            undo();
            return true;
        }
    }

    // The scroll bar.
    if (m.totalLength > l.listHeight && x >= l.listX + l.listWidth && x < l.listX + l.listWidth + 6 && y >= l.listY && y < l.listY + l.listHeight) {
        m.draggingScroll = true;
        mouseDragged(x, y);
        return true;
    }
    // The controls of the rows.
    const int itemWidth = l.listWidth - 7;
    if (x < l.listX || x >= l.listX + itemWidth || y < l.listY || y >= l.listY + l.listHeight) return false;
    for (const VideoSettingsModel::RowInfo& row : m.rows) {
        if (y < row.y || y >= row.y + kWidgetHeight) continue;
        Option& o = *row.option;
        if (o.enabled && !o.enabled(m.pending)) return true;
        const int controlWidth = std::min(static_cast<int>(static_cast<float>(itemWidth) * 0.5f) - 8, 120);
        const int controlX = l.listX + itemWidth - controlWidth - 8;
        if (x < controlX) return true;
        const int value = o.read(m.pending);
        navigator_.playButtonClick();
        switch (o.kind) {
            case Kind::Switch: o.set(m.pending, value != 0 ? 0 : 1); break;
            case Kind::Cycle: {
                const int count = static_cast<int>(o.values.size());
                if (x < controlX + 16) {
                    if (value > 0) o.set(m.pending, value - 1);
                } else if (x >= controlX + controlWidth - 16) {
                    if (value < count - 1) o.set(m.pending, value + 1);
                }
                break;
            }
            case Kind::Range:
                m.dragging = &o;
                mouseDragged(x, y);
                break;
        }
        return true;
    }
    return true;
}

void VideoSettingsScreen::mouseDragged(double x, double y) {
    VideoSettingsModel& m = *model_;
    const Layout& l = layout_;
    if (m.draggingScroll) {
        const int maxScroll = std::max(1, m.totalLength - l.listHeight);
        const int bar = std::clamp(l.listHeight * l.listHeight / std::max(1, m.totalLength), 32, l.listHeight - 8);
        const double t = (y - l.listY - bar / 2.0) / std::max(1.0, static_cast<double>(l.listHeight - bar));
        m.scroll = static_cast<float>(std::clamp(t, 0.0, 1.0) * maxScroll);
        return;
    }
    if (m.dragging != nullptr) {
        Option& o = *m.dragging;
        const int itemWidth = l.listWidth - 7;
        const int controlWidth = std::min(static_cast<int>(static_cast<float>(itemWidth) * 0.5f) - 8, 120);
        const int controlX = l.listX + itemWidth - controlWidth - 8;
        const double t = std::clamp((x - (controlX + 4)) / static_cast<double>(controlWidth - 8), 0.0, 1.0);
        const int value = o.min + static_cast<int>(std::lround(t * (o.max - o.min)));
        if (value != o.read(m.pending)) o.set(m.pending, value);
        return;
    }
    Screen::mouseDragged(x, y);  // (the search box selects text)
}

void VideoSettingsScreen::mouseReleased(double x, double y, int button) {
    model_->dragging = nullptr;
    model_->draggingScroll = false;
    Screen::mouseReleased(x, y, button);
}

bool VideoSettingsScreen::mouseScrolled(double x, double y, double dy) {
    VideoSettingsModel& m = *model_;
    const Layout& l = layout_;
    if (x < l.listX || x >= l.listX + l.listWidth + 6 || y < l.listY || y >= l.listY + l.listHeight) return Screen::mouseScrolled(x, y, dy);
    m.scroll = std::clamp(m.scroll - static_cast<float>(dy) * static_cast<float>(kWidgetHeight + kItemMargin) / 2.0f, 0.0f, static_cast<float>(std::max(0, m.totalLength - l.listHeight)));
    return true;
}

bool VideoSettingsScreen::keyPressed(int key, int mods) {
    if ((mods & kEditControl) != 0 && key == 'L' && search_ != nullptr) {  // Ctrl+L: to the search
        setFocus(search_);
        return true;
    }
    return Screen::keyPressed(key, mods);
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
