#pragma once

#include "apex/gfx/texture.hpp"
#include "apex/packs/pack_selection_model.hpp"
#include "apex/ui/scroll_list.hpp"
#include "apex/ui/screen.hpp"
#include "apex/game/screen_services.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace apex {

// "Select Resource Packs": Available and Selected lists of packs, a search box, Open Pack Folder
// and Done. Moving packs only changes the selection; applying it to the game's resources is not
// wired up yet.
class PackSelectionScreen : public Screen {
public:
    PackSelectionScreen(ScreenNavigator& navigator, ScreenServices& services);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseClicked(double x, double y, int button) override;
    // Done / Escape: commit the selection, then go back.
    void onClose() override;

    // What the arrows on a pack row can do. Requests are applied between frames so a row is never
    // rebuilt in the middle of its own click handler.
    enum class Action { Select, Unselect, MoveUp, MoveDown };
    void request(Action action, const std::string& packId);

    [[nodiscard]] const PackSelectionModel& model() const { return *model_; }
    [[nodiscard]] ScreenServices& services() { return services_; }
    // The pack's icon as a texture (created on first use); the "unknown pack" icon if it has none.
    [[nodiscard]] const Texture2D& iconTexture(const PackInfo& pack, GuiGraphics& graphics);

protected:
    void init() override;

private:
    void applyPending();
    void populate();
    [[nodiscard]] bool matchesFilter(const PackInfo& pack) const;

    ScreenServices& services_;
    std::unique_ptr<PackSelectionModel> model_;
    std::unordered_map<std::string, Texture2D> icons_;

    ScrollList* availableList_ = nullptr;
    ScrollList* selectedList_ = nullptr;
    std::string filter_;  // lower-case search text
    bool needsPopulate_ = true;

    struct Pending {
        Action action = Action::Select;
        std::string id;
    };
    std::vector<Pending> pending_;
    std::string focusId_;  // the row to select after the lists are rebuilt
};

}  // namespace apex
