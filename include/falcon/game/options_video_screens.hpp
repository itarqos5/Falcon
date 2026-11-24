#pragma once

#include "falcon/game/game_options_screen.hpp"

#include <memory>
#include <string>

namespace falcon {

struct VideoSettingsModel;  // the pages and options of the screen below (options_video_screens.cpp)

// "Video Settings", laid out like VulkanMod's settings screen: a column of pages down the left (Video, Graphics, LODs, Sky, Other, Developer), a search bar along the top, the options of the open
// page in a list (the name on the left, its control on the right: a switch, a row of values with arrows, or a slider), what the hovered option does in a box beside the list, and
// Undo, Apply and Done at the bottom right. A change waits in a copy of the options until Apply (Undo takes it back, Done leaves without it), the options a change touched are
// marked, and the Graphics page starts with the preset (Fast, Fancy or Fabulous!) that sets the quality options together.
class VideoSettingsScreen : public Screen {
public:
    VideoSettingsScreen(ScreenNavigator& navigator, ScreenServices& services);
    ~VideoSettingsScreen() override;

    // Sets the options a graphics preset controls (Fast, Fancy or Fabulous!), the LODs among them.
    static void applyPreset(GameOptions& options, GraphicsPreset preset);
    // Every option at what draws the highest frame rate and holds the least memory: the render distance and the simulation at their least, no LODs (their tiles are
    // the most memory the world holds), no clouds, no sky, sun, moon or stars, no smooth lighting, no mipmaps, no frame rate cap and no vsync, and the small touches
    // (shadows, the vignette, the chunk fade) off. What the Developer page's "Highest FPS Test" applies (falcon/game/fps_test.hpp).
    static void applyHighestFps(GameOptions& options);

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
    // The Developer page: how the last few hundred frames were spent (which task takes the most of an average frame, the average, the 1% and 0.1% lows, the LODs, the
    // chunks and the memory), and the buttons of the "Highest FPS Test" (which strips every setting to its least and measures again).
    void drawDeveloper(GuiGraphics& graphics);
    bool confirmHighestFps_ = false;  // the Highest FPS Test's "Are you sure?" panel is up (its buttons are made when it is drawn)
    std::string developerNote_;       // the panel's last word (what was applied, what was restored)

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
