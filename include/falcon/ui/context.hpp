#pragma once

#include "falcon/text/font.hpp"
#include "falcon/ui/gui_graphics.hpp"

#include <string>

namespace falcon {

// Everything the UI library needs to know about the application that uses it: how to translate a
// key into text, which font to measure and draw with, and what to paint behind a menu screen (the
// panorama in the game). The application implements this once; screens reach it with ui().
class UiContext {
public:
    virtual ~UiContext() = default;

    // The translation of `key` (or the key itself when there is none).
    [[nodiscard]] virtual std::string text(const std::string& key) const = 0;
    [[nodiscard]] virtual const Font& font() const = 0;
    // Paints the backdrop of a full-screen menu; called first, before the screen's widgets.
    virtual void drawBackdrop(GuiGraphics& graphics, int guiWidth, int guiHeight, float deltaTicks) = 0;
    // The same, but the darkening tint starts `tintTop` GUI units down: the strip above it shows the plain
    // backdrop (a screen whose header has its own look, like the Create New World tab bar). By default the
    // tint covers everything, as drawBackdrop() does.
    virtual void drawBackdropFrom(GuiGraphics& graphics, int guiWidth, int guiHeight, float deltaTicks, int /*tintTop*/) {
        drawBackdrop(graphics, guiWidth, guiHeight, deltaTicks);
    }
};

}  // namespace falcon
