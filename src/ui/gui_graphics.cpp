#include "apex/ui/gui_graphics.hpp"

#include "apex/text/text_layout.hpp"
#include "apex/util/text_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace apex {
namespace {

std::uint32_t alphaByte(float alpha) {
    // ARGB.as8BitChannel: floor(value * 255), clamped to a byte.
    const float scaled = std::floor(alpha * 255.0f);
    return static_cast<std::uint32_t>(std::clamp(scaled, 0.0f, 255.0f));
}

// ARGB.scaleRGB(color, 0.25f): the text drop-shadow colour.
std::uint32_t shadowColor(std::uint32_t argb) {
    const auto scale = [](std::uint32_t channel) {
        return static_cast<std::uint32_t>(static_cast<float>(channel) * 0.25f);
    };
    return (argb & 0xFF000000u) | (scale((argb >> 16) & 0xFF) << 16) |
           (scale((argb >> 8) & 0xFF) << 8) | scale(argb & 0xFF);
}

}  // namespace

std::uint32_t argbWhite(float alpha) { return (alphaByte(alpha) << 24) | 0x00FFFFFFu; }

std::uint32_t argbWithAlpha(std::uint32_t rgb, float alpha) {
    return (alphaByte(alpha) << 24) | (rgb & 0x00FFFFFFu);
}

GuiGraphics::GuiGraphics(GuiRenderer& renderer, SpriteManager& sprites, TextureManager& textures,
                         const Font& font)
    : renderer_(renderer), sprites_(sprites), textures_(textures), font_(font) {}

void GuiGraphics::beginFrame(int guiWidth, int guiHeight, int framebufferWidth,
                             int framebufferHeight) {
    guiWidth_ = guiWidth;
    guiHeight_ = guiHeight;
    framebufferWidth_ = framebufferWidth;
    framebufferHeight_ = framebufferHeight;
    cursor_ = CursorType::Default;
    scissorActive_ = false;
}

// ---- textures ---------------------------------------------------------------------------

void GuiGraphics::blit(const Texture2D& texture, int x, int y, float u, float v, int width,
                       int height, int textureWidth, int textureHeight, std::uint32_t color) {
    blit(texture, x, y, u, v, width, height, width, height, textureWidth, textureHeight, color);
}

void GuiGraphics::blit(const Texture2D& texture, int x, int y, float u, float v, int width,
                       int height, int uWidth, int vHeight, int textureWidth, int textureHeight,
                       std::uint32_t color) {
    const auto tw = static_cast<float>(textureWidth);
    const auto th = static_cast<float>(textureHeight);
    renderer_.quad(texture, static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(x + width), static_cast<float>(y + height), u / tw, v / th,
                   (u + static_cast<float>(uWidth)) / tw, (v + static_cast<float>(vHeight)) / th,
                   color);
}

// ---- sprites ----------------------------------------------------------------------------

void GuiGraphics::drawRegion(const Texture2D& tex, int sx, int sy, int sw, int sh, int dx, int dy,
                             int dw, int dh, std::uint32_t color) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    const auto tw = static_cast<float>(tex.width());
    const auto th = static_cast<float>(tex.height());
    renderer_.quad(tex, static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dx + dw),
                   static_cast<float>(dy + dh), static_cast<float>(sx) / tw,
                   static_cast<float>(sy) / th, static_cast<float>(sx + sw) / tw,
                   static_cast<float>(sy + sh) / th, color);
}

void GuiGraphics::drawTiled(const Texture2D& tex, int sx, int sy, int sw, int sh, int dx, int dy,
                            int dw, int dh, bool stretch, std::uint32_t color) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (stretch) {
        drawRegion(tex, sx, sy, sw, sh, dx, dy, dw, dh, color);
        return;
    }
    for (int ty = 0; ty < dh; ty += sh) {
        const int tileH = std::min(sh, dh - ty);
        for (int tx = 0; tx < dw; tx += sw) {
            const int tileW = std::min(sw, dw - tx);
            drawRegion(tex, sx, sy, tileW, tileH, dx + tx, dy + ty, tileW, tileH, color);
        }
    }
}

void GuiGraphics::drawNineSlice(const Sprite& sprite, int x, int y, int width, int height,
                                std::uint32_t color) {
    const SpriteScaling& s = sprite.scaling;
    const Texture2D& tex = sprite.texture;
    const int sw = s.width;
    const int sh = s.height;
    const int l = std::min(s.left, width / 2);
    const int r = std::min(s.right, width / 2);
    const int t = std::min(s.top, height / 2);
    const int b = std::min(s.bottom, height / 2);
    const bool stretch = s.stretchInner;

    if (width == sw && height == sh) {
        drawRegion(tex, 0, 0, sw, sh, x, y, width, height, color);
    } else if (height == sh) {
        drawRegion(tex, 0, 0, l, sh, x, y, l, height, color);
        drawTiled(tex, l, 0, sw - l - r, sh, x + l, y, width - l - r, height, stretch, color);
        drawRegion(tex, sw - r, 0, r, sh, x + width - r, y, r, height, color);
    } else if (width == sw) {
        drawRegion(tex, 0, 0, sw, t, x, y, width, t, color);
        drawTiled(tex, 0, t, sw, sh - t - b, x, y + t, width, height - t - b, stretch, color);
        drawRegion(tex, 0, sh - b, sw, b, x, y + height - b, width, b, color);
    } else {
        // corners
        drawRegion(tex, 0, 0, l, t, x, y, l, t, color);
        drawRegion(tex, sw - r, 0, r, t, x + width - r, y, r, t, color);
        drawRegion(tex, 0, sh - b, l, b, x, y + height - b, l, b, color);
        drawRegion(tex, sw - r, sh - b, r, b, x + width - r, y + height - b, r, b, color);
        // edges
        drawTiled(tex, l, 0, sw - l - r, t, x + l, y, width - l - r, t, stretch, color);
        drawTiled(tex, l, sh - b, sw - l - r, b, x + l, y + height - b, width - l - r, b, stretch,
                  color);
        drawTiled(tex, 0, t, l, sh - t - b, x, y + t, l, height - t - b, stretch, color);
        drawTiled(tex, sw - r, t, r, sh - t - b, x + width - r, y + t, r, height - t - b, stretch,
                  color);
        // centre
        drawTiled(tex, l, t, sw - l - r, sh - t - b, x + l, y + t, width - l - r, height - t - b,
                  stretch, color);
    }
}

void GuiGraphics::blitSprite(const std::string& id, int x, int y, int width, int height,
                             std::uint32_t color) {
    const Sprite& sprite = sprites_.get(id);
    switch (sprite.scaling.type) {
        case SpriteScaling::Type::Stretch:
            renderer_.quad(sprite.texture, static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(x + width), static_cast<float>(y + height), 0.0f,
                           0.0f, 1.0f, 1.0f, color);
            break;
        case SpriteScaling::Type::Tile:
            drawTiled(sprite.texture, 0, 0, sprite.scaling.width, sprite.scaling.height, x, y,
                      width, height, false, color);
            break;
        case SpriteScaling::Type::NineSlice:
            drawNineSlice(sprite, x, y, width, height, color);
            break;
    }
}

void GuiGraphics::blitSpriteFrame(const std::string& id, int x, int y, int width, int height,
                                  int frame, int frameCount, std::uint32_t color) {
    const Sprite& sprite = sprites_.get(id);
    const float v0 = static_cast<float>(frame) / static_cast<float>(frameCount);
    const float v1 = static_cast<float>(frame + 1) / static_cast<float>(frameCount);
    renderer_.quad(sprite.texture, static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(x + width), static_cast<float>(y + height), 0.0f, v0, 1.0f, v1,
                   color);
}

// ---- text -------------------------------------------------------------------------------

void GuiGraphics::drawTextPass(std::string_view text, float x, float y, std::uint32_t argb,
                               bool bold) {
    float pen = x;
    for (const char32_t cp : decodeUtf8(text)) {
        const Glyph& g = font_.glyph(cp);
        if (g.texture != nullptr) {
            renderer_.quad(*g.texture, pen + g.x0, y + g.y0, pen + g.x1, y + g.y1, g.u0, g.v0,
                           g.u1, g.v1, argb);
            if (bold) {  // bold = the same glyph again, one pixel to the right
                renderer_.quad(*g.texture, pen + 1.0f + g.x0, y + g.y0, pen + 1.0f + g.x1, y + g.y1,
                               g.u0, g.v0, g.u1, g.v1, argb);
            }
        }
        pen += static_cast<float>(g.advance) + (bold ? 1.0f : 0.0f);
    }
}

void GuiGraphics::setTooltip(const std::string& text, int mouseX, int mouseY, int wx, int wy, int ww, int wh) {
    tooltipPending_ = true;
    itemTooltipLines_.clear();
    tooltipText_ = text;
    tooltipMouseX_ = mouseX;
    tooltipMouseY_ = mouseY;
    tooltipX_ = wx;
    tooltipY_ = wy;
    tooltipW_ = ww;
    tooltipH_ = wh;
}

void GuiGraphics::setItemTooltip(std::vector<TooltipLine> lines, int mouseX, int mouseY) {
    tooltipPending_ = !lines.empty();
    tooltipText_.clear();
    itemTooltipLines_ = std::move(lines);
    itemTooltipMouseX_ = mouseX;
    itemTooltipMouseY_ = mouseY;
}

void GuiGraphics::renderTooltip() {
    if (!tooltipPending_) return;
    if (!itemTooltipLines_.empty()) {
        tooltipPending_ = false;
        const std::vector<TooltipLine> lines = std::move(itemTooltipLines_);
        itemTooltipLines_.clear();
        int width = 0;
        for (const TooltipLine& l : lines) width = std::max(width, font_.width(l.text));
        const int height = lines.size() == 1 ? 8 : 10 * static_cast<int>(lines.size()) + 2;
        // DefaultTooltipPositioner.
        int x = itemTooltipMouseX_ + 12;
        int y = itemTooltipMouseY_ - 12;
        if (x + width > guiWidth_) x = std::max(x - 24 - width, 4);
        if (y + height + 3 > guiHeight_) y = guiHeight_ - height - 3;
        renderer_.flush();
        blitSprite("tooltip/background", x - 12, y - 12, width + 24, height + 24);
        blitSprite("tooltip/frame", x - 12, y - 12, width + 24, height + 24);
        int lineY = y;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            text(lines[i].text, static_cast<float>(x), static_cast<float>(lineY), lines[i].argb);
            lineY += 10 + (i == 0 ? 2 : 0);
        }
        return;
    }
    tooltipPending_ = false;

    // Wrap each paragraph to 170 px.
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= tooltipText_.size()) {
        const std::size_t end = std::min(tooltipText_.find('\n', start), tooltipText_.size());
        const std::string paragraph = tooltipText_.substr(start, end - start);
        if (paragraph.empty()) {
            lines.emplace_back();
        } else {
            for (std::string& line : wrapText(font_, paragraph, 170)) lines.push_back(std::move(line));
        }
        start = end + 1;
    }
    int textWidth = 0;
    for (const std::string& line : lines) textWidth = std::max(textWidth, font_.width(line));
    const int textHeight = lines.size() == 1 ? 8 : 10 * static_cast<int>(lines.size());

    // MenuTooltipPositioner: beside the cursor, nudged clear of the hovered widget.
    const auto offset = [](int a, int b, int c) {
        const float t = static_cast<float>(std::min(std::abs(a - b), c)) / static_cast<float>(c);
        return static_cast<int>(std::lround(static_cast<float>(c - 3) + t * (5.0f - static_cast<float>(c - 3))));
    };
    int px = tooltipMouseX_ + 12;
    int py = tooltipMouseY_;
    if (px + textWidth > guiWidth_ - 5) px = std::max(tooltipMouseX_ - 12 - textWidth, 9);
    py += 3;
    const int boxHeight = textHeight + 6;
    const int below = tooltipY_ + tooltipH_ + 3 + offset(0, 0, tooltipH_);
    if (below + boxHeight <= guiHeight_ - 5) {
        py += offset(py, tooltipY_, tooltipH_);
    } else {
        py -= boxHeight + offset(py, tooltipY_ + tooltipH_, tooltipH_);
    }

    renderer_.flush();
    blitSprite("tooltip/background", px - 12, py - 12, textWidth + 24, textHeight + 24);
    blitSprite("tooltip/frame", px - 12, py - 12, textWidth + 24, textHeight + 24);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text(lines[i], static_cast<float>(px), static_cast<float>(py + 10 * static_cast<int>(i)), kWhite);
    }
}

void GuiGraphics::enableScissor(int x0, int y0, int x1, int y1) {
    scissorX0_ = std::clamp(x0, 0, guiWidth_);
    scissorY0_ = std::clamp(y0, 0, guiHeight_);
    scissorX1_ = std::clamp(x1, 0, guiWidth_);
    scissorY1_ = std::clamp(y1, 0, guiHeight_);
    scissorActive_ = true;
    renderer_.enableScissor(scissorX0_, scissorY0_, scissorX1_, scissorY1_);
}

void GuiGraphics::disableScissor() {
    scissorActive_ = false;
    renderer_.disableScissor();
}

void GuiGraphics::pushScissor(int x0, int y0, int x1, int y1) {
    scissorStack_.push_back({scissorActive_, scissorX0_, scissorY0_, scissorX1_, scissorY1_});
    if (scissorActive_) {  // never wider than the clip we are already inside
        x0 = std::max(x0, scissorX0_);
        y0 = std::max(y0, scissorY0_);
        x1 = std::min(x1, scissorX1_);
        y1 = std::min(y1, scissorY1_);
    }
    enableScissor(x0, y0, std::max(x1, x0), std::max(y1, y0));
}

void GuiGraphics::popScissor() {
    if (scissorStack_.empty()) return;
    const SavedScissor saved = scissorStack_.back();
    scissorStack_.pop_back();
    if (saved.active) {
        enableScissor(saved.x0, saved.y0, saved.x1, saved.y1);
    } else {
        disableScissor();
    }
}

bool GuiGraphics::containsPointInScissor(int x, int y) const {
    return !scissorActive_ ||
           (x >= scissorX0_ && x < scissorX1_ && y >= scissorY0_ && y < scissorY1_);
}

void GuiGraphics::text(std::string_view text, float x, float y, std::uint32_t argb, bool shadow,
                       bool underline, bool bold) {
    if ((argb & 0xFC000000u) == 0) return;  // effectively transparent: the game skips it too
    renderer_.setTextMode(true);
    if (shadow) drawTextPass(text, x + 1.0f, y + 1.0f, shadowColor(argb), bold);
    drawTextPass(text, x, y, argb, bold);
    if (underline) {
        const auto w = static_cast<float>(font_.width(text, bold));
        // The underline is a 1px bar on the line below the baseline.
        if (shadow) {
            renderer_.fill(x + 1.0f, y + 9.0f, x + 1.0f + w, y + 10.0f, shadowColor(argb));
        }
        renderer_.fill(x, y + 8.0f, x + w, y + 9.0f, argb);
    }
    renderer_.setTextMode(false);
}

}  // namespace apex
