#pragma once

#include "falcon/net/protocol_data.hpp"
#include "falcon/world/menu.hpp"

#include <string>

namespace falcon {

// The containers of a server that Falcon can show: a chest, a barrel, an ender chest or a shulker box (rows of nine), a dispenser or dropper (3 x 3), a hopper, a furnace, a blast
// furnace, a smoker, a crafting table, an anvil, an enchanting table and a villager's trades. Each is drawn from the game's own picture, with slots where the game puts them.
enum class RemoteMenuKind { Chest, Shulker, Dispenser, Hopper, Furnace, BlastFurnace, Smoker, Crafting, Anvil, Enchanting, Merchant };

// The kind (and for a chest the number of rows) of a menu type of the game's registry ("generic_9x3", "furnace", ...); false for a type that is not shown ("brewing_stand").
[[nodiscard]] bool remoteMenuKindOf(const std::string& typeName, RemoteMenuKind& kind, int& rows);

// (The same windows are used for the blocks of a world played here: then `storage` is the block's own container, and a crafting table or a furnace works things out itself, see below.)
//
// A menu whose contents are the server's: the slots of the container then the player's own 27 and the hotbar, numbered as the server numbers them. A click is played out here at
// once by the menu's own rules (which are the game's) and told to the server (the onClicked hook); what the server says about a slot overwrites it. A result slot (a furnace's, a
// crafting table's) takes nothing put in.
class RemoteMenu final : public ContainerMenu {
public:
    // `storage` (not null): the slots of the container are those of this container (a chest of the world, a double chest as one), not a copy that a server fills.
    // `local`: no server: a crafting table works out its result itself and gives the grid back when closed.
    RemoteMenu(MenuPlayer player, RemoteMenuKind kind, int rows, int containerId, Container* storage = nullptr, bool local = false);

    [[nodiscard]] int containerId() const { return containerId_; }
    [[nodiscard]] RemoteMenuKind kind() const { return kind_; }
    [[nodiscard]] int rows() const { return rows_; }
    [[nodiscard]] int containerSlots() const { return containerSlots_; }
    [[nodiscard]] int imageHeight() const;
    [[nodiscard]] int imageWidth() const { return kind_ == RemoteMenuKind::Merchant ? 276 : 176; }

    // The server says what a slot holds (the menu's own numbering). When it is one of the player's slots, the number of that slot in the Inventory is returned (so that what the
    // game keeps about the inventory can follow); otherwise -1.
    int setFromServer(int menuSlot, ItemStack stack);
    // The numbers a menu shows (ContainerData): a furnace's burn time left, its total, cooking progress and its total; an anvil's cost; an enchanting table's three costs, the seed and
    // the clue of each of the three (which enchantment, which level).
    void setData(int index, int value) {
        if (index >= 0 && index < kDataSize) data_[index] = value;
    }
    [[nodiscard]] int data(int index) const { return index >= 0 && index < kDataSize ? data_[index] : 0; }
    static constexpr int kDataSize = 12;

    // What the window of a menu can ask of the game around it: the player's experience level, the name typed into an anvil, a button pressed in an enchanting table.
    std::function<int()> experienceLevel;
    std::function<void(const std::string&)> onRename;
    std::function<void(int)> onButton;
    std::function<void(int)> onSelectTrade;  // an offer of a villager is chosen

    // A villager's window (RemoteMenuKind::Merchant): the offers it makes and how far it is in its levels.
    std::vector<net::TradeOffer> offers;
    int traderLevel = 0, traderXp = 0;
    bool showProgress = false;

    // A furnace that the game runs here: what its fuel slot and shift-click accept (AbstractFurnaceMenu.isFuel / canSmelt), and what happens when something is taken out of the output (the
    // player is paid the experience of what was made). Unset (a server's furnace): anything goes into the input and the fuel slots.
    std::function<bool(const ItemStack&)> isFuel;
    std::function<bool(const ItemStack&)> canSmelt;
    std::function<void()> onOutputTaken;

    ItemStack quickMoveStack(int index) override;
    void slotsChanged() override;
    void removed() override;

private:
    RemoteMenuKind kind_;
    int rows_;
    int containerId_;
    int containerSlots_ = 0;
    void bindContainer(Container* storage);
    ItemStack quickMoveLocal(int index);
    void updateCraftingResult();

    Container* store_ = nullptr;  // the container the first containerSlots_ slots show
    SimpleContainer owned_;       // what a server's menu fills (store_ points here unless there is a storage)
    SimpleContainer craftSlots_{9};
    ResultContainer resultSlots_;
    bool localCrafting_ = false;
    bool updating_ = false;
    int data_[kDataSize] = {};
};

}  // namespace falcon
