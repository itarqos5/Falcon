#pragma once

#include "apex/game/game_options_screen.hpp"

namespace apex {

// "Skin Customization": the seven model-part toggles and Main Hand.
class SkinCustomizationScreen : public GameOptionsScreen {
public:
    SkinCustomizationScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.skinCustomisation.title") {}

protected:
    void addOptions() override;
};

// "Chat Settings": eighteen options in nine rows.
class ChatOptionsScreen : public GameOptionsScreen {
public:
    ChatOptionsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.chat.title") {}

protected:
    void addOptions() override;
};

// "Accessibility Settings": the narrator and Controls first, then the other accessibility options.
class AccessibilityOptionsScreen : public GameOptionsScreen {
public:
    AccessibilityOptionsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.accessibility.title") {}

protected:
    void addOptions() override;
    void addFooter(const ListScreenLayout& layout) override;  // "Accessibility Guide" next to Done
};

// "Online Options": friends list, servers and Realms settings.
class OnlineOptionsScreen : public GameOptionsScreen {
public:
    OnlineOptionsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.online.title") {}

protected:
    void addOptions() override;
};

}  // namespace apex
