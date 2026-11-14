#include "apex/game/safety_screen.hpp"

#include "apex/game/join_multiplayer_screen.hpp"
#include "apex/text/text_layout.hpp"

#include <algorithm>

namespace apex {
namespace {

constexpr int kSpacing = 8;         // between the stacked elements
constexpr int kMessagePadding = 12;  // above and below the message
constexpr int kTitleHeight = 9;

}  // namespace

SafetyScreen::SafetyScreen(ScreenNavigator& navigator, ScreenServices& services) : Screen(navigator), services_(services) {}

void SafetyScreen::init() {
    // One stack, centred on the screen: the title, the message, the check box, and the two buttons.
    lines_ = wrapText(font(), text("multiplayerWarning.message"), width_ - 100);
    const int maxLines = std::max(1, (height_ - 100) / 9);
    if (static_cast<int>(lines_.size()) > maxLines) lines_.resize(static_cast<std::size_t>(maxLines));
    const int messageHeight = static_cast<int>(lines_.size()) * 9;

    const int stackHeight = kTitleHeight + kSpacing + kMessagePadding + messageHeight + kMessagePadding + kSpacing +
                            CheckboxWidget::kBoxSize + kSpacing + 20;
    const int top = static_cast<int>(0.5f * static_cast<float>(height_ - stackHeight));
    titleY_ = top;
    messageY_ = top + kTitleHeight + kSpacing + kMessagePadding;
    const int checkboxY = messageY_ + messageHeight + kMessagePadding + kSpacing;
    const int buttonsY = checkboxY + CheckboxWidget::kBoxSize + kSpacing;

    const std::string label = text("multiplayerWarning.check");
    const int checkboxWidth = CheckboxWidget::kBoxSize + CheckboxWidget::kLabelGap + font().width(label);
    addCheckbox(label, static_cast<int>(0.5f * static_cast<float>(width_ - checkboxWidth)), checkboxY, dontShowAgain_,
                [this](bool checked) { dontShowAgain_ = checked; });

    const auto buttons = layout::centeredRow(width_, buttonsY, {150, 150}, kSpacing);
    addButton(text("gui.proceed"), buttons[0], [this] {
        if (dontShowAgain_) services_.options.skipMultiplayerWarning = true;  // saved with the other settings
        navigator_.pop();
        navigator_.push(std::make_unique<JoinMultiplayerScreen>(navigator_, services_));
    });
    addButton(text("gui.back"), buttons[1], [this] { onClose(); });
}

void SafetyScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    const std::string title = text("multiplayerWarning.header");
    graphics.text(title, static_cast<float>(width_ / 2 - graphics.textWidth(title, true) / 2), static_cast<float>(titleY_),
                  kWhite, true, false, /*bold=*/true);
    int y = messageY_;
    for (const std::string& line : lines_) {
        graphics.text(line, static_cast<float>(width_ / 2 - graphics.textWidth(line) / 2), static_cast<float>(y), kWhite);
        y += 9;
    }
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace apex
