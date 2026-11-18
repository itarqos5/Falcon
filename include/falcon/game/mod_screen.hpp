#pragma once

#include "falcon/game/screen_services.hpp"
#include "falcon/ui/screen.hpp"

namespace falcon {

// An interactive screen opened by a mod (e.g. wWaypoints SettingsScreen).
// Forwards rendering and user input (clicks, drags, scrolls, typing, keys)
// to the mod's active Screen through falcon::jvm::ModHost.
class ModScreen : public Screen {
public:
    ModScreen(ScreenNavigator& navigator, ScreenServices& services);
    ~ModScreen() override;

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseClicked(double x, double y, int button) override;
    void mouseReleased(double x, double y, int button) override;
    void mouseDragged(double x, double y) override;
    bool mouseScrolled(double x, double y, double dy) override;
    bool keyPressed(int key, int mods) override;
    void keyReleased(int key, int mods) override;
    void charTyped(unsigned int codePoint) override;
    void onClose() override;

    [[nodiscard]] bool keepsWorldTicking() const override { return true; }

protected:
    void init() override;

private:
    ScreenServices& services_;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
};

}  // namespace falcon
