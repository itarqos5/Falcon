#include "falcon/game/game_session.hpp"
#include "game_session_internal.hpp"

#include <algorithm>
#include <cmath>

// GameSession: using an item that takes time (eating and drinking): the use starts with the click and ends when the key is let go, the hotbar slot is changed or the time runs out
// (LivingEntity.startUsingItem / updatingUsingItem / completeUsingItem and the Consumable component, from the 26.3 bytecode).

namespace falcon {

using namespace session;

// Consumable.canConsume: food only when the player is hungry (or the food can always be eaten, or it is creative mode); anything else (a potion, milk) always.
bool GameSession::canConsume(const ItemStack& stack) const {
    if (stack.isEmpty() || !stack.type().consumable) return false;
    if (const auto& food = stack.type().food) return creative() || food->canAlwaysEat || vitals_.food < 20;
    return true;
}

void GameSession::startUsingItem() {
    const ItemStack& held = inventory_.selectedItem();
    if (held.isEmpty() || !held.type().consumable) return;
    const int ticks = static_cast<int>(held.type().consumeSeconds * 20.0f);
    usedSlot_ = inventory_.selected;
    useTicksLeft_ = useDuration_ = std::max(ticks, 1);
    usingItem_ = true;
    swingArm();
}

void GameSession::stopUsingItem() {
    usingItem_ = false;
    useTicksLeft_ = 0;
    useDuration_ = 0;
    usedSlot_ = -1;
}

// Consumable.emitParticlesAndSounds: a bite is quiet or loud by chance with a pitch around 1, a sip is quiet with a pitch of 0.9 to 1. (The crumbs that fly from a bite are not drawn.)
void GameSession::emitUseEffects(const ItemType& type) {
    const bool drink = type.consumeAnimation == "drink";
    const float volume = drink ? 0.5f : (randomFloat() < 0.5f ? 0.5f : 1.0f);
    const float pitch = drink ? 0.9f + randomFloat() * 0.1f : 1.0f + (randomFloat() - randomFloat()) * 0.2f;
    playWorldSound(type.consumeSound, SoundSource::Players, volume, pitch);
}

// LivingEntity.updatingUsingItem for the item that is eaten: a bite is heard every fourth tick once the first third is over; when the time is up it is eaten.
void GameSession::tickUsingItem() {
    if (!usingItem_) return;
    ItemStack& held = inventory_.item(usedSlot_);
    if (!useHeld_ || dead_ || info_.gameType == 3 || inventory_.selected != usedSlot_ || held.isEmpty() || !held.type().consumable) {
        stopUsingItem();
        return;
    }
    const ItemType& type = held.type();
    const int consumeTicks = static_cast<int>(type.consumeSeconds * 20.0f);
    const int elapsed = consumeTicks - useTicksLeft_;
    if (elapsed > static_cast<int>(static_cast<float>(consumeTicks) * 0.21875f) && useTicksLeft_ % 4 == 0) emitUseEffects(type);
    if (--useTicksLeft_ > 0) return;
    if (remote_) {  // (on a server what was eaten is the server's to say)
        stopUsingItem();
        return;
    }
    completeUsingItem();
}

// Consumable.onConsume and FoodProperties.onConsume: the last bite, the food (and the sound of it), the burp, and one of the stack is gone (a potion leaves its bottle, a stew its
// bowl); creative mode uses nothing up. The effects a food or a potion carries (regeneration, hunger, ...) are not applied: there are no effects yet.
void GameSession::completeUsingItem() {
    ItemStack& held = inventory_.item(usedSlot_);
    const ItemStack before = held;
    const ItemType& type = before.type();
    emitUseEffects(type);
    if (const auto& food = type.food) {
        playWorldSound(type.consumeSound, SoundSource::Neutral, 1.0f, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
        vitals_.food = std::min(20, vitals_.food + food->nutrition);
        vitals_.saturation = std::clamp(vitals_.saturation + food->saturation, 0.0f, static_cast<float>(vitals_.food));
        playWorldSound("entity.player.burp", SoundSource::Players, 0.5f, 0.9f + randomFloat() * 0.1f);
    }
    if (!creative()) {
        held.shrink(1);
        const auto remainder = type.useRemainder.empty() ? std::nullopt : itemRegistry().find(type.useRemainder);
        if (held.isEmpty()) {
            held = remainder && *remainder != Item::Air ? ItemStack(*remainder, 1) : ItemStack();
        } else if (remainder && *remainder != Item::Air) {
            ItemStack left(*remainder, 1);
            inventory_.placeItemBack(left, [this](const ItemStack& s) { dropFromPlayer(s); });
        }
    }
    stopUsingItem();
}

}  // namespace falcon
