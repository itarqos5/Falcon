#pragma once

#include "apex/game/container_screen.hpp"
#include "apex/game/creative_inventory.hpp"

#include <memory>

namespace apex {

// The "E" screen of survival (InventoryScreen): 176 x 166 from textures/gui/container/inventory.png: the four armor slots, the
// player's picture (a PLACEHOLDER silhouette here: a later stage draws the player), the offhand slot, the 2 x 2 crafting grid with
// its result, the 27 inventory slots and the hotbar, and the "Crafting" label. Clicks go through the menu rules.
class InventoryScreen final : public ContainerScreen {
public:
    InventoryScreen(ScreenNavigator& navigator, ScreenServices& services);

protected:
    ContainerMenu& menu() override;
    std::vector<SlotView> slotViews() override;
    void drawBackground(GuiGraphics& graphics, int mouseX, int mouseY) override;
    void drawLabels(GuiGraphics& graphics, int mouseX, int mouseY) override;
};

// The "E" screen of creative (CreativeModeInventoryScreen): 195 x 136, a tab bar above and below with the icons of the tabs, a grid
// of 9 x 5 items that scrolls (wheel or the scroller), a search tab with a text box, the survival inventory tab (armor, the 27 slots,
// the hotbar and the destroy slot), and the hotbar row under the grid. Click an item to take one, shift for a full stack; click
// an item with the cursor holding one to put it away.
class CreativeScreen final : public ContainerScreen {
public:
    CreativeScreen(ScreenNavigator& navigator, ScreenServices& services);

    bool mouseClicked(double x, double y, int button) override;
    void mouseReleased(double x, double y, int button) override;
    void mouseDragged(double x, double y) override;
    bool mouseScrolled(double x, double y, double dy) override;
    bool keyPressed(int key, int mods) override;
    void charTyped(unsigned int codePoint) override;
    void onClose() override;

    [[nodiscard]] CreativeInventory& logic() { return *logic_; }

protected:
    void init() override;
    ContainerMenu& menu() override;
    std::vector<SlotView> slotViews() override;
    void drawBackground(GuiGraphics& graphics, int mouseX, int mouseY) override;
    void drawLabels(GuiGraphics& graphics, int mouseX, int mouseY) override;
    void slotClicked(const SlotView* view, int slotId, int button, ClickType type) override;
    [[nodiscard]] bool hasClickedOutside(double x, double y) const override;
    [[nodiscard]] std::vector<TooltipLine> tooltipLines(const SlotView& view) override;

private:
    [[nodiscard]] int tabX(const CreativeTab& tab) const;
    [[nodiscard]] int tabY(const CreativeTab& tab) const;
    [[nodiscard]] bool tabHit(const CreativeTab& tab, double x, double y) const;
    void drawTab(GuiGraphics& graphics, const CreativeTab& tab, int mouseX, int mouseY);
    [[nodiscard]] bool insideScrollbar(double x, double y) const;
    void scrollTo(double mouseY);
    [[nodiscard]] std::string tabName(const CreativeTab& tab) const;

    std::shared_ptr<const CreativeTabs> tabs_;
    std::unique_ptr<CreativeInventory> logic_;
    bool scrolling_ = false;
    bool ignoreChar_ = false;
};

}  // namespace apex
