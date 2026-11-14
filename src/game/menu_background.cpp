#include "falcon/game/menu_background.hpp"

namespace falcon {

void renderMenuBackground(GuiGraphics& graphics, Panorama& panorama, const GameOptions& options,
                          int guiWidth, int guiHeight, float deltaTicks, int tintTop) {
    panorama.render(graphics, deltaTicks, options.menuBackgroundBlurriness);
    graphics.fill(0, tintTop, guiWidth, guiHeight, 0x40000000u);
}

}  // namespace falcon
