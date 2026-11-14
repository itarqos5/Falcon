#include "apex/game/panorama.hpp"

#include "apex/util/math.hpp"

namespace apex {

void Panorama::render(GuiGraphics& graphics, float deltaTicks, int blurRadius) {
    if (shouldSpin_) spin_ = wrapDegrees(spin_ + deltaTicks * speed_ * 0.1f);

    // The panorama is a separate 3D pass drawn behind everything else.
    graphics.renderer().flush();
    renderer_.render(10.0f, -spin_, graphics.framebufferWidth(), graphics.framebufferHeight(),
                     blurRadius);
    graphics.renderer().restoreState();

    // panorama_overlay.png is a single pixel stretched over the whole screen.
    const Texture2D& overlay = graphics.textures().get("gui/title/background/panorama_overlay.png");
    graphics.blit(overlay, 0, 0, 0.0f, 0.0f, graphics.guiWidth(), graphics.guiHeight(), 16, 128, 16,
                  128);
}

}  // namespace apex
