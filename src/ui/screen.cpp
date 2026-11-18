#include "falcon/ui/screen.hpp"

namespace falcon {

namespace {
Screen::PostInitHook& postInitHook() {
    static Screen::PostInitHook hook;
    return hook;
}
}  // namespace

void Screen::setPostInitHook(PostInitHook hook) { postInitHook() = std::move(hook); }

void Screen::resize(int width, int height) {
    width_ = width;
    height_ = height;
    widgets_.clear();
    init();
    if (postInitHook()) postInitHook()(*this);
}

void Screen::setFocus(Widget* widget) {
    for (auto& w : widgets_) w->focused = (w.get() == widget);
}

void Screen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    graphics.setTimeMillis(time.millis);
    for (auto& widget : widgets_) widget->render(graphics, mouseX, mouseY);
    graphics.renderTooltip();  // a hovered widget's tooltip goes on top of everything
}

bool Screen::mouseClicked(double x, double y, int button) {
    // Topmost widget first.
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        Widget& widget = **it;
        if (!widget.mouseClicked(x, y, button)) continue;
        setFocus(widget.focusable() ? &widget : nullptr);
        if (widget.clicksOnPress()) navigator_.playButtonClick();
        return true;
    }
    setFocus(nullptr);  // a click on empty space drops keyboard focus
    return false;
}

void Screen::mouseReleased(double x, double y, int button) {
    for (auto& widget : widgets_) {
        if (widget->mouseReleased(x, y, button)) navigator_.playButtonClick();
    }
}

void Screen::mouseDragged(double x, double y) {
    for (auto& widget : widgets_) widget->mouseDragged(x, y);
}

bool Screen::mouseScrolled(double x, double y, double dy) {
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        if ((*it)->mouseScrolled(x, y, dy)) return true;
    }
    return false;
}

bool Screen::keyPressed(int key, int mods) {
    for (auto& widget : widgets_) {
        if (widget->focused && widget->keyPressed(key, mods)) return true;
    }
    return false;
}

void Screen::charTyped(unsigned int codePoint) {
    for (auto& widget : widgets_) {
        if (widget->focused) widget->charTyped(codePoint);
    }
}

Button& Screen::addButton(const std::string& label, const Rect& rect, std::function<void()> onPress) {
    Button& button = addWidget<Button>(label, rect.x, rect.y, rect.width, rect.height);
    button.onPress = std::move(onPress);
    return button;
}

IconButton& Screen::addIconButton(const std::string& sprite, int iconSize, const Rect& rect,
                                  std::function<void()> onPress) {
    IconButton& button = addWidget<IconButton>(sprite, iconSize, iconSize, rect.x, rect.y, rect.width);
    button.onPress = std::move(onPress);
    return button;
}

LockedButton& Screen::addLockedButton(const std::string& label, const Rect& rect) {
    return addWidget<LockedButton>(label, rect.x, rect.y, rect.width, rect.height);
}

LabelWidget& Screen::addLabel(const std::string& label, int x, int y, std::uint32_t rgb) {
    return addWidget<LabelWidget>(label, x, y, font().width(label), Font::kLineHeight, rgb);
}

LabelWidget& Screen::addCenteredLabel(const std::string& label, int centerX, int y, std::uint32_t rgb) {
    return addLabel(label, centerX - font().width(label) / 2, y, rgb);
}

CheckboxWidget& Screen::addCheckbox(const std::string& label, int x, int y, bool checked, std::function<void(bool)> onChange) {
    auto& box = addWidget<CheckboxWidget>(label, x, y, font().width(label), checked);
    box.onChange = std::move(onChange);
    return box;
}

EditBox& Screen::addEditBox(const Rect& rect, const std::string& hint, std::size_t maxLength) {
    return addWidget<EditBox>(font(), rect.x, rect.y, rect.width, rect.height, hint, maxLength);
}

SliderWidget& Screen::addSlider(const Rect& rect, double initial, std::function<std::string(double)> label,
                                std::function<void(double)> onChange) {
    SliderWidget& slider = addWidget<SliderWidget>(rect.x, rect.y, rect.width, rect.height, initial, std::move(label));
    slider.onValueChanged = std::move(onChange);
    return slider;
}

}  // namespace falcon
