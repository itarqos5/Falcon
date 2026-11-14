#include "apex/game/options_screens.hpp"

#include "apex/game/options_video_screens.hpp"
#include "apex/util/text_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace apex {
namespace {

std::string fixed(double v, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
    return buffer;
}

// Options that need a world, a signed-in account or a feature that does not exist here are shown
// but cannot be pressed; this is what the not-yet-built screens print.
void notImplemented(const char* what) {
    std::printf("[falcon] '%s' pressed (not implemented yet)\n", what);
    std::fflush(stdout);
}

}  // namespace

// ---- Skin Customization ---------------------------------------------------------------------

void SkinCustomizationScreen::addOptions() {
    static constexpr const char* kParts[7] = {"cape", "jacket", "left_sleeve", "right_sleeve",
                                              "left_pants_leg", "right_pants_leg", "hat"};
    std::vector<std::unique_ptr<Widget>> widgets;
    for (std::size_t i = 0; i < 7; ++i) {
        widgets.push_back(makeToggle(std::string("options.modelPart.") + kParts[i], services_.options.modelParts[i],
                                     kSmallWidth, [this, i](bool on) { services_.options.modelParts[i] = on; }));
    }
    widgets.push_back(makeChoice("options.mainHand", {"options.mainHand.left", "options.mainHand.right"},
                                 services_.options.mainHandRight ? 1 : 0, kSmallWidth,
                                 [this](int i) { services_.options.mainHandRight = i == 1; }));
    for (std::size_t i = 0; i < widgets.size(); i += 2) addPair(std::move(widgets[i]), std::move(widgets[i + 1]));
}

// ---- Chat Settings --------------------------------------------------------------------------

void ChatOptionsScreen::addOptions() {
    GameOptions& o = services_.options;

    addPair(makeChoice("options.chat.visibility",
                       {"options.chat.visibility.full", "options.chat.visibility.system", "options.chat.visibility.hidden"},
                       static_cast<int>(o.chatVisibility), kSmallWidth,
                       [this](int i) { services_.options.chatVisibility = static_cast<ChatVisibility>(i); }),
            optionToggle("options.chat.color", &GameOptions::chatColors));
    addPair(optionToggle("options.chat.links", &GameOptions::chatLinks),
            optionToggle("options.chat.links.prompt", &GameOptions::chatLinksPrompt));
    addPair(makeSlider(
                o.chatOpacity, kSmallWidth,
                [this](double v) { return percentLabel("options.chat.opacity", v * 0.9 + 0.1); },
                [this](double v) { services_.options.chatOpacity = v; }),
            percentSlider("options.accessibility.text_background_opacity", &GameOptions::textBackgroundOpacity));
    addPair(percentSlider("options.chat.scale", &GameOptions::chatScale, {}, /*zeroIsOff=*/true),
            percentSlider("options.chat.line_spacing", &GameOptions::chatLineSpacing));
    addPair(makeIntSlider(
                0, 60, static_cast<int>(std::lround(o.chatDelay * 10.0)), kSmallWidth,
                [this](int i) {
                    const double seconds = i / 10.0;
                    return seconds <= 0.0 ? text("options.chat.delay_none")
                                          : formatLanguageString(text("options.chat.delay"), {fixed(seconds, 1)});
                },
                [this](int i) { services_.options.chatDelay = i / 10.0; }),
            makeSlider(
                o.chatWidth, kSmallWidth,
                [this](double v) { return pixelLabel("options.chat.width", static_cast<int>(std::floor(v * 280.0 + 40.0))); },
                [this](double v) { services_.options.chatWidth = v; }));
    addPair(makeSlider(
                o.chatHeightFocused, kSmallWidth,
                [this](double v) { return pixelLabel("options.chat.height.focused", static_cast<int>(std::floor(v * 160.0 + 20.0))); },
                [this](double v) { services_.options.chatHeightFocused = v; }),
            makeSlider(
                o.chatHeightUnfocused, kSmallWidth,
                [this](double v) { return pixelLabel("options.chat.height.unfocused", static_cast<int>(std::floor(v * 160.0 + 20.0))); },
                [this](double v) { services_.options.chatHeightUnfocused = v; }));
    addPair(makeNarratorButton(), optionToggle("options.autoSuggestCommands", &GameOptions::autoSuggestions));
    addPair(optionToggle("options.hideMatchedNames", &GameOptions::hideMatchedNames, "options.hideMatchedNames.tooltip"),
            optionToggle("options.reducedDebugInfo", &GameOptions::reducedDebugInfo));
    addPair(optionToggle("options.onlyShowSecureChat", &GameOptions::onlyShowSecureChat, "options.onlyShowSecureChat.tooltip"),
            optionToggle("options.chat.drafts", &GameOptions::saveChatDrafts, "options.chat.drafts.tooltip"));
}

// ---- Accessibility Settings -----------------------------------------------------------------

void AccessibilityOptionsScreen::addFooter(const ListScreenLayout&) {
    // Two 150x20 buttons, 8 apart: the guide link and Done.
    const auto footer = layout::footerRow(width_, height_, 2, metrics::kButtonWidth);
    addButton(text("options.accessibility.link"), footer[0], [] { notImplemented("accessibility guide"); });
    addButton(text("gui.done"), footer[1], [this] { onClose(); });
}

void AccessibilityOptionsScreen::addOptions() {
    GameOptions& o = services_.options;

    addPair(makeNarratorButton(), makeLink("options.controls", kSmallWidth, [this] {
                return std::make_unique<ControlsScreen>(navigator_, services_);
            }));

    // High Contrast works by selecting the built-in High Contrast resource pack.
    const bool packAvailable = services_.packs.find("high_contrast") != nullptr;
    const auto packSelected = [this] {
        const auto& packs = services_.options.resourcePacks;
        return std::find(packs.begin(), packs.end(), "high_contrast") != packs.end();
    };
    auto highContrast = makeToggle("options.accessibility.high_contrast", packSelected(), kSmallWidth, [this](bool on) {
        auto& packs = services_.options.resourcePacks;
        const auto it = std::find(packs.begin(), packs.end(), "high_contrast");
        if (on && it == packs.end()) packs.push_back("high_contrast");
        if (!on && it != packs.end()) packs.erase(it);
        services_.options.highContrast = on;
        if (services_.packsChanged) services_.packsChanged();
    });
    highContrast->tooltip = text(packAvailable ? "options.accessibility.high_contrast.tooltip"
                                               : "options.accessibility.high_contrast.error.tooltip");
    highContrast->active = packAvailable;
    addPair(optionToggle("options.showSubtitles", &GameOptions::showSubtitles, "options.showSubtitles.tooltip"),
            std::move(highContrast));

    addPair(withTooltip(makeIntSlider(
                            0, 10, o.menuBackgroundBlurriness, kSmallWidth,
                            [this](int v) { return numberOrOffLabel("options.accessibility.menu_background_blurriness", v); },
                            [this](int v) { services_.options.menuBackgroundBlurriness = v; }),
                        "options.accessibility.menu_background_blurriness.tooltip"),
            percentSlider("options.accessibility.text_background_opacity", &GameOptions::textBackgroundOpacity));
    addPair(makeChoice("options.accessibility.text_background",
                       {"options.accessibility.text_background.chat", "options.accessibility.text_background.everywhere"},
                       o.backgroundForChatOnly ? 0 : 1, kSmallWidth,
                       [this](int i) { services_.options.backgroundForChatOnly = i == 0; }),
            makeSlider(
                o.chatOpacity, kSmallWidth,
                [this](double v) { return percentLabel("options.chat.opacity", v * 0.9 + 0.1); },
                [this](double v) { services_.options.chatOpacity = v; }));
    addPair(percentSlider("options.chat.line_spacing", &GameOptions::chatLineSpacing),
            makeIntSlider(
                0, 60, static_cast<int>(std::lround(o.chatDelay * 10.0)), kSmallWidth,
                [this](int i) {
                    const double seconds = i / 10.0;
                    return seconds <= 0.0 ? text("options.chat.delay_none")
                                          : formatLanguageString(text("options.chat.delay"), {fixed(seconds, 1)});
                },
                [this](int i) { services_.options.chatDelay = i / 10.0; }));
    addPair(withTooltip(makeIntSlider(
                            5, 100, static_cast<int>(std::lround(o.notificationDisplayTime * 10.0)), kSmallWidth,
                            [this](int i) {
                                return optionLabel("options.notifications.display_time",
                                                   formatLanguageString(text("options.multiplier"), {fixed(i / 10.0, 1)}));
                            },
                            [this](int i) { services_.options.notificationDisplayTime = i / 10.0; }),
                        "options.notifications.display_time.tooltip"),
            optionToggle("options.viewBobbing", &GameOptions::bobView));

    // The two effect strengths that feel linear are stored squared: the slider shows the square root.
    const auto squaredSlider = [this](const std::string& captionKey, double GameOptions::*field, const std::string& tip) {
        auto slider = makeSlider(
            std::sqrt(services_.options.*field), kSmallWidth,
            [this, captionKey](double s) { return percentOrOffLabel(captionKey, s * s); },
            [this, field](double s) { services_.options.*field = s * s; });
        slider->tooltip = text(tip);
        return slider;
    };
    addPair(percentSlider("options.screenEffectScale", &GameOptions::screenEffectScale, "options.screenEffectScale.tooltip", true),
            squaredSlider("options.fovEffectScale", &GameOptions::fovEffectScale, "options.fovEffectScale.tooltip"));
    addPair(squaredSlider("options.darknessEffectScale", &GameOptions::darknessEffectScale, "options.darknessEffectScale.tooltip"),
            percentSlider("options.damageTiltStrength", &GameOptions::damageTiltStrength, "options.damageTiltStrength.tooltip", true));
    addPair(percentSlider("options.glintSpeed", &GameOptions::glintSpeed, "options.glintSpeed.tooltip", true),
            percentSlider("options.glintStrength", &GameOptions::glintStrength, "options.glintStrength.tooltip", true));
    addPair(optionToggle("options.hideLightningFlashes", &GameOptions::hideLightningFlash, "options.hideLightningFlashes.tooltip"),
            optionToggle("options.darkMojangStudiosBackgroundColor", &GameOptions::darkMojangStudiosBackground,
                         "options.darkMojangStudiosBackgroundColor.tooltip"));
    addPair(percentSlider("options.accessibility.panorama_speed", &GameOptions::panoramaSpeed),
            optionToggle("options.hideSplashTexts", &GameOptions::hideSplashTexts, "options.hideSplashTexts.tooltip"));

    auto minecart = optionToggle("options.rotateWithMinecart", &GameOptions::rotateWithMinecart, "options.rotateWithMinecart.tooltip");
    minecart->active = false;  // only in worlds with the Minecart Improvements experiment
    addPair(optionToggle("options.accessibility.narrator_hotkey", &GameOptions::narratorHotkey,
                         "options.accessibility.narrator_hotkey.tooltip"),
            std::move(minecart));
    addPair(optionToggle("options.accessibility.high_contrast_block_outline", &GameOptions::highContrastBlockOutline,
                         "options.accessibility.high_contrast_block_outline.tooltip"));
}

// ---- Online Options -------------------------------------------------------------------------

void OnlineOptionsScreen::addOptions() {
    GameOptions& o = services_.options;

    // Friends need a signed-in account, so this section is shown but off and not usable.
    addHeader(text("options.online.friends.header"));
    auto friends = makeToggle("options.friendsList", false, kSmallWidth, nullptr);
    friends->active = false;
    auto requests = withTooltip(makeToggle("options.allowFriendRequests", false, kSmallWidth, nullptr),
                                "options.allowFriendRequests.tooltip");
    requests->active = false;
    addPair(std::move(friends), std::move(requests));

    auto notification = optionToggle("options.inGameNotification", &GameOptions::inGameNotification,
                                     "options.inGameNotification.tooltip");
    notification->active = false;
    static constexpr const char* kPresence[3] = {"none", "limited", "all"};  // cycle order: Hidden, Limited, Full
    auto presence = makeChoice("options.sharePresence",
                               {"options.sharePresence.none", "options.sharePresence.limited", "options.sharePresence.all"},
                               static_cast<int>(o.sharePresence), kSmallWidth, nullptr);
    CycleButton* presenceRaw = presence.get();
    const auto presenceTip = [this](int i) { return text(std::string("options.sharePresence.") + kPresence[i] + ".tooltip"); };
    presence->tooltip = presenceTip(static_cast<int>(o.sharePresence));
    presence->onChange = [this, presenceRaw, presenceTip](int i) {
        services_.options.sharePresence = static_cast<PresenceSharing>(i);
        presenceRaw->tooltip = presenceTip(i);
    };
    presence->active = false;
    addPair(std::move(notification), std::move(presence));
    addBig(makeLink("options.online.xboxSettings", kBigWidth, [] {
        notImplemented("xbox settings");
        return std::unique_ptr<Screen>();
    }));

    addHeader(text("options.online.servers.header"));
    addBig(optionToggle("options.allowServerListing", &GameOptions::allowServerListing, "options.allowServerListing.tooltip", kBigWidth));
    addHeader(text("options.online.realms.header"));
    addBig(optionToggle("options.realmsNotifications.button", &GameOptions::realmsNotifications,
                        "options.realmsNotifications.tooltip", kBigWidth));
}

}  // namespace apex
