#pragma once

#include "falcon/game/menu_background.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/ui/context.hpp"

namespace falcon {

// Plugs the game into the UI library: translations come from the loaded language, the font is the
// game's, and every menu is drawn over the blurred rotating panorama, or, while a world is being played, over
// the world itself, darkened.
class GameUiContext final : public UiContext {
public:
    explicit GameUiContext(ScreenServices& services) : services_(services) {}

    [[nodiscard]] std::string text(const std::string& key) const override { return services_.language.get(key); }
    [[nodiscard]] const Font& font() const override { return services_.font; }
    void drawBackdrop(GuiGraphics& graphics, int guiWidth, int guiHeight, float deltaTicks) override {
        if (darkenWorld(graphics, guiWidth, guiHeight)) return;
        renderMenuBackground(graphics, services_.panorama, services_.options, guiWidth, guiHeight, deltaTicks);
    }
    void drawBackdropFrom(GuiGraphics& graphics, int guiWidth, int guiHeight, float deltaTicks, int tintTop) override {
        if (darkenWorld(graphics, guiWidth, guiHeight)) return;
        renderMenuBackground(graphics, services_.panorama, services_.options, guiWidth, guiHeight, deltaTicks, tintTop);
    }

private:
    // In a world the menus sit over the 3D scene, which the main loop has already drawn: dim it (the pause menu's
    // own gradient is 0xC0101010 at the top to 0xD0101010 at the bottom).
    bool darkenWorld(GuiGraphics& graphics, int guiWidth, int guiHeight) const {
        if (services_.session == nullptr) return false;
        graphics.fill(0, 0, guiWidth, guiHeight, 0xC8101010u);
        return true;
    }

    ScreenServices& services_;
};

}  // namespace falcon
