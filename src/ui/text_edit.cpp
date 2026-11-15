#include "falcon/ui/text_edit.hpp"

#include "falcon/platform/clipboard.hpp"
#include "falcon/util/text_util.hpp"

#include <algorithm>

namespace falcon {

namespace {

TextClipboard& customClipboard() {
    static TextClipboard clipboard;
    return clipboard;
}

std::string clipboardGet() {
    const TextClipboard& custom = customClipboard();
    return custom.get ? custom.get() : clipboardText();
}

void clipboardSet(const std::string& utf8) {
    const TextClipboard& custom = customClipboard();
    if (custom.set) custom.set(utf8);
    else setClipboardText(utf8);
}

std::u32string selectedText(const std::u32string& text, const TextSelection& selection) {
    return text.substr(selection.begin(), selection.end() - selection.begin());
}

// EditBox.deleteChars / deleteWords: what is selected goes, or else the character (or the word) before or after the caret.
bool deleteAt(std::u32string& text, TextSelection& selection, std::size_t maxLength, int direction, bool wholeWord) {
    if (text.empty()) return false;
    if (selection.any()) {
        insertText(text, selection, maxLength, {});
        return true;
    }
    std::size_t from = selection.cursor;
    std::size_t to = wholeWord ? wordPosition(text, from, direction) : (direction < 0 ? (from > 0 ? from - 1 : 0) : std::min(from + 1, text.size()));
    if (to == from) return false;
    if (to < from) std::swap(from, to);
    text.erase(from, to - from);
    selection.collapseTo(from);
    return true;
}

}  // namespace

void setTextClipboard(TextClipboard clipboard) { customClipboard() = std::move(clipboard); }

bool isAllowedTextChar(char32_t c) { return c >= 32 && c != 127 && c != 167; }

void insertText(std::u32string& text, TextSelection& selection, std::size_t maxLength, const std::u32string& inserted) {
    std::u32string clean;
    for (const char32_t c : inserted) {
        if (isAllowedTextChar(c)) clean.push_back(c);
    }
    const std::size_t begin = std::min(selection.begin(), text.size()), end = std::min(selection.end(), text.size());
    const std::size_t kept = text.size() - (end - begin);
    const std::size_t room = maxLength > kept ? maxLength - kept : 0;
    if (clean.size() > room) clean.resize(room);
    text.replace(begin, end - begin, clean);
    selection.collapseTo(begin + clean.size());
}

std::size_t wordPosition(const std::u32string& text, std::size_t from, int direction) {
    std::size_t i = std::min(from, text.size());
    if (direction > 0) {
        const std::size_t space = text.find(U' ', i);
        if (space == std::u32string::npos) return text.size();
        i = space;
        while (i < text.size() && text[i] == U' ') ++i;
        return i;
    }
    while (i > 0 && text[i - 1] == U' ') --i;
    while (i > 0 && text[i - 1] != U' ') --i;
    return i;
}

bool editKey(std::u32string& text, TextSelection& selection, std::size_t maxLength, int key, int mods, bool& textChanged) {
    constexpr int kBackspace = 259, kDelete = 261, kRight = 262, kLeft = 263, kHome = 268, kEnd = 269;
    const bool shift = (mods & kEditShift) != 0, control = (mods & kEditControl) != 0;
    textChanged = false;
    selection.cursor = std::min(selection.cursor, text.size());
    selection.highlight = std::min(selection.highlight, text.size());
    const auto moveTo = [&](std::size_t position) {
        selection.cursor = std::min(position, text.size());
        if (!shift) selection.highlight = selection.cursor;
    };
    if (control && !shift) {
        switch (key) {
            case 'A':
                selection.cursor = text.size();
                selection.highlight = 0;
                return true;
            case 'C':
                if (selection.any()) clipboardSet(encodeUtf8(selectedText(text, selection)));
                return true;
            case 'X':
                if (selection.any()) {
                    clipboardSet(encodeUtf8(selectedText(text, selection)));
                    insertText(text, selection, maxLength, {});
                    textChanged = true;
                }
                return true;
            case 'V': {
                const std::vector<char32_t> pasted = decodeUtf8(clipboardGet());
                const std::size_t before = text.size();
                const std::u32string old = text;
                insertText(text, selection, maxLength, std::u32string(pasted.begin(), pasted.end()));
                textChanged = text != old || before != text.size();
                return true;
            }
            default: break;
        }
    }
    switch (key) {
        case kBackspace: textChanged = deleteAt(text, selection, maxLength, -1, control); return true;
        case kDelete: textChanged = deleteAt(text, selection, maxLength, 1, control); return true;
        case kLeft: moveTo(control ? wordPosition(text, selection.cursor, -1) : (selection.cursor > 0 ? selection.cursor - 1 : 0)); return true;
        case kRight: moveTo(control ? wordPosition(text, selection.cursor, 1) : selection.cursor + 1); return true;
        case kHome: moveTo(0); return true;
        case kEnd: moveTo(text.size()); return true;
        default: return false;
    }
}

}  // namespace falcon
