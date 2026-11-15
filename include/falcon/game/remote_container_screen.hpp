#pragma once

#include "falcon/game/container_screen.hpp"
#include "falcon/game/remote_menu.hpp"
#include "falcon/ui/widgets.hpp"

#include <string>

namespace falcon {

// The window of a container that a server opened (a chest, a furnace, a crafting table ...): the game's own picture for it, the slots where the game puts them, the title the server
// gave, and the player's inventory under it. Clicks are played out by the menu and told to the server (see RemoteMenu).
class RemoteContainerScreen final : public ContainerScreen {
public:
    RemoteContainerScreen(ScreenNavigator& navigator, ScreenServices& services, RemoteMenu& menu, std::string title);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseClicked(double x, double y, int button) override;
    bool mouseScrolled(double x, double y, double dy) override;
    bool keyPressed(int key, int mods) override;
    void charTyped(unsigned int codePoint) override;
    // The names of enchantments by their number in the server's registry (for the clue of an enchanting table): set by the session.
    std::function<std::string(int enchantment, int level)> enchantmentName;

protected:
    void init() override;
    ContainerMenu& menu() override { return menu_; }
    std::vector<SlotView> slotViews() override;
    void drawBackground(GuiGraphics& graphics, int mouseX, int mouseY) override;
    void drawLabels(GuiGraphics& graphics, int mouseX, int mouseY) override;

private:
    [[nodiscard]] int enchantButtonAt(double x, double y) const;  // the button of an enchanting table under the point (0-2), or -1
    [[nodiscard]] bool canEnchant(int button) const;
    [[nodiscard]] int tradeRowAt(double x, double y) const;  // the offer in the list under the point (its number), or -1
    void selectTrade(int index);
    void drawTrades(GuiGraphics& graphics, int mouseX, int mouseY);
    RemoteMenu& menu_;
    std::string title_;
    EditBox* rename_ = nullptr;  // the anvil's name box
    int renamedFor_ = -2;        // the item whose name the box shows
    bool settingName_ = false;
    int scrollOff_ = 0;        // a villager's list: the first offer in view
    int selectedTrade_ = -1;
};

}  // namespace falcon
