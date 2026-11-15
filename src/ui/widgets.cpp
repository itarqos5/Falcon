#include "falcon/ui/widgets.hpp"

#include "falcon/util/text_util.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

constexpr std::uint32_t kActiveText = 0x00FFFFFFu;
constexpr std::uint32_t kInactiveText = 0x00A0A0A0u;  // -6250336 in the game's signed ARGB
constexpr int kSliderHandleWidth = 8;

// Button/slider label: centred horizontally between 2px margins, and vertically at
// (top + bottom - lineHeight) / 2 + 1.
void drawCentredLabel(GuiGraphics& graphics, const std::string& fullMessage, int x, int y, int width,
                      int height, bool active, float alpha) {
    // A label wider than the button (a long translation) is cut short with "..." so it stays inside.
    std::string message = fullMessage;
    const int room = width - 4;
    if (graphics.textWidth(message) > room) {
        const int dots = graphics.textWidth("...");
        while (!message.empty() && graphics.textWidth(message) > room - dots) {
            std::size_t n = message.size();
            do {
                --n;
            } while (n > 0 && (static_cast<unsigned char>(message[n]) & 0xC0) == 0x80);
            message.resize(n);
        }
        message += "...";
    }
    const int textWidth = graphics.textWidth(message);
    const int centerX = ((x + 2) + (x + width - 2)) / 2;
    const int textY = (y + (y + height) - Font::kLineHeight) / 2 + 1;
    graphics.text(message, static_cast<float>(centerX - textWidth / 2), static_cast<float>(textY),
                  argbWithAlpha(active ? kActiveText : kInactiveText, alpha));
}

}  // namespace

void Widget::render(GuiGraphics& graphics, int mouseX, int mouseY) {
    if (!visible) return;
    mouseX_ = mouseX;
    mouseY_ = mouseY;
    // Like the game, hover only counts inside the active scissor (e.g. the visible part of a list).
    hovered_ = graphics.containsPointInScissor(mouseX, mouseY) && contains(mouseX, mouseY);
    renderContents(graphics);
    if (hovered_ && interactive) graphics.requestCursor(hoverCursor());
    if (hovered_ && !tooltip.empty()) graphics.setTooltip(tooltip, mouseX, mouseY, x, y, width, height);
}

bool Widget::mouseClicked(double px, double py, int button) {
    if (!visible || !active || !interactive || button != 0 || !contains(px, py)) return false;
    if (onPress) onPress();
    return true;
}

// ---- Button -----------------------------------------------------------------------------

void Button::drawBackground(GuiGraphics& graphics) const {
    const char* sprite = !active ? "widget/button_disabled"
                                 : (isHovered() ? "widget/button_highlighted" : "widget/button");
    graphics.blitSprite(sprite, x, y, width, height, argbWhite(alpha));
}

void Button::drawLabel(GuiGraphics& graphics) const {
    drawCentredLabel(graphics, message_, x, y, width, height, active, alpha);
}

void Button::renderContents(GuiGraphics& graphics) {
    drawBackground(graphics);
    drawLabel(graphics);
}

// ---- LockedButton -----------------------------------------------------------------------

void LockedButton::renderContents(GuiGraphics& graphics) {
    constexpr int kLockCell = 20;  // the padlock sprites are 20x20
    const std::uint32_t tint = argbWhite(alpha);
    graphics.blitSprite("widget/button_disabled", x, y, width, height, tint);
    graphics.blitSprite("widget/locked_button_disabled", x, y, kLockCell, height, tint);
    drawCentredLabel(graphics, message_, x + kLockCell, y, width - kLockCell, height,
                     /*active=*/false, alpha);
}

// ---- IconButton -------------------------------------------------------------------------

void IconButton::renderContents(GuiGraphics& graphics) {
    drawBackground(graphics);
    const int iconX = x + width / 2 - iconWidth_ / 2;
    const int iconY = y + height / 2 - iconHeight_ / 2;
    graphics.blitSprite(icon_, iconX, iconY, iconWidth_, iconHeight_, argbWhite(alpha));
}

// ---- PlainTextButton --------------------------------------------------------------------

void PlainTextButton::renderContents(GuiGraphics& graphics) {
    graphics.text(message_, static_cast<float>(x), static_cast<float>(y), argbWhite(alpha),
                  /*shadow=*/true, /*underline=*/isHovered());
}

// ---- LabelWidget ------------------------------------------------------------------------

void LabelWidget::renderContents(GuiGraphics& graphics) {
    // StringWidget: drawn at (x, y + (height - lineHeight) / 2).
    graphics.text(text_, static_cast<float>(x),
                  static_cast<float>(y + (height - Font::kLineHeight) / 2), argbWithAlpha(rgb_, alpha));
}

// ---- CheckboxWidget ---------------------------------------------------------------------

bool CheckboxWidget::mouseClicked(double px, double py, int button) {
    if (!Widget::mouseClicked(px, py, button)) return false;
    checked_ = !checked_;
    if (onChange) onChange(checked_);
    return true;
}

void CheckboxWidget::renderContents(GuiGraphics& graphics) {
    const char* sprite = checked_ ? (isHovered() ? "widget/checkbox_selected_highlighted" : "widget/checkbox_selected")
                                  : (isHovered() ? "widget/checkbox_highlighted" : "widget/checkbox");
    graphics.blitSprite(sprite, x, y, kBoxSize, kBoxSize, argbWhite(alpha));
    graphics.text(label_, static_cast<float>(x + kBoxSize + kLabelGap),
                  static_cast<float>(y + (kBoxSize - Font::kLineHeight) / 2 + 1), argbWithAlpha(0xE0E0E0u, alpha));
}

// ---- CycleButton ------------------------------------------------------------------------

CycleButton::CycleButton(int px, int py, int w, int h, int valueCount, int initialIndex,
                         std::function<std::string(int index)> labelFor,
                         std::function<bool()> shiftDown)
    : Button({}, px, py, w, h),
      count_(std::max(valueCount, 1)),
      index_(std::clamp(initialIndex, 0, std::max(valueCount, 1) - 1)),
      labelFor_(std::move(labelFor)),
      shiftDown_(std::move(shiftDown)) {
    message_ = labelFor_(index_);
    onPress = [this] {
        if (count_ <= 1) return;  // nothing to cycle to
        const int step = (shiftDown_ && shiftDown_()) ? -1 : 1;  // shift+click goes backwards
        index_ = ((index_ + step) % count_ + count_) % count_;
        message_ = labelFor_(index_);
        if (onChange) onChange(index_);
    };
}

void CycleButton::setValues(int valueCount, int index) {
    count_ = std::max(valueCount, 1);
    index_ = std::clamp(index, 0, count_ - 1);
    message_ = labelFor_(index_);
}

// ---- EditBox ----------------------------------------------------------------------------

EditBox::EditBox(const Font& font, int px, int py, int w, int h, std::string hint,
                 std::size_t maxLength)
    : Widget(px, py, w, h), font_(font), hint_(std::move(hint)), maxLength_(maxLength) {}

std::string EditBox::text() const { return encodeUtf8(chars_); }

void EditBox::setText(const std::string& utf8) {
    chars_.clear();
    for (const char32_t cp : decodeUtf8(utf8)) {
        if (chars_.size() < maxLength_) chars_.push_back(cp);
    }
    cursor_ = chars_.size();
}

void EditBox::changed() {
    if (onChange) onChange(text());
}

int EditBox::prefixWidth(std::size_t count) const {
    return font_.width(encodeUtf8(chars_.substr(0, count)));
}

bool EditBox::mouseClicked(double px, double py, int button) {
    if (!visible || !active || button != 0 || !contains(px, py)) return false;
    // Put the caret at the character boundary nearest to the click.
    const double relative = px - (x + 4);
    std::size_t best = 0;
    double bestDistance = relative < 0 ? -relative : relative;
    for (std::size_t i = 1; i <= chars_.size(); ++i) {
        const double d = std::abs(static_cast<double>(prefixWidth(i)) - relative);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    cursor_ = best;
    return true;
}

bool EditBox::keyPressed(int key, int /*mods*/) {
    constexpr int kBackspace = 259;
    constexpr int kDelete = 261;
    constexpr int kRight = 262;
    constexpr int kLeft = 263;
    constexpr int kHome = 268;
    constexpr int kEnd = 269;
    switch (key) {
        case kBackspace:
            if (cursor_ > 0) {
                chars_.erase(cursor_ - 1, 1);
                --cursor_;
                changed();
            }
            return true;
        case kDelete:
            if (cursor_ < chars_.size()) {
                chars_.erase(cursor_, 1);
                changed();
            }
            return true;
        case kLeft:
            if (cursor_ > 0) --cursor_;
            return true;
        case kRight:
            if (cursor_ < chars_.size()) ++cursor_;
            return true;
        case kHome:
            cursor_ = 0;
            return true;
        case kEnd:
            cursor_ = chars_.size();
            return true;
        default:
            return false;
    }
}

void EditBox::charTyped(unsigned int codePoint) {
    if (codePoint < 32 || codePoint == 127 || chars_.size() >= maxLength_) return;
    chars_.insert(cursor_, 1, static_cast<char32_t>(codePoint));
    ++cursor_;
    changed();
}

void EditBox::renderContents(GuiGraphics& graphics) {
    if (bordered) {
        graphics.blitSprite(focused ? "widget/text_field_highlighted" : "widget/text_field", x, y, width, height, argbWhite(alpha));
    }
    const int textX = bordered ? x + 4 : x;
    const int textY = bordered ? y + (height - 8) / 2 : y;
    constexpr std::uint32_t kTextColor = 0xFFE0E0E0u;  // EditBox.DEFAULT_TEXT_COLOR
    constexpr std::uint32_t kHintColor = 0xFFAAAAAAu;  // ChatFormatting.GRAY

    if (chars_.empty() && !focused) {
        graphics.text(hint_, static_cast<float>(textX), static_cast<float>(textY), kHintColor);
        return;
    }
    graphics.text(text(), static_cast<float>(textX), static_cast<float>(textY), kTextColor);
    if (focused && (static_cast<long long>(graphics.timeMillis()) / 300) % 2 == 0) {
        const int caretX = textX + prefixWidth(cursor_);
        if (cursor_ == chars_.size()) {
            graphics.text("_", static_cast<float>(caretX), static_cast<float>(textY), kTextColor);
        } else {
            graphics.fill(caretX, textY - 1, caretX + 1, textY + 1 + Font::kLineHeight, 0xFFD0D0D0u);
        }
    }
}

// ---- SliderWidget -----------------------------------------------------------------------

SliderWidget::SliderWidget(int px, int py, int w, int h, double initialValue, LabelFunction label)
    : Widget(px, py, w, h),
      value_(std::clamp(initialValue, 0.0, 1.0)),
      labelFunction_(std::move(label)) {
    message_ = labelFunction_ ? labelFunction_(value_) : std::string();
}

void SliderWidget::setValue(double value) {
    const double old = value_;
    value_ = std::clamp(value, 0.0, 1.0);
    if (old != value_ && onValueChanged) onValueChanged(value_);
    if (labelFunction_) message_ = labelFunction_(value_);
}

void SliderWidget::syncValue(double value) {
    value_ = std::clamp(value, 0.0, 1.0);
    if (labelFunction_) message_ = labelFunction_(value_);
}

void SliderWidget::setValueFromMouse(double px) {
    setValue((px - (x + kSliderHandleWidth / 2)) / static_cast<double>(width - kSliderHandleWidth));
}

bool SliderWidget::mouseClicked(double px, double py, int button) {
    if (!visible || !active || button != 0 || !contains(px, py)) return false;
    dragging_ = true;
    setValueFromMouse(px);
    return true;
}

bool SliderWidget::mouseReleased(double, double, int button) {
    if (button != 0 || !dragging_) return false;
    dragging_ = false;
    return true;  // the game plays the click when a slider is let go
}

void SliderWidget::mouseDragged(double px, double) {
    if (dragging_) setValueFromMouse(px);
}

CursorType SliderWidget::hoverCursor() const {
    if (!active) return CursorType::NotAllowed;
    return dragging_ ? CursorType::ResizeHorizontal : CursorType::PointingHand;
}

void SliderWidget::renderContents(GuiGraphics& graphics) {
    const std::uint32_t tint = argbWhite(alpha);
    graphics.blitSprite("widget/slider", x, y, width, height, tint);

    const int handleX = x + static_cast<int>(value_ * static_cast<double>(width - kSliderHandleWidth));
    const bool highlighted = active && (isHovered() || dragging_);
    graphics.blitSprite(highlighted ? "widget/slider_handle_highlighted" : "widget/slider_handle",
                        handleX, y, kSliderHandleWidth, height, tint);

    drawCentredLabel(graphics, message_, x, y, width, height, active, alpha);
}

}  // namespace falcon
