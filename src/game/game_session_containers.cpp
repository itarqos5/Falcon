#include "falcon/game/game_session.hpp"
#include "game_session_internal.hpp"

#include "falcon/world/block_render.hpp"
#include "falcon/world/recipes.hpp"

#include <array>
#include <cmath>
#include <string>

// GameSession: the blocks of a world played here that hold items or open a window: a crafting table, chests (single and double), barrels, shulker boxes, hoppers, dispensers and droppers,
// the player's ender chest, and furnaces, blast furnaces and smokers that burn fuel and cook.

namespace falcon {

using namespace session;

namespace {

// The four horizontal directions in the order the game turns them clockwise (Direction.getClockWise): north, east, south, west.
constexpr int kStepX[4] = {0, 1, 0, -1};
constexpr int kStepZ[4] = {-1, 0, 1, 0};
constexpr const char* kDirectionName[4] = {"north", "east", "south", "west"};

int directionOf(std::string_view name) {
    for (int i = 0; i < 4; ++i) {
        if (name == kDirectionName[i]) return i;
    }
    return -1;
}
int clockwise(int d) { return (d + 1) % 4; }
int counterClockwise(int d) { return (d + 3) % 4; }
int opposite(int d) { return (d + 2) % 4; }
// The horizontal direction of a block face (-1 for up and down).
int directionOfFace(int face) {
    switch (face) {
        case kEast: return 1;
        case kWest: return 3;
        case kSouth: return 2;
        case kNorth: return 0;
        default: return -1;
    }
}

}  // namespace

// ChestBlock.getConnectedDirection: where the other half of a double chest is.
static int connectedDirection(int facing, std::string_view type) { return type == "left" ? clockwise(facing) : counterClockwise(facing); }

// A chest that was just put down joins a single chest of the same facing next to it (ChestBlock.getStateForPlacement), and that chest becomes the other half
// (ChestBlock.updateShape). Sneaking joins only the chest that was clicked on the side.
void GameSession::connectChest(int x, int y, int z) {
    const Block placed = renderer_.level().block(x, y, z);
    if (storageKindOf(registry_->type(placed).name) != StorageKind::Chest || registry_->get(placed, "type").empty()) return;
    const int block = registry_->state(placed).block;
    const auto candidate = [&](int dir) -> int {  // ChestBlock.candidatePartnerFacing: the facing of a single chest of this block on that side, or -1
        const Block there = renderer_.level().block(x + kStepX[dir], y, z + kStepZ[dir]);
        if (registry_->state(there).block != block || registry_->get(there, "type") != "single") return -1;
        return directionOf(registry_->get(there, "facing"));
    };
    int facing = directionOf(registry_->get(placed, "facing"));
    if (facing < 0) return;
    std::string type = "single";
    const bool secondary = player_.sneaking;
    const int clicked = target_ ? directionOfFace(target_->face) : -1;
    if (clicked >= 0 && secondary) {
        const int connected = candidate(opposite(clicked));
        if (connected >= 0 && (connected % 2) != (clicked % 2)) {
            facing = connected;
            type = counterClockwise(connected) == opposite(clicked) ? "right" : "left";
        }
    }
    if (type == "single" && !secondary) {
        if (facing == candidate(clockwise(facing))) type = "left";
        else if (facing == candidate(counterClockwise(facing))) type = "right";
    }
    if (type == "single") return;
    Block now = registry_->with(placed, "facing", kDirectionName[facing]);
    now = registry_->with(now, "type", type);
    setBlock(x, y, z, now);
    const int side = connectedDirection(facing, type);
    const int px = x + kStepX[side], pz = z + kStepZ[side];
    const Block partner = renderer_.level().block(px, y, pz);
    setBlock(px, y, pz, registry_->with(partner, "type", type == "left" ? "right" : "left"));
}

// A block that held items was taken away or turned into another block: what it held falls out (Containers.dropContents), and the other half of a double chest is a single chest again.
void GameSession::storageBlockReplaced(Block old, Block now, int x, int y, int z) {
    if (!registry_->state(old).has(kStateBlockEntity) || registry_->state(old).block == registry_->state(now).block) return;
    const StorageKind kind = storageKindOf(registry_->type(old).name);
    if (kind == StorageKind::None) return;
    if (kind == StorageKind::Chest) {
        const std::string_view type = registry_->get(old, "type");
        const int facing = directionOf(registry_->get(old, "facing"));
        if ((type == "left" || type == "right") && facing >= 0) {
            const int side = connectedDirection(facing, type);
            const int px = x + kStepX[side], pz = z + kStepZ[side];
            const Block partner = renderer_.level().block(px, y, pz);
            if (registry_->state(partner).block == registry_->state(old).block && registry_->get(partner, "type") != "single") setBlock(px, y, pz, registry_->with(partner, "type", "single"));
        }
    }
    const auto storage = storages_.take({x, y, z});
    if (openStoragePos_ && *openStoragePos_ == std::array<int, 3>{x, y, z}) containerCloseRequested_ = true;
    if (!storage) return;
    for (int i = 0; i < storage->items.size(); ++i) {
        ItemStack stack = storage->items.item(i);
        while (!stack.isEmpty()) {  // Containers.dropItemStack: in pieces of 10 to 30, spread over the cell
            const double px = x + 0.125 + randomFloat() * 0.75, py = y + randomFloat() * 0.75, pz = z + 0.125 + randomFloat() * 0.75;
            const ItemStack piece = stack.split(10 + static_cast<int>(randomFloat() * 21.0f));
            itemEntities_.spawn(piece, px, py, pz, (randomFloat() - randomFloat()) * 0.115, 0.2 + (randomFloat() - randomFloat()) * 0.115, (randomFloat() - randomFloat()) * 0.115, 10,
                                randomFloat() * 6.2831855f);
        }
    }
}

// Player.giveExperiencePoints: the bar fills and the level rises.
void GameSession::addExperience(int points) {
    if (points <= 0) return;
    const auto needed = [](int level) { return level >= 30 ? 112 + (level - 30) * 9 : level >= 15 ? 37 + (level - 15) * 5 : 7 + level * 2; };
    vitals_.progress += static_cast<float>(points) / static_cast<float>(needed(vitals_.level));
    while (vitals_.progress >= 1.0f) {
        vitals_.progress = (vitals_.progress - 1.0f) * static_cast<float>(needed(vitals_.level));
        ++vitals_.level;
        vitals_.progress /= static_cast<float>(needed(vitals_.level));
    }
    vitals_.progress = std::clamp(vitals_.progress, 0.0f, 0.999f);
    playWorldSound("entity.experience_orb.pickup", SoundSource::Players, 0.1f, 0.5f * ((randomFloat() - randomFloat()) * 0.7f + 1.8f));
}

// The experience a furnace owes for what it made is paid when something is taken out of its output.
void GameSession::awardFurnaceExperience(BlockStorage& storage) {
    const auto recipes = currentRecipeManager();
    addExperience(collectFurnaceExperience(storage.furnace, recipes.get(), randomFloat()));
}

void GameSession::openLocalMenu(RemoteMenuKind kind, int rows, Container* storage, bool local, const std::string& title, StorageKind storageKind, const std::array<int, 3>* at) {
    remoteMenu_.reset();
    remoteMenu_ = std::make_unique<RemoteMenu>(inventoryMenu_->player(), kind, rows, 1, storage, local);
    const std::string soundBase = storageKind == StorageKind::Chest      ? "block.chest"
                                  : storageKind == StorageKind::EnderChest ? "block.ender_chest"
                                  : storageKind == StorageKind::Barrel    ? "block.barrel"
                                  : storageKind == StorageKind::Shulker   ? "block.shulker_box"
                                                                          : std::string();
    std::array<double, 3> where = {0.0, 0.0, 0.0};
    if (at != nullptr) where = {(*at)[0] + 0.5, (*at)[1] + 0.5, (*at)[2] + 0.5};
    remoteMenu_->onRemoved = [this, soundBase, where] {
        remoteMenuOpen_ = false;
        openStoragePos_.reset();
        if (!soundBase.empty()) playWorldSound(soundBase + ".close", SoundSource::Blocks, 0.5f, randomFloat() * 0.1f + 0.9f, &where);
    };
    remoteMenu_->experienceLevel = [this] { return vitals_.level; };
    if (isFurnaceKind(storageKind) && at != nullptr) {
        const std::array<int, 3> pos = *at;
        remoteMenu_->isFuel = [storageKind](const ItemStack& s) { return fuelBurnTicks(s, storageKind) > 0; };
        remoteMenu_->canSmelt = [storageKind](const ItemStack& s) {
            const auto recipes = currentRecipeManager();
            return recipes && recipes->findCooking(cookingKindOf(storageKind), s) != nullptr;
        };
        remoteMenu_->onOutputTaken = [this, pos] {
            if (BlockStorage* s = storages_.find(pos)) awardFurnaceExperience(*s);
        };
    }
    remoteMenuTitle_ = title;
    remoteMenuOpen_ = true;
    containerOpenRequested_ = true;
    if (at != nullptr) openStoragePos_ = *at;
    if (!soundBase.empty()) playWorldSound(soundBase + ".open", SoundSource::Blocks, 0.5f, randomFloat() * 0.1f + 0.9f, &where);
}

// The right click on a block that opens a window: true if it was one (the click is used up, nothing is put down).
bool GameSession::useBlockContainer(int x, int y, int z) {
    if (remote_) return false;
    const Block block = renderer_.level().block(x, y, z);
    const std::string& name = registry_->type(block).name;
    const auto text = [this](const std::string& key) { return services_.language.get(key); };
    if (name == "crafting_table") {
        openLocalMenu(RemoteMenuKind::Crafting, 0, nullptr, true, text("container.crafting"), StorageKind::None, nullptr);
        return true;
    }
    const StorageKind kind = storageKindOf(name);
    if (kind == StorageKind::None) {
        static const char* const kNotYet[] = {"anvil", "chipped_anvil", "damaged_anvil", "enchanting_table", "brewing_stand", "beacon", "grindstone", "loom", "stonecutter",
                                              "cartography_table", "smithing_table", "lectern", "crafter"};
        for (const char* part : kNotYet) {
            if (name != part) continue;
            if (!unsupportedHint_) addChatMessage("\xC2\xA7" "7Falcon cannot show this window yet.");
            unsupportedHint_ = true;
            return true;
        }
        return false;
    }
    const std::array<int, 3> pos = {x, y, z};
    const auto blockedAbove = [this](int bx, int by, int bz) { return registry_->state(renderer_.level().block(bx, by + 1, bz)).has(kStateOpaqueCube); };
    switch (kind) {
        case StorageKind::EnderChest:
            if (blockedAbove(x, y, z)) return true;
            openLocalMenu(RemoteMenuKind::Chest, 3, &enderChest_, false, text("container.enderchest"), kind, &pos);
            return true;
        case StorageKind::Chest: {
            if (blockedAbove(x, y, z)) return true;  // ChestBlock.isChestBlockedAt: a solid block on top keeps it shut
            BlockStorage& self = storages_.ensure(pos, kind);
            const std::string_view type = registry_->get(block, "type");
            const int facing = directionOf(registry_->get(block, "facing"));
            if ((type == "left" || type == "right") && facing >= 0) {
                const int side = connectedDirection(facing, type);
                const int px = x + kStepX[side], pz = z + kStepZ[side];
                const Block other = renderer_.level().block(px, y, pz);
                if (registry_->state(other).block == registry_->state(block).block && registry_->get(other, "type") != "single") {
                    if (blockedAbove(px, y, pz)) return true;
                    BlockStorage& partner = storages_.ensure({px, y, pz}, kind);
                    // The right half is the first of the two (DoubleBlockCombiner: the "first" block of a double chest is the right one).
                    openCompound_ = type == "right" ? std::make_unique<CompoundContainer>(self.items, partner.items) : std::make_unique<CompoundContainer>(partner.items, self.items);
                    openLocalMenu(RemoteMenuKind::Chest, 6, openCompound_.get(), false, text("container.chestDouble"), kind, &pos);
                    return true;
                }
            }
            openLocalMenu(RemoteMenuKind::Chest, 3, &self.items, false, text("container.chest"), kind, &pos);
            return true;
        }
        case StorageKind::Barrel:
            openLocalMenu(RemoteMenuKind::Chest, 3, &storages_.ensure(pos, kind).items, false, text("container.barrel"), kind, &pos);
            return true;
        case StorageKind::Shulker:
            openLocalMenu(RemoteMenuKind::Shulker, 3, &storages_.ensure(pos, kind).items, false, text("block.minecraft." + name), kind, &pos);
            return true;
        case StorageKind::Hopper:
            openLocalMenu(RemoteMenuKind::Hopper, 0, &storages_.ensure(pos, kind).items, false, text("container.hopper"), kind, &pos);
            return true;
        case StorageKind::Dispenser:
            openLocalMenu(RemoteMenuKind::Dispenser, 0, &storages_.ensure(pos, kind).items, false, text(name == "dropper" ? "container.dropper" : "container.dispenser"), kind, &pos);
            return true;
        case StorageKind::Furnace:
        case StorageKind::BlastFurnace:
        case StorageKind::Smoker: {
            const RemoteMenuKind menuKind = kind == StorageKind::Furnace ? RemoteMenuKind::Furnace : kind == StorageKind::BlastFurnace ? RemoteMenuKind::BlastFurnace : RemoteMenuKind::Smoker;
            openLocalMenu(menuKind, 0, &storages_.ensure(pos, kind).items, false, text(kind == StorageKind::Furnace ? "container.furnace" : kind == StorageKind::BlastFurnace ? "container.blast_furnace" : "container.smoker"),
                          kind, &pos);
            return true;
        }
        case StorageKind::None: break;
    }
    return false;
}

// One game tick of every furnace whose chunk is loaded: fuel burns, food and ore cook, the block's flame follows; and the numbers a furnace window shows are brought up to date.
void GameSession::tickContainers() {
    if (remote_ || storages_.size() == 0) return;
    const auto recipes = currentRecipeManager();
    std::vector<std::pair<std::array<int, 3>, bool>> flips;
    for (auto& [pos, storage] : storages_.all()) {
        if (!isFurnaceKind(storage->kind) || !renderer_.level().isLoaded(pos[0], pos[2])) continue;
        if (storageKindOf(registry_->type(renderer_.level().block(pos[0], pos[1], pos[2])).name) != storage->kind) continue;
        const FurnaceTickResult r = tickFurnace(*storage, recipes.get());
        if (r.litChanged) flips.emplace_back(pos, r.lit);
    }
    for (const auto& [pos, lit] : flips) {
        const Block block = renderer_.level().block(pos[0], pos[1], pos[2]);
        setBlock(pos[0], pos[1], pos[2], registry_->with(block, "lit", lit ? "true" : "false"));
    }
    if (remoteMenuOpen_ && openStoragePos_ && remoteMenu_ != nullptr) {
        if (const BlockStorage* s = storages_.find(*openStoragePos_); s != nullptr && isFurnaceKind(s->kind)) {
            remoteMenu_->setData(0, s->furnace.litTimeRemaining);
            remoteMenu_->setData(1, s->furnace.litTotalTime);
            remoteMenu_->setData(2, s->furnace.cookingProgress);
            remoteMenu_->setData(3, s->furnace.cookingTotalTime);
        }
    }
}

}  // namespace falcon
