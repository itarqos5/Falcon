#pragma once

#include "falcon/game/container_screen.hpp"
#include "falcon/game/remote_menu.hpp"

#include <string>

namespace falcon {

// The window of a container that a server opened (a chest, a furnace, a crafting table ...): the game's own picture for it, the slots where the game puts them, the title the server
// gave, and the player's inventory under it. Clicks are played out by the menu and told to the server (see RemoteMenu).
class RemoteContainerScreen final : public ContainerScreen {
public:
    RemoteContainerScreen(ScreenNavigator& navigator, ScreenServices& services, RemoteMenu& menu, std::string title);

protected:
    ContainerMenu& menu() override { return menu_; }
    std::vector<SlotView> slotViews() override;
    void drawBackground(GuiGraphics& graphics, int mouseX, int mouseY) override;
    void drawLabels(GuiGraphics& graphics, int mouseX, int mouseY) override;

private:
    RemoteMenu& menu_;
    std::string title_;
};

}  // namespace falcon
