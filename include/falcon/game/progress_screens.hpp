#pragma once

#include "falcon/game/game_options_screen.hpp"

#include <string>
#include <vector>

namespace falcon {

// The three screens behind the pause menu's buttons: Advancements (the trees of the game's advancements, a tab for each), Statistics (what the player has done: General, Items, Mobs)
// and World Options (the difficulty, the game mode, cheats and the game rules of the world being played).

// "Advancements": a window with a tab along the top for each root (Minecraft, Nether, The End, Adventure, Husbandry) and the tree of the open one inside, which is dragged round with
// the mouse; what is done has a lit frame, the rest a dark one, and a hovered one says what it is. Only the early-game ones can be done (see game/advancements.hpp).
class AdvancementsScreen : public Screen {
public:
    AdvancementsScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}
    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseClicked(double x, double y, int button) override;
    void mouseReleased(double x, double y, int button) override;
    void mouseDragged(double x, double y) override;

protected:
    void init() override;

private:
    static constexpr int kWindowWidth = 252, kWindowHeight = 140, kInsideX = 9, kInsideY = 18, kInsideWidth = 234, kInsideHeight = 113;
    void selectTab(int tab);

    ScreenServices& services_;
    int tab_ = 0;
    double scrollX_ = 0.0, scrollY_ = 0.0;
    bool centred_ = false;     // the tree was put in the middle of the window (once for each tab)
    bool dragging_ = false;
    double lastX_ = 0.0, lastY_ = 0.0;
};

// "Statistics": the general counters (time, distance, jumps, deaths ...), what was mined and picked up, and what was killed.
class StatisticsScreen : public Screen {
public:
    StatisticsScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}
    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseScrolled(double x, double y, double dy) override;

protected:
    void init() override;

private:
    struct Row {
        std::string name;
        std::vector<std::string> values;  // one column for General and Mobs, two for Items (mined, picked up)
    };
    [[nodiscard]] std::vector<Row> rows() const;
    ScreenServices& services_;
    int tab_ = 0;  // 0 General, 1 Items, 2 Mobs
    double scroll_ = 0.0;
};

// "World Options": difficulty, game mode, cheats and the game rules.
class WorldOptionsScreen : public GameOptionsScreen {
public:
    WorldOptionsScreen(ScreenNavigator& navigator, ScreenServices& services) : GameOptionsScreen(navigator, services, "World Options") {}  // (no key for the title: the text is the title)

protected:
    void addOptions() override;
};

// "Edit Game Rules": every game rule of the world, a switch for the ones that are on or off and a slider for the numbers.
class GameRulesScreen : public GameOptionsScreen {
public:
    GameRulesScreen(ScreenNavigator& navigator, ScreenServices& services) : GameOptionsScreen(navigator, services, "editGamerule.title") {}

protected:
    void addOptions() override;
};

}  // namespace falcon
