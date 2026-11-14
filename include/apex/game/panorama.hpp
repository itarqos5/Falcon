#pragma once

#include "apex/gfx/panorama_renderer.hpp"
#include "apex/ui/gui_graphics.hpp"

namespace apex {

// Title screen background: the rotating cube-map panorama plus its overlay texture.
// Rotation is 0.1 degrees per game tick of real time, yaw = -spin, pitch fixed at 10 degrees.
class Panorama {
public:
    explicit Panorama(PanoramaRenderer& renderer) : renderer_(renderer) {}

    void setSpeed(float speed) { speed_ = speed; }  // the "Panorama Scroll Speed" option
    void startSpin() { shouldSpin_ = true; }
    void holdSpin() { shouldSpin_ = false; }
    [[nodiscard]] float spin() const { return spin_; }

    // `blurRadius` > 0 blurs the panorama like menu screens do (0..10, the game's default is 5).
    void render(GuiGraphics& graphics, float deltaTicks, int blurRadius = 0);

private:
    PanoramaRenderer& renderer_;
    float spin_ = 0.0f;
    bool shouldSpin_ = true;
    float speed_ = 1.0f;  // the "panorama scroll speed" option
};

}  // namespace apex
