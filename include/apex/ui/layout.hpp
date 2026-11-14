#pragma once

#include "apex/ui/metrics.hpp"
#include "apex/ui/rect.hpp"

#include <vector>

namespace apex::layout {

// Small helpers that turn "centred in the screen" and "stacked" into rectangles, using the same
// integer rounding Minecraft's layout code does (positions are truncated, never rounded).

// The x that centres something `width` wide in `containerWidth`.
[[nodiscard]] constexpr int centeredX(int containerWidth, int width) { return (containerWidth - width) / 2; }
// The y that centres something `height` tall in `containerHeight`.
[[nodiscard]] constexpr int centeredY(int containerHeight, int height) { return (containerHeight - height) / 2; }

// A row of widgets side by side, as a group centred horizontally in `containerWidth`, all at `y`.
// `widths` are the widgets' widths, `gap` the space between them.
[[nodiscard]] inline std::vector<Rect> centeredRow(int containerWidth, int y, const std::vector<int>& widths, int gap,
                                                   int height = metrics::kButtonHeight) {
    int total = gap * (static_cast<int>(widths.size()) - 1);
    for (const int w : widths) total += w;
    std::vector<Rect> rects;
    int x = centeredX(containerWidth, total);
    for (const int w : widths) {
        rects.push_back({x, y, w, height});
        x += w + gap;
    }
    return rects;
}

// The Done-style footer: `count` equal buttons of `width` centred in the strip at the bottom of the
// screen that is `footerHeight` tall (kFooterHeight by default).
[[nodiscard]] inline std::vector<Rect> footerRow(int screenWidth, int screenHeight, int count, int width,
                                                 int gap = metrics::kFooterGap,
                                                 int footerHeight = metrics::kFooterHeight) {
    const int y = screenHeight - footerHeight + (footerHeight - metrics::kButtonHeight) / 2;
    return centeredRow(screenWidth, y, std::vector<int>(static_cast<std::size_t>(count), width), gap);
}

// Widgets stacked top to bottom in one column. Ask for the next rectangle with next().
class Column {
public:
    Column(int x, int top, int width, int height = metrics::kButtonHeight, int gap = metrics::kButtonGap)
        : x_(x), y_(top), width_(width), height_(height), gap_(gap) {}

    // A column centred on the screen's middle.
    static Column centered(int screenWidth, int top, int width, int height = metrics::kButtonHeight,
                           int gap = metrics::kButtonGap) {
        return Column(centeredX(screenWidth, width), top, width, height, gap);
    }

    Rect next() {
        const Rect r{x_, y_, width_, height_};
        y_ += height_ + gap_;
        return r;
    }
    // Leaves a gap (e.g. between groups of buttons).
    void skip(int pixels) { y_ += pixels; }
    [[nodiscard]] int y() const { return y_; }

private:
    int x_, y_, width_, height_, gap_;
};

// A two-column option grid like the Options screen's: cell (column 0/1, row) of `columnWidth`
// wide cells with metrics::kColumnGap between them, centred on the screen, rows `rowPitch` apart.
[[nodiscard]] constexpr Rect gridCell(int screenWidth, int top, int column, int row, int columnWidth = metrics::kButtonWidth,
                                      int rowPitch = metrics::kRowHeight, int height = metrics::kButtonHeight) {
    const int left = centeredX(screenWidth, 2 * columnWidth + metrics::kColumnGap);
    return {left + column * (columnWidth + metrics::kColumnGap), top + row * rowPitch, columnWidth, height};
}

}  // namespace apex::layout
