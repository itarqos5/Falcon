#include "falcon/game/disconnected_screen.hpp"

#include "falcon/text/text_layout.hpp"

#include <algorithm>

namespace falcon {

void DisconnectedScreen::init() {
    // The title, the reason and the button stand in one stack that is centred on the screen.
    if (!reason_.empty() && reason_[0] >= 'a' && reason_[0] <= 'z') reason_[0] = static_cast<char>(reason_[0] - 'a' + 'A');  // (Falcon's own reasons start in lower case)
    lines_ = wrapText(font(), reason_, std::max(50, width_ - 100));
    const int maxLines = std::max(1, (height_ - 100) / 9);
    if (static_cast<int>(lines_.size()) > maxLines) lines_.resize(static_cast<std::size_t>(maxLines));
    const int messageHeight = static_cast<int>(lines_.size()) * 9;
    const int stackHeight = 9 + 12 + messageHeight + 12 + 20;
    const int top = std::max(10, (height_ - stackHeight) / 2);
    titleY_ = top;
    messageY_ = top + 9 + 12;
    addButton(text("gui.toMenu"), {width_ / 2 - 100, messageY_ + messageHeight + 12, 200, 20}, [this] { onClose(); });
}

void DisconnectedScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    const std::string title = text(titleKey_);
    graphics.text(title, static_cast<float>(width_ / 2 - graphics.textWidth(title) / 2), static_cast<float>(titleY_), kWhite);
    int y = messageY_;
    for (const std::string& line : lines_) {
        graphics.text(line, static_cast<float>(width_ / 2 - graphics.textWidth(line) / 2), static_cast<float>(y), 0xFFAAAAAAu);
        y += 9;
    }
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
