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

// GameSession: the player's body, health, sounds and what happens to the player every tick.

namespace falcon {

// Camera.tick and AbstractClientPlayer.getFieldOfViewModifier: flying is 10% wider, and the speed against the walking speed (sprinting is +30%) widens it by half of the
// difference; the option scales it all, and what is shown follows it by half the distance every tick.
void GameSession::tickFovModifier() {
    float target = 1.0f;
    if (player_.flying) target *= 1.1f;
    const float speed = player_.sprinting ? 1.3f : 1.0f;  // (the movement speed against the walking speed of 0.1)
    target *= (speed + 1.0f) / 2.0f;
    const float scale = static_cast<float>(services_.options.fovEffectScale);
    target = 1.0f + (target - 1.0f) * scale;
    fovModifierO_ = fovModifier_;
    fovModifier_ = std::clamp(fovModifier_ + (target - fovModifier_) * 0.5f, 0.1f, 1.5f);
}

void GameSession::tickPlayer(double seconds, bool steering) {
    tickAccumulator_ += std::max(seconds, 0.0);
    const int due = static_cast<int>(tickAccumulator_ / kTickSeconds);
    tickAccumulator_ -= due * kTickSeconds;
    partialTick_ = std::clamp(tickAccumulator_ / kTickSeconds, 0.0, 1.0);
    if (due == 0) return;
    // A menu that does not stop the game (the inventory) takes the keys: no walking, no jumping, but the world goes on.
    const PlayerInput input = steering ? sampleInput() : PlayerInput{};
    if (steering && services_.options.toggleSprint && !input.forward) sprintLatched_ = false;  // stopping ends a toggled sprint
    // On a server a stall is not caught up in a burst: an anti-cheat (Grim's Timer) reads ten ticks in one frame as a client that runs fast. The late ticks are dropped.
    const int limit = remote_ ? kMaxRemoteTicksPerFrame : kMaxTicksPerFrame;
    for (int i = 0; i < std::min(due, limit); ++i) {
        playerAgeTicks_ += 1.0;
        if (!pinnedPose_) {  // (a pinned camera, for screenshots, freezes the player; the world goes on)
            const double vx = player_.vx, vy = player_.vy, vz = player_.vz;  // (what it was going at, for a splash)
            if (riding()) {
                tickRiding(input);
            } else {
                if (ridingEntity_ >= 0 || boat_) leaveVehicle();
                player_.tick(input, renderer_.level());
                sendRemoteTick(input);
            }
            tickFovModifier();
            waterVisionTicks_ = nextWaterVisionTicks(waterVisionTicks_, player_.isUnderWater(), info_.gameType == 3);  // the eyes get used to the water
            if (ready_ && !dead_) {
                footsteps(input.shift);
                if (const int damage = player_.takeFallDamage(); damage > 0 && survivalBars() && !remote_) fallDamage(damage);  // (on a server it says how much it hurt)
                if (player_.inWater && !wasInWater_) splash(vx, vy, vz);
                wasInWater_ = player_.inWater;
                tickBody();
                pickUpItems();
            }
        }
        tickHealth();
        tickBurning();
        if (ready_ && !remote_) itemEntities_.tick(renderer_.level());  // (on a server the items are the server's: pumpRemote puts them there)
        // The water that flows: its updates that are due this tick.
        if (!remote_) fluids_.tick(renderer_.level(), [this](int bx, int by, int bz, Block b) { return renderer_.setBlock(bx, by, bz, b); });
        if (ready_ && physics_) physics_->tick();
        if (ready_ && redstone_) redstone_->tick();
    }
    // Out of the world (below the void line) the void hurts a survival player (4 a tick, through any protection) until they die; anyone else
    // starts again at the spawn.
    if (!pinnedPose_ && player_.y < static_cast<double>(kMinY) - 64.0) {
        if (survivalBars()) {
            hurt(4.0f, true);
        } else {
            player_.setPosition(spawn_[0], spawn_[1], spawn_[2]);
            player_.vx = player_.vy = player_.vz = 0.0;
            player_.fallDistance = 0.0;
            player_.lastFallDamage = 0;
        }
    }
}

void GameSession::tickHud(double seconds) {
    hudTickAccumulator_ += std::max(seconds, 0.0);
    const int due = static_cast<int>(hudTickAccumulator_ / kTickSeconds);
    hudTickAccumulator_ -= due * kTickSeconds;
    for (int i = 0; i < std::min(due, kMaxTicksPerFrame); ++i) {
        highlight_.tick(inventory_.selectedItem(), services_.options.notificationDisplayTime);
        hand_.tick(inventory_.selectedItem(), player_.yaw, player_.pitch);
        chat_.tick();  // ages chat messages for the fade
    }
}

// Player.drop. Thrown from the hand: out of the face, in the direction of the look, with a little spread. Scattered (what is lost at death): up and
// out in any direction.
void GameSession::dropFromPlayer(const ItemStack& stack, bool scatter) {
    if (stack.isEmpty()) return;
    ++droppedCount_;
    double vx, vy, vz;
    if (scatter) {
        const float f = randomFloat() * 0.5f, g = randomFloat() * 6.2831855f;
        vx = -std::sin(g) * f;
        vy = 0.2;
        vz = std::cos(g) * f;
    } else {
        const float degrees = 0.017453292f;
        const float sp = std::sin(player_.pitch * degrees), cp = std::cos(player_.pitch * degrees);
        const float sy = std::sin(player_.yaw * degrees), cy = std::cos(player_.yaw * degrees);
        const float k = randomFloat() * 6.2831855f, l = 0.02f * randomFloat();
        vx = static_cast<double>(-sy * cp * 0.3f) + std::cos(k) * l;
        vy = static_cast<double>(-sp * 0.3f + 0.1f + (randomFloat() - randomFloat()) * 0.1f);
        vz = static_cast<double>(cy * cp * 0.3f) + std::sin(k) * l;
    }
    itemEntities_.spawn(stack, player_.x, player_.eyeY() - 0.3, player_.z, vx, vy, vz, 40, randomFloat() * 6.2831855f);
}

void GameSession::playWorldSound(const std::string& event, SoundSource source, float volume, float pitch, const std::array<double, 3>* at) {
    if (at != nullptr) {  // fades linearly with the distance from the head: gone at 16 blocks (more for a louder sound)
        const auto head = player_.eye();
        const double dx = (*at)[0] - head[0], dy = (*at)[1] - head[1], dz = (*at)[2] - head[2];
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double range = 16.0 * std::max(1.0f, volume);
        if (distance >= range) return;
        volume *= static_cast<float>(1.0 - std::max(0.0, distance - 1.0) / (range - 1.0));
    }
    pitch = std::clamp(pitch, 0.5f, 2.0f);
    static const bool log = std::getenv("FALCON_SOUND_LOG") != nullptr;  // (for tests, which run muted: what would have been heard)
    if (log) std::printf("[falcon] sound %s volume %.2f pitch %.2f\n", event.c_str(), static_cast<double>(volume), static_cast<double>(pitch));
    services_.sounds.play(event, source, volume, pitch);
}

void GameSession::playBlockSound(Block block, const char* kind, SoundMix mix, SoundSource source, const std::array<double, 3>* at) {
    const std::string& set = registry_->type(block).sound;
    if (set.empty()) return;
    playWorldSound(blockSoundEvent(set, kind), source, mix.volume, mix.pitch, at);
}

// Entity.applyMovementEmissionAndPlaySound for the player. Nothing sounds while flying or while sneaking on the ground (Player.getMovementEmission);
// otherwise the walked distance runs the clock, and a step is due when it passes the next mark: on a block (not air), with the feet on the ground
// (or edging along while sneaking), not swimming. In water the step is a swimming sound and a muffled step.
void GameSession::footsteps(bool sneakKey) {
    if (player_.flying || player_.spectator || (player_.onGround && sneakKey)) return;
    const double dx = player_.x - player_.xo, dz = player_.z - player_.zo;
    stepClock_.add(std::hypot(dx, dz));
    if (!stepClock_.due()) return;
    const int bx = static_cast<int>(std::floor(player_.x)), by = static_cast<int>(std::floor(player_.y - 0.2)), bz = static_cast<int>(std::floor(player_.z));
    const Block under = renderer_.level().block(bx, by, bz);
    if (registry_->isAir(under)) return;  // (the clock keeps running: the first step on the ground that is met sounds at once)
    const BlockType& type = registry_->type(under);
    const bool onFoot = (player_.onGround || (player_.sneaking && player_.y == player_.yo)) && !player_.isSwimming();
    if (onFoot) {
        stepClock_.played();
        if (player_.inWater) {  // Player.playStepSound: waterSwimSound, then the muffled step
            const float volume = std::min(1.0f, std::sqrt(static_cast<float>(player_.vx * player_.vx * 0.2 + player_.vy * player_.vy + player_.vz * player_.vz * 0.2)) * 0.4f);
            playWorldSound("entity.player.swim", SoundSource::Players, volume, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
            const SoundMix mix = mutedStepMix(type.soundVolume, type.soundPitch);
            playBlockSound(under, "step", mix, SoundSource::Players);
        } else {
            // A thin block on top (snow, a moss carpet) is what the foot lands on, so it sounds instead of the one below.
            Block sounding = under;
            const Block above = renderer_.level().block(bx, by + 1, bz);
            if (!registry_->isAir(above)) {
                const std::string& n = registry_->type(above).name;
                if (n == "snow" || n == "moss_carpet" || n == "pale_moss_carpet") sounding = above;
            }
            const BlockType& stepType = registry_->type(sounding);
            playBlockSound(sounding, "step", stepMix(stepType.soundVolume, stepType.soundPitch), SoundSource::Players);
        }
    } else if (player_.inWater) {
        stepClock_.played();
        const float volume = std::min(1.0f, std::sqrt(static_cast<float>(player_.vx * player_.vx * 0.2 + player_.vy * player_.vy + player_.vz * player_.vz * 0.2)) * 0.4f);
        playWorldSound("entity.player.swim", SoundSource::Players, volume, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
    }
}

// Entity.doWaterSplashEffect: the harder it came down, the louder the splash (and the bigger one from 1.25 blocks a tick).
void GameSession::splash(double vx, double vy, double vz) {
    const float volume = std::min(1.0f, std::sqrt(static_cast<float>(vx * vx * 0.2 + vy * vy + vz * vz * 0.2)) * 0.2f);
    playWorldSound(volume < 0.25f ? "entity.player.splash" : "entity.player.splash.high_speed", SoundSource::Players, volume, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
}

// LivingEntity.causeFallDamage: the cry for the size of the fall, the block's fall sound, then the hurt itself.
void GameSession::fallDamage(int damage) {
    if (!gameRuleOn("fall_damage")) return;
    playWorldSound(fallDamageEvent(damage), SoundSource::Players, 1.0f, 1.0f);
    const Block under = renderer_.level().block(static_cast<int>(std::floor(player_.x)), static_cast<int>(std::floor(player_.y - 0.2)), static_cast<int>(std::floor(player_.z)));
    if (!registry_->isAir(under)) {
        const BlockType& type = registry_->type(under);
        playBlockSound(under, "fall", fallMix(type.soundVolume, type.soundPitch), SoundSource::Players);
    }
    hurt(static_cast<float>(damage), false, true);
}

float GameSession::armorPoints() {
    float total = 0.0f;
    for (const int slot : {Inventory::kFeet, Inventory::kLegs, Inventory::kChest, Inventory::kHead}) {
        const ItemStack& stack = inventory_.item(slot);
        if (!stack.isEmpty() && stack.type().armorSlot != EquipSlot::None) total += stack.type().armor;
    }
    return total;
}

float GameSession::armorToughness() {
    float total = 0.0f;
    for (const int slot : {Inventory::kFeet, Inventory::kLegs, Inventory::kChest, Inventory::kHead}) {
        const ItemStack& stack = inventory_.item(slot);
        if (!stack.isEmpty() && stack.type().armorSlot != EquipSlot::None) total += stack.type().armorToughness;
    }
    return total;
}

bool GameSession::hurt(float amount, bool bypassCooldown, bool bypassArmor) {
    if (!survivalBars() || dead_ || amount <= 0.0f) return false;  // creative and spectator players cannot be hurt
    if (!bypassArmor) {  // CombatRules.getDamageAfterAbsorb: armor takes up to four fifths of a hit (more of a small one), toughness helps against big ones
        const float armor = armorPoints();
        if (armor > 0.0f) {
            const float f = 2.0f + armorToughness() / 4.0f;
            const float g = std::clamp(armor - amount / f, armor * 0.2f, 20.0f);
            amount *= 1.0f - g / 25.0f;
        }
    }
    float applied = amount;
    if (!bypassCooldown && invulnerableTime_ > 10) {  // LivingEntity.hurtServer: in the half second after a hit only a stronger one counts, for the difference
        if (amount <= lastHurt_) return false;
        applied = amount - lastHurt_;
        lastHurt_ = amount;
    } else {
        lastHurt_ = amount;
        invulnerableTime_ = 20;
        hurtTime_ = 10;
        hurtDirection_ = randomFloat() < 0.5f ? 0.0f : 180.0f;  // (a hit with no attacker tilts the view to a random side)
        if (vitals_.health - applied > 0.0f) playWorldSound("entity.player.hurt", SoundSource::Players, 1.0f, 1.0f + (randomFloat() - randomFloat()) * 0.2f);
    }
    vitals_.health = std::max(0.0f, vitals_.health - applied);
    if (vitals_.health <= 0.0f) die();
    return true;
}

void GameSession::die() {
    dead_ = true;
    deathRequested_ = true;
    playWorldSound("entity.player.death", SoundSource::Players, 1.0f, 1.0f + (randomFloat() - randomFloat()) * 0.2f);
    if (remote_) return;  // (on a server what the player carried is dropped by it)
    // Inventory.dropAll: everything the player carried is scattered where they died (unless the game rule keep_inventory is on).
    for (int slot = 0; slot < Inventory::kSize && !gameRuleOn("keep_inventory"); ++slot) {
        ItemStack& stack = inventory_.item(slot);
        if (stack.isEmpty()) continue;
        dropFromPlayer(stack, true);
        stack = ItemStack();
    }
}

void GameSession::respawn() {
    if (remote_) {  // ServerboundClientCommandPacket PERFORM_RESPAWN: the server puts the player back (a position, the health, the inventory) and says so
        remote_->sendRespawn();
        dead_ = false;
        vitals_.health = vitals_.maxHealth;
        hurtTime_ = invulnerableTime_ = 0;
        return;
    }
    dead_ = false;
    vitals_.health = vitals_.maxHealth;
    vitals_.food = 20;
    vitals_.saturation = 5.0f;
    invulnerableTime_ = hurtTime_ = regenTimer_ = 0;
    lastHurt_ = 0.0f;
    lastHealthShown_ = displayHealth_ = static_cast<int>(vitals_.maxHealth);
    player_.setPosition(spawn_[0], spawn_[1], spawn_[2]);
    player_.vx = player_.vy = player_.vz = 0.0;
    player_.fallDistance = 0.0;
    player_.lastFallDamage = 0;
    stepClock_ = StepClock();
}

// The part of a game tick that is about health: the protection and the tilt run down, and a player who is fed (18 or more of food) heals half a
// heart every four seconds (FoodData.tick's slow healing; the fast one needs food to burn, and none is used up yet).
void GameSession::tickHealth() {
    if (invulnerableTime_ > 0) --invulnerableTime_;
    if (hurtTime_ > 0) --hurtTime_;
    if (!survivalBars() || dead_ || remote_) return;  // (on a server the health is what it says)
    if (vitals_.health > 0.0f && vitals_.health < vitals_.maxHealth && vitals_.food >= 18 && gameRuleOn("natural_health_regeneration")) {
        if (++regenTimer_ >= 80) {
            vitals_.health = std::min(vitals_.maxHealth, vitals_.health + 1.0f);
            regenTimer_ = 0;
        }
    } else {
        regenTimer_ = 0;
    }
}

// Fire and lava: standing in a fire burns for 8 seconds (1 damage a second, 2 in soul fire), lava for 15 (4 a second); water puts it out. No flames are drawn on the
// screen yet (the burning overlay).
void GameSession::tickBurning() {
    if (remote_) {  // on a server the flames come from its word that the player burns (the entity flags), and it does the damage
        burnTicks_ = selfOnFire_ ? 20 : 0;
        return;
    }
    if (!survivalBars() || dead_) {
        burnTicks_ = 0;
        return;
    }
    const Aabb box = player_.boundingBox();
    bool inFire = false, inSoulFire = false;
    if (behaviours_) {
        for (int y = static_cast<int>(std::floor(box.minY)); y <= static_cast<int>(std::floor(box.maxY - 1.0E-7)); ++y) {
            for (int x = static_cast<int>(std::floor(box.minX)); x <= static_cast<int>(std::floor(box.maxX - 1.0E-7)); ++x) {
                for (int z = static_cast<int>(std::floor(box.minZ)); z <= static_cast<int>(std::floor(box.maxZ - 1.0E-7)); ++z) {
                    const int index = registry_->state(renderer_.level().block(x, y, z)).block;
                    if (!behaviours_->isFire(index)) continue;
                    inFire = true;
                    inSoulFire = inSoulFire || registry_->type(index).name == "soul_fire";
                }
            }
        }
    }
    const bool fireDamage = gameRuleOn("fire_damage");
    if (player_.isInLava()) {
        burnTicks_ = 300;
        if (fireDamage) hurt(4.0f);
    } else if (inFire) {
        burnTicks_ = std::max(burnTicks_, 160);
        if (fireDamage) hurt(inSoulFire ? 2.0f : 1.0f);
    }
    if (burnTicks_ > 0) {
        if (player_.inWater) {
            burnTicks_ = 0;
            playWorldSound("entity.generic.extinguish_fire", SoundSource::Players, 0.7f, 1.6f + (randomFloat() - randomFloat()) * 0.4f);
        } else {
            if (burnTicks_ % 20 == 0 && fireDamage) hurt(1.0f);
            --burnTicks_;
        }
    }
}

// Player.aiStep: the items the player's box, grown by a block either side and half a block up and down, touches are taken once their delay is over.
void GameSession::pickUpItems() {
    if (info_.gameType == 3 || dead_ || remote_) return;  // (on a server the server takes the items and says what is in the inventory)
    Aabb reach = player_.boundingBox();
    reach = {reach.minX - 1.0, reach.minY - 0.5, reach.minZ - 1.0, reach.maxX + 1.0, reach.maxY + 0.5, reach.maxZ + 1.0};
    for (ItemEntity& e : itemEntities_.items()) {
        if (e.pickupDelay > 0 || e.stack.isEmpty() || !e.box().intersects(reach)) continue;
        const int before = e.stack.count;
        inventory_.add(e.stack);
        const int taken = before - (e.stack.isEmpty() ? 0 : e.stack.count);
        if (taken > 0) playWorldSound("entity.item.pickup", SoundSource::Players, 0.2f, ((randomFloat() - randomFloat()) * 0.7f + 1.0f) * 2.0f);
    }
}

// The walk cycle and where the body faces (LivingEntity.tick and tickHeadTurn): the body turns towards where the player is going, never more than
// 75 degrees away from where the head looks.
void GameSession::tickBody() {
    swimAmountOld_ = swimAmount_;  // Player.updateSwimAmount
    swimAmount_ = player_.isVisuallySwimming() ? std::min(1.0f, swimAmount_ + 0.09f) : std::max(0.0f, swimAmount_ - 0.09f);
    const double dx = player_.x - player_.xo, dz = player_.z - player_.zo;
    walkSpeedOld_ = walkSpeed_;
    walkSpeed_ += (std::min(static_cast<float>(std::sqrt(dx * dx + dz * dz)) * 4.0f, 1.0f) - walkSpeed_) * 0.4f;
    walkPosition_ += walkSpeed_;
    const auto wrap = [](float degrees) {
        float d = std::fmod(degrees, 360.0f);
        if (d >= 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    };
    bodyYawOld_ = bodyYaw_;
    float target = bodyYaw_;
    if (dx * dx + dz * dz > 0.0025000002) {
        const float heading = static_cast<float>(std::atan2(dz, dx) * 57.29577951308232) - 90.0f;
        const float away = std::abs(wrap(player_.yaw) - heading);
        target = (95.0f < away && away < 265.0f) ? heading - 180.0f : heading;  // walking backwards faces the other way round
    }
    if (hand_.swingProgress(0.0) > 0.0f) target = player_.yaw;  // an attack or a placed block faces the way the player looks
    bodyYaw_ += wrap(target - bodyYaw_) * 0.3f;
    float diff = std::clamp(wrap(player_.yaw - bodyYaw_), -75.0f, 75.0f);
    bodyYaw_ = player_.yaw - diff;
    if (diff * diff > 2500.0f) bodyYaw_ += diff * 0.2f;
}

bool GameSession::giveItem(const std::string& spec) {
    // "item", "item:count" or "item:count:wear".
    std::vector<std::string> parts;
    for (std::size_t from = 0;;) {
        const std::size_t colon = spec.find(':', from);
        parts.push_back(spec.substr(from, colon == std::string::npos ? std::string::npos : colon - from));
        if (colon == std::string::npos) break;
        from = colon + 1;
    }
    // "minecraft:stone:5" starts with a namespace: join it back.
    if (parts.size() >= 2 && parts[0] == "minecraft") {
        parts[1] = "minecraft:" + parts[1];
        parts.erase(parts.begin());
    }
    const int count = parts.size() > 1 ? std::max(1, std::atoi(parts[1].c_str())) : 1;
    const int wear = parts.size() > 2 ? std::max(0, std::atoi(parts[2].c_str())) : 0;
    const auto item = itemRegistry().find(parts[0]);
    if (!item || *item == Item::Air) return false;
    ItemStack stack(*item, count);
    stack.damage = std::min(wear, std::max(0, stack.maxDamage() - 1));
    const int max = stack.maxStackSize();
    while (!stack.isEmpty()) {  // as many full stacks as it takes
        ItemStack part = stack.split(std::min(stack.count, max));
        if (!inventory_.add(part)) break;
    }
    return true;
}

}  // namespace falcon
