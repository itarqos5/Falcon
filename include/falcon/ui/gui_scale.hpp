#pragma once

namespace falcon {

// Window.calculateScale: with `requested` == 0 ("auto") this is the largest integer scale
// at which the window is still at least 320x240 GUI units. `forceUnicode` rounds up to even.
[[nodiscard]] constexpr int calculateGuiScale(int framebufferWidth, int framebufferHeight,
                                              int requested, bool forceUnicode) {
    int scale = 1;
    while (scale != requested && scale < framebufferWidth && scale < framebufferHeight &&
           framebufferWidth / (scale + 1) >= 320 && framebufferHeight / (scale + 1) >= 240) {
        ++scale;
    }
    if (forceUnicode && scale % 2 != 0) ++scale;
    return scale;
}

// Size of the window in GUI units (rounded up).
[[nodiscard]] constexpr int scaledSize(int pixels, int scale) {
    return (pixels + scale - 1) / scale;
}

}  // namespace falcon
