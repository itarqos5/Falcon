#include "falcon/world/boat.hpp"

#include "player_internal.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {

namespace {

constexpr float kHeightF = 0.5625f;
constexpr float kWidthF = 1.375f;

// The water in a block: its fluid state is water (water itself, a waterlogged block, kelp ...) and how high it stands.
bool waterAt(const Level& level, int bx, int by, int bz, float& height, bool& source) {
    const BlockRegistry& registry = blockRegistry();
    const StateInfo& s = registry.state(level.block(bx, by, bz));
    if (!s.has(kStateWater)) return false;
    height = fluidHeightAt(level, registry, bx, by, bz, s, false);
    source = s.fluidLevel == 0;
    return true;
}

}  // namespace

Aabb BoatBody::boundingBox() const {
    const double half = static_cast<double>(kWidthF / 2.0f);
    return {x - half, y, z - half, x + half, y + static_cast<double>(kHeightF), z + half};
}

// AbstractBoat.isUnderwater: water over the whole of the boat's top (a hair above it). Still water gives UnderWater, any flowing water UnderFlowingWater.
bool BoatBody::underwaterStatus(const Level& level, BoatStatus& out) {
    const Aabb box = boundingBox();
    const double top = box.maxY + 0.001;
    bool any = false;
    for (int bx = mthFloor(box.minX); bx < mthCeil(box.maxX); ++bx) {
        for (int by = mthFloor(box.maxY); by < mthCeil(top); ++by) {
            for (int bz = mthFloor(box.minZ); bz < mthCeil(box.maxZ); ++bz) {
                float h = 0.0f;
                bool source = false;
                if (!waterAt(level, bx, by, bz, h, source)) continue;
                if (top < static_cast<double>(static_cast<float>(by) + h)) {
                    if (!source) {
                        out = BoatStatus::UnderFlowingWater;
                        return true;
                    }
                    any = true;
                }
            }
        }
    }
    if (any) out = BoatStatus::UnderWater;
    return any;
}

// AbstractBoat.checkInWater: whether the bottom is in water, and the highest surface it touches (the water level to float at).
bool BoatBody::checkInWater(const Level& level) {
    const Aabb box = boundingBox();
    bool inWater = false;
    waterLevel_ = -1.7976931348623157e308;
    for (int bx = mthFloor(box.minX); bx < mthCeil(box.maxX); ++bx) {
        for (int by = mthFloor(box.minY); by < mthCeil(box.minY + 0.001); ++by) {
            for (int bz = mthFloor(box.minZ); bz < mthCeil(box.maxZ); ++bz) {
                float h = 0.0f;
                bool source = false;
                if (!waterAt(level, bx, by, bz, h, source)) continue;
                const float surface = static_cast<float>(by) + h;
                waterLevel_ = std::max(static_cast<double>(surface), waterLevel_);
                inWater = inWater || box.minY < static_cast<double>(surface);
            }
        }
    }
    return inWater;
}

// AbstractBoat.getWaterLevelAbove: the surface of the water over the boat, found layer by layer upward from its top.
float BoatBody::waterLevelAbove(const Level& level) const {
    const Aabb box = boundingBox();
    const int y1 = mthCeil(box.maxY - lastYd_);
    for (int by = mthFloor(box.maxY); by < y1; ++by) {
        float f = 0.0f;
        bool full = false;
        for (int bx = mthFloor(box.minX); bx < mthCeil(box.maxX) && !full; ++bx) {
            for (int bz = mthFloor(box.minZ); bz < mthCeil(box.maxZ); ++bz) {
                float h = 0.0f;
                bool source = false;
                if (!waterAt(level, bx, by, bz, h, source)) continue;
                f = std::max(f, h);
                if (f >= 1.0f) {
                    full = true;
                    break;
                }
            }
        }
        if (!full && f < 1.0f) return static_cast<float>(by) + f;
    }
    return static_cast<float>(y1 + 1);
}

// AbstractBoat.getGroundFriction: the average friction of the blocks that are under the boat (a lily pad does not count), 0 when there are none.
float BoatBody::groundFriction(const Level& level) const {
    const Aabb box = boundingBox();
    const Aabb below = {box.minX, box.minY - 0.001, box.minZ, box.maxX, box.minY, box.maxZ};
    const int x0 = mthFloor(below.minX) - 1, x1 = mthCeil(below.maxX) + 1;
    const int y0 = mthFloor(below.minY) - 1, y1 = mthCeil(below.maxY) + 1;
    const int z0 = mthFloor(below.minZ) - 1, z1 = mthCeil(below.maxZ) + 1;
    const BlockRegistry& registry = blockRegistry();
    static const int lilyPad = [&registry] {
        const auto lily = registry.find("lily_pad");
        return lily ? static_cast<int>(registry.state(*lily).block) : -1;
    }();
    const CollisionShapes& shapes = collisionShapes();
    float sum = 0.0f;
    int count = 0;
    for (int bx = x0; bx < x1; ++bx) {
        for (int bz = z0; bz < z1; ++bz) {
            const int edges = (bx == x0 || bx == x1 - 1 ? 1 : 0) + (bz == z0 || bz == z1 - 1 ? 1 : 0);
            if (edges == 2) continue;  // (the corners of the walk)
            for (int by = y0; by < y1; ++by) {
                if (edges > 0 && (by == y0 || by == y1 - 1)) continue;
                const Block block = level.block(bx, by, bz);
                const StateInfo& s = registry.state(block);
                if (static_cast<int>(s.block) == lilyPad) continue;
                const VoxelShape shape = shapes.shape(block);
                if (shape.empty()) continue;
                const bool touches = shape.isUnitCube() ? below.intersects({static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz), static_cast<double>(bx) + 1.0,
                                                                            static_cast<double>(by) + 1.0, static_cast<double>(bz) + 1.0})
                                                         : shape.intersects(below, bx, by, bz);
                if (!touches) continue;
                sum += s.friction;
                ++count;
            }
        }
    }
    return count == 0 ? 0.0f : sum / static_cast<float>(count);
}

BoatStatus BoatBody::findStatus(const Level& level) {
    BoatStatus under = BoatStatus::InAir;
    if (underwaterStatus(level, under)) {
        waterLevel_ = boundingBox().maxY;
        return under;
    }
    if (checkInWater(level)) return BoatStatus::InWater;
    const float friction = groundFriction(level);
    if (friction > 0.0f) {
        landFriction_ = friction;
        return BoatStatus::OnLand;
    }
    return BoatStatus::InAir;
}

// AbstractBoat.floatBoat: gravity, the lift of the water and the drag of whatever the boat is in. A boat that has just come down onto water is put at its surface at once.
void BoatBody::floatBoat(const Level& level) {
    double gravity = -0.04;
    double buoyancy = 0.0;
    float invFriction = 0.05f;
    if (oldStatus_ == BoatStatus::InAir && status_ != BoatStatus::InAir && status_ != BoatStatus::OnLand) {
        waterLevel_ = y + static_cast<double>(kHeightF);
        const double surface = static_cast<double>(waterLevelAbove(level) - kHeightF) + 0.101;
        if (!aabbCollides(level, boundingBox().moved(0.0, surface - y, 0.0))) {
            y = surface;
            vy = 0.0;
            lastYd_ = 0.0;
        }
        status_ = BoatStatus::InWater;  // (whether or not there was room to come up to the surface)
        return;
    }
    switch (status_) {
        case BoatStatus::InWater:
            buoyancy = (waterLevel_ - y) / static_cast<double>(kHeightF);
            invFriction = 0.9f;
            break;
        case BoatStatus::UnderFlowingWater:
            gravity = -7.0E-4;
            invFriction = 0.9f;
            break;
        case BoatStatus::UnderWater:
            buoyancy = 0.009999999776482582;
            invFriction = 0.45f;
            break;
        case BoatStatus::InAir: invFriction = 0.9f; break;
        case BoatStatus::OnLand:
            invFriction = landFriction_;
            landFriction_ /= 2.0f;  // (a player drives: each tick on land it grips half as much again)
            break;
    }
    vx *= static_cast<double>(invFriction);
    vy += gravity;
    vz *= static_cast<double>(invFriction);
    deltaRotation *= invFriction;
    if (buoyancy > 0.0) vy = (vy + buoyancy * (0.04 / 0.65)) * 0.75;
}

// AbstractBoat.controlBoat: left and right turn it a degree a tick faster, forward pushes by 0.04, back by 0.005, and one side alone gives a little way.
void BoatBody::controlBoat(const BoatInput& in) {
    float push = 0.0f;
    if (in.left) deltaRotation -= 1.0f;
    if (in.right) deltaRotation += 1.0f;
    if (in.right != in.left && !in.up && !in.down) push += 0.005f;
    yaw += deltaRotation;
    if (in.up) push += 0.04f;
    if (in.down) push -= 0.005f;
    vx += static_cast<double>(mthSin(static_cast<double>(-yaw * kDegToRad)) * push);
    vz += static_cast<double>(mthCos(static_cast<double>(yaw * kDegToRad)) * push);
    paddleLeft = (in.right && !in.left) || in.up;
    paddleRight = (in.left && !in.right) || in.up;
}

// Entity.move(SELF) for a boat (it cannot step up): the move that its box is let make, what stops it, and the speed that is lost against a wall or the ground.
void BoatBody::move(const Level& level) {
    const std::array<double, 3> allowed = collideAabb(level, boundingBox(), vx, vy, vz);
    if (allowed[0] * allowed[0] + allowed[1] * allowed[1] + allowed[2] * allowed[2] > 1.0E-7) {
        x += allowed[0];
        y += allowed[1];
        z += allowed[2];
    }
    const bool xBlocked = !mthEqual(vx, allowed[0]), zBlocked = !mthEqual(vz, allowed[2]);
    horizontalCollision = xBlocked || zBlocked;
    const bool verticalCollision = vy != allowed[1];
    onGround = verticalCollision && vy < 0.0;
    lastYd_ = vy;  // (AbstractBoat.checkFallDamage keeps the speed it had going into the ground)
    if (horizontalCollision) {
        if (xBlocked) vx = 0.0;
        if (zBlocked) vz = 0.0;
    }
    if (verticalCollision) vy = 0.0;
}

void BoatBody::tick(const Level& level, const BoatInput& input) {
    oldStatus_ = status_;
    status_ = findStatus(level);
    floatBoat(level);
    controlBoat(input);
    move(level);
}

std::array<double, 3> BoatBody::seat(bool raft, int index, int riders) const {
    float along = 0.0f;
    if (riders > 1) along = index == 0 ? 0.2f : -0.6f;
    const float rideHeight = raft ? kHeightF * 0.8888889f : kHeightF / 3.0f;
    const double angle = static_cast<double>(-yaw * kDegToRad);
    const double s = static_cast<double>(mthSin(angle)), c = static_cast<double>(mthCos(angle));
    return {x + static_cast<double>(along) * s, y + static_cast<double>(rideHeight) - 0.6, z + static_cast<double>(along) * c};
}

}  // namespace falcon
