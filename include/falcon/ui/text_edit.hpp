#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace falcon {

// The caret of a text box and the other end of what is selected (the game's cursorPos and highlightPos); the same place twice means nothing is selected.
struct TextSelection {
    std::size_t cursor = 0;
    std::size_t highlight = 0;
    [[nodiscard]] bool any() const { return cursor != highlight; }
    [[nodiscard]] std::size_t begin() const { return cursor < highlight ? cursor : highlight; }
    [[nodiscard]] std::size_t end() const { return cursor < highlight ? highlight : cursor; }
    void collapseTo(std::size_t position) { cursor = highlight = position; }
};

// The modifier bits of a key press (GLFW's).
inline constexpr int kEditShift = 1;
inline constexpr int kEditControl = 2;

// The clipboard the editing keys use: the system's, unless a test sets another (an empty one puts the system's back).
struct TextClipboard {
    std::function<std::string()> get;
    std::function<void(const std::string&)> set;
};
void setTextClipboard(TextClipboard clipboard);

// What the game lets into a text box (StringUtil.isAllowedChatCharacter): no control characters and no section sign.
[[nodiscard]] bool isAllowedTextChar(char32_t c);

// EditBox.insertText: puts `inserted` in place of what is selected (what is not allowed is dropped, and it is cut to what still fits) and the caret after it.
void insertText(std::u32string& text, TextSelection& selection, std::size_t maxLength, const std::u32string& inserted);

// EditBox.getWordPosition: where the word before (direction -1) or after (+1) `from` starts, with the spaces skipped.
[[nodiscard]] std::size_t wordPosition(const std::u32string& text, std::size_t from, int direction);

// EditBox.keyPressed for the keys that edit or move in a line of text: Ctrl+A select all, Ctrl+C copy, Ctrl+X cut, Ctrl+V paste, Backspace and Delete (with Ctrl a word),
// the arrows (with Ctrl a word, with Shift they select), Home and End. True when the key is one of them (it has been dealt with); `textChanged` says whether the text differs.
bool editKey(std::u32string& text, TextSelection& selection, std::size_t maxLength, int key, int mods, bool& textChanged);

}  // namespace falcon
