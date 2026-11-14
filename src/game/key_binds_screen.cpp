#include "falcon/game/key_binds_screen.hpp"

#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <cmath>
#include <numbers>

namespace falcon {
namespace {

constexpr std::uint32_t kYellow = 0xFFFF55u;  // ChatFormatting.YELLOW: the brackets and arrows
constexpr std::uint32_t kWhite24 = 0xFFFFFFu;
constexpr std::uint32_t kConflictBar = 0xFFFFFF00u;
constexpr int kHeaderHeight = 33;
constexpr int kFooterHeight = 33;

// What a row needs from the screen that owns it.
struct RowContext {
    ScreenServices& services;
    std::function<int()> waiting;     // the action waiting for a key, or -1
    std::function<void(int)> select;  // start waiting for a key for this action
};

// The key button: a standard button whose label is coloured pieces, scrolled back and forth when it is wider
// than the button (AbstractWidget.extractScrollingStringOverContents).
class KeyButton : public Button {
public:
    KeyButton() : Button({}, 0, 0, kKeyButtonWidth, kKeyBindsRowHeight) {}
    std::vector<KeyLabelPiece> pieces;

protected:
    void renderContents(GuiGraphics& g) override {
        drawBackground(g);
        int total = 0;
        for (const KeyLabelPiece& p : pieces) total += g.textWidth(p.text);
        const int left = x + 2;
        const int right = x + width - 2;
        const int textY = (y + (y + height) - Font::kLineHeight) / 2 + 1;
        int start = (left + right) / 2 - total / 2;
        const bool scrolls = total > right - left;
        if (scrolls) {
            // Slide the text so both ends are seen, pausing at each (the game's formula).
            const double overflow = total - (right - left);
            const double seconds = g.timeMillis() / 1000.0;
            const double period = std::max(overflow * 0.5, 3.0);
            const double f = std::sin(std::numbers::pi / 2.0 * std::cos(2.0 * std::numbers::pi * seconds / period)) / 2.0 + 0.5;
            start = left - static_cast<int>(f * overflow);
            g.pushScissor(left, y, right, y + height);
        }
        float tx = static_cast<float>(start);
        for (const KeyLabelPiece& p : pieces) {
            g.text(p.text, tx, static_cast<float>(textY), 0xFF000000u | p.rgb, true, p.underlined);
            tx += static_cast<float>(g.textWidth(p.text));
        }
        if (scrolls) g.popScissor();
    }
};

// A category title: bold and underlined, centred on the screen.
class CategoryEntry : public ScrollList::Entry {
public:
    CategoryEntry(std::string label, int screenWidth) : Entry(kKeyBindsRowHeight), label_(std::move(label)), screenWidth_(screenWidth) {}

    void render(GuiGraphics& g, const Rect& r, int, int, const ScrollList::RowState&) override {
        const int widgetWidth = g.textWidth(label_, true) + 8;  // FocusableTextWidget: 4 px of padding each side
        const int x = screenWidth_ / 2 - widgetWidth / 2;
        g.text(label_, static_cast<float>(x + 4), static_cast<float>(r.y + 1 + 4), kWhite, true, /*underline=*/true, /*bold=*/true);
    }

private:
    std::string label_;
    int screenWidth_;
};

// One action: its name, the key button and the Reset button.
class KeyEntry : public ScrollList::Entry {
public:
    KeyEntry(std::size_t index, std::shared_ptr<RowContext> context)
        : Entry(kKeyBindsRowHeight),
          index_(index),
          ctx_(std::move(context)),
          name_(ctx_->services.language.get(keyMappings()[index].name)),
          reset_(ctx_->services.language.get("controls.reset"), 0, 0, kResetButtonWidth, kKeyBindsRowHeight) {}

    void render(GuiGraphics& g, const Rect& r, int mouseX, int mouseY, const ScrollList::RowState&) override {
        KeyBindings& bindings = ctx_->services.options.keyBindings;
        const Language& lang = ctx_->services.language;
        place(r);

        // The name, left of the buttons (it is not clipped to them).
        g.text(name_, static_cast<float>(r.x + kKeyNameOffset), static_cast<float>(r.y + 6), kWhite);

        reset_.active = !bindings.isDefault(index_);
        reset_.render(g, mouseX, mouseY);

        const std::vector<std::size_t> conflicts = bindings.conflicts(index_);
        const bool conflict = !conflicts.empty();
        change_.pieces = keyButtonLabel(keyDisplayName(bindings.key(index_), lang), conflict, ctx_->waiting() == static_cast<int>(index_));
        change_.tooltip.clear();
        if (conflict) {  // "This key is also used for:" and the other actions, comma-separated
            std::string names;
            for (const std::size_t other : conflicts) names += (names.empty() ? "" : ",") + lang.get(keyMappings()[other].name);
            change_.tooltip = formatLanguageString(lang.get("controls.keybinds.duplicateKeybinds"), {names});
        }
        change_.render(g, mouseX, mouseY);
        if (conflict) g.fill(change_.x - 6, r.y + 1, change_.x - 3, r.y + 18, kConflictBar);  // the yellow bar
    }

    ScrollList::ClickOutcome mouseClicked(const Rect& r, double px, double py, int button) override {
        if (button != 0) return {};
        place(r);
        if (change_.contains(px, py)) {
            ctx_->select(static_cast<int>(index_));
            return {true, true};
        }
        if (reset_.contains(px, py) && !ctx_->services.options.keyBindings.isDefault(index_)) {
            ctx_->services.options.keyBindings.reset(index_);
            return {true, true};
        }
        return {};
    }

private:
    void place(const Rect& r) {
        reset_.x = r.x + kResetButtonOffset;
        reset_.y = r.y;
        change_.x = r.x + kKeyButtonOffset;
        change_.y = r.y;
    }

    std::size_t index_;
    std::shared_ptr<RowContext> ctx_;
    std::string name_;
    Button reset_;
    KeyButton change_;
};

}  // namespace

std::vector<KeyLabelPiece> keyButtonLabel(const std::string& keyName, bool conflict, bool waiting) {
    std::vector<KeyLabelPiece> pieces;
    if (conflict) {
        pieces = {{"[", kYellow, false}, {keyName, kWhite24, false}, {" ]", kYellow, false}};
    } else {
        pieces = {{keyName, kWhite24, false}};
    }
    if (!waiting) return pieces;
    for (KeyLabelPiece& p : pieces) p.underlined = true;  // the key (and its brackets) are underlined inside "> <"
    pieces.insert(pieces.begin(), {">", kYellow, false});
    pieces.push_back({" <", kYellow, false});
    return pieces;
}

KeyBindsLayout computeKeyBindsLayout(int width, int height, int titleWidth) {
    KeyBindsLayout l;
    l.titleX = (width - titleWidth) / 2;
    l.titleY = (kHeaderHeight - 9) / 2;  // 12
    l.list = {0, kHeaderHeight, width, height - kHeaderHeight - kFooterHeight};
    const int x = (width - 308) / 2;
    const int y = height - kFooterHeight + (kFooterHeight - 20) / 2;  // H - 27
    l.resetAll = {x, y, 150, 20};
    l.done = {x + 158, y, 150, 20};
    return l;
}

KeyBindsScreen::KeyBindsScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}

void KeyBindsScreen::init() {
    const std::string title = text("controls.keybinds.title");
    const KeyBindsLayout l = computeKeyBindsLayout(width_, height_, font().width(title));
    addLabel(title, l.titleX, l.titleY);

    auto& list = addWidget<ScrollList>(l.list.x, l.list.y, l.list.width, l.list.height, kKeyBindsRowHeight, kKeyBindsRowWidth,
                                       ScrollList::ScrollbarPlacement::RightOfRow,
                                       /*shrinkRowsWhenScrollable=*/false, /*selectable=*/false);

    auto context = std::make_shared<RowContext>(
        RowContext{services_, [this] { return waiting_; }, [this](int index) { waiting_ = index; }});
    // Grouped by category, each group sorted by the names in the current language.
    const auto order = sortedKeyMappingOrder([this](const std::string& key) { return text(key); });
    std::optional<KeyCategory> current;
    for (const std::size_t index : order) {
        const KeyCategory category = keyMappings()[index].category;
        if (!current || *current != category) {
            current = category;
            list.addEntry<CategoryEntry>(text(keyCategoryLabelKey(category)), width_);
        }
        list.addEntry<KeyEntry>(index, context);
    }

    resetAll_ = &addButton(text("controls.resetAll"), l.resetAll, [this] { bindings().resetAll(); });
    addButton(text("gui.done"), l.done, [this] { onClose(); });
}

bool KeyBindsScreen::mouseClicked(double x, double y, int button) {
    if (waiting_ >= 0) {  // any mouse press, anywhere, becomes the new binding
        bindings().set(static_cast<std::size_t>(waiting_), KeyId::mouse(button));
        waiting_ = -1;
        return true;
    }
    return Screen::mouseClicked(x, y, button);
}

bool KeyBindsScreen::keyPressed(int key, int mods) {
    constexpr int kEscape = 256;
    if (waiting_ >= 0) {
        if (key == kEscape) {
            bindings().set(static_cast<std::size_t>(waiting_), KeyId::unbound());  // unbinds; the screen stays open
        } else if (KeyId::isBindableKey(key)) {
            bindings().set(static_cast<std::size_t>(waiting_), KeyId::keyboard(key));
        } else {
            return true;  // a key GLFW cannot name: keep waiting
        }
        waiting_ = -1;
        return true;
    }
    return Screen::keyPressed(key, mods);
}

void KeyBindsScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    if (resetAll_ != nullptr) resetAll_->active = bindings().anyChanged();  // greyed while everything is at its default
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
