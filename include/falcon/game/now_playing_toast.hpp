#pragma once

#include "falcon/ui/gui_graphics.hpp"

#include <string>

namespace falcon {

// The "Now Playing" toast (NowPlayingToast): a 30px-tall strip anchored at the top-left that
// slides in over 600 ms, stays for 5 s, and slides out. It shows the track's translated title next
// to an animated, colour-cycling music-note icon. Off by default (Music Toast = Never).
class NowPlayingToast {
public:
    static constexpr double kSlideMillis = 600.0;
    static constexpr double kVisibleMillis = 5000.0;
    static constexpr int kHeight = 30;

    // Starts (or restarts) the toast for `title`, e.g. "fingerspit - Ebb".
    void show(std::string title, double nowMillis);
    void hide() { active_ = false; }
    [[nodiscard]] bool active() const { return active_; }

    // How far the toast has slid in (0 = hidden off-screen, 1 = fully shown) at `nowMillis`;
    // 0 once it has finished.
    [[nodiscard]] double visiblePortion(double nowMillis) const;

    void render(GuiGraphics& graphics, double nowMillis);

    // The music-note tint: a smooth cycle through the dye colours, 30 steps of 25 ms per colour.
    [[nodiscard]] static std::uint32_t noteColor(double nowMillis);

private:
    std::string title_;
    double startMillis_ = 0.0;
    bool active_ = false;
};

}  // namespace falcon
