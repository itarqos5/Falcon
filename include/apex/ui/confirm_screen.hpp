#pragma once

#include "apex/ui/screen.hpp"

#include <functional>
#include <string>
#include <vector>

namespace apex {

// A modal question: a title, a wrapped message and a Yes / No pair of 150x20 buttons, all centred
// on the application's backdrop. Escape counts as No. The screen closes itself (pop) before
// `callback` runs, so the callback may open another screen.
//
//     navigator_.push(std::make_unique<ConfirmScreen>(
//         navigator_, text("question.title"), text("question.message"), text("gui.yes"), text("gui.no"),
//         [this](bool yes) { if (yes) doIt(); }));
class ConfirmScreen : public Screen {
public:
    ConfirmScreen(ScreenNavigator& navigator, std::string title, std::string message, std::string yesLabel,
                  std::string noLabel, std::function<void(bool yes)> callback);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    void onClose() override { answer(false); }

protected:
    void init() override;

private:
    void answer(bool yes);

    std::string title_;
    std::string message_;
    std::string yesLabel_;
    std::string noLabel_;
    std::function<void(bool)> callback_;
    std::vector<std::string> lines_;
    int messageX_ = 0;
    int messageY_ = 0;
    int titleX_ = 0;
    int titleY_ = 0;
};

}  // namespace apex
