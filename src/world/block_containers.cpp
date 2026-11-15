#include "falcon/world/block_containers.hpp"

#include "falcon/world/item_registry.hpp"
#include "falcon/world/world_save.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {

namespace {

bool endsWith(std::string_view s, std::string_view suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

NbtTag integer(std::int64_t v) { return NbtTag::ofInteger(NbtTag::Type::Int, v); }

}  // namespace

StorageKind storageKindOf(std::string_view name) {
    if (name == "chest" || name == "trapped_chest" || endsWith(name, "copper_chest")) return StorageKind::Chest;
    if (name == "ender_chest") return StorageKind::EnderChest;
    if (name == "barrel") return StorageKind::Barrel;
    if (endsWith(name, "shulker_box")) return StorageKind::Shulker;
    if (name == "hopper") return StorageKind::Hopper;
    if (name == "dispenser" || name == "dropper") return StorageKind::Dispenser;
    if (name == "furnace") return StorageKind::Furnace;
    if (name == "blast_furnace") return StorageKind::BlastFurnace;
    if (name == "smoker") return StorageKind::Smoker;
    return StorageKind::None;
}

int storageSlotCount(StorageKind kind) {
    switch (kind) {
        case StorageKind::Chest:
        case StorageKind::EnderChest:
        case StorageKind::Barrel:
        case StorageKind::Shulker: return 27;
        case StorageKind::Hopper: return 5;
        case StorageKind::Dispenser: return 9;
        case StorageKind::Furnace:
        case StorageKind::BlastFurnace:
        case StorageKind::Smoker: return 3;
        case StorageKind::None: break;
    }
    return 0;
}

CookingKind cookingKindOf(StorageKind kind) {
    return kind == StorageKind::BlastFurnace ? CookingKind::Blasting : kind == StorageKind::Smoker ? CookingKind::Smoking : CookingKind::Smelting;
}

// ---------------------------------------------------------------------------------------------------------------

BlockStorage& BlockStorages::ensure(const Pos& pos, StorageKind kind) {
    auto it = storages_.find(pos);
    if (it == storages_.end() || it->second->kind != kind) it = storages_.insert_or_assign(pos, std::make_unique<BlockStorage>(kind)).first;
    return *it->second;
}

BlockStorage* BlockStorages::find(const Pos& pos) {
    const auto it = storages_.find(pos);
    return it == storages_.end() ? nullptr : it->second.get();
}

std::unique_ptr<BlockStorage> BlockStorages::take(const Pos& pos) {
    const auto it = storages_.find(pos);
    if (it == storages_.end()) return nullptr;
    auto out = std::move(it->second);
    storages_.erase(it);
    return out;
}

NbtTag BlockStorages::toNbt() const {
    std::vector<NbtTag> list;
    for (const auto& [pos, storage] : storages_) {
        std::vector<NbtTag> slots;
        for (int i = 0; i < storage->items.size(); ++i) {
            const ItemStack& stack = storage->items.item(i);
            if (stack.isEmpty()) continue;
            NbtTag s = stackToNbt(stack);
            s.put("slot", integer(i));
            slots.push_back(std::move(s));
        }
        if (slots.empty() && !isFurnaceKind(storage->kind)) continue;  // (an empty chest is the same as none)
        NbtTag t = NbtTag::compound();
        t.put("x", integer(pos[0])).put("y", integer(pos[1])).put("z", integer(pos[2])).put("kind", integer(static_cast<int>(storage->kind)));
        t.put("items", NbtTag::ofList(std::move(slots)));
        if (isFurnaceKind(storage->kind)) {
            const FurnaceState& f = storage->furnace;
            t.put("lit", integer(f.litTimeRemaining)).put("litTotal", integer(f.litTotalTime)).put("cook", integer(f.cookingProgress)).put("cookTotal", integer(f.cookingTotalTime));
            NbtTag used = NbtTag::compound();
            for (const auto& [id, count] : f.recipesUsed) used.put(id, integer(count));
            t.put("used", std::move(used));
        }
        list.push_back(std::move(t));
    }
    return NbtTag::ofList(std::move(list));
}

void BlockStorages::fromNbt(const NbtTag& list) {
    storages_.clear();
    if (list.type != NbtTag::Type::List) return;
    for (const NbtTag& t : list.items) {
        const int kindNumber = static_cast<int>(t.getInt("kind", 0));
        if (kindNumber <= 0 || kindNumber > static_cast<int>(StorageKind::Smoker)) continue;
        const auto kind = static_cast<StorageKind>(kindNumber);
        const Pos pos = {static_cast<int>(t.getInt("x")), static_cast<int>(t.getInt("y")), static_cast<int>(t.getInt("z"))};
        BlockStorage& storage = ensure(pos, kind);
        if (const NbtTag* slots = t.get("items"); slots != nullptr) {
            for (const NbtTag& s : slots->items) {
                ItemStack stack = stackFromNbt(s);
                const int slot = static_cast<int>(s.getInt("slot", -1));
                if (!stack.isEmpty() && slot >= 0 && slot < storage.items.size()) storage.items.item(slot) = std::move(stack);
            }
        }
        if (isFurnaceKind(kind)) {
            FurnaceState& f = storage.furnace;
            f.litTimeRemaining = static_cast<int>(t.getInt("lit"));
            f.litTotalTime = static_cast<int>(t.getInt("litTotal"));
            f.cookingProgress = static_cast<int>(t.getInt("cook"));
            f.cookingTotalTime = static_cast<int>(t.getInt("cookTotal"));
            if (const NbtTag* used = t.get("used"); used != nullptr) {
                for (const auto& [id, count] : used->fields) f.recipesUsed[id] = static_cast<int>(count.integer);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Furnaces

int fuelBurnTicks(const ItemStack& stack, StorageKind kind) {
    if (stack.isEmpty()) return 0;
    const int ticks = stack.type().fuelBurnTicks;
    return kind == StorageKind::Furnace ? ticks : ticks / 2;
}

namespace {

// AbstractFurnaceBlockEntity.canBurn: there is something to cook and the output has room for what it makes.
bool canBurn(const CookingRecipe* recipe, SimpleContainer& items) {
    if (recipe == nullptr || items.item(0).isEmpty() || recipe->result.isEmpty()) return false;
    const ItemStack& out = items.item(2);
    if (out.isEmpty()) return true;
    if (!isSameItemSameComponents(out, recipe->result)) return false;
    return out.count + recipe->result.count <= out.maxStackSize();
}

// AbstractFurnaceBlockEntity.burn: one of the input becomes the result.
void burn(const CookingRecipe& recipe, SimpleContainer& items) {
    ItemStack& input = items.item(0);
    ItemStack& out = items.item(2);
    if (out.isEmpty()) {
        out = recipe.result;
    } else {
        out.grow(recipe.result.count);
    }
    input.shrink(1);
    if (input.isEmpty()) input = ItemStack();
}

}  // namespace

FurnaceTickResult tickFurnace(BlockStorage& storage, const RecipeManager* recipes) {
    FurnaceState& f = storage.furnace;
    SimpleContainer& items = storage.items;
    FurnaceTickResult result;
    const bool wasLit = f.litTimeRemaining > 0;
    // Another kind of thing in the input slot starts the cooking over (AbstractFurnaceBlockEntity.setItem).
    const int inputNow = items.item(0).isEmpty() ? 0 : static_cast<int>(itemId(items.item(0).item));
    if (f.lastInput != -1 && f.lastInput != inputNow && f.cookingProgress != 0) f.cookingProgress = 0;
    f.lastInput = inputNow;

    if (f.litTimeRemaining > 0) --f.litTimeRemaining;
    const ItemStack& fuel = items.item(1);
    const bool hasInput = !items.item(0).isEmpty();
    const bool hasFuel = !fuel.isEmpty();
    const CookingRecipe* recipe = hasInput && recipes != nullptr ? recipes->findCooking(cookingKindOf(storage.kind), items.item(0)) : nullptr;
    const int step = storage.kind == StorageKind::Furnace ? 1 : 2;  // the speed multiplier of a "fast cooking" block
    if (f.litTimeRemaining > 0 || (hasFuel && hasInput)) {
        if (recipe != nullptr) f.cookingTotalTime = recipe->cookingTime;
        if (f.litTimeRemaining <= 0 && canBurn(recipe, items)) {  // light the next piece of fuel
            const int duration = fuelBurnTicks(fuel, storage.kind);
            f.litTimeRemaining = duration;
            f.litTotalTime = duration;
            if (duration > 0) {
                ItemStack& stack = items.item(1);
                const std::string& remainder = stack.type().useRemainder;
                if (!remainder.empty() && stack.count == 1) {  // a lava bucket leaves its bucket
                    const auto item = itemRegistry().find(remainder);
                    stack = item ? ItemStack(*item, 1) : ItemStack();
                } else {
                    stack.shrink(1);
                    if (stack.isEmpty()) stack = ItemStack();
                }
            }
        }
        if (f.litTimeRemaining > 0 && canBurn(recipe, items)) {
            f.cookingProgress += step;
            if (f.cookingProgress >= f.cookingTotalTime) {
                f.cookingProgress = 0;
                f.cookingTotalTime = recipe->cookingTime;
                burn(*recipe, items);
                ++f.recipesUsed[recipe->id];
            }
        } else {
            f.cookingProgress = 0;
        }
    } else if (f.cookingProgress > 0) {  // the fire is out: the progress runs back at twice the speed
        f.cookingProgress = std::clamp(f.cookingProgress - 2, 0, std::max(f.cookingTotalTime, 0));
    }
    const bool lit = f.litTimeRemaining > 0;
    result.lit = lit;
    result.litChanged = lit != wasLit;
    return result;
}

int collectFurnaceExperience(FurnaceState& state, const RecipeManager* recipes, float random) {
    int points = 0;
    for (const auto& [id, count] : state.recipesUsed) {
        float xp = 0.0f;
        if (recipes != nullptr) {
            for (const CookingRecipe& r : recipes->cooking()) {
                if (r.id == id) {
                    xp = r.experience;
                    break;
                }
            }
        }
        const float total = static_cast<float>(count) * xp;
        int whole = static_cast<int>(std::floor(total));
        const float fraction = total - static_cast<float>(whole);
        if (fraction != 0.0f && random < fraction) ++whole;
        points += whole;
    }
    state.recipesUsed.clear();
    return points;
}

}  // namespace falcon
