#include "falcon/game/title_screen.hpp"

#include "falcon/game/join_multiplayer_screen.hpp"
#include "falcon/game/language_screen.hpp"
#include "falcon/game/mods_screen.hpp"
#include "falcon/game/options_screen.hpp"
#include "falcon/game/options_screens.hpp"
#include "falcon/game/safety_screen.hpp"
#include "falcon/game/select_world_screen.hpp"
#include "falcon/util/math.hpp"

#include <cmath>
#include <cstdio>

namespace falcon {
namespace {

constexpr int kButtonHeight = 20;
constexpr int kRowSpacing = 24;

// Horizontal position of the `index`-th (1-based) of `count` equally sized buttons with a 4px
// gap, centred on the screen.
int horizontalPosition(int width, int index, int count, int size) {
    const int total = count * size + (count - 1) * 4;
    return width / 2 - total / 2 + (index - 1) * (size + 4);
}

}  // namespace

TitleScreenLayout computeTitleScreenLayout(int width, int height, int copyrightWidth) {
    TitleScreenLayout l;
    // The game starts its buttons at a quarter of the height and 48 below; with the Mods button there is a row more, and at the default window (240 units) the last row would end
    // below the bottom, so they start higher then, as far as the room under the logo allows (the footer line is 10 from the bottom, the logo and its badge end at 81).
    int y = std::max(std::min(height / 4 + 48, height - 150), 84);

    l.singleplayer = {width / 2 - 100, y, 200, kButtonHeight};
    y += kRowSpacing;
    l.multiplayer = {width / 2 - 100, y, 200, kButtonHeight};
    y += kRowSpacing;
    l.realms = {width / 2 - 100, y, 200, kButtonHeight};
    y += kRowSpacing;
    l.mods = {width / 2 - 100, y, 200, kButtonHeight};

    y += kRowSpacing;
    l.friends = {horizontalPosition(width, 1, 3, kButtonHeight), y, kButtonHeight, kButtonHeight};
    l.language = {horizontalPosition(width, 2, 3, kButtonHeight), y, kButtonHeight, kButtonHeight};
    l.accessibility = {horizontalPosition(width, 3, 3, kButtonHeight), y, kButtonHeight,
                       kButtonHeight};

    y += kRowSpacing;
    l.options = {width / 2 - 100, y, 98, kButtonHeight};
    l.quit = {width / 2 + 2, y, 98, kButtonHeight};

    l.copyrightX = width - copyrightWidth - 2;
    l.copyrightY = height - 10;
    return l;
}

TitleScreen::TitleScreen(ScreenNavigator& navigator, ScreenServices& services,
                         TitleScreenOptions options)
    : Screen(navigator),
      services_(services),
      language_(services.language),
      font_(services.font),
      panorama_(services.panorama),
      options_(std::move(options)),
      fading_(options_.fadeIn) {
    panorama_.startSpin();
}

void TitleScreen::init() {
    footerText_ = "not affiliated with Mojang";
    const TitleScreenLayout l = computeTitleScreenLayout(width_, height_, font().width(footerText_));
    footerX_ = l.copyrightX;
    footerY_ = l.copyrightY;

    // Singleplayer and Options open on top of the main menu (Done / Escape come back).
    addButton(text("menu.singleplayer"), l.singleplayer, openScreen<SelectWorldScreen>(navigator_, services_));
    // Multiplayer: the third-party warning first, unless the player turned it off; then the server list.
    addButton(text("menu.multiplayer"), l.multiplayer, [this] {
        if (services_.options.skipMultiplayerWarning) {
            navigator_.push(std::make_unique<JoinMultiplayerScreen>(navigator_, services_));
        } else {
            navigator_.push(std::make_unique<SafetyScreen>(navigator_, services_));
        }
    });
    // Minecraft Realms is a Mojang online service this client does not talk to: shown with the
    // padlock, like the other buttons that lead somewhere that does not exist here.
    addLockedButton(text("menu.online"), l.realms);
    // Mods: the mods in Falcon's mods folder, listed with what they say about themselves (they are not loaded yet).
    addButton("Mods", l.mods, openScreen<ModsScreen>(navigator_, services_));
    // The three icons under Minecraft Realms open the matching Options screens. The friends icon
    // opens Online Options, where the Friends List settings live.
    addIconButton("friends/friends", 15, l.friends, openScreen<OnlineOptionsScreen>(navigator_, services_));
    addIconButton("icon/language", 15, l.language, openScreen<LanguageSelectScreen>(navigator_, services_));
    addIconButton("icon/accessibility", 15, l.accessibility,
                  openScreen<AccessibilityOptionsScreen>(navigator_, services_));
    addButton(text("menu.options"), l.options, openScreen<OptionsScreen>(navigator_, services_));
    // Quit Game: Minecraft.stop() - leave the main loop and close the window.
    addButton(text("menu.quit"), l.quit, [this] { navigator_.quit(); });
}

void TitleScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    if (!fadeInStart_ && fading_) fadeInStart_ = time.millis;

    float alpha = 1.0f;
    if (fading_) {
        float t = static_cast<float>((time.millis - *fadeInStart_) / 2000.0);
        if (t > 1.0f) {
            fading_ = false;
        } else {
            t = clampf(t, 0.0f, 1.0f);
            alpha = clampedMap(t, 0.5f, 1.0f, 0.0f, 1.0f);
        }
        for (auto& widget : widgets_) widget->alpha = alpha;
    }

    panorama_.render(graphics, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);  // widgets
    renderLogo(graphics, alpha);
    if (options_.splash && !services_.options.hideSplashTexts) renderSplash(graphics, alpha, time.millis);

    graphics.text(options_.versionText, 2.0f, static_cast<float>(height_ - 10), argbWhite(alpha));
    graphics.text(footerText_, static_cast<float>(footerX_), static_cast<float>(footerY_), argbWhite(alpha));
}

void TitleScreen::renderLogo(GuiGraphics& graphics, float alpha) {
    const std::uint32_t color = argbWhite(alpha);
    const Texture2D& logo = graphics.textures().get(
        options_.easterEggLogo ? "gui/title/minceraft.png" : "gui/title/minecraft.png");
    // minecraft.png is 1024x256 but is addressed as a 256x64 texture, of which 256x44 is drawn.
    graphics.blit(logo, width_ / 2 - 128, 30, 0.0f, 0.0f, 256, 44, 256, 64, color);

    const Texture2D& edition = graphics.textures().get("gui/title/edition.png");
    graphics.blit(edition, width_ / 2 - 64, 30 + 44 - 7, 0.0f, 0.0f, 128, 14, 128, 16, color);
}

void TitleScreen::renderSplash(GuiGraphics& graphics, float alpha, double millis) {
    const std::string& splash = *options_.splash;
    const int textWidth = graphics.textWidth(splash);

    const auto ms = static_cast<long long>(millis) % 1000;
    float scale = 1.8f - std::abs(std::sin(static_cast<float>(ms) / 1000.0f * 6.2831855f) * 0.1f);
    scale = scale * 100.0f / static_cast<float>(textWidth + 32);

    graphics.pushPose();
    graphics.pose().translate(static_cast<float>(width_) / 2.0f + 123.0f, 69.0f)
        .rotate(-0.34906584f)
        .scale(scale);
    graphics.text(splash, static_cast<float>(-textWidth / 2), -8.0f,
                  argbWithAlpha(0x00FFFF00u, alpha));
    graphics.popPose();
}

}  // namespace falcon
