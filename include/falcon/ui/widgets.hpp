#pragma once

#include "falcon/ui/gui_graphics.hpp"

#include <functional>
#include <string>

namespace falcon {

// Base GUI widget: a rectangle that can be hovered and clicked.
class Widget {
public:
    Widget(int px, int py, int w, int h) : x(px), y(py), width(w), height(h) {}
    virtual ~Widget() = default;

    int x;
    int y;
    int width;
    int height;
    bool active = true;
    bool visible = true;
    bool interactive = true;  // false for plain labels: no hover cursor, never consumes clicks
    float alpha = 1.0f;
    std::function<void()> onPress;
    // Shown in a box next to the cursor while the widget is hovered ('\n' starts a new paragraph).
    std::string tooltip;

    [[nodiscard]] bool isHovered() const { return hovered_; }
    [[nodiscard]] bool contains(double px, double py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }

    // Updates hover state, draws the widget and asks for the matching cursor shape.
    void render(GuiGraphics& graphics, int mouseX, int mouseY);

    // Returns true if the click was consumed.
    virtual bool mouseClicked(double px, double py, int button);
    // Returns true if the release should play the click sound (sliders click on release).
    virtual bool mouseReleased(double /*px*/, double /*py*/, int /*button*/) { return false; }
    virtual void mouseDragged(double /*px*/, double /*py*/) {}
    // Whether pressing this widget plays the click sound right away.
    [[nodiscard]] virtual bool clicksOnPress() const { return true; }

    // Mouse wheel over the widget (dy > 0 = up). Returns true if consumed.
    virtual bool mouseScrolled(double /*px*/, double /*py*/, double /*dy*/) { return false; }
    // Keyboard input, delivered only to the focused widget.
    virtual bool keyPressed(int /*key*/, int /*mods*/) { return false; }
    virtual void charTyped(unsigned int /*codePoint*/) {}
    [[nodiscard]] virtual bool focusable() const { return false; }
    bool focused = false;

protected:
    virtual void renderContents(GuiGraphics& graphics) = 0;
    [[nodiscard]] virtual CursorType hoverCursor() const {
        return active ? CursorType::PointingHand : CursorType::NotAllowed;
    }

    int mouseX_ = 0;  // the mouse position passed to the current render()
    int mouseY_ = 0;

private:
    bool hovered_ = false;
};

// Standard 20px-tall button: nine-slice background sprite and a centred label.
class Button : public Widget {
public:
    Button(std::string message, int px, int py, int w, int h)
        : Widget(px, py, w, h), message_(std::move(message)) {}

    void setMessage(std::string message) { message_ = std::move(message); }
    [[nodiscard]] const std::string& message() const { return message_; }

protected:
    void renderContents(GuiGraphics& graphics) override;
    void drawBackground(GuiGraphics& graphics) const;
    void drawLabel(GuiGraphics& graphics) const;

    std::string message_;
};

// A button that is locked: it cannot be pressed and shows the padlock sprite. The wide body is the
// disabled button background, the left 20px is the padlock cell (`widget/locked_button_*`), and
// the label is centred in the space to its right.
class LockedButton : public Button {
public:
    LockedButton(std::string message, int px, int py, int w, int h) : Button(std::move(message), px, py, w, h) {
        active = false;  // hovering shows the "not allowed" cursor and clicks are ignored
    }

protected:
    void renderContents(GuiGraphics& graphics) override;
};

// Button with a sprite icon centred on top of the normal background.
class IconButton : public Button {
public:
    IconButton(std::string iconSprite, int iconW, int iconH, int px, int py, int size)
        : Button({}, px, py, size, size),
          icon_(std::move(iconSprite)),
          iconWidth_(iconW),
          iconHeight_(iconH) {}

protected:
    void renderContents(GuiGraphics& graphics) override;

private:
    std::string icon_;
    int iconWidth_;
    int iconHeight_;
};

// Borderless text that gets underlined while hovered (the copyright line).
class PlainTextButton : public Widget {
public:
    PlainTextButton(std::string message, int px, int py, int w, int h)
        : Widget(px, py, w, h), message_(std::move(message)) {}

protected:
    void renderContents(GuiGraphics& graphics) override;

private:
    std::string message_;
};

// Plain white text with a shadow, no background, not clickable (a screen title).
class LabelWidget : public Widget {
public:
    LabelWidget(std::string text, int px, int py, int w, int h, std::uint32_t rgb = 0xFFFFFFu)
        : Widget(px, py, w, h), text_(std::move(text)), rgb_(rgb & 0xFFFFFFu) {
        interactive = false;
    }

    bool mouseClicked(double, double, int) override { return false; }

protected:
    void renderContents(GuiGraphics& graphics) override;

private:
    std::string text_;
    std::uint32_t rgb_;
};

// A button that steps through a fixed list of values: click = next, shift+click = previous
// (CycleButton). The label is produced from the current index by `labelFor`.
class CycleButton : public Button {
public:
    CycleButton(int px, int py, int w, int h, int valueCount, int initialIndex,
                std::function<std::string(int index)> labelFor, std::function<bool()> shiftDown);

    // Called with the new index after every change.
    std::function<void(int index)> onChange;
    [[nodiscard]] int index() const { return index_; }
    [[nodiscard]] int valueCount() const { return count_; }

    // Replaces the number of values (the list behind the button changed) and the selected one,
    // refreshing the label. Does not call onChange.
    void setValues(int valueCount, int index);

    // With a single value there is nothing to cycle to: the button does nothing, not even click.
    [[nodiscard]] bool clicksOnPress() const override { return count_ > 1; }

private:
    int count_;
    int index_;
    std::function<std::string(int)> labelFor_;
    std::function<bool()> shiftDown_;
};

// Single-line text input (EditBox): caret, backspace/delete, arrows/home/end, max length, and a
// grey hint shown while it is empty and unfocused.
class EditBox : public Widget {
public:
    EditBox(const Font& font, int px, int py, int w, int h, std::string hint, std::size_t maxLength = 32);

    std::function<void(const std::string& text)> onChange;
    [[nodiscard]] std::string text() const;
    void setText(const std::string& utf8);

    bool mouseClicked(double px, double py, int button) override;
    bool keyPressed(int key, int mods) override;
    void charTyped(unsigned int codePoint) override;
    [[nodiscard]] bool focusable() const override { return true; }
    [[nodiscard]] bool clicksOnPress() const override { return false; }  // typing is silent

protected:
    void renderContents(GuiGraphics& graphics) override;
    [[nodiscard]] CursorType hoverCursor() const override { return CursorType::Text; }

private:
    void changed();
    [[nodiscard]] int prefixWidth(std::size_t chars) const;

    const Font& font_;
    std::string hint_;
    std::size_t maxLength_;
    std::u32string chars_;
    std::size_t cursor_ = 0;
};

// A tick box with its label to the right (Checkbox): a click toggles it. The 17x17 box is drawn from the
// widget/checkbox sprites; the widget is as wide as the box plus the label.
class CheckboxWidget : public Widget {
public:
    static constexpr int kBoxSize = 17;
    static constexpr int kLabelGap = 4;

    CheckboxWidget(std::string label, int px, int py, int labelWidth, bool checked)
        : Widget(px, py, kBoxSize + kLabelGap + labelWidth, kBoxSize), label_(std::move(label)), checked_(checked) {}

    // Called with the new state after every click.
    std::function<void(bool checked)> onChange;
    [[nodiscard]] bool checked() const { return checked_; }
    void setChecked(bool checked) { checked_ = checked; }

    bool mouseClicked(double px, double py, int button) override;

protected:
    void renderContents(GuiGraphics& graphics) override;

private:
    std::string label_;
    bool checked_;
};

// Horizontal slider with an 8px handle. The value runs 0..1 and the label is recomputed from it.
class SliderWidget : public Widget {
public:
    using LabelFunction = std::function<std::string(double value)>;

    SliderWidget(int px, int py, int w, int h, double initialValue, LabelFunction label);

    // Called whenever the value actually changes (also while dragging).
    std::function<void(double value)> onValueChanged;

    [[nodiscard]] double value() const { return value_; }
    [[nodiscard]] bool dragging() const { return dragging_; }
    void setValue(double value);  // clamps to 0..1
    // Sets the value and refreshes the label without calling onValueChanged (to re-sync a widget
    // after the option behind it was changed elsewhere).
    void syncValue(double value);

    bool mouseClicked(double px, double py, int button) override;
    bool mouseReleased(double px, double py, int button) override;
    void mouseDragged(double px, double py) override;
    [[nodiscard]] bool clicksOnPress() const override { return false; }

protected:
    void renderContents(GuiGraphics& graphics) override;
    [[nodiscard]] CursorType hoverCursor() const override;

private:
    void setValueFromMouse(double px);

    double value_;
    bool dragging_ = false;
    LabelFunction labelFunction_;
    std::string message_;
};

}  // namespace falcon
