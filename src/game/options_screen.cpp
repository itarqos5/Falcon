#include "apex/game/options_screen.hpp"

#include "apex/game/language_screen.hpp"
#include "apex/game/menu_background.hpp"
#include "apex/game/options_screens.hpp"
#include "apex/game/options_video_screens.hpp"
#include "apex/game/pack_selection_screen.hpp"
#include "apex/game/sound_options_screen.hpp"
#include "apex/util/text_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace apex {
namespace {

constexpr int kHeaderHeight = 61;
constexpr int kFooterHeight = 33;
constexpr int kContentMarginTop = 30;
constexpr int kButtonWidth = 150;
constexpr int kButtonHeight = 20;
constexpr int kHeaderSpacing = 8;
constexpr int kCellWidth = kButtonWidth + 4 + 4;  // 4px horizontal padding on each side
constexpr int kCellHeight = kButtonHeight + 4;    // 4px bottom padding
constexpr int kGridWidth = 2 * kCellWidth;
constexpr int kGridHeight = 5 * kCellHeight;
constexpr int kDoneWidth = 200;

// Math.round for the values that occur here (positive, .5 rounds up).
int javaRound(double v) { return static_cast<int>(std::floor(v + 0.5)); }

struct GridEntry {
    const char* key;
    const char* name;  // for the console note when pressed
};

// Row-major, two buttons per row.
constexpr GridEntry kGrid[10] = {
    {"options.skinCustomisation", "skin customization"},
    {"options.sounds", "music & sounds"},
    {"options.video", "video settings"},
    {"options.controls", "controls"},
    {"options.language", "language"},
    {"options.chat", "chat settings"},
    {"options.resourcepack", "resource packs"},
    {"options.accessibility", "accessibility settings"},
    {"options.telemetry", "telemetry data"},
    {"options.credits_and_attribution", "credits & attribution"},
};

}  // namespace

OptionsScreenLayout computeOptionsScreenLayout(int width, int height, int titleWidth) {
    OptionsScreenLayout l;

    // Header: a vertical layout (title, then FOV slider + Online... side by side), centred in
    // a frame that is `width` wide and 61 tall.
    const int headerWidth = std::max(titleWidth, kButtonWidth + kHeaderSpacing + kButtonWidth);
    const int headerHeight = Font::kLineHeight + kHeaderSpacing + kButtonHeight;
    const int headerX = (width - headerWidth) / 2;  // truncates toward zero, like the game
    const int headerY = javaRound((kHeaderHeight - headerHeight) / 2.0);
    l.title = {headerX + (headerWidth - titleWidth) / 2, headerY, titleWidth, Font::kLineHeight};
    const int rowY = headerY + Font::kLineHeight + kHeaderSpacing;
    l.fov = {headerX, rowY, kButtonWidth, kButtonHeight};
    l.online = {headerX + kButtonWidth + kHeaderSpacing, rowY, kButtonWidth, kButtonHeight};

    // Contents: the grid sits 30px below the header, or higher if the footer would overlap it.
    const int gridX = (width - kGridWidth) / 2;
    const int gridY = std::min(kHeaderHeight + kContentMarginTop, height - kFooterHeight - kGridHeight);
    for (int i = 0; i < 10; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        l.grid[static_cast<std::size_t>(i)] = {gridX + kCellWidth * col + 4, gridY + kCellHeight * row,
                                               kButtonWidth, kButtonHeight};
    }

    // Footer: Done centred in the 33px bar; (33 - 20) / 2 = 6.5 rounds up to 7.
    l.done = {(width - kDoneWidth) / 2,
              height - kFooterHeight + javaRound((kFooterHeight - kButtonHeight) / 2.0), kDoneWidth,
              kButtonHeight};
    return l;
}

double fovToSliderValue(int fov) {
    if (fov <= kFovMin) return 0.0;
    if (fov >= kFovMax) return 1.0;
    return (static_cast<double>(fov) + 0.5 - kFovMin) / static_cast<double>(kFovMax + 1 - kFovMin);
}

int sliderValueToFov(double sliderValue) {
    if (sliderValue >= 1.0) sliderValue = 0.99998998;
    return static_cast<int>(std::floor(kFovMin + sliderValue * (kFovMax + 1 - kFovMin)));
}

OptionsScreen::OptionsScreen(ScreenNavigator& navigator, ScreenServices& services)
    : Screen(navigator),
      services_(services),
      language_(services.language),
      font_(services.font),
      options_(services.options) {}

std::string OptionsScreen::fovLabel(int fov) const {
    std::string value;
    if (fov == 70) {
        value = language_.get("options.fov.min");  // "Normal"
    } else if (fov == kFovMax) {
        value = language_.get("options.fov.max");  // "Quake Pro"
    } else {
        value = std::to_string(fov);
    }
    return formatLanguageString(language_.get("options.generic_value"),
                                {language_.get("options.fov"), value});
}

void OptionsScreen::init() {
    const auto stub = [](const char* name) {
        return [name] {
            std::printf("[falcon] options: '%s' pressed (not implemented yet)\n", name);
            std::fflush(stdout);
        };
    };

    const std::string title = text("options.title");
    const OptionsScreenLayout l = computeOptionsScreenLayout(width_, height_, font().width(title));

    addLabel(title, l.title.x, l.title.y);
    addSlider(l.fov, fovToSliderValue(options_.fov), [this](double value) { return fovLabel(sliderValueToFov(value)); },
              [this](double value) { options_.fov = sliderValueToFov(value); });
    addButton(text("options.online"), l.online, openScreen<OnlineOptionsScreen>(navigator_, services_));

    // What each button of the 2x5 grid opens; a key that is not listed has no screen yet.
    const std::unordered_map<std::string_view, std::function<void()>> opens = {
        {"options.skinCustomisation", openScreen<SkinCustomizationScreen>(navigator_, services_)},
        {"options.sounds", openScreen<SoundOptionsScreen>(navigator_, services_)},
        {"options.video", openScreen<VideoSettingsScreen>(navigator_, services_)},
        {"options.controls", openScreen<ControlsScreen>(navigator_, services_)},
        {"options.language", openScreen<LanguageSelectScreen>(navigator_, services_)},
        {"options.chat", openScreen<ChatOptionsScreen>(navigator_, services_)},
        {"options.resourcepack", openScreen<PackSelectionScreen>(navigator_, services_)},
        {"options.accessibility", openScreen<AccessibilityOptionsScreen>(navigator_, services_)},
    };
    for (std::size_t i = 0; i < l.grid.size(); ++i) {
        const std::string_view key = kGrid[i].key;
        if (key == "options.telemetry" || key == "options.credits_and_attribution") {
            addLockedButton(text(kGrid[i].key), l.grid[i]);  // shown, with the padlock, but not usable
            continue;
        }
        const auto opener = opens.find(key);
        addButton(text(kGrid[i].key), l.grid[i], opener != opens.end() ? opener->second : stub(kGrid[i].name));
    }

    // Done / Escape: back to the screen this was opened from.
    addButton(text("gui.done"), l.done, [this] { onClose(); });
}

void OptionsScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace apex
