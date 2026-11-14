#pragma once

#include "falcon/ui/rect.hpp"
#include "falcon/ui/widgets.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace falcon {

// A vertically scrolling list of rows, like the game's AbstractSelectionList: a translucent
// background, rows clipped to the list, header/footer separator lines, a mouse-wheel and a
// draggable scrollbar (shown only when the rows do not all fit).
class ScrollList : public Widget {
public:
    struct ClickOutcome {
        bool consumed = false;
        bool clickSound = false;  // play the UI click for this press
    };

    // What the list knows about a row when it is drawn.
    struct RowState {
        bool hovered = false;      // the cursor is over this row
        bool selected = false;     // this is the list's selected row
        bool listFocused = false;  // the list has keyboard focus
    };

    class Entry {
    public:
        explicit Entry(int entryHeight) : height(entryHeight) {}
        virtual ~Entry() = default;

        int height;

        // `r` is the row's rectangle. The mouse position is far off-screen when the cursor is not
        // over the visible part of the list.
        virtual void render(GuiGraphics& graphics, const Rect& r, int mouseX, int mouseY,
                            const RowState& state) = 0;
        virtual ClickOutcome mouseClicked(const Rect& /*r*/, double /*px*/, double /*py*/, int /*button*/) {
            return {};
        }
        // Returns true if the release should play the click sound.
        virtual bool mouseReleased(const Rect& /*r*/, double /*px*/, double /*py*/, int /*button*/) {
            return false;
        }
        virtual void mouseDragged(const Rect& /*r*/, double /*px*/, double /*py*/) {}
    };

    enum class ScrollbarPlacement {
        RightOfRow,   // 8px right of the row (options lists)
        InsideRight,  // over the right edge of the list (pack lists)
    };

    // `rowWidth` 0 means "the list width minus 4". With `shrinkRowsWhenScrollable` rows are 6px
    // narrower while the scrollbar is shown.
    ScrollList(int px, int py, int w, int h, int itemHeight, int rowWidth,
               ScrollbarPlacement placement, bool shrinkRowsWhenScrollable, bool selectable);

    template <class T, class... Args>
    T& addEntry(Args&&... args) {
        auto entry = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *entry;
        entries_.push_back(std::move(entry));
        clampScroll();
        return ref;
    }
    void clearEntries();
    [[nodiscard]] std::size_t entryCount() const { return entries_.size(); }

    // Moves/resizes the list (e.g. after a window resize); the scroll position is re-clamped.
    void setBounds(int px, int py, int w, int h);

    [[nodiscard]] int contentHeight() const;
    [[nodiscard]] int maxScroll() const;
    [[nodiscard]] bool scrollable() const { return maxScroll() > 0; }
    [[nodiscard]] double scrollAmount() const { return scroll_; }
    void setScrollAmount(double amount);
    [[nodiscard]] int scrollerHeight() const;
    [[nodiscard]] int scrollbarX() const;
    [[nodiscard]] int rowLeft() const { return x + width / 2 - baseRowWidth() / 2; }
    [[nodiscard]] int entryWidth() const;
    // The rectangle of entry `index` at the current scroll position.
    [[nodiscard]] Rect entryRect(std::size_t index) const;

    [[nodiscard]] int selectedIndex() const { return selected_; }
    void setSelectedIndex(int index) { selected_ = index; }  // silently
    // Selects a row (-1 for none) as a click or the arrow keys do: scrolls it into view when asked
    // and calls onSelectionChanged.
    void select(int index, bool scrollIntoView = false);
    void scrollToEntry(int index);

    // The user changed the selection (click or Up/Down); -1 means nothing is selected any more.
    std::function<void(int index)> onSelectionChanged;
    // Enter, Keypad Enter or Space on the selected row.
    std::function<void(int index)> onActivate;

    bool mouseClicked(double px, double py, int button) override;
    bool keyPressed(int key, int mods) override;
    bool mouseReleased(double px, double py, int button) override;
    void mouseDragged(double px, double py) override;
    bool mouseScrolled(double px, double py, double dy) override;
    [[nodiscard]] bool clicksOnPress() const override { return clickSound_; }
    [[nodiscard]] bool focusable() const override { return true; }

protected:
    void renderContents(GuiGraphics& graphics) override;

private:
    [[nodiscard]] int baseRowWidth() const { return rowWidth_ > 0 ? rowWidth_ : width - 4; }
    [[nodiscard]] int scrollbarY() const;
    [[nodiscard]] bool overScrollbar(double px, double py) const;
    [[nodiscard]] int entryIndexAt(double px, double py) const;
    void clampScroll();

    std::vector<std::unique_ptr<Entry>> entries_;
    int itemHeight_;
    int rowWidth_;
    ScrollbarPlacement placement_;
    bool shrinkRows_;
    bool selectable_;

    double scroll_ = 0.0;
    int selected_ = -1;
    bool scrolling_ = false;
    double lastDragY_ = 0.0;
    bool clickSound_ = false;
    bool overScrollbarHover_ = false;
};

}  // namespace falcon
