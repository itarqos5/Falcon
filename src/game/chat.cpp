#include "apex/game/chat.hpp"

#include "apex/text/font.hpp"
#include "apex/ui/gui_graphics.hpp"

#include <algorithm>
#include <cmath>

namespace apex {
namespace {

// ChatComponent's fade: a message is at full brightness for its first ticks and then fades out, gone by 200 ticks.
double timeFactor(int ticksLived) {
    double d = static_cast<double>(ticksLived) / 200.0;
    d = 1.0 - d;
    d *= 10.0;
    d = std::clamp(d, 0.0, 1.0);
    return d * d;
}

}  // namespace

void ChatComponent::addMessage(const std::string& text, const Font& font, const GameOptions& options) {
    const std::vector<StyledText> wrapped = wrapStyledText(font, parseLegacyFormatting(text), width(options), kMaxLines);
    const int added = static_cast<int>(wrapped.size());
    for (const StyledText& line : wrapped) lines_.push_back({line, clock_});
    while (lines_.size() > kMaxLines) lines_.pop_front();
    // Keep the view where it was if the player has scrolled up.
    if (scrollPos_ > 0) scrollPos_ = std::min(scrollPos_ + added, static_cast<int>(lines_.size()) - 1);
}

void ChatComponent::scroll(int lines, const GameOptions& options) {
    const int page = linesPerPage(options, true);
    const int maxScroll = std::max(0, static_cast<int>(lines_.size()) - page);
    scrollPos_ = std::clamp(scrollPos_ + lines, 0, maxScroll);
}

void ChatComponent::remember(const std::string& sent) {
    if (sent.empty()) return;
    if (!sentHistory_.empty() && sentHistory_.back() == sent) return;
    sentHistory_.push_back(sent);
    if (sentHistory_.size() > kMaxSentHistory) sentHistory_.erase(sentHistory_.begin());
}

void ChatComponent::render(GuiGraphics& g, int guiWidth, int guiHeight, const GameOptions& options, bool focused) const {
    (void)guiWidth;
    if (lines_.empty()) return;
    const int page = linesPerPage(options, focused);
    const int w = width(options);
    const int shown = std::min(page, static_cast<int>(lines_.size()) - scrollPos_);
    const double textOpacity = options.chatOpacity * 0.9 + 0.1;
    const double bgOpacity = options.textBackgroundOpacity;
    const int anchorBottom = guiHeight - kBottomMargin;

    for (int i = 0; i < shown; ++i) {
        const std::size_t index = lines_.size() - 1 - static_cast<std::size_t>(scrollPos_ + i);
        const Line& line = lines_[index];
        const double fade = focused ? 1.0 : timeFactor(clock_ - line.addedClock);
        if (fade <= 0.01) continue;
        const int top = anchorBottom - kLineHeight * (i + 1);
        const auto alpha = [](double a) { return static_cast<std::uint32_t>(std::clamp(a, 0.0, 1.0) * 255.0) << 24; };
        g.fill(2, top - 1, 2 + w + kIndent, top + kLineHeight - 1, alpha(fade * bgOpacity));
        // The line's colour runs, left to right, each with the message's current fade.
        float x = static_cast<float>(kIndent);
        for (const TextSpan& span : line.text) {
            const std::uint32_t rgb = span.hasColor ? span.rgb : 0xFFFFFFu;
            g.text(span.text, x, static_cast<float>(top), alpha(fade * textOpacity) | rgb, true, span.underlined, span.bold);
            x += static_cast<float>(g.textWidth(span.text, span.bold));
        }
    }

    // The scrollbar, when the chat is open and there is more above the page.
    if (focused && static_cast<int>(lines_.size()) > page) {
        const int totalPx = static_cast<int>(lines_.size()) * kLineHeight;
        const int pagePx = page * kLineHeight;
        const int top = anchorBottom - pagePx;
        const int barHeight = std::max(4, pagePx * pagePx / totalPx);
        const int range = pagePx - barHeight;
        const int maxScroll = std::max(1, static_cast<int>(lines_.size()) - page);
        const int barTop = top + range - range * scrollPos_ / maxScroll;
        g.fill(w + kIndent, barTop, w + kIndent + 2, barTop + barHeight, 0x80CCCCCCu);
    }
}

}  // namespace apex
