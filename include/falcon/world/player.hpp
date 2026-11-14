#pragma once

#include "falcon/world/chunk.hpp"
#include "falcon/world/collision_shapes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace falcon {

// The keys the player holds down during one tick (the game's `Input` record). Sneak and sprint are "is the key
// down", so a toggle mode is the caller's business: it keeps the latch and passes the result.
struct PlayerInput {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool shift = false;
    bool sprint = false;
};

enum class PlayerPose : std::uint8_t { Standing, Crouching, Swimming };

// The player's attributes (attributes.json, entities.minecraft:player); the doubles are the game's own, a float
// attribute such as the 0.1f movement speed shows as its double value.
struct PlayerAttributes {
    double movementSpeed = 0.10000000149011612;
    double sneakingSpeed = 0.3;
    double stepHeight = 0.6;
    double jumpStrength = 0.41999998688697815;
    double gravity = 0.08;
    double safeFallDistance = 3.0;
    double fallDamageMultiplier = 1.0;
    double airDragModifier = 1.0;
    double frictionModifier = 1.0;
    double movementEfficiency = 0.0;
    double waterMovementEfficiency = 0.0;
    double bounciness = 0.0;
};

// The camera's shake from walking (GameRenderer.bobView): a shift in view space, a roll and a tilt.
struct ViewBob {
    float x = 0.0f;
    float y = 0.0f;
    float rollDegrees = 0.0f;
    float pitchDegrees = 0.0f;
};

// The local player's body and its physics, the game's own: a fixed 20 ticks a second (see tick()), with the same
// order of steps, the same float / double mix, the same collision and step-up rules. Distances are in blocks and
// velocities in blocks per tick (the game's deltaMovement).
class Player {
public:
    static constexpr double kWidth = 0.6;
    static constexpr double kHeight = 1.8;
    static constexpr double kEyeHeight = 1.62;
    static constexpr double kSneakEyeHeight = 1.27;
    static constexpr int kTicksPerSecond = 20;

    // Feet position (the middle of the bottom of the box) and velocity per tick.
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    // Where the player was at the start of the last tick, for drawing between ticks.
    double xo = 0.0;
    double yo = 0.0;
    double zo = 0.0;
    float yaw = 0.0f;    // degrees, 0 looks south (+Z), 90 west; not wrapped
    float pitch = 0.0f;  // degrees, positive looks down
    float yawO = 0.0f;
    float pitchO = 0.0f;

    bool onGround = false;
    bool flying = false;     // creative flight (abilities.flying)
    bool canFly = false;     // abilities.mayfly: creative and spectator
    bool spectator = false;  // no collision, always flying
    bool inWater = false;    // the body touches water
    bool sneaking = false;   // the pose is crouching
    bool sprinting = false;
    bool horizontalCollision = false;
    bool verticalCollision = false;
    double fallDistance = 0.0;
    int foodLevel = 20;      // below 7 the player cannot start to sprint (survival will drive this later)
    bool usingItem = false;  // holding a use-key item: input slows to useItemSpeedMultiplier and sprint cannot start
    float useItemSpeedMultiplier = 0.2f;
    bool useItemCanSprint = false;
    int sprintWindow = 7;    // ticks within which a second tap of forward starts a sprint (0 turns it off)
    PlayerAttributes attributes;

    // The fall damage the last landing would have dealt (0: none); takeFallDamage() reads and clears it.
    int lastFallDamage = 0;

    // ---- the tick ----
    // One game tick (1/20 s) with the keys held right now: the same steps in the same order as the game's
    // LocalPlayer.tick: remember the old position, update the fluids, read the keys (sprint, crouch, flight), jump,
    // travel and collide, then pick the pose.
    void tick(const PlayerInput& input, const Level& level);
    // Mouse look: degrees, the game's Entity.turn (0.15 degrees per unit); the old rotation moves along.
    void turn(double deltaYaw, double deltaPitch);
    // Puts the player somewhere without any in-between frames.
    void setPosition(double newX, double newY, double newZ);
    // Creative: turns flying on or off (does nothing without canFly).
    void toggleFlying();
    [[nodiscard]] int takeFallDamage() {
        const int d = lastFallDamage;
        lastFallDamage = 0;
        return d;
    }

    // ---- what the body is ----
    [[nodiscard]] PlayerPose pose() const { return pose_; }
    [[nodiscard]] float eyeHeight() const { return eyeHeight_; }
    [[nodiscard]] double eyeY() const { return y + static_cast<double>(eyeHeight_); }
    [[nodiscard]] std::array<double, 3> eye() const { return {x, eyeY(), z}; }
    [[nodiscard]] Aabb boundingBox() const;
    [[nodiscard]] bool isSwimming() const { return !flying && !spectator && swimming_; }
    [[nodiscard]] bool isMovingSlowly() const { return crouching_ || (pose_ == PlayerPose::Swimming && !inWater); }
    [[nodiscard]] bool isUnderWater() const { return wasUnderwater_; }  // the eyes were in water
    [[nodiscard]] bool isInLava() const { return !firstTick_ && lavaHeight_ > 0.0; }
    [[nodiscard]] double waterHeight() const { return waterHeight_; }
    [[nodiscard]] double lavaHeight() const { return lavaHeight_; }
    [[nodiscard]] float movementSpeed() const;  // the movement_speed attribute (with the sprint bonus) as a float

    // ---- between ticks (partial: how far into the next tick the frame is, 0..1) ----
    [[nodiscard]] std::array<double, 3> position(double partial) const;
    // Entity.getEyePosition: for aiming.
    [[nodiscard]] std::array<double, 3> eyePosition(double partial) const;
    // Where the camera is: the interpolated position plus the eye height, which eases when crouching.
    [[nodiscard]] std::array<double, 3> cameraPosition(double partial) const;
    [[nodiscard]] ViewBob viewBob(double partial) const;

    // Whether the standing box (feet at x, y, z) overlaps a collision shape. Chunks that are not loaded count as
    // solid, so nobody falls through the world while it is still loading.
    [[nodiscard]] static bool collides(const Level& level, double x, double y, double z);

private:
    struct Vec3 {
        double x = 0.0, y = 0.0, z = 0.0;
        [[nodiscard]] double get(int axis) const { return axis == 0 ? x : axis == 1 ? y : z; }
        void set(int axis, double v) { (axis == 0 ? x : axis == 1 ? y : z) = v; }
        [[nodiscard]] double lengthSqr() const { return x * x + y * y + z * z; }
        [[nodiscard]] double horizontalDistanceSqr() const { return x * x + z * z; }
    };
    struct BlockPos {
        int x = 0, y = 0, z = 0;
    };
    struct Collider {
        VoxelShape shape;
        int x = 0, y = 0, z = 0;
    };
    struct FluidTrack {
        double height = 0.0;
        bool eyesInside = false;
    };

    // --- Entity ---
    void baseTick();
    void updateFluidInteraction();
    void applyWaterCurrent();
    void updateSwimming();
    void moveSelf(Vec3 delta);
    [[nodiscard]] Vec3 maybeBackOffFromEdge(Vec3 delta) const;
    [[nodiscard]] bool canFallAtLeast(double dx, double dz, double h) const;
    [[nodiscard]] Vec3 collide(Vec3 delta) const;
    [[nodiscard]] Vec3 collideWithShapes(Vec3 v, const Aabb& box, const std::vector<Collider>& shapes) const;
    [[nodiscard]] bool noCollision(const Aabb& box) const;
    [[nodiscard]] bool isFree(double dx, double dy, double dz) const;
    void collectColliders(const Aabb& box, std::vector<Collider>& out) const;
    void checkSupportingBlock(bool ground, const Vec3* movement);
    [[nodiscard]] std::optional<BlockPos> findSupportingBlock(const Aabb& probe) const;
    [[nodiscard]] BlockPos getOnPos(float offset) const;
    [[nodiscard]] Block blockAt(int bx, int by, int bz) const;
    [[nodiscard]] Block blockAtFeet() const;
    [[nodiscard]] Block inBlockState();
    void checkFallDamage(double movedY, Block landedOn);
    void restituteMovementAfterCollisions(Block landedOn, bool xCol, bool zCol, const Vec3& actual);
    void resetFallDistance() { fallDistance = 0.0; }
    [[nodiscard]] float blockSpeedFactor();
    [[nodiscard]] bool isHorizontalCollisionMinor(const Vec3& actual) const;
    void setPos(double nx, double ny, double nz) {
        x = nx;
        y = ny;
        z = nz;
    }
    void setPose(PlayerPose p);
    void moveRelative(float speed, Vec3 input);
    void applyEffectsFromBlocks();

    // --- LocalPlayer / LivingEntity / Player ---
    void aiStep(const PlayerInput& input);
    void updatePlayerPose();
    [[nodiscard]] bool canFitIn(PlayerPose p) const;
    void computeMoveVector();
    [[nodiscard]] bool hasForwardImpulse() const { return moveY_ > 1.0e-5f; }
    [[nodiscard]] bool isSprintingPossible(bool flyingNow) const;
    [[nodiscard]] bool canStartSprinting() const;
    [[nodiscard]] bool shouldStopRunSprinting() const;
    [[nodiscard]] bool shouldStopSwimSprinting() const;
    void applyInput();
    void jumpFromGround();
    [[nodiscard]] float blockJumpFactor();
    [[nodiscard]] bool onClimbable();
    [[nodiscard]] Vec3 handleOnClimbable(Vec3 v);
    void travel(Vec3 input);
    void travelInAir(Vec3 input);
    void travelInFluid(Vec3 input);
    void travelInWater(Vec3 input, double gravity, bool falling, double yBefore);
    void travelInLava(Vec3 input, double gravity, bool falling, double yBefore);
    [[nodiscard]] Vec3 fluidFallingAdjustedMovement(double gravity, bool falling, Vec3 v) const;
    void jumpOutOfFluid(double yBefore);
    [[nodiscard]] Vec3 handleRelativeFrictionAndCalculateMovement(Vec3 input, float friction);
    [[nodiscard]] float frictionInfluencedSpeed(float friction) const;
    [[nodiscard]] float flyingSpeed() const;
    [[nodiscard]] float effectiveFlyingSpeedOfAbilities() const { return 0.05f; }
    void updateBob();

    const Level* level_ = nullptr;  // only set during tick()
    PlayerPose pose_ = PlayerPose::Standing;
    float eyeHeight_ = 1.62f;
    bool firstTick_ = true;
    bool swimming_ = false;
    bool crouching_ = false;  // LocalPlayer.crouching: the slowdown flag, one tick behind the shift key
    bool wasUnderwater_ = false;
    double waterHeight_ = 0.0;
    double lavaHeight_ = 0.0;
    // The water's current at the body, worked out by updateFluidInteraction: the sum of the flow of every water block touched (the
    // ones the water barely covers count for less) and how many there were; baseTick pushes the player with their average.
    std::array<double, 3> currentSum_{};
    int currentCount_ = 0;
    bool eyesInWater_ = false;
    bool eyesInLava_ = false;

    PlayerInput keys_;
    float moveX_ = 0.0f;  // KeyboardInput.moveVector: x strafe (left is +), y forward
    float moveY_ = 0.0f;
    float xxa_ = 0.0f;
    float zza_ = 0.0f;
    bool jumping_ = false;
    int noJumpDelay_ = 0;
    int jumpTriggerTime_ = 0;
    int sprintTriggerTime_ = 0;

    bool verticalCollisionBelow_ = false;
    bool minorHorizontalCollision_ = false;
    std::optional<BlockPos> supporting_;
    bool onGroundNoBlocks_ = false;
    Vec3 stuck_;  // makeStuckInBlock's multiplier for the next move
    bool inBlockValid_ = false;
    Block inBlock_ = Block::Air;

    float walkDist_ = 0.0f;
    float walkDistO_ = 0.0f;
    float bob_ = 0.0f;
    float bobO_ = 0.0f;
    float cameraEyeHeight_ = 1.62f;
    float cameraEyeHeightOld_ = 1.62f;
};

// The move a box may make through the world's collision shapes (Entity.collide without the step up), for what is not the player: dropped
// items. Chunks that are not loaded are solid. Returns the allowed (dx, dy, dz).
[[nodiscard]] std::array<double, 3> collideAabb(const Level& level, const Aabb& box, double dx, double dy, double dz);
// Whether a box overlaps any collision shape (an unloaded chunk counts as solid).
[[nodiscard]] bool aabbCollides(const Level& level, const Aabb& box);

}  // namespace falcon
