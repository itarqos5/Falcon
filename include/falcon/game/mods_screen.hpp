#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/mods/mod_info.hpp"
#include "falcon/mods/optimization_mods.hpp"
#include "falcon/ui/scroll_list.hpp"
#include "falcon/ui/screen.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace falcon {

// Where everything on the Mods screen sits for a GUI-scaled window size: Mod Menu's own layout (its ModsScreen.init, read from the 26.3 build): two panes, the left a little under half the
// width, with the title and the search box above a list of mods, the right with the selected mod's icon, name, version and authors, the Website and Issues buttons, and its description below;
// Open Mods Folder and Done along the bottom.
struct ModsLayout {
    int paneWidth = 0;   // width / 2 - 8
    int rightPaneX = 0;  // width - paneWidth
    int titleX = 0, titleY = 8;
    Rect search;         // the search box (20 high, 22 from the top, centred over the list)
    Rect list;           // the mods: from 48 down to 36 above the bottom
    int headerX = 0;     // the icon's left edge on the right pane; its text starts 36 further
    int headerY = 48;
    Rect website, issues;  // the two buttons under the header (84 from the top)
    Rect descriptions;     // from 108 down to the bottom of the list
    Rect folder, done;     // 150 x 20, 28 above the bottom, either side of the middle
};

[[nodiscard]] ModsLayout computeModsLayout(int width, int height);

// "Mods": the mods in Falcon's mods folder (the one the launcher's Mods tab installs into) as Mod Menu shows them: a searchable list on the left (icon, name, badges, the first lines of the
// description) and on the right what the selected mod says about itself (see mods/mod_info.hpp): its icon, name, version, authors, a description, links, credits, licence and the other
// details. For now they are only listed: Falcon does not load them. An optimization mod is marked as not compatible (see mods/optimization_mods.hpp).
class ModsScreen : public Screen {
public:
    ModsScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

    struct Listed {
        ModInfo info;
        OptimizationVerdict verdict;  // not compatible, and why
    };
    [[nodiscard]] const std::vector<Listed>& mods() const { return mods_; }
    // The mod's icon as a texture (made on first use); the "unknown pack" picture when it has none.
    [[nodiscard]] const Texture2D& iconTexture(std::size_t index, GuiGraphics& graphics);
    [[nodiscard]] ScreenServices& services() { return services_; }

    // The small boxes after a mod's name ("Client", "Not compatible" ...): the text and its two colours.
    struct Badge {
        std::string text;
        std::uint32_t outline, fill;
    };
    [[nodiscard]] static std::vector<Badge> badgesOf(const Listed& mod);
    // The lines under "Details" on the right: what is not shown elsewhere (empty values left out). Public for the tests.
    [[nodiscard]] static std::vector<std::pair<std::string, std::string>> details(const ModInfo& info);
    // Whether a mod matches the search text (already lower case): its name, id, description, authors and loader.
    [[nodiscard]] static bool matches(const ModInfo& info, const std::string& lowerCaseText);

protected:
    void init() override;

private:
    void populate();
    void showDetails();

    ScreenServices& services_;
    std::vector<Listed> mods_;
    std::vector<std::unique_ptr<Texture2D>> icons_;  // by mod; made when first drawn
    ModsLayout layout_;
    ScrollList* list_ = nullptr;
    ScrollList* descriptions_ = nullptr;
    Button* website_ = nullptr;
    Button* issues_ = nullptr;
    std::vector<std::size_t> shown_;  // the mods that match the search, in the list's order
    std::string filter_;              // the search text, lower case
    int selected_ = -1;               // a mod (an index into mods_), or -1
};

}  // namespace falcon
