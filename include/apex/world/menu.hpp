#pragma once

#include "apex/world/inventory.hpp"
#include "apex/world/recipes.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace apex {

// The kinds of click a menu understands (the game's ClickType / ContainerInput).
enum class ClickType : std::uint8_t {
    Pickup,      // left (button 0) or right (button 1) click: pick up, put down, swap, split
    QuickMove,   // shift-click
    Swap,        // a number key (button 0-8: that hotbar slot) or F (button 40: the offhand) over a slot
    Clone,       // middle click in creative: a full stack of the item under the cursor
    Throw,       // Q (button 0: one) or Ctrl+Q (button 1: all) over a slot
    QuickCraft,  // dragging a stack over slots to spread it (the drag header and type are in `button`)
    PickupAll,   // double click: gather every matching stack onto the cursor
};

// Slot number meaning "outside the window": clicking there drops what the cursor holds.
inline constexpr int kOutsideSlot = -999;

// Who a menu belongs to, as far as the click logic needs to know.
struct MenuPlayer {
    Inventory* inventory = nullptr;
    bool infiniteMaterials = false;  // creative
    bool canDrop = true;
    // Items thrown out of the menu (Q, a click outside, a full inventory). The game spawns an item entity; here the
    // stack only goes to this function, so a test can see what was dropped.
    std::function<void(const ItemStack&)> dropHook;
    void drop(const ItemStack& stack) const {
        if (!stack.isEmpty() && dropHook) dropHook(stack);
    }
};

// One place in a menu that shows a stack of a container (the game's Slot): a container, which of its slots, and where it is
// drawn (relative to the menu's window).
class Slot {
public:
    Slot(Container& owner, int slotInContainer, int px, int py) : container(&owner), slot(slotInContainer), x(px), y(py) {}
    virtual ~Slot() = default;

    Container* container;
    int slot;       // number inside the container
    int index = -1;  // number inside the menu
    int x = 0;
    int y = 0;

    [[nodiscard]] ItemStack& getItem() const { return container->item(slot); }
    [[nodiscard]] bool hasItem() const { return !getItem().isEmpty(); }
    virtual void set(ItemStack stack) { container->setItem(slot, std::move(stack)); }
    virtual void setByPlayer(ItemStack stack) { set(std::move(stack)); }
    void setChanged() const { container->setChanged(); }
    [[nodiscard]] virtual bool mayPlace(const ItemStack&) const { return true; }
    [[nodiscard]] virtual bool mayPickup(const MenuPlayer&) const { return true; }
    [[nodiscard]] virtual int maxStackSize() const { return container->maxStackSize(); }
    [[nodiscard]] int maxStackSize(const ItemStack& stack) const { return std::min(maxStackSize(), stack.maxStackSize()); }
    virtual ItemStack remove(int amount) { return container->removeItem(slot, amount); }
    virtual void onTake(const MenuPlayer&, const ItemStack&) { setChanged(); }
    // Items taken out by a shift-click: `picked` is what is left, `original` what it was.
    virtual void onQuickCraft(const ItemStack&, const ItemStack&) {}
    virtual void onSwapCraft(int) {}
    // The sprite (under textures/gui/sprites) an empty slot shows, or "".
    [[nodiscard]] virtual std::string noItemIcon() const { return {}; }
    [[nodiscard]] virtual bool isHighlightable() const { return true; }
    [[nodiscard]] virtual ItemStack safeClone() const { return getItem().copyWithCount(getItem().maxStackSize()); }
    // The slot a stack could come out of whole (Slot.allowModification).
    [[nodiscard]] bool allowModification(const MenuPlayer& player) const { return mayPickup(player) && mayPlace(getItem()); }

    // Slot.tryRemove / safeTake / safeInsert.
    bool tryRemove(int amount, int maxAmount, const MenuPlayer& player, ItemStack& out);
    ItemStack safeTake(int amount, int maxAmount, const MenuPlayer& player);
    ItemStack safeInsert(ItemStack stack, int increment);
    ItemStack safeInsert(ItemStack stack) {
        const int all = stack.count;
        return safeInsert(std::move(stack), all);
    }
};

// A slot as a screen draws it: the menu's slot and where it is on that screen (relative to the window's corner). The creative
// inventory shows the survival slots somewhere else than the survival screen does.
struct SlotView {
    Slot* slot = nullptr;
    int x = 0;
    int y = 0;
};

// The menu: a numbered list of slots, the stack on the cursor ("carried"), and the click rules of the game's
// AbstractContainerMenu (a click, a shift-click, a number key, a drag, a double click, ...).
class ContainerMenu {
public:
    explicit ContainerMenu(MenuPlayer player) : player_(std::move(player)) {}
    virtual ~ContainerMenu() = default;
    ContainerMenu(const ContainerMenu&) = delete;
    ContainerMenu& operator=(const ContainerMenu&) = delete;

    [[nodiscard]] int slotCount() const { return static_cast<int>(slots_.size()); }
    [[nodiscard]] Slot& slot(int index) const { return *slots_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] bool isValidSlotIndex(int index) const { return index >= -1 && index < slotCount(); }
    [[nodiscard]] ItemStack& carried() { return carried_; }
    [[nodiscard]] const ItemStack& carried() const { return carried_; }
    void setCarried(ItemStack stack) { carried_ = std::move(stack); }
    [[nodiscard]] MenuPlayer& player() { return player_; }

    // AbstractContainerMenu.clicked: `button` is the mouse button (0 left, 1 right), the key (see ClickType) or, for
    // QuickCraft, getQuickcraftMask(header, type).
    void clicked(int slotIndex, int button, ClickType type);

    // Shift-click (QUICK_MOVE): moves the stack of slot `index` elsewhere; returns what it was (empty if nothing moved).
    virtual ItemStack quickMoveStack(int index) = 0;
    [[nodiscard]] virtual bool canTakeItemForPickAll(const ItemStack&, const Slot&) const { return true; }
    [[nodiscard]] virtual bool canDragTo(const Slot&) const { return true; }
    // A container of this menu changed (the crafting grid: the result is worked out again).
    virtual void slotsChanged() {}
    // The menu is closed: the stack on the cursor goes back to the inventory (or onto the ground).
    virtual void removed();

    // ---- the drag state (a drag over slots with a button held), for the screen ----
    static constexpr int kDragStart = 0, kDragAdd = 1, kDragEnd = 2;
    [[nodiscard]] static int getQuickcraftType(int mask) { return (mask >> 2) & 3; }
    [[nodiscard]] static int getQuickcraftHeader(int mask) { return mask & 3; }
    [[nodiscard]] static int getQuickcraftMask(int header, int type) { return (header & 3) | ((type & 3) << 2); }
    // Type 0 spreads the stack evenly, 1 gives one to each slot, 2 (creative, middle button) fills each to its maximum.
    [[nodiscard]] static bool isValidQuickcraftType(int type, const MenuPlayer& player);
    [[nodiscard]] static bool canItemQuickReplace(const Slot* slot, const ItemStack& stack, bool ignoreSize);
    [[nodiscard]] static int getQuickCraftPlaceCount(int slotCount, int type, const ItemStack& stack);
    [[nodiscard]] int quickcraftStatus() const { return quickcraftStatus_; }

protected:
    Slot& addSlot(std::unique_ptr<Slot> slot);
    // AbstractContainerMenu.addStandardInventorySlots: 27 main slots (rows of 9 from container slot 9) then the hotbar
    // 58 pixels below, at (x, y).
    void addStandardInventorySlots(Container& inventory, int x, int y);
    // AbstractContainerMenu.moveItemStackTo: puts `stack` into slots [start, end) (backwards if `reverse`), first onto
    // stacks it can join, then into empty slots that accept it. True if anything moved.
    bool moveItemStackTo(ItemStack& stack, int start, int end, bool reverse);
    void resetQuickCraft();
    void clearContainer(Container& container);

    MenuPlayer player_;
    std::vector<std::unique_ptr<Slot>> slots_;
    ItemStack carried_;

private:
    void doClick(int slotIndex, int button, ClickType type);

    int quickcraftStatus_ = 0;
    int quickcraftType_ = -1;
    std::vector<int> quickcraftSlots_;  // menu slot numbers
};

// ---- the survival inventory menu (InventoryMenu) ----

// The slot a crafting result sits in: nothing can be put in, taking it uses up the ingredients.
class ResultSlot final : public Slot {
public:
    ResultSlot(Container& result, SimpleContainer& craftSlots, int gridWidth, int gridHeight, int px, int py)
        : Slot(result, 0, px, py), craftSlots_(craftSlots), gridWidth_(gridWidth), gridHeight_(gridHeight) {}
    [[nodiscard]] bool mayPlace(const ItemStack&) const override { return false; }
    void onTake(const MenuPlayer& player, const ItemStack& taken) override;
    // A result never gets a recipe-specific clone (the game also runs onCraftedBy, which does nothing here).
    [[nodiscard]] bool isHighlightable() const override { return true; }

private:
    SimpleContainer& craftSlots_;
    int gridWidth_;
    int gridHeight_;
};

// An armor slot: one item that can be worn in that slot.
class ArmorSlot final : public Slot {
public:
    ArmorSlot(Container& inventory, int slotInContainer, EquipSlot wears, std::string emptyIcon, int px, int py)
        : Slot(inventory, slotInContainer, px, py), wears_(wears), emptyIcon_(std::move(emptyIcon)) {}
    [[nodiscard]] bool mayPlace(const ItemStack& stack) const override { return !stack.isEmpty() && stack.type().equipSlot == wears_; }
    [[nodiscard]] int maxStackSize() const override { return 1; }
    [[nodiscard]] std::string noItemIcon() const override { return emptyIcon_; }
    [[nodiscard]] EquipSlot wears() const { return wears_; }

private:
    EquipSlot wears_;
    std::string emptyIcon_;
};

class OffhandSlot final : public Slot {
public:
    OffhandSlot(Container& inventory, int px, int py) : Slot(inventory, Inventory::kOffhand, px, py) {}
    [[nodiscard]] std::string noItemIcon() const override { return "container/slot/shield"; }
};

class InventoryMenu final : public ContainerMenu {
public:
    // The game's slot numbers.
    static constexpr int kResult = 0;
    static constexpr int kCraftStart = 1, kCraftEnd = 5;
    static constexpr int kArmorStart = 5, kArmorEnd = 9;
    static constexpr int kInvStart = 9, kInvEnd = 36;
    static constexpr int kHotbarStart = 36, kHotbarEnd = 45;
    static constexpr int kOffhandSlot = 45;
    static constexpr int kSlotCount = 46;

    explicit InventoryMenu(MenuPlayer player);

    [[nodiscard]] SimpleContainer& craftSlots() { return craftSlots_; }
    [[nodiscard]] ResultContainer& resultSlots() { return resultSlots_; }
    ItemStack quickMoveStack(int index) override;
    [[nodiscard]] bool canTakeItemForPickAll(const ItemStack& stack, const Slot& slot) const override;
    void slotsChanged() override;
    void removed() override;
    // True for the 9 hotbar slots and the offhand (InventoryMenu.isHotbarSlot).
    [[nodiscard]] static bool isHotbarSlot(int menuSlot) { return (menuSlot >= kHotbarStart && menuSlot < kHotbarEnd) || menuSlot == kOffhandSlot; }

private:
    void updateResult();

    SimpleContainer craftSlots_{4};
    ResultContainer resultSlots_;
    bool updating_ = false;
};

}  // namespace apex
