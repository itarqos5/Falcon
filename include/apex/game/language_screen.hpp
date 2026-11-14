#pragma once

#include "apex/game/game_options_screen.hpp"
#include "apex/ui/rect.hpp"
#include "apex/ui/screen.hpp"
#include "apex/game/screen_services.hpp"
#include "apex/ui/scroll_list.hpp"

#include <string>
#include <vector>

namespace apex {

struct LanguageScreenLayout {
    Rect title;
    Rect search;
    Rect list;
    Rect fontSettings;
    Rect done;
    int warningX = 0;  // the accuracy warning's text origin (centred over the buttons)
    int warningY = 0;
};

// LanguageSelectScreen's geometry: header 36 (title, a 200x15 search box), footer 53 (the
// accuracy warning above Font Settings... and Done), rows 18 high and 270 wide.
[[nodiscard]] LanguageScreenLayout computeLanguageScreenLayout(int width, int height, int titleWidth, int warningWidth);

// Whether `filter` matches a language: a case-insensitive part of its name or its region.
[[nodiscard]] bool languageMatchesFilter(const LanguageInfo& language, const std::string& filter);

// "Language": every language the game ships, by code, with a search box. Done (or a double click, or
// Enter) switches to the selected one: the translations are loaded in the background and swapped in
// without stalling. Escape leaves without changing anything.
class LanguageSelectScreen : public Screen {
public:
    LanguageSelectScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    void init() override;

private:
    void fillEntries(bool centreOnSelection);
    void done();
    [[nodiscard]] const LanguageInfo* selected() const;

    ScreenServices& services_;
    std::vector<const LanguageInfo*> shown_;  // the languages that pass the filter = list rows
    std::string filter_;
    std::string selectedCode_;                // empty = nothing selected
    ScrollList* list_ = nullptr;
};

// "Font Settings": Force Unicode Font and Japanese Glyph Variants.
class FontSettingsScreen : public GameOptionsScreen {
public:
    FontSettingsScreen(ScreenNavigator& navigator, ScreenServices& services)
        : GameOptionsScreen(navigator, services, "options.font.title") {}

protected:
    void addOptions() override;
};

}  // namespace apex
