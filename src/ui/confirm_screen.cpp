#include "apex/ui/confirm_screen.hpp"

#include "apex/text/text_layout.hpp"

#include <algorithm>

namespace apex {
namespace {

constexpr int kMaxRows = 15;

}  // namespace

ConfirmScreen::ConfirmScreen(ScreenNavigator& navigator, std::string title, std::string message, std::string yesLabel,
                             std::string noLabel, std::function<void(bool)> callback)
    : Screen(navigator),
      title_(std::move(title)),
      message_(std::move(message)),
      yesLabel_(std::move(yesLabel)),
      noLabel_(std::move(noLabel)),
      callback_(std::move(callback)) {}

void ConfirmScreen::answer(bool yes) {
    navigator_.pop();
    if (callback_) callback_(yes);
}

void ConfirmScreen::init() {
    using namespace metrics;
    lines_ = wrapText(font(), message_, std::max(1, width_ - 50));
    if (lines_.size() > kMaxRows) lines_.resize(kMaxRows);

    int messageWidth = 0;
    for (const std::string& line : lines_) messageWidth = std::max(messageWidth, font().width(line));
    const int titleWidth = font().width(title_);
    const int rowWidth = 2 * kButtonWidth + kButtonGap;  // 304
    const int lines = static_cast<int>(lines_.size());

    // One centred column: title, message, then the buttons row with 16 px of padding on top.
    const int layoutWidth = std::max({rowWidth, titleWidth, messageWidth});
    const int layoutHeight = 9 + 8 + 9 * lines + 8 + (16 + kButtonHeight);
    const int lx = layout::centeredX(width_, layoutWidth);
    const int ly = layout::centeredY(height_, layoutHeight);
    titleX_ = lx + (layoutWidth - titleWidth) / 2;
    titleY_ = ly;
    messageX_ = lx + (layoutWidth - messageWidth) / 2;
    messageY_ = ly + 17;

    const int rowY = ly + 17 + 9 * lines + 8 + 16;
    const auto row = layout::centeredRow(layoutWidth, rowY, {kButtonWidth, kButtonWidth}, kButtonGap);
    addButton(yesLabel_, {lx + row[0].x, row[0].y, row[0].width, row[0].height}, [this] { answer(true); });
    addButton(noLabel_, {lx + row[1].x, row[1].y, row[1].width, row[1].height}, [this] { answer(false); });
}

void ConfirmScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    graphics.text(title_, static_cast<float>(titleX_), static_cast<float>(titleY_), kWhite);
    // The message lines are centred on the block, like the game's MultiLineTextWidget.
    int blockWidth = 0;
    for (const std::string& line : lines_) blockWidth = std::max(blockWidth, font().width(line));
    int y = messageY_;
    for (const std::string& line : lines_) {
        const int x = messageX_ + (blockWidth - font().width(line)) / 2;
        graphics.text(line, static_cast<float>(x), static_cast<float>(y), kWhite);
        y += 9;
    }
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace apex
