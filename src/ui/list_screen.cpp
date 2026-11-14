#include "falcon/ui/list_screen.hpp"

#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

int javaRound(double v) { return static_cast<int>(std::floor(v + 0.5)); }

// A section title row: a text widget (padding 4) centred in a row of 18, plus 18 above it unless
// it is the first row.
class HeaderRow : public ScrollList::Entry {
public:
    HeaderRow(std::string text, int screenWidth, bool first)
        : Entry(first ? 18 : 36), text_(std::move(text)), screenWidth_(screenWidth), paddingTop_(first ? 0 : 18) {}

    void render(GuiGraphics& g, const Rect& r, int, int, const ScrollList::RowState&) override {
        const int widgetWidth = g.textWidth(text_, true) + 8;
        const int widgetX = screenWidth_ / 2 - widgetWidth / 2;
        const int widgetY = r.y + 2 + paddingTop_;
        g.text(text_, static_cast<float>(widgetX + 4), static_cast<float>(widgetY + 4), kWhite, true, true, true);
    }

private:
    std::string text_;
    int screenWidth_;
    int paddingTop_;
};

}  // namespace

ListScreenLayout computeListScreenLayout(int width, int height, int titleWidth) {
    using namespace metrics;
    ListScreenLayout l;
    l.title = {(width - titleWidth) / 2, javaRound((kHeaderHeight - kLineHeight) / 2.0), titleWidth, kLineHeight};
    l.list = {0, kHeaderHeight, width, height - kHeaderHeight - kFooterHeight};
    l.done = {layout::centeredX(width, kWideButtonWidth),
              height - kFooterHeight + (kFooterHeight - kButtonHeight) / 2, kWideButtonWidth, kButtonHeight};  // (33 - 20) / 2 truncates to 6
    return l;
}

// ---- ListRow ------------------------------------------------------------------------------

void ListRow::place(const Rect& r) {
    int index = 0;
    for (auto& child : children_) {
        child->x = screenWidth_ / 2 - 155 + 160 * index;
        child->y = r.y + 2;
        ++index;
    }
}

void ListRow::render(GuiGraphics& graphics, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState&) {
    place(r);
    for (auto& child : children_) child->render(graphics, mouseX, mouseY);
}

ScrollList::ClickOutcome ListRow::mouseClicked(const Rect& r, double px, double py, int button) {
    place(r);
    for (auto& child : children_) {
        if (child->mouseClicked(px, py, button)) return {true, child->clicksOnPress()};
    }
    return {};
}

bool ListRow::mouseReleased(const Rect&, double px, double py, int button) {
    bool click = false;
    for (auto& child : children_) click = child->mouseReleased(px, py, button) || click;
    return click;
}

void ListRow::mouseDragged(const Rect&, double px, double py) {
    for (auto& child : children_) child->mouseDragged(px, py);
}

// ---- ListScreen ---------------------------------------------------------------------------

ListScreen::ListScreen(ScreenNavigator& navigator, std::string titleKey)
    : Screen(navigator), titleKey_(std::move(titleKey)) {}

void ListScreen::init() {
    const std::string title = text(titleKey_);
    const ListScreenLayout l = computeListScreenLayout(width_, height_, font().width(title));

    addLabel(title, l.title.x, l.title.y);
    list_ = &addWidget<ScrollList>(l.list.x, l.list.y, l.list.width, l.list.height, metrics::kRowHeight, kBigWidth,
                                   ScrollList::ScrollbarPlacement::RightOfRow,
                                   /*shrinkRowsWhenScrollable=*/false, /*selectable=*/false);
    addOptions();
    addFooter(l);
}

void ListScreen::addFooter(const ListScreenLayout& l) { addButton(text("gui.done"), l.done, [this] { onClose(); }); }

void ListScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

void ListScreen::addBig(std::unique_ptr<Widget> widget) { list_->addEntry<ListRow>(width_).add(std::move(widget)); }

void ListScreen::addPair(std::unique_ptr<Widget> left, std::unique_ptr<Widget> right) {
    auto& row = list_->addEntry<ListRow>(width_);
    row.add(std::move(left));
    if (right) row.add(std::move(right));
}

void ListScreen::addHeader(const std::string& headerText) {
    list_->addEntry<HeaderRow>(headerText, width_, list_->entryCount() == 0);
}

std::string ListScreen::optionLabel(const std::string& captionKey, const std::string& value) const {
    return formatLanguageString(text("options.generic_value"), {text(captionKey), value});
}

std::function<bool()> ListScreen::shiftDown() const {
    return [this] { return navigator_.isShiftDown(); };
}

std::unique_ptr<CycleButton> ListScreen::makeToggle(const std::string& captionKey, bool initial, int width,
                                                    std::function<void(bool)> onChange) {
    auto button = std::make_unique<CycleButton>(
        0, 0, width, kWidgetHeight, 2, initial ? 0 : 1,
        [this, captionKey](int i) { return optionLabel(captionKey, text(i == 0 ? "options.on" : "options.off")); },
        shiftDown());
    button->onChange = [onChange = std::move(onChange)](int i) {
        if (onChange) onChange(i == 0);
    };
    return button;
}

std::unique_ptr<CycleButton> ListScreen::makeChoice(const std::string& captionKey, std::vector<std::string> valueKeys,
                                                    int current, int width, std::function<void(int)> onChange) {
    const int count = static_cast<int>(valueKeys.size());
    auto button = std::make_unique<CycleButton>(
        0, 0, width, kWidgetHeight, count, current,
        [this, captionKey, valueKeys = std::move(valueKeys)](int i) {
            return optionLabel(captionKey, text(valueKeys[static_cast<std::size_t>(i)]));
        },
        shiftDown());
    button->onChange = std::move(onChange);
    return button;
}

std::unique_ptr<SliderWidget> ListScreen::makeSlider(double initial, int width, std::function<std::string(double)> label,
                                                     std::function<void(double)> onChange) {
    auto slider = std::make_unique<SliderWidget>(0, 0, width, kWidgetHeight, initial, std::move(label));
    slider->onValueChanged = std::move(onChange);
    return slider;
}

double ListScreen::intToSlider(int min, int max, int v) {
    if (v <= min) return 0.0;
    if (v >= max) return 1.0;
    return (static_cast<double>(v) + 0.5 - min) / static_cast<double>(max + 1 - min);
}

std::unique_ptr<SliderWidget> ListScreen::makeIntSlider(int min, int max, int current, int width,
                                                        std::function<std::string(int)> label,
                                                        std::function<void(int)> onChange) {
    // OptionInstance.IntRange: toSliderValue / fromSliderValue.
    const auto fromSlider = [min, max](double s) {
        const double clamped = std::min(s, 0.99999);
        return std::min(max, min + static_cast<int>(std::floor(clamped * static_cast<double>(max + 1 - min))));
    };
    return makeSlider(
        intToSlider(min, max, current), width,
        [label = std::move(label), fromSlider](double s) { return label(fromSlider(s)); },
        [onChange = std::move(onChange), fromSlider](double s) {
            if (onChange) onChange(fromSlider(s));
        });
}

std::string ListScreen::percentLabel(const std::string& captionKey, double value) const {
    return formatLanguageString(text("options.percent_value"),
                                {text(captionKey), std::to_string(static_cast<int>(value * 100.0))});
}

std::string ListScreen::percentOrOffLabel(const std::string& captionKey, double value) const {
    return value == 0.0 ? optionLabel(captionKey, text("options.off")) : percentLabel(captionKey, value);
}

std::string ListScreen::pixelLabel(const std::string& captionKey, int pixels) const {
    return formatLanguageString(text("options.pixel_value"), {text(captionKey), std::to_string(pixels)});
}

std::string ListScreen::numberOrOffLabel(const std::string& captionKey, int value) const {
    return optionLabel(captionKey, value == 0 ? text("options.off") : std::to_string(value));
}

std::unique_ptr<Button> ListScreen::makeLink(const std::string& labelKey, int width,
                                             std::function<std::unique_ptr<Screen>()> open) {
    auto button = std::make_unique<Button>(text(labelKey), 0, 0, width, kWidgetHeight);
    button->onPress = [this, open = std::move(open)] {
        if (auto screen = open()) navigator_.push(std::move(screen));  // a link without a screen yet does nothing
    };
    return button;
}

}  // namespace falcon
