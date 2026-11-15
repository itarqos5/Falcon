#include "falcon/ui/text_edit.hpp"
#include "falcon/game/chat_screen.hpp"

#include "falcon/game/game_session.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>

namespace falcon {
namespace {
constexpr int kEnter = 257, kKpEnter = 335, kEscape = 256, kTab = 258;
constexpr int kArrowDown = 264, kArrowUp = 265, kPageUp = 266, kPageDown = 267;
constexpr int kSuggestionLines = 10;      // the most the list shows at once (autoSuggestions' line limit)
constexpr int kRowHeight = 12;
constexpr std::uint32_t kListBackground = 0xD0000000u, kSelectedColor = 0xFFFFFF00u, kListColor = 0xFFAAAAAAu, kGhostColor = 0xFF808080u;
constexpr const char* kSection = "\xC2\xA7";
}  // namespace

ChatScreen::ChatScreen(ScreenNavigator& navigator, ScreenServices& services, std::string initial)
    : Screen(navigator), services_(services) {
    for (const char32_t cp : decodeUtf8(initial)) input_.push_back(cp);
    cursor_ = highlight_ = input_.size();
    draft_ = initial;
    refreshCommand();
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
            cursor_ = highlight_ = input_.size();
            refreshCommand();
            return;
        }
        --historyPos_;
    }
    const std::string& entry = history[history.size() - 1 - static_cast<std::size_t>(historyPos_)];
    input_.clear();
    for (const char32_t cp : decodeUtf8(entry)) input_.push_back(cp);
    cursor_ = highlight_ = input_.size();
    refreshCommand();
}

// CommandSuggestions.updateCommandInfo: for a text that starts with "/", the suggestions at the caret, and when there are none the usage of what could come
// next (or the mistake, when what is typed cannot be a command).
void ChatScreen::refreshCommand() {
    suggestions_ = CommandSuggestions();
    usage_.clear();
    commandError_.clear();
    selected_ = 0;
    firstShown_ = 0;
    hidden_ = false;
    if (services_.session == nullptr || input_.empty() || input_[0] != U'/') return;
    const CommandTree* tree = services_.session->commandTree();
    if (tree == nullptr) return;
    const CommandContext context = services_.session->commandContext();
    const std::string before = encodeUtf8(input_.substr(1, cursor_ > 0 ? cursor_ - 1 : 0));
    suggestions_ = suggestCommands(*tree, context, before);
    if (!suggestions_.list.empty()) return;
    const CommandParse parse = parseCommand(*tree, context, before);
    if (!parse.complete && parse.end < before.size() && !parse.error.empty()) {
        commandError_ = parse.error.rfind("command.", 0) == 0 ? services_.language.get(parse.error) : parse.error;
        return;
    }
    usage_ = commandUsage(*tree, context, before);
    if (usage_.size() > static_cast<std::size_t>(kSuggestionLines)) usage_.resize(kSuggestionLines);
}

void ChatScreen::cycleSuggestion(int delta) {
    const int count = static_cast<int>(suggestions_.list.size());
    if (count == 0) return;
    selected_ = ((selected_ + delta) % count + count) % count;
    if (selected_ < firstShown_) firstShown_ = selected_;
    if (selected_ >= firstShown_ + kSuggestionLines) firstShown_ = selected_ - kSuggestionLines + 1;
    if (delta > 0 && selected_ == 0) firstShown_ = 0;
    if (delta < 0 && selected_ == count - 1) firstShown_ = std::max(0, count - kSuggestionLines);
}

// SuggestionsList.useSuggestion: the word being typed becomes the chosen suggestion.
void ChatScreen::applySuggestion() {
    if (suggestions_.list.empty() || hidden_) return;
    const std::string& chosen = suggestions_.list[static_cast<std::size_t>(selected_)];
    const std::size_t from = std::min(1 + suggestions_.start, input_.size());
    input_.erase(from, cursor_ > from ? cursor_ - from : 0);
    const std::vector<char32_t> decoded = decodeUtf8(chosen);
    const std::u32string text(decoded.begin(), decoded.end());
    input_.insert(from, text);
    cursor_ = highlight_ = from + text.size();
    historyPos_ = -1;
    refreshCommand();
}

bool ChatScreen::keyPressed(int key, int mods) {
    if (services_.session == nullptr) return true;
    {  // the keys that edit the line (Ctrl+A / C / X / V, Backspace, Delete, the arrows, Home, End) are the game's EditBox
        TextSelection selection{cursor_, highlight_};
        bool textChanged = false;
        if (editKey(input_, selection, ChatComponent::kMaxMessageLength, key, mods, textChanged)) {
            cursor_ = selection.cursor;
            highlight_ = selection.highlight;
            if (textChanged) historyPos_ = -1;
            refreshCommand();
            return true;
        }
    }
    const bool listShown = !suggestions_.list.empty() && !hidden_;
    switch (key) {
        case kEnter:
        case kKpEnter:
            send();
            return true;
        case kTab:
            if (listShown) {
                const bool shift = (mods & 1) != 0;
                if (shift) cycleSuggestion(-1);
                applySuggestion();
            }
            return true;
        case kEscape:
            if (listShown) {  // the first Escape only puts the list away
                hidden_ = true;
                return true;
            }
            return true;
        case kArrowUp:
            if (listShown) cycleSuggestion(-1);
            else setFromHistory(-1);
            return true;
        case kArrowDown:
            if (listShown) cycleSuggestion(1);
            else setFromHistory(1);
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
    if (!isAllowedTextChar(static_cast<char32_t>(codePoint)) || (input_.size() >= ChatComponent::kMaxMessageLength && cursor_ == highlight_)) return;
    TextSelection selection{cursor_, highlight_};
    insertText(input_, selection, ChatComponent::kMaxMessageLength, std::u32string(1, static_cast<char32_t>(codePoint)));
    cursor_ = selection.cursor;
    highlight_ = selection.highlight;
    historyPos_ = -1;
    refreshCommand();
}

bool ChatScreen::mouseScrolled(double /*x*/, double /*y*/, double dy) {
    if (!suggestions_.list.empty() && !hidden_) {  // over the list the wheel moves the choice
        cycleSuggestion(dy > 0 ? -1 : 1);
        return true;
    }
    if (services_.session != nullptr) services_.session->chat().scroll(static_cast<int>(dy > 0 ? 1 : -1) * (dy == 0 ? 0 : std::max(1, static_cast<int>(std::abs(dy)))), services_.options);
    return true;
}

// CommandSuggestions.formatChat: the arguments of a command take turns in aqua, yellow, green, light purple and gold; what could not be understood is red.
std::string ChatScreen::coloredInput() const {
    const std::string all = encodeUtf8(input_);
    if (services_.session == nullptr || input_.empty() || input_[0] != U'/') return all;
    const CommandTree* tree = services_.session->commandTree();
    if (tree == nullptr) return all;
    const std::string text = all.substr(1);
    const CommandParse parse = parseCommand(*tree, services_.session->commandContext(), text);
    static const char kColors[] = {'b', 'e', 'a', 'd', '6'};
    std::string out = "/";
    std::size_t at = 0;
    int argument = 0;
    for (const CommandPart& part : parse.parts) {
        if (part.start > at) out += text.substr(at, part.start - at);
        const std::string word = text.substr(part.start, part.end - part.start);
        if (part.node->isArgument()) out += std::string(kSection) + kColors[argument++ % 5] + word + kSection + "r";
        else out += word;
        at = part.end;
    }
    if (at < text.size()) {
        const std::size_t keep = parse.complete ? text.size() : std::min(std::max(parse.end, at), text.size());
        if (keep > at) out += text.substr(at, keep - at);
        if (keep < text.size()) out += std::string(kSection) + "c" + text.substr(keep);
    }
    return out;
}

void ChatScreen::render(GuiGraphics& g, int /*mouseX*/, int /*mouseY*/, const FrameTime& time) {
    if (services_.session != nullptr) services_.session->renderHud(g, width_, height_, /*chatFocused=*/true);

    // The input box across the bottom, with the typed text and a blinking caret.
    const int boxTop = height_ - 14;
    g.fill(2, boxTop, width_ - 2, height_ - 2, 0x80000000u);
    const std::string before = encodeUtf8(input_.substr(0, cursor_));
    const float textY = static_cast<float>(height_ - 12);
    {  // (the colour runs of a command: arguments in turn, what is not understood in red)
        float x = 4.0f;
        for (const TextSpan& span : parseLegacyFormatting(coloredInput())) {
            g.text(span.text, x, textY, 0xFF000000u | (span.hasColor ? span.rgb : 0xE0E0E0u), true, span.underlined, span.bold);
            x += static_cast<float>(g.textWidth(span.text, span.bold));
        }
    }
    if (cursor_ != highlight_) {  // what is selected: a light bar with the text in blue on it
        const std::size_t from = std::min(cursor_, highlight_), to = std::max(cursor_, highlight_);
        const int left = 4 + g.textWidth(encodeUtf8(input_.substr(0, from))), right = 4 + g.textWidth(encodeUtf8(input_.substr(0, to)));
        g.fill(left, static_cast<int>(textY) - 1, right, static_cast<int>(textY) + Font::kLineHeight, 0xFFFFFFFFu);
        g.text(encodeUtf8(input_.substr(from, to - from)), static_cast<float>(left), textY, 0xFF1F1FFFu, false);
    }
    const int caretX = 4 + g.textWidth(before);
    // The first suggestion's rest, as grey text after the caret (only when what is typed so far is a start of it).
    const bool listShown = !suggestions_.list.empty() && !hidden_;
    if (listShown && cursor_ == input_.size()) {
        const std::string typed = encodeUtf8(input_.substr(std::min(1 + suggestions_.start, input_.size())));
        const std::string& chosen = suggestions_.list[static_cast<std::size_t>(selected_)];
        if (chosen.size() > typed.size() && chosen.compare(0, typed.size(), typed) == 0) g.text(chosen.substr(typed.size()), static_cast<float>(caretX), textY, kGhostColor);
    }
    const bool caretOn = (static_cast<long long>(time.millis / 400.0) % 2) == 0;
    if (caretOn) {
        if (cursor_ < input_.size()) {
            g.fill(caretX, static_cast<int>(textY) - 1, caretX + 1, static_cast<int>(textY) + Font::kLineHeight, 0xFFE0E0E0u);
        } else {
            g.text("_", static_cast<float>(caretX), textY, 0xFFE0E0E0u);
        }
    }

    // Above the box: the list of suggestions, or what could come next, or the mistake.
    const int listX = std::max(0, 4 + g.textWidth(encodeUtf8(input_.substr(0, std::min(1 + suggestions_.start, input_.size())))));
    if (listShown) {
        const int count = std::min(static_cast<int>(suggestions_.list.size()), kSuggestionLines);
        int widest = 0;
        for (const std::string& s : suggestions_.list) widest = std::max(widest, g.textWidth(s));
        const int x = std::clamp(listX, 0, std::max(0, width_ - widest - 1));
        const int bottom = height_ - 15, top = bottom - count * kRowHeight;
        g.fill(x - 1, top, x + widest + 1, bottom, kListBackground);
        for (int i = 0; i < count; ++i) {
            const int index = firstShown_ + i;
            if (index >= static_cast<int>(suggestions_.list.size())) break;
            g.text(suggestions_.list[static_cast<std::size_t>(index)], static_cast<float>(x), static_cast<float>(top + i * kRowHeight + 2), index == selected_ ? kSelectedColor : kListColor);
        }
    } else if (!commandError_.empty() || !usage_.empty()) {
        std::vector<std::pair<std::string, std::uint32_t>> lines;
        if (!commandError_.empty()) lines.push_back({commandError_, 0xFFFF5555u});
        for (const std::string& u : usage_) lines.push_back({u, kListColor});
        int widest = 0;
        for (const auto& line : lines) widest = std::max(widest, g.textWidth(line.first));
        const int x = std::clamp(listX, 0, std::max(0, width_ - widest - 1));
        const int bottom = height_ - 15, top = bottom - static_cast<int>(lines.size()) * kRowHeight;
        g.fill(x - 1, top, x + widest + 1, bottom, kListBackground);
        for (std::size_t i = 0; i < lines.size(); ++i) g.text(lines[i].first, static_cast<float>(x), static_cast<float>(top + static_cast<int>(i) * kRowHeight + 2), lines[i].second);
    }
}

}  // namespace falcon
