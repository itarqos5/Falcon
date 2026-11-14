#include "apex/game/chat_screen.hpp"

#include "apex/game/game_session.hpp"
#include "apex/util/text_util.hpp"

#include <algorithm>

namespace apex {
namespace {
constexpr int kEnter = 257, kKpEnter = 335, kBackspace = 259, kDelete = 261;
constexpr int kRight = 262, kLeft = 263, kArrowDown = 264, kArrowUp = 265, kPageUp = 266, kPageDown = 267, kHome = 268, kEnd = 269;
}  // namespace

ChatScreen::ChatScreen(ScreenNavigator& navigator, ScreenServices& services, std::string initial)
    : Screen(navigator), services_(services) {
    for (const char32_t cp : decodeUtf8(initial)) input_.push_back(cp);
    cursor_ = input_.size();
    draft_ = initial;
}

void ChatScreen::onClose() {
    if (services_.session != nullptr) services_.session->chat().resetScroll();
    navigator_.pop();
}

void ChatScreen::send() {
    std::string text = encodeUtf8(input_);
    // Trim trailing spaces, as the game does.
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (services_.session != nullptr && !text.empty()) services_.session->chatInput(text);
    onClose();
}

void ChatScreen::setFromHistory(int delta) {
    if (services_.session == nullptr) return;
    const std::vector<std::string>& history = services_.session->chat().history();
    if (history.empty()) return;
    if (delta < 0) {  // older
        if (historyPos_ == -1) {
            draft_ = encodeUtf8(input_);
            historyPos_ = 0;
        } else {
            historyPos_ = std::min(historyPos_ + 1, static_cast<int>(history.size()) - 1);
        }
    } else {  // newer
        if (historyPos_ <= 0) {
            historyPos_ = -1;
            input_.clear();
            for (const char32_t cp : decodeUtf8(draft_)) input_.push_back(cp);
            cursor_ = input_.size();
            return;
        }
        --historyPos_;
    }
    const std::string& entry = history[history.size() - 1 - static_cast<std::size_t>(historyPos_)];
    input_.clear();
    for (const char32_t cp : decodeUtf8(entry)) input_.push_back(cp);
    cursor_ = input_.size();
}

bool ChatScreen::keyPressed(int key, int /*mods*/) {
    if (services_.session == nullptr) return true;
    switch (key) {
        case kEnter:
        case kKpEnter:
            send();
            return true;
        case kBackspace:
            if (cursor_ > 0) {
                input_.erase(cursor_ - 1, 1);
                --cursor_;
                historyPos_ = -1;
            }
            return true;
        case kDelete:
            if (cursor_ < input_.size()) input_.erase(cursor_, 1);
            return true;
        case kLeft:
            if (cursor_ > 0) --cursor_;
            return true;
        case kRight:
            if (cursor_ < input_.size()) ++cursor_;
            return true;
        case kHome:
            cursor_ = 0;
            return true;
        case kEnd:
            cursor_ = input_.size();
            return true;
        case kArrowUp:
            setFromHistory(-1);
            return true;
        case kArrowDown:
            setFromHistory(1);
            return true;
        case kPageUp:
            services_.session->chat().scroll(ChatComponent::heightFocused(services_.options) / ChatComponent::kLineHeight - 1, services_.options);
            return true;
        case kPageDown:
            services_.session->chat().scroll(-(ChatComponent::heightFocused(services_.options) / ChatComponent::kLineHeight - 1), services_.options);
            return true;
        default:
            return true;  // swallow everything else so the world does not receive it
    }
}

void ChatScreen::charTyped(unsigned int codePoint) {
    if (codePoint < 32 || codePoint == 127 || input_.size() >= ChatComponent::kMaxMessageLength) return;
    input_.insert(cursor_, 1, static_cast<char32_t>(codePoint));
    ++cursor_;
    historyPos_ = -1;
}

bool ChatScreen::mouseScrolled(double /*x*/, double /*y*/, double dy) {
    if (services_.session != nullptr) services_.session->chat().scroll(static_cast<int>(dy > 0 ? 1 : -1) * (dy == 0 ? 0 : std::max(1, static_cast<int>(std::abs(dy)))), services_.options);
    return true;
}

void ChatScreen::render(GuiGraphics& g, int /*mouseX*/, int /*mouseY*/, const FrameTime& time) {
    if (services_.session != nullptr) services_.session->renderHud(g, width_, height_, /*chatFocused=*/true);

    // The input box across the bottom, with the typed text and a blinking caret.
    const int boxTop = height_ - 14;
    g.fill(2, boxTop, width_ - 2, height_ - 2, 0x80000000u);
    const std::string before = encodeUtf8(input_.substr(0, cursor_));
    const std::string all = encodeUtf8(input_);
    const float textY = static_cast<float>(height_ - 12);
    g.text(all, 4.0f, textY, 0xFFE0E0E0u);
    const bool caretOn = (static_cast<long long>(time.millis / 400.0) % 2) == 0;
    if (caretOn) {
        const int caretX = 4 + g.textWidth(before);
        if (cursor_ < input_.size()) {
            g.fill(caretX, static_cast<int>(textY) - 1, caretX + 1, static_cast<int>(textY) + Font::kLineHeight, 0xFFE0E0E0u);
        } else {
            g.text("_", static_cast<float>(caretX), textY, 0xFFE0E0E0u);
        }
    }
}

}  // namespace apex
