#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/ui/screen.hpp"

#include <string>

namespace falcon {

// The chat input: opened over the world with the Chat or Command key. It shows the chat log in full (focused), an input
// box across the bottom with a blinking cursor, and lets the player scroll the log and walk the sent-message history. It
// follows the game's ChatScreen: Enter sends, Escape closes, Up/Down are the history, PageUp/PageDown scroll.
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

    ScreenServices& services_;
    std::u32string input_;
    std::size_t cursor_ = 0;
    int historyPos_ = -1;   // -1: the live edit; else an index from the end of the sent history
    std::string draft_;     // the live edit, kept while browsing history
};

}  // namespace falcon
