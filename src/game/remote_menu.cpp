#include "falcon/game/remote_menu.hpp"

#include <cstdlib>

namespace falcon {

namespace {

// A slot that only gives (a furnace's output, a crafting table's result).
class ResultOnlySlot final : public Slot {
public:
    using Slot::Slot;
    [[nodiscard]] bool mayPlace(const ItemStack&) const override { return false; }
};

}  // namespace

bool remoteMenuKindOf(const std::string& typeName, RemoteMenuKind& kind, int& rows) {
    rows = 0;
    if (typeName.compare(0, 8, "generic_") == 0 && typeName.size() == 11 && typeName[8] == '9' && typeName[9] == 'x' && typeName[10] >= '1' && typeName[10] <= '6') {
        kind = RemoteMenuKind::Chest;
        rows = typeName[10] - '0';
        return true;
    }
    if (typeName == "generic_3x3") { kind = RemoteMenuKind::Dispenser; return true; }
    if (typeName == "shulker_box") { kind = RemoteMenuKind::Shulker; return true; }
    if (typeName == "hopper") { kind = RemoteMenuKind::Hopper; return true; }
    if (typeName == "furnace") { kind = RemoteMenuKind::Furnace; return true; }
    if (typeName == "blast_furnace") { kind = RemoteMenuKind::BlastFurnace; return true; }
    if (typeName == "smoker") { kind = RemoteMenuKind::Smoker; return true; }
    if (typeName == "crafting") { kind = RemoteMenuKind::Crafting; return true; }
    return false;
}

RemoteMenu::RemoteMenu(MenuPlayer player, RemoteMenuKind kind, int rows, int containerId)
    : ContainerMenu(std::move(player)), kind_(kind), rows_(rows), containerId_(containerId) {
    // The slots as the game's menus lay them out (ChestMenu, ShulkerBoxMenu, DispenserMenu, HopperMenu, AbstractFurnaceMenu, CraftingMenu): the container's, then the inventory's.
    int inventoryTop = 84;
    switch (kind) {
        case RemoteMenuKind::Chest:
            containerSlots_ = rows * 9;
            container_ = SimpleContainer(containerSlots_);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < 9; ++c) addSlot(std::make_unique<Slot>(container_, r * 9 + c, 8 + c * 18, 18 + r * 18));
            }
            inventoryTop = 103 + (rows - 4) * 18;
            break;
        case RemoteMenuKind::Shulker:
            containerSlots_ = 27;
            container_ = SimpleContainer(containerSlots_);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 9; ++c) addSlot(std::make_unique<Slot>(container_, r * 9 + c, 8 + c * 18, 18 + r * 18));
            }
            break;
        case RemoteMenuKind::Dispenser:
            containerSlots_ = 9;
            container_ = SimpleContainer(containerSlots_);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) addSlot(std::make_unique<Slot>(container_, r * 3 + c, 62 + c * 18, 17 + r * 18));
            }
            break;
        case RemoteMenuKind::Hopper:
            containerSlots_ = 5;
            container_ = SimpleContainer(containerSlots_);
            for (int i = 0; i < 5; ++i) addSlot(std::make_unique<Slot>(container_, i, 44 + i * 18, 20));
            inventoryTop = 51;
            break;
        case RemoteMenuKind::Furnace:
        case RemoteMenuKind::BlastFurnace:
        case RemoteMenuKind::Smoker:
            containerSlots_ = 3;
            container_ = SimpleContainer(containerSlots_);
            addSlot(std::make_unique<Slot>(container_, 0, 56, 17));            // what is cooked
            addSlot(std::make_unique<Slot>(container_, 1, 56, 53));            // the fuel
            addSlot(std::make_unique<ResultOnlySlot>(container_, 2, 116, 35));  // what comes out
            break;
        case RemoteMenuKind::Crafting:
            containerSlots_ = 10;
            container_ = SimpleContainer(containerSlots_);
            addSlot(std::make_unique<ResultOnlySlot>(container_, 0, 124, 35));
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) addSlot(std::make_unique<Slot>(container_, 1 + r * 3 + c, 30 + c * 18, 17 + r * 18));
            }
            break;
    }
    addStandardInventorySlots(*player_.inventory, 8, inventoryTop);
}

int RemoteMenu::imageHeight() const {
    switch (kind_) {
        case RemoteMenuKind::Chest: return 114 + rows_ * 18;
        case RemoteMenuKind::Shulker: return 167;
        case RemoteMenuKind::Hopper: return 133;
        default: return 166;
    }
}

int RemoteMenu::setFromServer(int menuSlot, ItemStack stack) {
    if (menuSlot < 0 || menuSlot >= slotCount()) return -1;
    Slot& s = *slots_[static_cast<std::size_t>(menuSlot)];
    const bool players = menuSlot >= containerSlots_;
    s.set(std::move(stack));
    return players ? s.slot : -1;
}

// AbstractContainerMenu.quickMoveStack in its plain form: from the container into the inventory (the end first), from the inventory into the container (a furnace takes the input
// and the fuel, a crafting table its grid, never the result). The server does the same and says what it really came to.
ItemStack RemoteMenu::quickMoveStack(int index) {
    ItemStack original;
    if (index < 0 || index >= slotCount()) return original;
    Slot& slot = *slots_[static_cast<std::size_t>(index)];
    if (!slot.hasItem()) return original;
    ItemStack& stack = slot.getItem();
    original = stack;
    if (index < containerSlots_) {
        if (!moveItemStackTo(stack, containerSlots_, slotCount(), true)) return ItemStack();
    } else {
        int from = 0, to = containerSlots_;
        if (kind_ == RemoteMenuKind::Furnace || kind_ == RemoteMenuKind::BlastFurnace || kind_ == RemoteMenuKind::Smoker) to = 2;
        if (kind_ == RemoteMenuKind::Crafting) from = 1;
        if (!moveItemStackTo(stack, from, to, false)) return ItemStack();
    }
    if (stack.isEmpty()) slot.set(ItemStack());
    else slot.setChanged();
    return original;
}

}  // namespace falcon
