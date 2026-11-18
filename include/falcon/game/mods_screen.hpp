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
#include <vector>

namespace falcon {

// "Mods": the mods in Falcon's mods folder (the one the launcher's Mods tab installs into), a list on the left and on the right what the selected one says about itself: its icon, name,
// version, authors, description, licence and the other details of its metadata (see mods/mod_info.hpp). For now they are only listed: Falcon does not load them. An optimization mod is
// marked as not compatible (see mods/optimization_mods.hpp).
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

    // The detail lines of a mod, as (label, value) pairs: what the panel on the right shows under the description (empty values left out). Public for the tests.
    [[nodiscard]] static std::vector<std::pair<std::string, std::string>> facts(const ModInfo& info);

protected:
    void init() override;

private:
    void showDetails(int index);

    ScreenServices& services_;
    std::vector<Listed> mods_;
    std::vector<std::unique_ptr<Texture2D>> icons_;  // by mod; made when first drawn
    ScrollList* list_ = nullptr;
    ScrollList* details_ = nullptr;
    int selected_ = -1;
};

}  // namespace falcon
