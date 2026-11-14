#pragma once

#include "apex/game/game_options.hpp"
#include "apex/text/styled_text.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace apex {

class GuiGraphics;
class Font;

// The chat: the scrolling log of messages at the bottom-left of the screen, and the sent-message history. It follows the
// game's ChatComponent: messages are word-wrapped to a width that the Chat Width option sets, each stays bright for 200
// ticks and then fades, and when the chat is open (focused) all of them show at full brightness and can be scrolled.
class ChatComponent {
public:
    // Chat dimensions follow the game's own formulas (ChatComponent.getWidth / getHeight), from the chat options.
    [[nodiscard]] static int width(const GameOptions& o) { return static_cast<int>(o.chatWidth * 280.0 + 40.0); }
    [[nodiscard]] static int heightFocused(const GameOptions& o) { return static_cast<int>(o.chatHeightFocused * 160.0 + 20.0); }
    [[nodiscard]] static int heightUnfocused(const GameOptions& o) { return static_cast<int>(o.chatHeightUnfocused * 160.0 + 20.0); }

    static constexpr int kLineHeight = 9;
    static constexpr int kBottomMargin = 40;  // the chat's lowest line sits this far above the bottom
    static constexpr int kIndent = 4;
    static constexpr std::size_t kMaxLines = 100;
    static constexpr std::size_t kMaxSentHistory = 100;
    static constexpr std::size_t kMaxMessageLength = 256;

    // Adds a message to the log (word-wrapped). The text may use section-sign colour codes.
    void addMessage(const std::string& text, const Font& font, const GameOptions& options);
    // One game tick: ages the messages for the fade.
    void tick() { ++clock_; }

    // Draws the log. `focused` (the chat screen is open) shows every line at full brightness with a scrollbar.
    void render(GuiGraphics& g, int guiWidth, int guiHeight, const GameOptions& options, bool focused) const;

    // Scrolling while the chat is open.
    void scroll(int lines, const GameOptions& options);
    void resetScroll() { scrollPos_ = 0; }
    [[nodiscard]] int scrollPos() const { return scrollPos_; }

    // The sent-message history (Up/Down in the chat screen), newest last.
    void remember(const std::string& sent);
    [[nodiscard]] const std::vector<std::string>& history() const { return sentHistory_; }

    void clear() { lines_.clear(); scrollPos_ = 0; }

private:
    [[nodiscard]] int linesPerPage(const GameOptions& o, bool focused) const {
        return std::max(1, (focused ? heightFocused(o) : heightUnfocused(o)) / kLineHeight);
    }

    struct Line {
        StyledText text;  // one wrapped line, its colour runs kept
        int addedClock = 0;
    };
    std::deque<Line> lines_;             // wrapped lines, oldest at the front, newest at the back
    std::vector<std::string> sentHistory_;
    int clock_ = 0;
    int scrollPos_ = 0;                  // lines scrolled up from the bottom
};

}  // namespace apex
