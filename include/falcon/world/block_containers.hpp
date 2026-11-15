#pragma once

#include "falcon/assets/nbt.hpp"
#include "falcon/world/inventory.hpp"
#include "falcon/world/recipes.hpp"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace falcon {

// The blocks that hold items (the game's block entities with a container): what each one is, and so how many slots it has and which window it opens.
enum class StorageKind : std::uint8_t { None, Chest, EnderChest, Barrel, Shulker, Hopper, Dispenser, Furnace, BlastFurnace, Smoker };

// The kind of a block by its name ("chest", "trapped_chest", "barrel", "red_shulker_box", "hopper", "dropper", "blast_furnace" ...); None for any other block.
[[nodiscard]] StorageKind storageKindOf(std::string_view blockName);
[[nodiscard]] int storageSlotCount(StorageKind kind);
[[nodiscard]] inline bool isFurnaceKind(StorageKind kind) { return kind == StorageKind::Furnace || kind == StorageKind::BlastFurnace || kind == StorageKind::Smoker; }
// What a furnace of this kind cooks (RecipeType.SMELTING / BLASTING / SMOKING).
[[nodiscard]] CookingKind cookingKindOf(StorageKind kind);

// Two containers read as one (CompoundContainer): a double chest, the first half's slots then the second's.
class CompoundContainer final : public Container {
public:
    CompoundContainer(Container& first, Container& second) : first_(first), second_(second) {}
    [[nodiscard]] int size() const override { return first_.size() + second_.size(); }
    ItemStack& item(int slot) override { return slot < first_.size() ? first_.item(slot) : second_.item(slot - first_.size()); }
    void setChanged() override {
        first_.setChanged();
        second_.setChanged();
    }

private:
    Container& first_;
    Container& second_;
};

// What a furnace keeps besides its three slots (AbstractFurnaceBlockEntity): the fuel burning, the cooking, and the recipes it has made that the player is owed experience for.
struct FurnaceState {
    int litTimeRemaining = 0;  // ticks of burning left
    int litTotalTime = 0;      // what the fuel that is burning gave (the flame's height)
    int cookingProgress = 0;
    int cookingTotalTime = 0;
    std::map<std::string, int> recipesUsed;  // recipe id -> how many were made since the output was last taken
    int lastInput = -1;                      // the item in the input slot at the last tick (a new kind starts the cooking over); not saved
};

// The contents of one block of the world.
struct BlockStorage {
    explicit BlockStorage(StorageKind k) : kind(k), items(storageSlotCount(k)) {}
    StorageKind kind;
    SimpleContainer items;  // a furnace: 0 what is cooked, 1 the fuel, 2 the output
    FurnaceState furnace;
};

// Every such block that has been used or filled, by position. Saved with the world.
class BlockStorages {
public:
    using Pos = std::array<int, 3>;
    // The storage of the block at `pos`, made empty if there is none yet (or if the block there is another kind now).
    BlockStorage& ensure(const Pos& pos, StorageKind kind);
    [[nodiscard]] BlockStorage* find(const Pos& pos);
    // Takes it out (a broken block); null if there is none.
    std::unique_ptr<BlockStorage> take(const Pos& pos);
    [[nodiscard]] std::map<Pos, std::unique_ptr<BlockStorage>>& all() { return storages_; }
    [[nodiscard]] std::size_t size() const { return storages_.size(); }
    void clear() { storages_.clear(); }

    // A list of compounds ({x, y, z, kind, items, furnace}); fromNbt replaces everything with what such a list holds (a stack of an item this game does not know is skipped).
    [[nodiscard]] NbtTag toNbt() const;
    void fromNbt(const NbtTag& list);

private:
    std::map<Pos, std::unique_ptr<BlockStorage>> storages_;
};

// The burn time of a stack as fuel in a furnace of this kind (0 if it is not a fuel): a blast furnace and a smoker burn it twice as fast (the "fast_cooking" blocks).
[[nodiscard]] int fuelBurnTicks(const ItemStack& stack, StorageKind kind);

struct FurnaceTickResult {
    bool litChanged = false;  // the fire went out or was lit: the block's `lit` property follows
    bool lit = false;
};
// AbstractFurnaceBlockEntity.serverTick: one game tick of a furnace. `recipes` may be null (nothing is cooked).
FurnaceTickResult tickFurnace(BlockStorage& storage, const RecipeManager* recipes);

// The experience owed for what a furnace made (AbstractFurnaceBlockEntity.getRecipesToAwardAndPopExperience): the sum of each recipe's experience times how often it was used,
// the fractional part kept with that chance (`random` is a number in [0, 1)); the list of used recipes is cleared.
[[nodiscard]] int collectFurnaceExperience(FurnaceState& state, const RecipeManager* recipes, float random);

}  // namespace falcon
