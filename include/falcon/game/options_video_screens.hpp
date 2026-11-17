#pragma once

#include "falcon/game/game_options_screen.hpp"

#include <memory>

namespace falcon {

struct VideoSettingsModel;  // the pages and options of the screen below (options_video_screens.cpp)

// "Video Settings", laid out like VulkanMod's settings screen: a column of pages down the left (Video, Graphics, LODs, Other), a search bar along the top, the options of the open
// page in a list (the name on the left, its control on the right: a switch, a row of values with arrows, or a slider), what the hovered option does in a box beside the list, and
// Undo, Apply and Done at the bottom right. A change waits in a copy of the options until Apply (Undo takes it back, Done leaves without it), the options a change touched are
// marked, and the Graphics page starts with the preset (Fast, Fancy or Fabulous!) that sets the quality options together.
class VideoSettingsScreen : public Screen {
public:
    VideoSettingsScreen(ScreenNavigator& navigator, ScreenServices& services);
    ~VideoSettingsScreen() override;

    // Sets the options a graphics preset controls (Fast, Fancy or Fabulous!), the LODs among them.
    static void applyPreset(GameOptions& options, GraphicsPreset preset);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseClicked(double x, double y, int button) override;
    void mouseReleased(double x, double y, int button) override;
    void mouseDragged(double x, double y) override;
    bool mouseScrolled(double x, double y, double dy) override;
    bool keyPressed(int key, int mods) override;
    void onClose() override;  // Escape: the search first, then the screen

protected:
    void init() override;

private:
    struct Layout {
        int listX = 0, listY = 0, listWidth = 0, listHeight = 0;
        int tipX = 0, tipY = 0, tipWidth = 0;
        int searchX = 0, searchWidth = 0;
        int doneX = 0, applyX = 0, undoX = 0, buttonY = 0, doneWidth = 0, applyWidth = 0, undoWidth = 0;
    };
    void computeLayout();
    void apply();
    void undo();
    void selectPage(int page);
    void searchChanged(const std::string& query);

    ScreenServices& services_;
    std::unique_ptr<VideoSettingsModel> model_;
    Layout layout_;
    EditBox* search_ = nullptr;
};

// "Controls": Mouse Settings, Key Binds and the hold/toggle and movement options.
class ControlsScreen : public GameOptionsScreen {
public:
    ControlsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "controls.title") {}

protected:
    void addOptions() override;
};

// "Mouse Settings": sensitivity, scrolling, inversion and cursor shape.
class MouseSettingsScreen : public GameOptionsScreen {
public:
    MouseSettingsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.mouse_settings.title") {}

protected:
    void addOptions() override;
};

}  // namespace falcon
