#pragma once

#include "falcon/game/game_options.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/game/panorama.hpp"

namespace falcon {

// The background every options-style screen has when no world is loaded: the same panorama as the
// main menu (still spinning), blurred by the "Menu Background Blur" option, plus
// menu_background.png - a uniform 25% black tint.
void renderMenuBackground(GuiGraphics& graphics, Panorama& panorama, const GameOptions& options,
                          int guiWidth, int guiHeight, float deltaTicks, int tintTop = 0);

}  // namespace falcon
