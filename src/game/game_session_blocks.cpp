#include "falcon/game/game_session.hpp"
#include "game_session_internal.hpp"

#include "falcon/game/block_placement.hpp"
#include "falcon/world/block_outlines.hpp"
#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/game/menu_background.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/recipes.hpp"
#include "falcon/world/water_fog.hpp"
#include "falcon/worldgen/underground.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <thread>

#include <GLFW/glfw3.h>

// GameSession: what the player does to blocks: digging, drops, placing, fire starters, the right click.

namespace falcon {

// A block broken in survival drops its item into the world (Block.popResource): a little above the middle of the block, thrown up a little.
// What drops is the block's loot table (data/minecraft/loot_table/blocks), and only when the tool in the hand is the right one for the block
// (a block of stone dug by hand gives nothing; with a pickaxe it gives cobblestone).
void GameSession::collectDrop(Block broken, int x, int y, int z, const ItemStack& tool, bool correctTool) {
    if (!correctTool) return;
    std::vector<ItemStack> drops;
    if (loot_) {
        drops = loot_->drops(broken, tool, rng_);
    } else if (const Item own = itemRegistry().itemOfBlock(registry_->state(broken).block); own != Item::Air) {
        drops.emplace_back(own, 1);
    }
    const auto jitter = [this] { return static_cast<double>(randomFloat()) * 0.5 - 0.25; };
    for (const ItemStack& drop : drops) {
        itemEntities_.spawn(drop, x + 0.5 + jitter(), y + 0.5 + jitter() - 0.125, z + 0.5 + jitter(), static_cast<double>(randomFloat()) * 0.2 - 0.1, 0.2,
                            static_cast<double>(randomFloat()) * 0.2 - 0.1, 10, randomFloat() * 6.2831855f);
    }
}

void GameSession::damageHeldTool(int amount) {
    ItemStack& tool = inventory_.selectedItem();
    if (tool.isEmpty() || !tool.isDamageable() || amount <= 0) return;
    tool.damage += amount;
    if (tool.damage >= tool.maxDamage()) {  // worn out: it breaks, with its sound
        tool = ItemStack();
        playWorldSound("entity.item.break", SoundSource::Players, 0.8f, 0.8f + 0.4f * randomFloat());
    }
}

void GameSession::breakBlockAt(int x, int y, int z, bool survival) {
    const Block broken = renderer_.level().block(x, y, z);
    setBlock(x, y, z, Block::Air);
    swingArm();
    if (registry_->isAir(broken)) return;
    {  // the break sound of the block, heard where it was (a fire put out hisses instead)
        const std::array<double, 3> where = {x + 0.5, y + 0.5, z + 0.5};
        const BlockType& type = registry_->type(broken);
        if (behaviours_ && behaviours_->isFire(registry_->state(broken).block)) {
            playWorldSound("block.fire.extinguish", SoundSource::Blocks, 0.5f, 2.6f + (randomFloat() - randomFloat()) * 0.8f, &where);
        } else {
            playBlockSound(broken, "break", breakOrPlaceMix(type.soundVolume, type.soundPitch), SoundSource::Blocks, &where);
        }
    }
    if (!survival || remote_ || !gameRuleOn("block_drops")) return;  // (on a server the loot is the server's)
    const ItemStack tool = inventory_.selectedItem();
    collectDrop(broken, x, y, z, tool, !mining_ || mining_->correctForDrops(tool, broken));
    // Item.mineBlock: a tool wears by one use (more for some) from a block that was not instant to dig.
    if (!tool.isEmpty() && tool.type().isTool && registry_->state(broken).hardness != 0.0f) damageHeldTool(tool.type().toolDamagePerBlock);
}

// FlintAndSteelItem.useOn / FireChargeItem.useOn, and TntBlock.useItemOn for TNT: the block clicked decides.
bool GameSession::useFireStarter(const std::string& item, const BlockHit& hit) {
    if (!physics_ || !behaviours_) return false;
    const bool flint = item == "flint_and_steel";
    const Block clicked = renderer_.level().block(hit.x, hit.y, hit.z);
    const int index = registry_->state(clicked).block;
    const std::array<double, 3> where = {hit.x + 0.5, hit.y + 0.5, hit.z + 0.5};
    const auto spend = [&] {
        if (creative()) return;
        if (flint) {
            damageHeldTool(1);
        } else {
            ItemStack& held = inventory_.selectedItem();
            held.shrink(1);
            if (held.isEmpty()) held = ItemStack();
        }
    };
    if (behaviours_->isTnt(index)) {  // the TNT is lit where it stands: it becomes primed TNT
        setBlock(hit.x, hit.y, hit.z, Block::Air);
        physics_->primeTnt(hit.x, hit.y, hit.z);
        swingArm();
        spend();
        return true;
    }
    const std::string& name = registry_->type(index).name;
    if ((name.find("campfire") != std::string::npos || name.find("candle") != std::string::npos) && registry_->get(clicked, "lit") == "false" &&
        registry_->get(clicked, "waterlogged") != "true") {  // CampfireBlock.canLight, CandleBlock.canLight
        setBlock(hit.x, hit.y, hit.z, registry_->with(clicked, "lit", "true"));
        playWorldSound(flint ? "item.flintandsteel.use" : "item.firecharge.use", SoundSource::Blocks, 1.0f, flint ? randomFloat() * 0.4f + 0.8f : (randomFloat() - randomFloat()) * 0.2f + 1.0f, &where);
        swingArm();
        spend();
        return true;
    }
    const std::array<int, 3> at = hit.placePosition();
    if (!physics_->canPlaceFireAt(at[0], at[1], at[2])) return false;
    const std::array<double, 3> fireAt = {at[0] + 0.5, at[1] + 0.5, at[2] + 0.5};
    playWorldSound(flint ? "item.flintandsteel.use" : "item.firecharge.use", SoundSource::Blocks, 1.0f, flint ? randomFloat() * 0.4f + 0.8f : (randomFloat() - randomFloat()) * 0.2f + 1.0f, &fireAt);
    setBlock(at[0], at[1], at[2], physics_->fireStateAt(at[0], at[1], at[2]));
    swingArm();
    spend();
    return true;
}

void GameSession::swingArm() {
    hand_.swing();
    if (remote_) remote_->sendSwing();
}

// MultiPlayerGameMode.stopDestroyBlock: a dig that is let go of before it is done is told to the server as aborted.
void GameSession::stopDigging(bool abort) {
    if (abort && digPos_ && remote_) remote_->sendDig(1, (*digPos_)[0], (*digPos_)[1], (*digPos_)[2], digFace_);
    digPos_.reset();
    digProgress_ = 0.0f;
    digTicks_ = 0;
}

// Survival digging, as MultiPlayerGameMode does it. The click starts it (a block that is instant to dig goes at once); each game tick after that takes
// another share off the block until it is gone, then comes a pause of 5 ticks. Aiming at another block, or another item in the hand, starts over.
void GameSession::digSurvival(double dt, bool fresh) {
    ItemStack& tool = inventory_.selectedItem();
    if (!target_ || (digPos_ && (*digPos_ != std::array<int, 3>{target_->x, target_->y, target_->z} || digItem_ != tool.item))) stopDigging();
    if (!target_) return;
    const std::array<int, 3> pos = {target_->x, target_->y, target_->z};
    const auto perTick = [&](Block b) { return mining_ ? mining_->progressPerTick(tool, b, player_.onGround, player_.isUnderWater()) : 1.0f; };
    if (fresh) digClock_ = 0.0;
    digClock_ += dt;
    int ticks = static_cast<int>(digClock_ / kTickSeconds);
    digClock_ -= ticks * kTickSeconds;
    for (; ticks > 0 && digDelay_ > 0; --ticks) --digDelay_;  // (the pause only runs while the button is held)
    if (!digPos_) {
        const Block block = renderer_.level().block(pos[0], pos[1], pos[2]);
        if (registry_->isAir(block) || pos[1] <= kMinY) return;
        if (digDelay_ > 0 && !fresh) return;
        digFace_ = target_->face;
        if (remote_) remote_->sendDig(0, pos[0], pos[1], pos[2], digFace_);  // (START_DESTROY_BLOCK, also for a block that goes at once)
        if (perTick(block) >= 1.0f) {
            breakBlockAt(pos[0], pos[1], pos[2], true);
            return;
        }
        digPos_ = pos;
        digProgress_ = 0.0f;
        digTicks_ = 0;
        digItem_ = tool.item;
        swingArm();
        return;
    }
    for (; ticks > 0 && digPos_; --ticks) {
        const Block block = renderer_.level().block(pos[0], pos[1], pos[2]);
        if (registry_->isAir(block)) {
            stopDigging();
            break;
        }
        digProgress_ += perTick(block);
        if (digTicks_ % 4 == 0) {  // the hit sound, every fourth tick: quieter and lower than the break
            const std::array<double, 3> where = {pos[0] + 0.5, pos[1] + 0.5, pos[2] + 0.5};
            const BlockType& type = registry_->type(block);
            playBlockSound(block, "hit", {(type.soundVolume + 1.0f) / 8.0f, type.soundPitch * 0.5f}, SoundSource::Blocks, &where);
        }
        ++digTicks_;
        swingArm();
        if (digProgress_ >= 1.0f) {
            if (remote_) remote_->sendDig(2, pos[0], pos[1], pos[2], digFace_);  // STOP_DESTROY_BLOCK: the block is done
            breakBlockAt(pos[0], pos[1], pos[2], true);
            stopDigging(false);
            digDelay_ = 5;
        }
    }
}

// MultiPlayerGameMode.useItemOn on a server: the click is told with where on the block it hit; the server decides what it does (opens a door, puts the block
// down ...) and says so in block changes. True when the click can also be played out here at once (a block in the hand put down on a block that does not answer to
// a click, or any block when sneaking), so that the new block does not wait for the answer.
bool GameSession::useItemRemote() {
    const std::array<double, 3> eye = player_.eyePosition(partialTick_);
    const auto look = Look{player_.yaw, player_.pitch}.forward();
    remote_->sendUseItemOn(target_->x, target_->y, target_->z, target_->face, eye[0] + look[0] * target_->distance - target_->x, eye[1] + look[1] * target_->distance - target_->y,
                           eye[2] + look[2] * target_->distance - target_->z);
    const ItemStack& held = inventory_.selectedItem();
    if (player_.sneaking && !held.isEmpty()) return true;
    const std::string& name = registry_->type(registry_->state(renderer_.level().block(target_->x, target_->y, target_->z)).block).name;
    static const char* const kAnswers[] = {"door", "trapdoor", "button", "lever", "chest", "barrel", "furnace", "smoker", "crafting_table", "anvil", "_bed", "shulker_box", "hopper",
                                           "dispenser", "dropper", "note_block", "jukebox", "cauldron", "composter", "lectern", "loom", "grindstone", "stonecutter", "cartography_table",
                                           "smithing_table", "enchanting_table", "brewing_stand", "beacon", "command_block", "repeater", "comparator", "daylight_detector", "fence_gate",
                                           "_sign", "bell", "respawn_anchor", "cake", "flower_pot", "ender_chest", "crafter", "vault", "decorated_pot", "candle", "campfire"};
    for (const char* part : kAnswers) {
        if (name.find(part) != std::string::npos) {
            swingArm();
            return false;
        }
    }
    return true;
}

void GameSession::interact(double dt, bool active) {
    const auto look = Look{player_.yaw, player_.pitch}.forward();
    const std::array<double, 3> eye = player_.eyePosition(partialTick_);
    target_ = raycastBlocks(renderer_.level(), eye, look, kReach);
    breakCooldown_ = std::max(0.0, breakCooldown_ - dt);
    placeCooldown_ = std::max(0.0, placeCooldown_ - dt);
    if (!active) {  // a screen open lets go of a toggled key
        attackLatched_ = useLatched_ = attackRawO_ = useRawO_ = false;
        useHeld_ = false;
        return;
    }
    // Spectators do nothing; adventure players cannot break or place blocks (on a server they can still hit).
    if (info_.gameType == 3 || (info_.gameType == 2 && !remote_)) return;

    // ToggleKeyMapping: with the option on a press switches the action on or off instead of holding it.
    const bool attackRaw = isDown("key.attack"), useRaw = isDown("key.use");
    if (attackRaw && !attackRawO_) attackLatched_ = !attackLatched_;
    if (useRaw && !useRawO_) useLatched_ = !useLatched_;
    attackRawO_ = attackRaw;
    useRawO_ = useRaw;
    bool attack = services_.options.toggleAttack ? attackLatched_ : attackRaw;
    const bool use = services_.options.toggleUse ? useLatched_ : useRaw;
    useHeld_ = use;
    const bool pressed = attack;
    const bool freshPress = pressed && !digHeld_;
    if (remote_ && attack) {  // a player or mob under the crosshair, nearer than what is behind it, is hit (not dug at): the server is told, the arm swings, the attack timer starts over
        const int entity = entityUnderCrosshair(eye, look, std::min(3.0, target_ ? target_->distance : 3.0));
        if (entity >= 0) {
            stopDigging();
            if (freshPress) {
                remote_->sendAttack(entity);
                swingArm();
                hand_.resetAttackStrength();
            }
            attack = false;  // (nothing to dig this frame)
        }
    }
    digHeld_ = pressed;
    if (remote_ && use && placeCooldown_ <= 0.0 && info_.gameType != 3) {  // MultiPlayerGameMode.interact: a thing under the crosshair, nearer than any block, gets the click
        double distance = 0.0;
        const int entity = entityUnderCrosshair(eye, look, std::min(3.0, target_ ? target_->distance : 3.0), &distance);
        if (entity >= 0) {
            const auto found = remote_->players().find(entity);
            if (found != remote_->players().end()) {
                const net::RemotePlayer& thing = found->second;
                const double hx = eye[0] + look[0] * distance, hy = eye[1] + look[1] * distance, hz = eye[2] + look[2] * distance;
                const double px = thing.xo + (thing.x - thing.xo) * partialTick_, py = thing.yo + (thing.y - thing.yo) * partialTick_, pz = thing.zo + (thing.z - thing.zo) * partialTick_;
                remote_->sendInteract(entity, hx - px, hy - py, hz - pz, player_.sneaking);
                swingArm();
                placeCooldown_ = kRepeatDelay;
                return;  // (the server's answer is the whole of it: no item is used on what is behind)
            }
        }
    }
    if (info_.gameType >= 2) return;
    if (!attack) {
        breakCooldown_ = 0.0;
        stopDigging();
    }
    if (!use) {
        placeCooldown_ = 0.0;
        unsupportedHint_ = false;
    }
    if (remote_) {  // using an item (eating, drawing a bow ...) goes to the server once; letting go tells it that the use stopped
        const ItemStack& held = inventory_.selectedItem();
        const std::string& heldName = held.isEmpty() ? std::string() : held.type().name;
        const bool usable = !held.isEmpty() && (held.type().food.has_value() || heldName == "bow" || heldName == "crossbow" || heldName == "trident" || heldName == "shield" ||
                                                 heldName == "ender_pearl" || heldName == "snowball" || heldName == "egg" || heldName == "fishing_rod" || heldName == "spyglass" ||
                                                 heldName == "experience_bottle" || heldName == "firework_rocket" || heldName == "wind_charge" || heldName == "potion" || heldName == "splash_potion" ||
                                                 heldName == "lingering_potion" || heldName.find("bucket") != std::string::npos ||
                                                 held.type().armorSlot != EquipSlot::None);  // (armor in the hand goes on by a click)
        if (use && usable && !usingRemote_ && placeCooldown_ <= 0.0) {
            remote_->sendUseItem(player_.yaw, player_.pitch);
            usingRemote_ = true;
            placeCooldown_ = kRepeatDelay;
            if (!usingItem_ && held.type().consumable && canConsume(held)) startUsingItem();  // (the bites are heard and the hand brings it up while the server counts)
        }
        if (!use && usingRemote_) {
            remote_->sendAction(5);  // RELEASE_USE_ITEM
            usingRemote_ = false;
        }
    }
    if (usingItem_) return;  // (LocalPlayer.isHandsBusy: nothing is dug, hit or put down while something is eaten)
    if (attack && !target_ && breakCooldown_ <= 0.0) {  // a swing at the air: the attack cooldown starts over
        breakCooldown_ = kRepeatDelay;
        swingArm();
        hand_.resetAttackStrength();
    }
    if (attack && !creative()) digSurvival(dt, freshPress);
    if (use && placeCooldown_ <= 0.0 && !remote_ && !inventory_.selectedItem().isEmpty()) {  // (on a server the click goes to it)
        // Food, a potion, milk ...: using it starts (a block item, a carrot say, is put down first when there is a place for it).
        if (!usingItem_ && inventory_.selectedItem().type().consumable && (!target_ || inventory_.selectedItem().type().blockIndex < 0) && canConsume(inventory_.selectedItem())) {
            startUsingItem();
            placeCooldown_ = kRepeatDelay;
            return;
        }
        // Equippable.swapWithEquipmentSlot: armor used in the hand (aimed at a block or at the air) goes on, and what was worn there comes into the hand.
        const EquipSlot armorSlot = inventory_.selectedItem().type().armorSlot;
        if (armorSlot == EquipSlot::Head || armorSlot == EquipSlot::Chest || armorSlot == EquipSlot::Legs || armorSlot == EquipSlot::Feet) {
            const int slot = armorSlot == EquipSlot::Head ? Inventory::kHead : armorSlot == EquipSlot::Chest ? Inventory::kChest : armorSlot == EquipSlot::Legs ? Inventory::kLegs : Inventory::kFeet;
            std::swap(inventory_.selectedItem(), inventory_.item(slot));
            placeCooldown_ = kRepeatDelay * 2.0;
            playWorldSound("item.armor.equip_generic", SoundSource::Players, 1.0f, 1.0f);
            swingArm();
            return;
        }
        // FireworkRocketItem.use: a rocket used in the air while gliding goes off attached to the player and pushes the glide along for about a second (10 ticks of the flight duration
        // of 1, and up to 11 more by chance); it is used up (not in creative).
        if (inventory_.selectedItem().type().name == "firework_rocket" && player_.fallFlying) {
            if (!creative()) {
                inventory_.selectedItem().shrink(1);
                if (inventory_.selectedItem().isEmpty()) inventory_.selectedItem() = ItemStack();
            }
            player_.rocketBoostTicks = 10 + static_cast<int>(randomFloat() * 6.0f) + static_cast<int>(randomFloat() * 7.0f);
            placeCooldown_ = kRepeatDelay * 2.0;
            playWorldSound("entity.firework_rocket.launch", SoundSource::Ambient, 3.0f, 1.0f);
            swingArm();
            return;
        }
    }
    if (!target_) return;

    if (attack && creative() && breakCooldown_ <= 0.0) {  // creative: every block goes at once, five ticks apart
        breakCooldown_ = kRepeatDelay;
        if (target_->y > kMinY) {  // the floor of the world stays
            if (remote_) remote_->sendDig(0, target_->x, target_->y, target_->z, target_->face);
            breakBlockAt(target_->x, target_->y, target_->z, false);
            target_ = raycastBlocks(renderer_.level(), eye, look, kReach);
        }
    }
    if (attack && !creative()) target_ = raycastBlocks(renderer_.level(), eye, look, kReach);  // (a block that was dug is gone: aim past it)
    if (use && placeCooldown_ <= 0.0 && target_) {
        placeCooldown_ = kRepeatDelay;
        if (remote_ && !useItemRemote()) return;  // (the server answers a lever, a chest ...: only a block put down is shown at once)
        // A lever, button, door, trapdoor or fence gate answers to the right click (sneaking with something in the hand puts the item down instead).
        if (redstone_ && !(player_.sneaking && !inventory_.selectedItem().isEmpty()) && redstone_->use(target_->x, target_->y, target_->z)) {
            swingArm();
            return;
        }
        // A crafting table, chest, furnace ...: its window opens (sneaking with something in the hand puts the item down instead).
        if (!remote_ && !(player_.sneaking && !inventory_.selectedItem().isEmpty()) && useBlockContainer(target_->x, target_->y, target_->z)) {
            swingArm();
            return;
        }
        if (!inventory_.selectedItem().isEmpty()) {
            const std::string& heldName = inventory_.selectedItem().type().name;
            if ((heldName == "flint_and_steel" || heldName == "fire_charge") && useFireStarter(heldName, *target_)) return;
        }
        // SlabBlock.canBeReplaced: a slab put on the open side of a half slab of the same kind makes the double slab.
        if (!inventory_.selectedItem().isEmpty() && inventory_.selectedItem().type().blockIndex >= 0) {
            const Block under = renderer_.level().block(target_->x, target_->y, target_->z);
            const int heldIndex = inventory_.selectedItem().type().blockIndex;
            if (registry_->state(under).block == heldIndex && registry_->get(under, "type").size() > 0 && registry_->type(heldIndex).name.size() > 5 &&
                registry_->type(heldIndex).name.compare(registry_->type(heldIndex).name.size() - 5, 5, "_slab") == 0) {
                const std::string_view half = registry_->get(under, "type");
                if ((half == "bottom" && target_->face == kUp) || (half == "top" && target_->face == kDown)) {
                    setBlock(target_->x, target_->y, target_->z, registry_->with(under, "type", "double"));
                    const std::array<double, 3> where = {target_->x + 0.5, target_->y + 0.5, target_->z + 0.5};
                    const BlockType& slab = registry_->type(under);
                    playBlockSound(under, "place", breakOrPlaceMix(slab.soundVolume, slab.soundPitch), SoundSource::Blocks, &where);
                    swingArm();
                    if (!creative() && !remote_) {  // (on a server the new count comes from it)
                        inventory_.selectedItem().shrink(1);
                        if (inventory_.selectedItem().isEmpty()) inventory_.selectedItem() = ItemStack();
                    }
                    return;
                }
            }
        }
        // BlockPlaceContext: aiming at a block that can be replaced (grass, ferns, dead bushes, snow, water ...) puts the new block in ITS place; any other
        // block gets the new one next to the face that was hit (a flower, a torch, a slab: beside or on top of it).
        const bool replaceClicked = registry_->state(renderer_.level().block(target_->x, target_->y, target_->z)).has(kStateReplaceable);
        const auto place = replaceClicked ? std::array<int, 3>{target_->x, target_->y, target_->z} : target_->placePosition();
        const Block there = renderer_.level().block(place[0], place[1], place[2]);
        const bool inWorld = place[1] >= kMinY && place[1] < kMaxY;
        // Not into the player's own body (whatever the pose).
        const Aabb cell{static_cast<double>(place[0]), static_cast<double>(place[1]), static_cast<double>(place[2]),
                        static_cast<double>(place[0]) + 1.0, static_cast<double>(place[1]) + 1.0, static_cast<double>(place[2]) + 1.0};
        const bool insidePlayer = cell.intersects(player_.boundingBox());
        // The held stack: a block item places its block (in the state blockStateForPlacement() chooses) and, outside
        // creative, is used up one at a time.
        ItemStack& heldStack = inventory_.selectedItem();
        const int heldBlock = heldStack.isEmpty() ? -1 : heldStack.type().blockIndex;
        if (heldBlock >= 0 && inWorld && registry_->state(there).has(kStateReplaceable) && !insidePlayer) {
            const std::array<double, 3> cursor = {eye[0] + look[0] * target_->distance - target_->x, eye[1] + look[1] * target_->distance - target_->y, eye[2] + look[2] * target_->distance - target_->z};
            Block placed = blockStateForPlacement({*registry_, heldBlock, *target_, player_.yaw, player_.pitch, player_.sneaking, cursor});
            // BlockItem.canPlace: a plant needs its soil, and a tall one (DoubleHighBlockItem) the cell above free as well.
            const BlockSupport::Reader at = [this](int bx, int by, int bz) { return renderer_.level().block(bx, by, bz); };
            // A wall torch with nothing sturdy to hang on (a click on the side of a flower, say) falls back to the standing torch, as StandingAndWallBlockItem does.
            if (support_ && !support_->canSurvive(placed, place[0], place[1], place[2], at) && registry_->defaultState(heldBlock) != placed) placed = registry_->defaultState(heldBlock);
            const bool tall = support_ && support_->isLowerHalf(placed);
            const bool roomAbove = !tall || (place[1] + 1 < kMaxY && registry_->state(at(place[0], place[1] + 1, place[2])).has(kStateReplaceable));
            const bool stands = !support_ || support_->canSurvive(placed, place[0], place[1], place[2], at);
            if (!registry_->isAir(placed) && roomAbove && stands) {
                if (tall) {  // a tall plant or a door is two blocks: the upper half first, so the lower one finds its other half
                    renderer_.setBlock(place[0], place[1] + 1, place[2], support_->upperHalf(placed));
                }
                setBlock(place[0], place[1], place[2], placed);
                if (!remote_) connectChest(place[0], place[1], place[2]);  // a chest next to a single chest makes a double one
                swingArm();
                {  // the place sound of the new block
                    const std::array<double, 3> where = {place[0] + 0.5, place[1] + 0.5, place[2] + 0.5};
                    const BlockType& type = registry_->type(placed);
                    playBlockSound(placed, "place", breakOrPlaceMix(type.soundVolume, type.soundPitch), SoundSource::Blocks, &where);
                }
                if (!creative() && !remote_) {
                    heldStack.shrink(1);
                    if (heldStack.isEmpty()) heldStack = ItemStack();
                }
            }
        }
    }
}

}  // namespace falcon
