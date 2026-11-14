#include "apex/game/container_screen.hpp"

#include "apex/game/game_session.hpp"
#include "apex/game/item_names.hpp"
#include "apex/game/item_render.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace apex {
namespace {

constexpr double kDoubleClickMillis = 250.0;
constexpr std::uint32_t kYellow = 0xFFFFFF55u;

// Screen.extractTransparentBackground over a world: a dark veil, a little darker towards the bottom (0xC0101010 to 0xD0101010).
void veil(GuiGraphics& g, int width, int height) {
    constexpr int kBand = 4;
    for (int y = 0; y < height; y += kBand) {
        const double t = height > 0 ? static_cast<double>(y) / static_cast<double>(height) : 0.0;
        const auto alpha = static_cast<std::uint32_t>(0xC0 + t * (0xD0 - 0xC0));
        g.fill(0, y, width, std::min(height, y + kBand), (alpha << 24) | 0x101010u);
    }
}

}  // namespace

ContainerScreen::ContainerScreen(ScreenNavigator& navigator, ScreenServices& services, int imageWidth, int imageHeight)
    : Screen(navigator), imageWidth_(imageWidth), imageHeight_(imageHeight), services_(services) {}

void ContainerScreen::init() {
    leftPos_ = (width_ - imageWidth_) / 2;
    topPos_ = (height_ - imageHeight_) / 2;
}

bool ContainerScreen::bound(const char* action, int key) const {
    const std::size_t index = keyMappingIndex(action);
    return index < kKeyMappingCount && services_.options.keyBindings.key(index) == KeyId::keyboard(key);
}

bool ContainerScreen::pickItemMatchesMouse(int button) const {
    const std::size_t index = keyMappingIndex("key.pickItem");
    if (index >= kKeyMappingCount) return false;
    const KeyId bound = services_.options.keyBindings.key(index);
    return bound.type == KeyId::Type::Mouse && bound.code == button;
}

const SlotView* ContainerScreen::hoveredSlot(double x, double y) const {
    // isHovering: the slot's 16x16 and one pixel around it.
    for (const SlotView& v : views_) {
        const double px = x - leftPos_, py = y - topPos_;
        if (px >= v.x - 1 && px < v.x + 17 && py >= v.y - 1 && py < v.y + 17) return &v;
    }
    return nullptr;
}

bool ContainerScreen::hasClickedOutside(double x, double y) const {
    return x < leftPos_ || y < topPos_ || x >= leftPos_ + imageWidth_ || y >= topPos_ + imageHeight_;
}

void ContainerScreen::slotClicked(const SlotView*, int slotId, int button, ClickType type) { menu().clicked(slotId, button, type); }

std::vector<TooltipLine> ContainerScreen::tooltipLines(const SlotView& view) {
    return itemTooltipLines(view.slot->getItem(), services_.language, session().advancedTooltips());
}

bool ContainerScreen::shouldAddToQuickCraft(const SlotView& view) const {
    const ItemStack& carried = const_cast<ContainerScreen*>(this)->menu().carried();
    if (!quickCrafting_ || carried.isEmpty()) return false;
    if (!(carried.count > static_cast<int>(quickCraftSlots_.size()) || quickCraftType_ == 2)) return false;
    return ContainerMenu::canItemQuickReplace(view.slot, carried, true) && view.slot->mayPlace(carried) &&
           const_cast<ContainerScreen*>(this)->menu().canDragTo(*view.slot);
}

// How many of the carried stack are still on the cursor while it is being spread over slots.
void ContainerScreen::recalculateQuickCraftRemaining() {
    const ItemStack& carried = menu().carried();
    if (carried.isEmpty() || !quickCrafting_) return;
    if (quickCraftType_ == 2) {
        quickCraftRemainder_ = carried.maxStackSize();
        return;
    }
    quickCraftRemainder_ = carried.count;
    for (const Slot* slot : quickCraftSlots_) {
        const int have = slot->hasItem() ? slot->getItem().count : 0;
        const int place = ContainerMenu::getQuickCraftPlaceCount(static_cast<int>(quickCraftSlots_.size()), quickCraftType_, carried) + have;
        const int put = std::min(place, std::min(carried.maxStackSize(), slot->maxStackSize(carried)));
        quickCraftRemainder_ -= put - have;
    }
}

// The drag is over: the menu hears the start, each slot and the end of it (AbstractContainerScreen.quickCraftToSlots).
void ContainerScreen::quickCraftToSlots() {
    slotClicked(nullptr, kOutsideSlot, ContainerMenu::getQuickcraftMask(0, quickCraftType_), ClickType::QuickCraft);
    for (Slot* slot : quickCraftSlots_) slotClicked(nullptr, slot->index, ContainerMenu::getQuickcraftMask(1, quickCraftType_), ClickType::QuickCraft);
    slotClicked(nullptr, kOutsideSlot, ContainerMenu::getQuickcraftMask(2, quickCraftType_), ClickType::QuickCraft);
}

void ContainerScreen::drawSlots(GuiGraphics& g) {
    ContainerMenu& m = menu();
    ItemIcons& icons = session().itemIcons();
    for (const SlotView& view : views_) {
        Slot& slot = *view.slot;
        const int x = leftPos_ + view.x;
        const int y = topPos_ + view.y;
        ItemStack stack = slot.getItem();
        bool highlight = false;
        std::string countText;
        std::uint32_t countColor = kWhite;
        const bool inDrag = quickCrafting_ && std::find(quickCraftSlots_.begin(), quickCraftSlots_.end(), &slot) != quickCraftSlots_.end() && !m.carried().isEmpty();
        if (inDrag) {
            if (quickCraftSlots_.size() == 1) continue;  // (a lone slot is not shown until the drag spreads over a second one)
            if (ContainerMenu::canItemQuickReplace(&slot, m.carried(), true) && m.canDragTo(slot)) {
                highlight = true;
                const int max = std::min(m.carried().maxStackSize(), slot.maxStackSize(m.carried()));
                const int have = slot.hasItem() ? slot.getItem().count : 0;
                int count = ContainerMenu::getQuickCraftPlaceCount(static_cast<int>(quickCraftSlots_.size()), quickCraftType_, m.carried()) + have;
                if (count > max) {
                    count = max;
                    countText = std::to_string(max);
                    countColor = kYellow;
                }
                stack = m.carried().copyWithCount(count);
            } else {
                quickCraftSlots_.erase(std::remove(quickCraftSlots_.begin(), quickCraftSlots_.end(), &slot), quickCraftSlots_.end());
                recalculateQuickCraftRemaining();
            }
        }
        if (stack.isEmpty()) {
            const std::string icon = slot.noItemIcon();
            if (!icon.empty()) g.blitSprite(icon, x, y, 16, 16);
            continue;
        }
        if (highlight) g.fill(x, y, x + 16, y + 16, 0x80FFFFFFu);
        drawItemStack(g, icons, stack, x, y, countText.empty() ? nullptr : &countText, countColor);
    }
}

void ContainerScreen::drawCarried(GuiGraphics& g, int mouseX, int mouseY) {
    const ItemStack& carried = menu().carried();
    if (carried.isEmpty()) return;
    ItemStack shown = carried;
    std::string text;
    std::uint32_t color = kWhite;
    if (quickCrafting_ && quickCraftSlots_.size() > 1) {
        shown = carried.copyWithCount(quickCraftRemainder_);
        if (shown.isEmpty()) {
            shown = carried.copyWithCount(1);
            text = "0";
            color = kYellow;
        }
    }
    drawItemStack(g, session().itemIcons(), shown, mouseX - 8, mouseY - 8, text.empty() ? nullptr : &text, color);
}

void ContainerScreen::render(GuiGraphics& g, int mouseX, int mouseY, const FrameTime& time) {
    nowMillis_ = time.millis;
    mouseX_ = mouseX;
    mouseY_ = mouseY;
    views_ = slotViews();
    hovered_ = hoveredSlot(mouseX, mouseY);
    // A drag over slots adds the slot under the cursor (the game does it from the mouse-drag event; this is the same, every frame).
    if (quickCrafting_ && hovered_ != nullptr && shouldAddToQuickCraft(*hovered_) &&
        std::find(quickCraftSlots_.begin(), quickCraftSlots_.end(), hovered_->slot) == quickCraftSlots_.end()) {
        quickCraftSlots_.push_back(hovered_->slot);
        recalculateQuickCraftRemaining();
    }

    veil(g, width_, height_);
    drawBackground(g, mouseX, mouseY);
    drawLabels(g, mouseX, mouseY);
    if (hovered_ != nullptr && hovered_->slot->isHighlightable()) {
        g.blitSprite("container/slot_highlight_back", leftPos_ + hovered_->x - 4, topPos_ + hovered_->y - 4, 24, 24);
    }
    drawSlots(g);
    if (hovered_ != nullptr && hovered_->slot->isHighlightable()) {
        g.blitSprite("container/slot_highlight_front", leftPos_ + hovered_->x - 4, topPos_ + hovered_->y - 4, 24, 24);
    }
    drawCarried(g, mouseX, mouseY);
    // The tooltip of the hovered stack, unless the cursor holds one.
    if (hovered_ != nullptr && hovered_->slot->hasItem() && menu().carried().isEmpty()) {
        g.setItemTooltip(tooltipLines(*hovered_), mouseX, mouseY);
    }
    g.renderTooltip();
}

bool ContainerScreen::mouseClicked(double x, double y, int button) {
    if (Screen::mouseClicked(x, y, button)) return true;
    mouseX_ = static_cast<int>(x);
    mouseY_ = static_cast<int>(y);
    views_ = slotViews();
    const bool clone = pickItemMatchesMouse(button) && menu().player().infiniteMaterials;
    const SlotView* slot = hoveredSlot(x, y);
    doubleClick_ = lastClickSlot_ == (slot != nullptr ? slot->slot : nullptr) && nowMillis_ - lastClickMillis_ < kDoubleClickMillis;
    skipNextRelease_ = false;
    if (button == 0 || button == 1 || clone) {
        const bool outside = hasClickedOutside(x, y);
        int id = -1;
        if (slot != nullptr) id = slot->slot->index;
        if (outside) id = kOutsideSlot;
        if (id != -1 && !quickCrafting_) {
            if (menu().carried().isEmpty()) {
                if (clone) {
                    slotClicked(slot, id, 0, ClickType::Clone);
                } else {
                    const bool shift = id != kOutsideSlot && navigator_.isShiftDown();
                    ClickType type = ClickType::Pickup;
                    if (shift) {
                        lastQuickMoved_ = slot != nullptr && slot->slot->hasItem() ? slot->slot->getItem() : ItemStack();
                        type = ClickType::QuickMove;
                    } else if (id == kOutsideSlot) {
                        type = ClickType::Throw;
                    }
                    slotClicked(slot, id, button == 1 ? 1 : 0, type);
                }
                skipNextRelease_ = true;
            } else {
                quickCrafting_ = true;
                quickCraftButton_ = button;
                quickCraftSlots_.clear();
                quickCraftType_ = button == 0 ? 0 : button == 1 ? 1 : clone ? 2 : 0;
            }
        }
    }
    lastClickSlot_ = slot != nullptr ? slot->slot : nullptr;
    lastClickMillis_ = nowMillis_;
    return true;
}

void ContainerScreen::mouseReleased(double x, double y, int button) {
    views_ = slotViews();
    const SlotView* slot = hoveredSlot(x, y);
    const bool outside = hasClickedOutside(x, y);
    int id = -1;
    if (slot != nullptr) id = slot->slot->index;
    if (outside) id = kOutsideSlot;
    if (doubleClick_ && slot != nullptr && button == 0 && menu().canTakeItemForPickAll(ItemStack(), *slot->slot)) {
        if (navigator_.isShiftDown()) {
            // Shift + double click: every stack of the item in the same container is moved too.
            if (!lastQuickMoved_.isEmpty()) {
                for (const SlotView& other : views_) {
                    if (other.slot->mayPickup(menu().player()) && other.slot->hasItem() && other.slot->container == slot->slot->container &&
                        ContainerMenu::canItemQuickReplace(other.slot, lastQuickMoved_, true)) {
                        slotClicked(&other, other.slot->index, 0, ClickType::QuickMove);
                    }
                }
            }
        } else {
            slotClicked(slot, id, 0, ClickType::PickupAll);
        }
        doubleClick_ = false;
    } else {
        if (quickCrafting_ && quickCraftButton_ != button) {
            quickCrafting_ = false;
            quickCraftSlots_.clear();
            skipNextRelease_ = true;
            return;
        }
        if (skipNextRelease_) {
            skipNextRelease_ = false;
            return;
        }
        if (quickCrafting_ && !quickCraftSlots_.empty()) {
            quickCraftToSlots();
        } else if (!menu().carried().isEmpty()) {
            if (pickItemMatchesMouse(button)) {
                slotClicked(slot, id, 0, ClickType::Clone);
            } else {
                const bool shift = id != kOutsideSlot && navigator_.isShiftDown();
                if (shift) lastQuickMoved_ = slot != nullptr && slot->slot->hasItem() ? slot->slot->getItem() : ItemStack();
                slotClicked(slot, id, button == 1 ? 1 : 0, shift ? ClickType::QuickMove : ClickType::Pickup);
            }
        }
    }
    quickCrafting_ = false;
}

bool ContainerScreen::checkHotbarKeyPressed(int key) {
    if (!menu().carried().isEmpty() || hovered_ == nullptr) return false;
    if (bound("key.swapOffhand", key)) {
        slotClicked(hovered_, hovered_->slot->index, 40, ClickType::Swap);
        return true;
    }
    for (int i = 0; i < 9; ++i) {
        const std::string name = "key.hotbar." + std::to_string(i + 1);
        if (bound(name.c_str(), key)) {
            slotClicked(hovered_, hovered_->slot->index, i, ClickType::Swap);
            return true;
        }
    }
    return false;
}

bool ContainerScreen::keyPressed(int key, int mods) {
    if (Screen::keyPressed(key, mods)) return true;
    if (bound("key.inventory", key)) {
        onClose();
        return true;
    }
    views_ = slotViews();
    hovered_ = hoveredSlot(mouseX_, mouseY_);
    if (checkHotbarKeyPressed(key)) return true;
    if (hovered_ != nullptr && hovered_->slot->hasItem()) {
        if (bound("key.pickItem", key)) {
            slotClicked(hovered_, hovered_->slot->index, 0, ClickType::Clone);
        } else if (bound("key.drop", key)) {
            slotClicked(hovered_, hovered_->slot->index, (mods & GLFW_MOD_CONTROL) != 0 ? 1 : 0, ClickType::Throw);
        }
    }
    return false;
}

void ContainerScreen::onClose() {
    menu().removed();
    Screen::onClose();
}

}  // namespace apex
