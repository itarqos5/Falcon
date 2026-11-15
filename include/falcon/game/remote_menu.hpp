#pragma once

#include "falcon/world/menu.hpp"

#include <string>

namespace falcon {

// The containers of a server that Falcon can show: a chest, a barrel, an ender chest or a shulker box (rows of nine), a dispenser or dropper (3 x 3), a hopper, a furnace, a blast
// furnace, a smoker and a crafting table. Each is drawn from the game's own picture, with slots where the game puts them.
enum class RemoteMenuKind { Chest, Shulker, Dispenser, Hopper, Furnace, BlastFurnace, Smoker, Crafting };

// The kind (and for a chest the number of rows) of a menu type of the game's registry ("generic_9x3", "furnace", ...); false for a type that is not shown ("anvil").
[[nodiscard]] bool remoteMenuKindOf(const std::string& typeName, RemoteMenuKind& kind, int& rows);

// A menu whose contents are the server's: the slots of the container then the player's own 27 and the hotbar, numbered as the server numbers them. A click is played out here at
// once by the menu's own rules (which are the game's) and told to the server (the onClicked hook); what the server says about a slot overwrites it. A result slot (a furnace's, a
// crafting table's) takes nothing put in.
class RemoteMenu final : public ContainerMenu {
public:
    RemoteMenu(MenuPlayer player, RemoteMenuKind kind, int rows, int containerId);

    [[nodiscard]] int containerId() const { return containerId_; }
    [[nodiscard]] RemoteMenuKind kind() const { return kind_; }
    [[nodiscard]] int rows() const { return rows_; }
    [[nodiscard]] int containerSlots() const { return containerSlots_; }
    [[nodiscard]] int imageHeight() const;

    // The server says what a slot holds (the menu's own numbering). When it is one of the player's slots, the number of that slot in the Inventory is returned (so that what the
    // game keeps about the inventory can follow); otherwise -1.
    int setFromServer(int menuSlot, ItemStack stack);
    // The numbers a furnace shows (burn time left, its total, cooking progress, its total): ContainerData.
    void setData(int index, int value) {
        if (index >= 0 && index < 4) data_[index] = value;
    }
    [[nodiscard]] int data(int index) const { return index >= 0 && index < 4 ? data_[index] : 0; }

    ItemStack quickMoveStack(int index) override;

private:
    RemoteMenuKind kind_;
    int rows_;
    int containerId_;
    int containerSlots_ = 0;
    SimpleContainer container_;
    int data_[4] = {0, 0, 0, 0};
};

}  // namespace falcon
