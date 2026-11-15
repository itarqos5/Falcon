#pragma once

#include "falcon/game/command_tree.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/ui/screen.hpp"

#include <string>

namespace falcon {

// The chat: a box across the bottom that sends what is typed (CommandSuggestions: a text starting with "/" is a command, which is coloured as it is typed,
// completed with Tab, and offered suggestions above the box).
class ChatScreen : public Screen {
public:
    // `initial` is what the box starts with: empty for the Chat key, "/" for the Command key.
    ChatScreen(ScreenNavigator& navigator, ScreenServices& services, std::string initial);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool keyPressed(int key, int mods) override;
    void charTyped(unsigned int codePoint) override;
    bool mouseScrolled(double x, double y, double dy) override;

    // The world keeps running behind the chat, and the player cannot steer (the chat is not InGameScreen).
    [[nodiscard]] bool keepsWorldTicking() const override { return true; }
    // Typing must reach the box, so the global F-keys are held back.
    [[nodiscard]] bool wantsAllKeys() const override { return true; }
    void onClose() override;

protected:
    void init() override {}

private:
    void send();
    void setFromHistory(int delta);
    // Works out the suggestions, the usage and the error for the command being typed (after every change of the text or the caret).
    void refreshCommand();
    void applySuggestion();
    void cycleSuggestion(int delta);
    [[nodiscard]] std::string coloredInput() const;

    ScreenServices& services_;
    std::u32string input_;
    std::size_t cursor_ = 0;
    std::size_t highlight_ = 0;  // the other end of what is selected (the caret again: nothing)
    int historyPos_ = -1;   // -1: the live edit; else an index from the end of the sent history
    std::string draft_;     // the live edit, kept while browsing history

    // The command being typed.
    CommandSuggestions suggestions_;
    std::vector<std::string> usage_;
    std::string commandError_;
    int selected_ = 0;
    int firstShown_ = 0;
    bool hidden_ = false;  // Esc closed the list until the text changes
};

}  // namespace falcon
