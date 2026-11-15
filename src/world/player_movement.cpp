#include "falcon/world/player.hpp"
#include "player_internal.hpp"

#include "falcon/world/fluids.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// The local player's physics, the part that turns keys into movement: sprinting, jumping, climbing, friction, travel in air and fluids, aiStep.

namespace falcon {

float Player::movementSpeed() const {
    double value = attributes.movementSpeed;
    if (sprinting) value *= 1.0 + 0.30000001192092896;  // the sprinting modifier: +30% (ADD_MULTIPLIED_TOTAL)
    return static_cast<float>(value);
}

void Player::computeMoveVector() {
    const float forward = keys_.forward == keys_.backward ? 0.0f : keys_.forward ? 1.0f : -1.0f;
    const float strafe = keys_.left == keys_.right ? 0.0f : keys_.left ? 1.0f : -1.0f;
    const float length = std::sqrt(strafe * strafe + forward * forward);
    if (length < 1.0E-4f) {
        moveX_ = moveY_ = 0.0f;
    } else {
        moveX_ = strafe / length;
        moveY_ = forward / length;
    }
}

bool Player::isSprintingPossible(bool flyingNow) const {
    const bool enoughFood = foodLevel > 6 || canFly;
    const bool inShallowWater = inWater && !isUnderWater();
    return enoughFood && (flyingNow || !inShallowWater);
}

bool Player::canStartSprinting() const {
    const bool slowDueToItem = usingItem && !useItemCanSprint;
    return !sprinting && hasForwardImpulse() && isSprintingPossible(flying) && !slowDueToItem && (!isMovingSlowly() || isUnderWater());
}

bool Player::shouldStopRunSprinting() const {
    return !isSprintingPossible(flying) || !hasForwardImpulse() || (horizontalCollision && !minorHorizontalCollision_);
}

bool Player::shouldStopSwimSprinting() const {
    return !isSprintingPossible(true) || !inWater || (!hasForwardImpulse() && !onGround && !keys_.shift);
}

// LocalPlayer.applyInput + modifyInput: the keys' direction at 0.98, slowed when using an item or sneaking, and the
// unit circle stretched to the unit square (a diagonal walk is a little faster than a straight one).
void Player::applyInput() {
    xxa_ = 0.0f;
    zza_ = 0.0f;
    float sx = moveX_;
    float sy = moveY_;
    if (sx * sx + sy * sy != 0.0f) {
        sx *= 0.98f;
        sy *= 0.98f;
        if (usingItem) {
            sx *= useItemSpeedMultiplier;
            sy *= useItemSpeedMultiplier;
        }
        if (isMovingSlowly()) {
            const float slow = static_cast<float>(attributes.sneakingSpeed);
            sx *= slow;
            sy *= slow;
        }
        const float length = std::sqrt(sx * sx + sy * sy);
        if (length > 0.0f) {
            const float inv = 1.0f / length;
            const float dirX = sx * inv;
            const float dirY = sy * inv;
            const float lo = std::min(std::abs(dirX), std::abs(dirY));
            const float hi = std::max(std::abs(dirX), std::abs(dirY));
            const float ratio = hi == 0.0f ? 0.0f : lo / hi;
            const float toSquare = std::sqrt(1.0f + ratio * ratio);
            const float speed = std::min(length * toSquare, 1.0f);
            sx = dirX * speed;
            sy = dirY * speed;
        }
        xxa_ = sx;
        zza_ = sy;
    }
    jumping_ = keys_.jump;
}

float Player::blockJumpFactor() {
    const BlockRegistry& registry = blockRegistry();
    const float here = registry.state(blockAtFeet()).jumpFactor;
    const BlockPos below = getOnPos(0.500001f);
    const float under = registry.state(blockAt(below.x, below.y, below.z)).jumpFactor;
    return here == 1.0f ? under : here;
}

// LivingEntity.jumpFromGround.
void Player::jumpFromGround() {
    const float power = static_cast<float>(attributes.jumpStrength) * 1.0f * blockJumpFactor() + 0.0f;
    if (power <= 1.0E-5f) return;
    vy = std::max(static_cast<double>(power), vy);
    if (sprinting) {
        const float r = yaw * kDegToRad;
        vx += static_cast<double>(-mthSin(r)) * 0.2;
        vz += static_cast<double>(mthCos(r)) * 0.2;
    }
}

bool Player::onClimbable() {
    if (spectator || flying) return false;
    const BlockRegistry& registry = blockRegistry();
    const Block here = inBlockState();
    const std::uint32_t t = traitsOf(registry, here);
    if ((t & kClimbable) != 0) return true;
    if ((t & kTrapdoor) != 0 && registry.get(here, "open") == "true") {
        // A trapdoor over a ladder it faces the same way as can be climbed.
        const Block below = blockAt(mthFloor(x), mthFloor(y) - 1, mthFloor(z));
        return (traitsOf(registry, below) & kLadder) != 0 && registry.get(below, "facing") == registry.get(here, "facing");
    }
    return false;
}

Player::Vec3 Player::handleOnClimbable(Vec3 v) {
    if (!onClimbable()) return v;
    resetFallDistance();
    constexpr double kLimit = 0.15000000596046448;
    v.x = std::clamp(v.x, -kLimit, kLimit);
    v.z = std::clamp(v.z, -kLimit, kLimit);
    v.y = std::max(v.y, -kLimit);
    if (v.y < 0.0 && (traitsOf(blockRegistry(), inBlockState()) & kScaffolding) == 0 && keys_.shift && !flying) v.y = 0.0;  // hold on to the ladder
    return v;
}

// Entity.moveRelative + getInputVector: the keys' push, turned to face the way the player looks.
void Player::moveRelative(float speed, Vec3 input) {
    const double l2 = input.lengthSqr();
    if (l2 < 1.0E-7) return;
    Vec3 v = input;
    if (l2 > 1.0) {
        const double len = std::sqrt(l2);
        v = len < 1.0E-5 ? Vec3{} : Vec3{input.x / len, input.y / len, input.z / len};
    }
    const double s = static_cast<double>(speed);
    v = {v.x * s, v.y * s, v.z * s};
    const float sn = mthSin(yaw * kDegToRad);
    const float cs = mthCos(yaw * kDegToRad);
    vx += v.x * cs - v.z * sn;
    vy += v.y;
    vz += v.z * cs + v.x * sn;
}

// Player.getFlyingSpeed: the speed in the air (twice the creative flying speed when sprinting).
float Player::flyingSpeed() const {
    if (flying) return sprinting ? effectiveFlyingSpeedOfAbilities() * 2.0f : effectiveFlyingSpeedOfAbilities();
    return sprinting ? 0.025999999f : 0.02f;
}

float Player::frictionInfluencedSpeed(float friction) const {
    if (onGround) {
        return static_cast<double>(friction) > 0.6 ? movementSpeed() * (0.21600002f / (friction * friction * friction)) : movementSpeed();
    }
    return flyingSpeed();
}

Player::Vec3 Player::handleRelativeFrictionAndCalculateMovement(Vec3 input, float friction) {
    moveRelative(frictionInfluencedSpeed(friction), input);
    const Vec3 climbed = handleOnClimbable({vx, vy, vz});
    vx = climbed.x;
    vy = climbed.y;
    vz = climbed.z;
    moveSelf({vx, vy, vz});
    Vec3 v{vx, vy, vz};
    if ((horizontalCollision || jumping_) && onClimbable()) v.y = 0.2;
    return v;
}

// LivingEntity.travelInAir: walking, falling and creative flight.
void Player::travelInAir(Vec3 input) {
    const BlockRegistry& registry = blockRegistry();
    const BlockPos below = getOnPos(0.500001f);
    const float friction = onGround ? computeModifiedFriction(registry.state(blockAt(below.x, below.y, below.z)).friction, static_cast<float>(attributes.frictionModifier)) : 1.0f;
    const Vec3 v = handleRelativeFrictionAndCalculateMovement(input, friction);
    double ny = v.y;
    if (level_->isLoaded(below.x, below.z)) {
        ny -= attributes.gravity;
    } else if (y > static_cast<double>(kMinY)) {
        ny = -0.1;  // the ground has not arrived yet: sink slowly
    } else {
        ny = 0.0;
    }
    const float airDragModifier = static_cast<float>(attributes.airDragModifier);
    const float airDrag = computeModifiedFriction(0.91f, airDragModifier);
    const float horizontal = friction * airDrag;
    const float vertical = computeModifiedFriction(0.98f, airDragModifier);
    vx = v.x * static_cast<double>(horizontal);
    vy = ny * static_cast<double>(vertical);
    vz = v.z * static_cast<double>(horizontal);
}

Player::Vec3 Player::fluidFallingAdjustedMovement(double gravity, bool falling, Vec3 v) const {
    if (gravity != 0.0 && !sprinting) {
        double ny = 0.0;
        if (falling && std::abs(v.y - 0.005) >= 0.003 && std::abs(v.y - gravity / 16.0) < 0.003) {
            ny = -0.003;
        } else {
            ny = v.y - gravity / 16.0;
        }
        return {v.x, ny, v.z};
    }
    return v;
}

// A swimmer or wader pushing against a ledge pops up onto it.
void Player::jumpOutOfFluid(double yBefore) {
    if (horizontalCollision && isFree(vx, vy + 0.6000000238418579 - y + yBefore, vz)) vy = 0.30000001192092896;
}

void Player::travelInWater(Vec3 input, double gravity, bool falling, double yBefore) {
    float slow = sprinting ? 0.9f : 0.8f;
    float accel = 0.02f;
    float efficiency = static_cast<float>(attributes.waterMovementEfficiency);
    if (!onGround) efficiency *= 0.5f;
    if (efficiency > 0.0f) {
        slow += (0.54600006f - slow) * efficiency;
        accel += (movementSpeed() - accel) * efficiency;
    }
    moveRelative(accel, input);
    moveSelf({vx, vy, vz});
    Vec3 v{vx, vy, vz};
    if (horizontalCollision && onClimbable()) v.y = 0.2;
    v = {v.x * static_cast<double>(slow), v.y * 0.800000011920929, v.z * static_cast<double>(slow)};
    const Vec3 adjusted = fluidFallingAdjustedMovement(gravity, falling, v);
    vx = adjusted.x;
    vy = adjusted.y;
    vz = adjusted.z;
    jumpOutOfFluid(yBefore);
}

void Player::travelInLava(Vec3 input, double gravity, bool falling, double yBefore) {
    moveRelative(0.02f, input);
    moveSelf({vx, vy, vz});
    const double threshold = static_cast<double>(eyeHeight_) < 0.4 ? 0.0 : 0.4;
    if (lavaHeight_ <= threshold) {  // shallow lava
        const Vec3 adjusted = fluidFallingAdjustedMovement(gravity, falling, {vx * 0.5, vy * 0.800000011920929, vz * 0.5});
        vx = adjusted.x;
        vy = adjusted.y;
        vz = adjusted.z;
    } else {
        vx *= 0.5;
        vy *= 0.5;
        vz *= 0.5;
    }
    if (gravity != 0.0) vy += -gravity / 4.0;
    jumpOutOfFluid(yBefore);
}

void Player::travelInFluid(Vec3 input) {
    const bool falling = vy <= 0.0;
    const double yBefore = y;
    const double gravity = attributes.gravity;
    if (inWater) {
        travelInWater(input, gravity, falling, yBefore);
    } else {
        travelInLava(input, gravity, falling, yBefore);
    }
}

// Player.travel: swim steering by the look direction, and the creative flight wrapper (gravity is thrown away and the
// vertical velocity decays by 0.6 a tick); LivingEntity.travel picks the medium.
void Player::travel(Vec3 input) {
    if (isSwimming()) {
        const double lookY = static_cast<double>(-mthSin(pitch * kDegToRad));
        const double e = lookY < -0.2 ? 0.085 : 0.06;
        const BlockRegistry& registry = blockRegistry();
        const StateInfo& at = registry.state(blockAt(mthFloor(x), mthFloor(y + 1.0 - 0.1), mthFloor(z)));
        if (lookY <= 0.0 || jumping_ || at.has(kStateWater) || at.has(kStateLava)) vy += (lookY - vy) * e;
    }
    const auto livingTravel = [&](Vec3 in) {
        const bool inLiquid = inWater || isInLava();
        if (inLiquid && !flying) {
            travelInFluid(in);
        } else {
            travelInAir(in);
        }
    };
    if (flying) {
        const double savedY = vy;
        livingTravel(input);
        vy = savedY * 0.6;
    } else {
        livingTravel(input);
    }
}

void Player::updateBob() {
    const float target = onGround && !isSwimming() ? std::min(0.1f, static_cast<float>(std::sqrt(vx * vx + vz * vz))) : 0.0f;
    bobO_ = bob_;
    bob_ += (target - bob_) * 0.4f;
}

// LocalPlayer.aiStep and everything it calls, in order.
void Player::aiStep(const PlayerInput& input) {
    if (sprintTriggerTime_ > 0) --sprintTriggerTime_;
    const bool wasJumping = keys_.jump;
    const bool wasShift = keys_.shift;
    const bool hadForwardImpulse = hasForwardImpulse();
    // The crouch flag (what slows the walk) follows the shift key of the tick before.
    crouching_ = !flying && !isSwimming() && canFitIn(PlayerPose::Crouching) && (wasShift || !canFitIn(PlayerPose::Standing));
    keys_ = input;
    computeMoveVector();

    // Sprint: a double tap of forward within the window, or the sprint key; it stops when forward does.
    const bool slowDueToItem = usingItem && !useItemCanSprint;
    if (wasShift || slowDueToItem || keys_.backward) sprintTriggerTime_ = 0;
    if (canStartSprinting()) {
        if (!hadForwardImpulse) {
            if (sprintTriggerTime_ > 0) {
                sprinting = true;
            } else {
                sprintTriggerTime_ = sprintWindow;
            }
        }
        if (keys_.sprint) sprinting = true;
    }
    if (sprinting) {
        if (isSwimming()) {
            if (shouldStopSwimSprinting()) sprinting = false;
        } else if (shouldStopRunSprinting()) {
            sprinting = false;
        }
    }

    // Flying: spectators always, creative on a second tap of jump within the window.
    bool toggled = false;
    if (canFly) {
        if (spectator) {
            if (!flying) {
                flying = true;
                toggled = true;
            }
        } else if (!wasJumping && keys_.jump) {
            if (jumpTriggerTime_ == 0) {
                jumpTriggerTime_ = 7;
            } else if (!isSwimming()) {
                flying = !flying;
                if (flying && onGround) jumpFromGround();
                toggled = true;
                jumpTriggerTime_ = 0;
            }
        }
    }
    (void)toggled;
    if (inWater && keys_.shift && !flying) vy += -0.03999999910593033;  // sink
    if (flying) {
        const int dy = (keys_.shift ? -1 : 0) + (keys_.jump ? 1 : 0);
        if (dy != 0) vy += static_cast<double>(static_cast<float>(dy) * effectiveFlyingSpeedOfAbilities() * 3.0f);
    }

    updateBob();
    if (jumpTriggerTime_ > 0) --jumpTriggerTime_;
    if (flying) resetFallDistance();

    // LivingEntity.aiStep
    if (noJumpDelay_ > 0) --noJumpDelay_;
    double tx = vx;
    double ty = vy;
    double tz = vz;
    if (tx * tx + tz * tz < 9.0E-6) {  // a drift too small to see is stopped
        tx = 0.0;
        tz = 0.0;
    }
    if (std::abs(ty) < 0.003) ty = 0.0;
    vx = tx;
    vy = ty;
    vz = tz;
    applyInput();
    if (jumping_ && !flying) {
        const double fluidHeight = isInLava() ? lavaHeight_ : waterHeight_;
        const bool inWaterWithHeight = inWater && fluidHeight > 0.0;
        const double threshold = static_cast<double>(eyeHeight_) < 0.4 ? 0.0 : 0.4;
        if (inWaterWithHeight && (!onGround || fluidHeight > threshold)) {
            vy += 0.03999999910593033;
        } else if (isInLava() && (!onGround || !(lavaHeight_ <= threshold))) {
            vy += 0.03999999910593033;
        } else if ((onGround || (inWaterWithHeight && fluidHeight <= threshold)) && noJumpDelay_ == 0) {
            jumpFromGround();
            noJumpDelay_ = 10;
        }
    } else {
        noJumpDelay_ = 0;
    }
    travel({static_cast<double>(xxa_), 0.0, static_cast<double>(zza_)});
    applyEffectsFromBlocks();

    if (onGround && flying && !spectator) flying = false;  // landing ends creative flight
}

}  // namespace falcon
