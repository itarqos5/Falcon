#pragma once

#include "apex/game/screen_services.hpp"
#include "apex/ui/screen.hpp"
#include "apex/world/server_list.hpp"

#include <functional>
#include <string>

namespace apex {

// The Add Server / Edit Server Info screen: a name, an address and the "Server Resource Packs" setting.
// It edits a copy: `done` is called with true and the edited copy after Done, and with false after Cancel
// or Escape. The screen closes itself first, so `done` may open another screen.
class ManageServerScreen : public Screen {
public:
    using Done = std::function<void(bool saved, const ServerEntry& edited)>;
    ManageServerScreen(ScreenNavigator& navigator, ScreenServices& services, std::string titleKey, ServerEntry server,
                       Done done);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    void onClose() override { finish(false); }

protected:
    void init() override;

private:
    void finish(bool saved);

    ScreenServices& services_;
    std::string titleKey_;
    ServerEntry server_;
    Done done_;
    // The text boxes keep what was typed across a window resize (init() runs again).
    std::string nameText_;
    std::string ipText_;
    Button* doneButton_ = nullptr;
    EditBox* ipEdit_ = nullptr;
};

// The Direct Connection screen: type an address and join it. The address is remembered
// (GameOptions::lastServer) and offered again the next time.
class DirectJoinScreen : public Screen {
public:
    using Joined = std::function<void(const std::string& address)>;
    DirectJoinScreen(ScreenNavigator& navigator, ScreenServices& services, Joined joined);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool keyPressed(int key, int mods) override;

protected:
    void init() override;

private:
    void join();

    ScreenServices& services_;
    Joined joined_;
    Button* joinButton_ = nullptr;
    EditBox* ipEdit_ = nullptr;
};

}  // namespace apex
