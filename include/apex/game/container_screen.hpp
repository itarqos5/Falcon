#pragma once

#include "apex/game/screen_services.hpp"
#include "apex/ui/screen.hpp"
#include "apex/world/menu.hpp"

#include <string>
#include <vector>

namespace apex {

class GameSession;

// What every screen with slots has in common (the game's AbstractContainerScreen): a window of imageWidth x imageHeight in the
// middle of the GUI, the slots drawn with their stacks, the hovered slot highlighted, the stack on the cursor drawn under the
// mouse, the item's tooltip, and the mouse and key rules: a click, a shift-click, a double click, a drag over slots, a number
// key over a slot, Q over a slot, E to close.
class ContainerScreen : public Screen {
public:
    ContainerScreen(ScreenNavigator& navigator, ScreenServices& services, int imageWidth, int imageHeight);

    void render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) override;
    bool mouseClicked(double x, double y, int button) override;
    void mouseReleased(double x, double y, int button) override;
    bool keyPressed(int key, int mods) override;
    // The inventory does not stop the game: the world goes on behind it.
    [[nodiscard]] bool keepsWorldTicking() const override { return true; }
    void onClose() override;

    // The slot under the point (GUI coordinates), or null.
    [[nodiscard]] const SlotView* hoveredSlot(double x, double y) const;
    [[nodiscard]] int leftPos() const { return leftPos_; }
    [[nodiscard]] int topPos() const { return topPos_; }
    [[nodiscard]] bool isQuickCrafting() const { return quickCrafting_; }

protected:
    void init() override;

    // What a subclass provides.
    virtual ContainerMenu& menu() = 0;
    virtual std::vector<SlotView> slotViews() = 0;
    virtual void drawBackground(GuiGraphics& graphics, int mouseX, int mouseY) = 0;
    virtual void drawLabels(GuiGraphics& graphics, int mouseX, int mouseY) { (void)graphics; (void)mouseX; (void)mouseY; }
    // A click on a slot (null: outside any slot, id -999 outside the window): the menu works it out; the creative screen overrides
    // this with its own rules.
    virtual void slotClicked(const SlotView* view, int slotId, int button, ClickType type);
    [[nodiscard]] virtual bool hasClickedOutside(double x, double y) const;
    // The lines of the tooltip of a stack in a slot (the creative screen adds the blue tab names).
    [[nodiscard]] virtual std::vector<TooltipLine> tooltipLines(const SlotView& view);
    // Called after a slot was drawn (the creative screen has nothing to add; kept for symmetry).
    [[nodiscard]] GameSession& session() const { return *services_.session; }

    [[nodiscard]] bool bound(const char* action, int key) const;
    // A number key (or F) over a slot while the cursor is empty: swaps it with that hotbar slot (or the offhand).
    bool checkHotbarKeyPressed(int key);
    [[nodiscard]] const SlotView* hovered() const { return hovered_; }
    [[nodiscard]] double nowMillis() const { return nowMillis_; }

    // The window's own geometry.
    int imageWidth_;
    int imageHeight_;
    int leftPos_ = 0;
    int topPos_ = 0;
    ScreenServices& services_;

private:
    void drawSlots(GuiGraphics& graphics);
    void drawCarried(GuiGraphics& graphics, int mouseX, int mouseY);
    void recalculateQuickCraftRemaining();
    void quickCraftToSlots();
    [[nodiscard]] bool shouldAddToQuickCraft(const SlotView& view) const;
    [[nodiscard]] bool pickItemMatchesMouse(int button) const;

    std::vector<SlotView> views_;  // rebuilt every frame
    const SlotView* hovered_ = nullptr;
    // The click state of AbstractContainerScreen.
    bool quickCrafting_ = false;
    int quickCraftButton_ = 0;
    int quickCraftType_ = 0;
    int quickCraftRemainder_ = 0;
    std::vector<Slot*> quickCraftSlots_;
    bool skipNextRelease_ = false;
    bool doubleClick_ = false;
    const Slot* lastClickSlot_ = nullptr;
    double lastClickMillis_ = -1.0e9;
    double nowMillis_ = 0.0;
    ItemStack lastQuickMoved_;
    int mouseX_ = 0;
    int mouseY_ = 0;
};

}  // namespace apex
