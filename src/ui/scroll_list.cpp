#include "falcon/ui/scroll_list.hpp"

#include <algorithm>

namespace falcon {
namespace {

constexpr int kScrollbarWidth = 6;
constexpr int kMinScrollerHeight = 32;
constexpr int kContentPadding = 2;
constexpr int kFarAway = -10000;  // a mouse position that is never over anything

constexpr std::uint32_t kBackground = 0x70000000u;       // menu_list_background.png
constexpr std::uint32_t kSeparatorLight = 0x33FFFFFFu;   // header/footer separator rows
constexpr std::uint32_t kSeparatorDark = 0xBF000000u;

}  // namespace

ScrollList::ScrollList(int px, int py, int w, int h, int itemHeight, int rowWidth,
                       ScrollbarPlacement placement, bool shrinkRowsWhenScrollable, bool selectable)
    : Widget(px, py, w, h),
      itemHeight_(itemHeight),
      rowWidth_(rowWidth),
      placement_(placement),
      shrinkRows_(shrinkRowsWhenScrollable),
      selectable_(selectable) {
    interactive = false;  // the cursor is requested by the rows' own widgets / the scrollbar
}

void ScrollList::clearEntries() {
    entries_.clear();
    selected_ = -1;
    clampScroll();
}

void ScrollList::setBounds(int px, int py, int w, int h) {
    x = px;
    y = py;
    width = w;
    height = h;
    clampScroll();
}

int ScrollList::contentHeight() const {
    int total = 0;
    for (const auto& e : entries_) total += e->height;
    return total + 2 * kContentPadding;
}

int ScrollList::maxScroll() const { return std::max(0, contentHeight() - height); }

void ScrollList::setScrollAmount(double amount) {
    scroll_ = std::clamp(amount, 0.0, static_cast<double>(maxScroll()));
}

void ScrollList::clampScroll() { setScrollAmount(scroll_); }

int ScrollList::scrollerHeight() const {
    const int content = std::max(contentHeight(), 1);
    const auto raw = static_cast<int>(static_cast<float>(height * height) / static_cast<float>(content));
    return std::clamp(raw, kMinScrollerHeight, std::max(height - 8, kMinScrollerHeight));
}

int ScrollList::scrollbarX() const {
    return placement_ == ScrollbarPlacement::RightOfRow ? rowLeft() + baseRowWidth() + 6 + 2
                                                        : x + width - kScrollbarWidth;
}

int ScrollList::scrollbarY() const {
    const int max = maxScroll();
    if (max <= 0) return y;
    return std::max(y, static_cast<int>(scroll_) * (height - scrollerHeight()) / max + y);
}

int ScrollList::entryWidth() const {
    return baseRowWidth() - ((shrinkRows_ && scrollable()) ? kScrollbarWidth : 0);
}

Rect ScrollList::entryRect(std::size_t index) const {
    int top = y + kContentPadding - static_cast<int>(scroll_);
    for (std::size_t i = 0; i < index && i < entries_.size(); ++i) top += entries_[i]->height;
    return {rowLeft(), top, entryWidth(), index < entries_.size() ? entries_[index]->height : 0};
}

bool ScrollList::overScrollbar(double px, double py) const {
    if (!scrollable()) return false;
    const int sx = scrollbarX();
    return px >= sx && px <= sx + kScrollbarWidth && py >= y && py < y + height;
}

int ScrollList::entryIndexAt(double px, double py) const {
    if (!contains(px, py)) return -1;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Rect r = entryRect(i);
        if (px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ---- drawing ----------------------------------------------------------------------------

void ScrollList::renderContents(GuiGraphics& graphics) {
    // The mouse only counts when it is over the visible part of the list.
    const bool mouseInside = isHovered();
    const int mouseX = mouseInside ? mouseX_ : kFarAway;
    const int mouseY = mouseInside ? mouseY_ : kFarAway;
    overScrollbarHover_ = mouseInside && overScrollbar(mouseX_, mouseY_);

    graphics.fill(x, y, x + width, y + height, kBackground);

    graphics.enableScissor(x, y, x + width, y + height);
    const int bottom = y + height;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Rect r = entryRect(i);
        if (r.y + r.height < y || r.y > bottom) continue;  // not visible

        if (selectable_ && static_cast<int>(i) == selected_) {
            // Selection frame: white when the list has focus, grey otherwise, with a black inside.
            graphics.fill(r.x, r.y, r.x + r.width, r.y + r.height, focused ? 0xFFFFFFFFu : 0xFF808080u);
            graphics.fill(r.x + 1, r.y + 1, r.x + r.width - 1, r.y + r.height - 1, 0xFF000000u);
        }
        const bool hovered = mouseInside && !overScrollbarHover_ && mouseX >= r.x &&
                             mouseX < r.x + r.width && mouseY >= r.y && mouseY < r.y + r.height;
        entries_[i]->render(graphics, r, mouseX, mouseY,
                            RowState{hovered, selectable_ && static_cast<int>(i) == selected_, focused});
    }
    graphics.disableScissor();

    // Separator lines just outside the visible area: faint white then dark above the list, dark
    // then faint white below it.
    graphics.fill(x, y - 2, x + width, y - 1, kSeparatorLight);
    graphics.fill(x, y - 1, x + width, y, kSeparatorDark);
    graphics.fill(x, bottom, x + width, bottom + 1, kSeparatorDark);
    graphics.fill(x, bottom + 1, x + width, bottom + 2, kSeparatorLight);

    if (scrollable()) {
        graphics.blitSprite("widget/scroller_background", scrollbarX(), y, kScrollbarWidth, height);
        graphics.blitSprite("widget/scroller", scrollbarX(), scrollbarY(), kScrollbarWidth,
                            scrollerHeight());
        if (scrolling_) {
            graphics.requestCursor(CursorType::ResizeVertical);
        } else if (overScrollbarHover_) {
            graphics.requestCursor(CursorType::PointingHand);
        }
    }
}

// ---- input ------------------------------------------------------------------------------

bool ScrollList::mouseClicked(double px, double py, int button) {
    if (!visible || !active || button != 0) return false;
    if (overScrollbar(px, py)) {
        scrolling_ = true;
        lastDragY_ = py;
        clickSound_ = false;
        return true;
    }
    const int index = entryIndexAt(px, py);
    if (index < 0) return false;

    const Rect r = entryRect(static_cast<std::size_t>(index));
    const ClickOutcome outcome = entries_[static_cast<std::size_t>(index)]->mouseClicked(r, px, py, button);
    if (outcome.consumed) {
        clickSound_ = outcome.clickSound;
        return true;
    }
    if (selectable_) {  // plain click on a row selects it (silently)
        select(index);
        clickSound_ = false;
        return true;
    }
    return false;
}

void ScrollList::select(int index, bool scrollIntoView) {
    selected_ = index >= 0 && index < static_cast<int>(entries_.size()) ? index : -1;
    if (scrollIntoView && selected_ >= 0) scrollToEntry(selected_);
    if (onSelectionChanged) onSelectionChanged(selected_);
}

void ScrollList::scrollToEntry(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    const Rect r = entryRect(static_cast<std::size_t>(index));
    const int above = r.y - y - kContentPadding;            // how far the row sticks out over the top
    if (above < 0) setScrollAmount(scroll_ + above);
    const int below = (y + height) - r.y - r.height - kContentPadding;  // ... and under the bottom
    if (below < 0) setScrollAmount(scroll_ - below);
}

bool ScrollList::keyPressed(int key, int /*mods*/) {
    if (!selectable_ || entries_.empty()) return false;
    constexpr int kEnter = 257;
    constexpr int kKeypadEnter = 335;
    constexpr int kSpace = 32;
    constexpr int kDown = 264;
    constexpr int kUp = 265;
    const int last = static_cast<int>(entries_.size()) - 1;
    if (key == kDown || key == kUp) {
        int next;
        if (key == kDown) {
            next = selected_ < 0 ? 0 : selected_ + 1;
        } else {
            next = selected_ < 0 ? last : selected_ - 1;
        }
        if (next < 0 || next > last) {  // past either end: the selection and the focus are dropped
            select(-1);
            focused = false;
        } else {
            select(next, /*scrollIntoView=*/true);
        }
        return true;
    }
    if ((key == kEnter || key == kKeypadEnter || key == kSpace) && selected_ >= 0) {
        if (onActivate) onActivate(selected_);
        return true;
    }
    return false;
}

bool ScrollList::mouseReleased(double px, double py, int button) {
    if (button == 0) scrolling_ = false;
    bool click = false;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        click = entries_[i]->mouseReleased(entryRect(i), px, py, button) || click;
    }
    return click;
}

void ScrollList::mouseDragged(double px, double py) {
    if (scrolling_) {
        const int bottom = y + height;
        if (py < y) {
            setScrollAmount(0.0);
        } else if (py > bottom) {
            setScrollAmount(maxScroll());
        } else {
            const double track = std::max(1, height - scrollerHeight());
            const double ratio = std::max(1.0, static_cast<double>(maxScroll()) / track);
            setScrollAmount(scroll_ + (py - lastDragY_) * ratio);
        }
        lastDragY_ = py;
        return;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) entries_[i]->mouseDragged(entryRect(i), px, py);
}

bool ScrollList::mouseScrolled(double px, double py, double dy) {
    if (!contains(px, py)) return false;
    setScrollAmount(scroll_ - dy * static_cast<double>(itemHeight_ / 2));
    return true;
}

}  // namespace falcon
