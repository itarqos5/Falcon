#pragma once

#include "falcon/gfx/gui_renderer.hpp"
#include "falcon/gfx/texture_manager.hpp"
#include "falcon/platform/cursor.hpp"
#include "falcon/text/font.hpp"
#include "falcon/ui/sprite_manager.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace falcon {

inline constexpr std::uint32_t kWhite = 0xFFFFFFFFu;

// ARGB.white(float): opaque-white tint carrying `alpha` in the top byte.
[[nodiscard]] std::uint32_t argbWhite(float alpha);
// An RGB colour with `alpha` (0..1) in the top byte.
[[nodiscard]] std::uint32_t argbWithAlpha(std::uint32_t rgb, float alpha);

// One line of an item tooltip and its colour (ARGB).
struct TooltipLine {
    std::string text;
    std::uint32_t argb = 0xFFFFFFFFu;
};

// The draw API the screens use: texture blits, nine-slice sprites and text. Everything is
// expressed in GUI units; the renderer scales to the framebuffer.
class GuiGraphics {
public:
    GuiGraphics(GuiRenderer& renderer, SpriteManager& sprites, TextureManager& textures,
                const Font& font);

    void beginFrame(int guiWidth, int guiHeight, int framebufferWidth, int framebufferHeight);

    [[nodiscard]] int guiWidth() const { return guiWidth_; }
    [[nodiscard]] int guiHeight() const { return guiHeight_; }
    [[nodiscard]] int framebufferWidth() const { return framebufferWidth_; }
    [[nodiscard]] int framebufferHeight() const { return framebufferHeight_; }
    // The frame clock (milliseconds), for time-based effects such as the text caret blink.
    void setTimeMillis(double millis) { timeMillis_ = millis; }
    [[nodiscard]] double timeMillis() const { return timeMillis_; }
    [[nodiscard]] GuiRenderer& renderer() { return renderer_; }
    [[nodiscard]] TextureManager& textures() { return textures_; }
    [[nodiscard]] int guiScale() const { return renderer_.guiScale(); }
    [[nodiscard]] const Font& font() const { return font_; }

    // Draws the (width x height) region starting at pixel (u, v) of the texture.
    void blit(const Texture2D& texture, int x, int y, float u, float v, int width, int height,
              int textureWidth, int textureHeight, std::uint32_t color = kWhite);
    // Same, but the source region (uWidth x vHeight) may differ from the drawn size.
    void blit(const Texture2D& texture, int x, int y, float u, float v, int width, int height,
              int uWidth, int vHeight, int textureWidth, int textureHeight,
              std::uint32_t color = kWhite);
    void blitSprite(const std::string& id, int x, int y, int width, int height,
                    std::uint32_t color = kWhite);
    // A part of a sprite: the (sourceWidth x sourceHeight) region at (sourceX, sourceY) of its own picture, drawn (width x height).
    // (The attack indicator is a bar that is partly filled, which is this.)
    void blitSpriteRegion(const std::string& id, int sourceX, int sourceY, int sourceWidth,
                          int sourceHeight, int x, int y, int width, int height,
                          std::uint32_t color = kWhite);
    // One frame of an animated sprite whose texture stacks `frameCount` equal frames vertically.
    void blitSpriteFrame(const std::string& id, int x, int y, int width, int height, int frame,
                         int frameCount, std::uint32_t color = kWhite);

    // Text with the game's default font. `shadow` adds the dark 1px drop shadow; `bold` draws every
    // glyph twice, one pixel apart (and one pixel wider).
    void text(std::string_view text, float x, float y, std::uint32_t argb, bool shadow = true,
              bool underline = false, bool bold = false);
    [[nodiscard]] int textWidth(std::string_view text, bool bold = false) const {
        return font_.width(text, bold);
    }

    // Asks for a tooltip for this frame (the last request wins); the screen draws it on top of
    // everything with renderTooltip(). `wx..wh` is the hovered widget's rectangle, used to keep the
    // box clear of it. Text is wrapped to 170 px; '\n' starts a new paragraph.
    void setTooltip(const std::string& text, int mouseX, int mouseY, int wx, int wy, int ww, int wh);
    void renderTooltip();
    // An item's tooltip (the game's default positioner: beside the cursor, 12 right and 12 up, kept on the screen): lines
    // in their own colours with a 2 pixel gap under the first, which is the item's name. The last request of a frame wins.
    void setItemTooltip(std::vector<TooltipLine> lines, int mouseX, int mouseY);

    // A solid rectangle (GUI units).
    void fill(int x0, int y0, int x1, int y1, std::uint32_t argb) {
        renderer_.fill(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
                       static_cast<float>(y1), argb);
    }

    // Clips drawing to a rectangle (GUI units). Hover tests respect it, like the game.
    void enableScissor(int x0, int y0, int x1, int y1);
    void disableScissor();
    [[nodiscard]] bool containsPointInScissor(int x, int y) const;
    // A nested clip: clips to the given rectangle intersected with the one already active, until the
    // matching popScissor() restores the previous state (use these inside something that is itself clipped,
    // such as a row of a list).
    void pushScissor(int x0, int y0, int x1, int y1);
    void popScissor();

    void pushPose() { renderer_.pushPose(); }
    void popPose() { renderer_.popPose(); }
    [[nodiscard]] Pose2D& pose() { return renderer_.pose(); }

    // The widget under the mouse asks for a cursor shape; the client applies it after the frame.
    void requestCursor(CursorType type) { cursor_ = type; }
    [[nodiscard]] CursorType requestedCursor() const { return cursor_; }

private:
    void drawRegion(const Texture2D& tex, int sx, int sy, int sw, int sh, int dx, int dy, int dw,
                    int dh, std::uint32_t color);
    void drawTiled(const Texture2D& tex, int sx, int sy, int sw, int sh, int dx, int dy, int dw,
                   int dh, bool stretch, std::uint32_t color);
    void drawNineSlice(const Sprite& sprite, int x, int y, int width, int height,
                       std::uint32_t color);
    void drawTextPass(std::string_view text, float x, float y, std::uint32_t argb, bool bold);

    GuiRenderer& renderer_;
    SpriteManager& sprites_;
    TextureManager& textures_;
    const Font& font_;
    int guiWidth_ = 0;
    int guiHeight_ = 0;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    CursorType cursor_ = CursorType::Default;
    double timeMillis_ = 0.0;
    bool tooltipPending_ = false;
    std::vector<TooltipLine> itemTooltipLines_;
    int itemTooltipMouseX_ = 0;
    int itemTooltipMouseY_ = 0;
    std::string tooltipText_;
    int tooltipMouseX_ = 0;
    int tooltipMouseY_ = 0;
    int tooltipX_ = 0;  // the hovered widget
    int tooltipY_ = 0;
    int tooltipW_ = 0;
    int tooltipH_ = 0;
    bool scissorActive_ = false;
    int scissorX0_ = 0;
    int scissorY0_ = 0;
    int scissorX1_ = 0;
    int scissorY1_ = 0;
    struct SavedScissor {
        bool active;
        int x0, y0, x1, y1;
    };
    std::vector<SavedScissor> scissorStack_;
};

}  // namespace falcon
