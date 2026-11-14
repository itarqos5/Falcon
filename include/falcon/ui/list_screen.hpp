#pragma once

#include "falcon/ui/rect.hpp"
#include "falcon/ui/screen.hpp"
#include "falcon/ui/scroll_list.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

// Geometry shared by list screens: a 33px header with the title, a scrolling list between the
// 33px header and footer, and a 200px Done button centred in the footer.
struct ListScreenLayout {
    Rect title;
    Rect list;
    Rect done;
};

[[nodiscard]] ListScreenLayout computeListScreenLayout(int width, int height, int titleWidth);

// One row of a ListScreen: one wide widget or up to two half-width widgets side by side,
// re-positioned every frame like the game's OptionsList.Entry.
class ListRow : public ScrollList::Entry {
public:
    explicit ListRow(int screenWidth) : Entry(metrics::kRowHeight), screenWidth_(screenWidth) {}

    void add(std::unique_ptr<Widget> widget) { children_.push_back(std::move(widget)); }

    void render(GuiGraphics& graphics, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState&) override;
    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int button) override;
    bool mouseReleased(const Rect&, double px, double py, int button) override;
    void mouseDragged(const Rect&, double px, double py) override;

private:
    void place(const Rect& r);

    int screenWidth_;
    std::vector<std::unique_ptr<Widget>> children_;
};

// A screen made of a title, a scrolling list of rows (options, toggles, sliders, section headers)
// and a Done button, on the application's backdrop: Music & Sounds, Chat Settings, ... Adding one
// is a few lines: derive, give the title's translation key, and list the rows in addOptions():
//
//     class MyScreen : public ListScreen {
//     public:
//         explicit MyScreen(ScreenNavigator& n) : ListScreen(n, "my.title") {}
//     protected:
//         void addOptions() override {
//             addHeader(text("my.header"));
//             addPair(makeToggle("my.a", a_, kSmallWidth, [this](bool on) { a_ = on; }),
//                     makeToggle("my.b", b_, kSmallWidth, [this](bool on) { b_ = on; }));
//             addBig(makeSlider(v_, kBigWidth, [](double v) { return std::to_string(v); },
//                               [this](double v) { v_ = v; }));
//         }
//     };
class ListScreen : public Screen {
public:
    // `titleKey` is the translation key of the screen title.
    ListScreen(ScreenNavigator& navigator, std::string titleKey);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;

protected:
    static constexpr int kBigWidth = metrics::kFullRowWidth;
    static constexpr int kSmallWidth = metrics::kButtonWidth;
    static constexpr int kWidgetHeight = metrics::kButtonHeight;

    void init() final;
    virtual void addOptions() = 0;
    // The Done button by default (200 wide, centred); a screen may put its own buttons there.
    virtual void addFooter(const ListScreenLayout& layout);

    // Rows: one full-width widget, or one/two half-width widgets side by side.
    void addBig(std::unique_ptr<Widget> widget);
    void addPair(std::unique_ptr<Widget> left, std::unique_ptr<Widget> right = nullptr);
    // A bold, underlined section title row.
    void addHeader(const std::string& headerText);

    // "Caption: value" (options.generic_value) with the caption looked up from its key.
    [[nodiscard]] std::string optionLabel(const std::string& captionKey, const std::string& value) const;
    [[nodiscard]] std::function<bool()> shiftDown() const;

    // On / Off button: "<caption>: ON".
    [[nodiscard]] std::unique_ptr<CycleButton> makeToggle(const std::string& captionKey, bool initial, int width,
                                                          std::function<void(bool)> onChange);
    // Button cycling through values whose names are translation keys: "<caption>: <value>".
    [[nodiscard]] std::unique_ptr<CycleButton> makeChoice(const std::string& captionKey,
                                                          std::vector<std::string> valueKeys, int current, int width,
                                                          std::function<void(int)> onChange);
    // Button that opens another screen (a link without a screen yet may return null: nothing happens).
    [[nodiscard]] std::unique_ptr<Button> makeLink(const std::string& labelKey, int width,
                                                   std::function<std::unique_ptr<Screen>()> open);
    // Slider over 0..1 whose label is computed from its value; `onChange` gets every new value.
    [[nodiscard]] std::unique_ptr<SliderWidget> makeSlider(double initial, int width,
                                                           std::function<std::string(double)> label,
                                                           std::function<void(double)> onChange);
    // Slider over the whole numbers min..max (OptionInstance.IntRange): `label` and `onChange` get the number.
    [[nodiscard]] std::unique_ptr<SliderWidget> makeIntSlider(int min, int max, int current, int width,
                                                              std::function<std::string(int)> label,
                                                              std::function<void(int)> onChange);
    // Where a whole number sits on an IntRange(min, max) slider (0..1).
    [[nodiscard]] static double intToSlider(int min, int max, int value);

    // Label helpers (Options.genericValueLabel and friends).
    [[nodiscard]] std::string percentLabel(const std::string& captionKey, double value) const;      // "Caption: 50%"
    [[nodiscard]] std::string percentOrOffLabel(const std::string& captionKey, double value) const;  // 0 -> "Caption: OFF"
    [[nodiscard]] std::string pixelLabel(const std::string& captionKey, int pixels) const;           // "Caption: 320px"
    [[nodiscard]] std::string numberOrOffLabel(const std::string& captionKey, int value) const;      // 0 -> "Caption: OFF"
    // A tooltip for a widget, by translation key.
    template <class W>
    std::unique_ptr<W> withTooltip(std::unique_ptr<W> widget, const std::string& key) {
        widget->tooltip = text(key);
        return widget;
    }

private:
    std::string titleKey_;
    ScrollList* list_ = nullptr;
};

}  // namespace falcon
