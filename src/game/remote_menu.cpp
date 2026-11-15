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

// A furnace's fuel slot: only a fuel (or an empty bucket, to swap with a lava bucket) goes in.
class FuelSlot final : public Slot {
public:
    FuelSlot(Container& owner, int slotNumber, int px, int py, const RemoteMenu& menu) : Slot(owner, slotNumber, px, py), menu_(menu) {}
    [[nodiscard]] bool mayPlace(const ItemStack& stack) const override {
        if (!menu_.isFuel) return true;
        return menu_.isFuel(stack) || (stack.type().name == "bucket" && getItem().type().name != "bucket");
    }

private:
    const RemoteMenu& menu_;
};

// A furnace's output: nothing goes in; taking from it pays the experience of what was made.
class FurnaceOutputSlot final : public Slot {
public:
    FurnaceOutputSlot(Container& owner, int slotNumber, int px, int py, const RemoteMenu& menu) : Slot(owner, slotNumber, px, py), menu_(menu) {}
    [[nodiscard]] bool mayPlace(const ItemStack&) const override { return false; }
    void onTake(const MenuPlayer& player, const ItemStack& taken) override {
        Slot::onTake(player, taken);
        if (menu_.onOutputTaken) menu_.onOutputTaken();
    }

private:
    const RemoteMenu& menu_;
};

// A shulker box takes anything but another shulker box.
class ShulkerSlot final : public Slot {
public:
    using Slot::Slot;
    [[nodiscard]] bool mayPlace(const ItemStack& stack) const override {
        const std::string& name = stack.type().name;
        return name.size() < 11 || name.compare(name.size() - 11, 11, "shulker_box") != 0;
    }
};

// An anvil's result: it can be taken when the player has the levels it costs (or builds freely).
class AnvilResultSlot final : public Slot {
public:
    AnvilResultSlot(Container& owner, int slotNumber, int px, int py, const RemoteMenu& menu) : Slot(owner, slotNumber, px, py), menu_(menu) {}
    [[nodiscard]] bool mayPlace(const ItemStack&) const override { return false; }
    [[nodiscard]] bool mayPickup(const MenuPlayer& player) const override { return player.infiniteMaterials || (menu_.experienceLevel && menu_.experienceLevel() >= menu_.data(0) && menu_.data(0) > 0); }

private:
    const RemoteMenu& menu_;
};

// An enchanting table's slots: the thing to enchant (one at a time) and the lapis (nothing else goes there).
class OneItemSlot final : public Slot {
public:
    using Slot::Slot;
    [[nodiscard]] int maxStackSize() const override { return 1; }
};
class LapisSlot final : public Slot {
public:
    using Slot::Slot;
    [[nodiscard]] bool mayPlace(const ItemStack& stack) const override { return !stack.isEmpty() && stack.type().name == "lapis_lazuli"; }
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
    if (typeName == "anvil") { kind = RemoteMenuKind::Anvil; return true; }
    if (typeName == "enchantment") { kind = RemoteMenuKind::Enchanting; return true; }
    if (typeName == "merchant") { kind = RemoteMenuKind::Merchant; return true; }
    return false;
}

RemoteMenu::RemoteMenu(MenuPlayer player, RemoteMenuKind kind, int rows, int containerId, Container* storage, bool local)
    : ContainerMenu(std::move(player)), kind_(kind), rows_(rows), containerId_(containerId) {
    // The slots as the game's menus lay them out (ChestMenu, ShulkerBoxMenu, DispenserMenu, HopperMenu, AbstractFurnaceMenu, CraftingMenu): the container's, then the inventory's.
    int inventoryTop = 84;
    switch (kind) {
        case RemoteMenuKind::Chest:
            containerSlots_ = rows * 9;
            bindContainer(storage);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < 9; ++c) addSlot(std::make_unique<Slot>(*store_, r * 9 + c, 8 + c * 18, 18 + r * 18));
            }
            inventoryTop = 103 + (rows - 4) * 18;
            break;
        case RemoteMenuKind::Shulker:
            containerSlots_ = 27;
            bindContainer(storage);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 9; ++c) addSlot(std::make_unique<ShulkerSlot>(*store_, r * 9 + c, 8 + c * 18, 18 + r * 18));
            }
            break;
        case RemoteMenuKind::Dispenser:
            containerSlots_ = 9;
            bindContainer(storage);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) addSlot(std::make_unique<Slot>(*store_, r * 3 + c, 62 + c * 18, 17 + r * 18));
            }
            break;
        case RemoteMenuKind::Hopper:
            containerSlots_ = 5;
            bindContainer(storage);
            for (int i = 0; i < 5; ++i) addSlot(std::make_unique<Slot>(*store_, i, 44 + i * 18, 20));
            inventoryTop = 51;
            break;
        case RemoteMenuKind::Furnace:
        case RemoteMenuKind::BlastFurnace:
        case RemoteMenuKind::Smoker:
            containerSlots_ = 3;
            bindContainer(storage);
            addSlot(std::make_unique<Slot>(*store_, 0, 56, 17));            // what is cooked
            addSlot(std::make_unique<FuelSlot>(*store_, 1, 56, 53, *this));  // the fuel
            addSlot(std::make_unique<FurnaceOutputSlot>(*store_, 2, 116, 35, *this));  // what comes out
            break;
        case RemoteMenuKind::Anvil:  // AnvilMenu: the thing, what is added to it, and the result
            containerSlots_ = 3;
            bindContainer(storage);
            addSlot(std::make_unique<Slot>(*store_, 0, 27, 47));
            addSlot(std::make_unique<Slot>(*store_, 1, 76, 47));
            addSlot(std::make_unique<AnvilResultSlot>(*store_, 2, 134, 47, *this));
            break;
        case RemoteMenuKind::Enchanting:  // EnchantmentMenu: the thing and the lapis
            containerSlots_ = 2;
            bindContainer(storage);
            addSlot(std::make_unique<OneItemSlot>(*store_, 0, 15, 47));
            addSlot(std::make_unique<LapisSlot>(*store_, 1, 35, 47));
            break;
        case RemoteMenuKind::Merchant:  // MerchantMenu: the two things that are paid and the result, on the right of the list of offers
            containerSlots_ = 3;
            bindContainer(storage);
            addSlot(std::make_unique<Slot>(*store_, 0, 136, 37));
            addSlot(std::make_unique<Slot>(*store_, 1, 162, 37));
            addSlot(std::make_unique<ResultOnlySlot>(*store_, 2, 220, 37));
            break;
        case RemoteMenuKind::Crafting:
            containerSlots_ = 10;
            if (local) {  // the grid and the result are this game's own
                localCrafting_ = true;
                craftSlots_.onChanged = [this] { slotsChanged(); };
                addSlot(std::make_unique<ResultSlot>(resultSlots_, craftSlots_, 3, 3, 124, 35));
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) addSlot(std::make_unique<Slot>(craftSlots_, r * 3 + c, 30 + c * 18, 17 + r * 18));
                }
                break;
            }
            bindContainer(storage);
            addSlot(std::make_unique<ResultOnlySlot>(*store_, 0, 124, 35));
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) addSlot(std::make_unique<Slot>(*store_, 1 + r * 3 + c, 30 + c * 18, 17 + r * 18));
            }
            break;
    }
    addStandardInventorySlots(*player_.inventory, kind == RemoteMenuKind::Merchant ? 108 : 8, inventoryTop);
}

void RemoteMenu::bindContainer(Container* storage) {
    if (storage != nullptr) {
        store_ = storage;
    } else {
        owned_ = SimpleContainer(containerSlots_);
        store_ = &owned_;
    }
}

// CraftingMenu.slotChangedCraftingGrid: the result is what the recipes make of the grid.
void RemoteMenu::updateCraftingResult() {
    if (updating_) return;
    updating_ = true;
    std::vector<ItemStack> grid;
    for (int i = 0; i < 9; ++i) grid.push_back(craftSlots_.item(i));
    ItemStack result;
    if (const auto recipes = currentRecipeManager()) {
        if (const auto match = recipes->find(CraftingInput::fromGrid(3, 3, grid))) result = match->result;
    }
    resultSlots_.item(0) = std::move(result);
    updating_ = false;
}

void RemoteMenu::slotsChanged() {
    if (localCrafting_) updateCraftingResult();
}

// CraftingMenu.removed: what is left in the grid goes back to the player.
void RemoteMenu::removed() {
    ContainerMenu::removed();
    if (!localCrafting_) return;
    resultSlots_.clearContent();
    craftSlots_.onChanged = nullptr;  // (putting the grid back away must not work out recipes)
    clearContainer(craftSlots_);
    craftSlots_.onChanged = [this] { slotsChanged(); };
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
// CraftingMenu.quickMoveStack and AbstractFurnaceMenu.quickMoveStack, for the menus this game runs itself.
ItemStack RemoteMenu::quickMoveLocal(int index) {
    ItemStack original;
    Slot& slot = *slots_[static_cast<std::size_t>(index)];
    if (!slot.hasItem()) return original;
    ItemStack& stack = slot.getItem();
    original = stack;
    if (localCrafting_) {
        if (index == 0) {
            if (!moveItemStackTo(stack, 10, 46, true)) return ItemStack();
            slot.onQuickCraft(stack, original);
        } else if (index >= 10 && index < 46) {
            if (!moveItemStackTo(stack, 1, 10, false)) {
                if (index < 37) {
                    if (!moveItemStackTo(stack, 37, 46, false)) return ItemStack();
                } else if (!moveItemStackTo(stack, 10, 37, false)) {
                    return ItemStack();
                }
            }
        } else if (!moveItemStackTo(stack, 10, 46, false)) {
            return ItemStack();
        }
    } else {  // a furnace
        if (index == 2) {
            if (!moveItemStackTo(stack, 3, 39, true)) return ItemStack();
            slot.onQuickCraft(stack, original);
        } else if (index > 2) {
            if (canSmelt && canSmelt(stack)) {
                if (!moveItemStackTo(stack, 0, 1, false)) return ItemStack();
            } else if (isFuel && isFuel(stack)) {
                if (!moveItemStackTo(stack, 1, 2, false)) return ItemStack();
            } else if (index < 30) {
                if (!moveItemStackTo(stack, 30, 39, false)) return ItemStack();
            } else if (!moveItemStackTo(stack, 3, 30, false)) {
                return ItemStack();
            }
        } else if (!moveItemStackTo(stack, 3, 39, false)) {
            return ItemStack();
        }
    }
    if (stack.isEmpty()) slot.setByPlayer(ItemStack());
    else slot.setChanged();
    if (stack.count == original.count) return ItemStack();
    slot.onTake(player_, stack);
    if (index == 0 && localCrafting_) player_.drop(stack);
    return original;
}

ItemStack RemoteMenu::quickMoveStack(int index) {
    ItemStack original;
    if (index < 0 || index >= slotCount()) return original;
    const bool furnaceKind = kind_ == RemoteMenuKind::Furnace || kind_ == RemoteMenuKind::BlastFurnace || kind_ == RemoteMenuKind::Smoker;
    if (localCrafting_ || (furnaceKind && (isFuel || canSmelt))) return quickMoveLocal(index);
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
