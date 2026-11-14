#pragma once

#include "apex/game/screen_services.hpp"
#include "apex/ui/rect.hpp"
#include "apex/ui/screen.hpp"
#include "apex/world/new_world.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace apex {

// CreateWorldScreen's geometry (GUI units): a 24 tall tab bar on top, a 33 tall footer with the
// Create New World and Cancel buttons, and the tab contents in between.
struct CreateWorldLayout {
    int tabX = 0;      // the three tabs sit side by side from here, at y 0
    int tabWidth = 0;  // each is this wide (always even) and kTabBarHeight tall
    Rect create;
    Rect cancel;
    int footerSeparatorY = 0;  // two rows: dark, then light
    Rect tabArea;              // where the selected tab's contents are placed
};

inline constexpr int kCreateWorldTabBarHeight = 24;
inline constexpr int kCreateWorldFooterHeight = 33;

[[nodiscard]] CreateWorldLayout computeCreateWorldLayout(int width, int height);
// Where a tab's block of widgets, `gridWidth` x `gridHeight`, starts: centred sideways, with a sixth
// of the free height above it (GridLayoutTab.doLayout).
[[nodiscard]] Rect createWorldTabGrid(const CreateWorldLayout& layout, int gridWidth, int gridHeight);

// The Create New World screen: the Game, World and More tabs, a Create New World button that saves
// the world into <game dir>/saves, and Cancel.
//
// What exists: every option of the Game tab and the World tab except the editors behind Customize,
// and the Alt-key "Debug Mode" world type. Game Rules, Experiments and Data Packs (the More tab) are
// shown locked. What "Create" does is write the world's folder (level.dat + session.lock): there is no
// world generation and no way to play a world yet.
class CreateWorldScreen : public Screen {
public:
    // `onCreated` is called with the new world's folder name after the screen has closed itself.
    using Created = std::function<void(const std::string& folder)>;
    CreateWorldScreen(ScreenNavigator& navigator, ScreenServices& services, Created onCreated = nullptr);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool keyPressed(int key, int mods) override;

protected:
    void init() override;

private:
    enum Tab { kGameTab = 0, kWorldTab = 1, kMoreTab = 2, kTabCount = 3 };

    void buildContent();  // the selected tab's widgets
    void buildGameTab(const Rect& grid);
    void buildWorldTab(const Rect& grid);
    void buildMoreTab(const Rect& grid);
    void requestTab(int tab, bool sound);  // applied at the start of the next frame
    void refresh();                        // settings_ -> widgets (labels, locks, tooltips)
    void create();

    CycleButton& addCycle(const Rect& rect, int count, int index, std::function<std::string(int)> label,
                          std::function<void(int)> onChange);
    // An ON/OFF button; with `nameKey` the label is "<name>: ON", without it just "ON".
    CycleButton& addToggle(const Rect& rect, bool on, std::function<void(bool)> onChange, const char* nameKey = nullptr);
    [[nodiscard]] std::string optionLabel(const std::string& nameKey, const std::string& value) const;
    [[nodiscard]] std::filesystem::path savesDir() const { return services_.gameDir / "saves"; }
    [[nodiscard]] std::string targetFolder() const;

    ScreenServices& services_;
    Created onCreated_;
    NewWorldSettings settings_;
    CreateWorldLayout layout_;
    int tab_ = kGameTab;
    int pendingTab_ = -1;
    std::size_t contentStart_ = 0;  // widgets_ before this index are the tab bar and the footer buttons
    bool focusName_ = true;  // the name box gets the keyboard focus when the Game tab is built

    EditBox* nameEdit_ = nullptr;
    CycleButton* modeButton_ = nullptr;
    CycleButton* difficultyButton_ = nullptr;
    CycleButton* cheatsButton_ = nullptr;
    CycleButton* typeButton_ = nullptr;
    Button* customizeButton_ = nullptr;        // plain and grey: the world type has no editor
    LockedButton* customizeLocked_ = nullptr;  // padlocked: it has one in the game, but not here
    CycleButton* structuresButton_ = nullptr;
    CycleButton* bonusChestButton_ = nullptr;
};

}  // namespace apex
