#include "falcon/world/player.hpp"

#include "falcon/world/fluids.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// The local player's physics, ported from the game's own tick: LocalPlayer.aiStep (keys, sprint, flight),
// LivingEntity.aiStep (jump, travel), Entity.move / collide (blocks, step-up), the fluids and the pose. Float and
// double are used exactly where the game uses them: positions, velocities and fall distance are doubles; input, speed,
// friction, drag, rotation and the attribute casts are floats. Section numbers in comments refer to the research
// notes this was written from.

namespace falcon {
namespace {

constexpr float kDegToRad = 0.017453292f;

int mthFloor(double v) {
    const int i = static_cast<int>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}
int mthCeil(double v) {
    const int i = static_cast<int>(v);
    return v > static_cast<double>(i) ? i + 1 : i;
}
bool mthEqual(double a, double b) { return std::abs(b - a) < 9.999999747378752E-6; }
double signum(double v) { return v > 0.0 ? 1.0 : v < 0.0 ? -1.0 : 0.0; }

// Mth.sin / Mth.cos: a 65536-entry float table, indexed by the angle in 1/10430.378 radians.
struct SinTable {
    std::array<float, 65536> value;
    SinTable() {
        for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<float>(std::sin(static_cast<double>(i) / 10430.378350470453));
    }
};
const SinTable& sinTable() {
    static const SinTable table;
    return table;
}
float mthSin(double angle) {
    return sinTable().value[static_cast<std::uint32_t>(static_cast<std::int64_t>(angle * 10430.378350470453)) & 65535u];
}
float mthCos(double angle) {
    return sinTable().value[static_cast<std::uint32_t>(static_cast<std::int64_t>(angle * 10430.378350470453 + 16384.0)) & 65535u];
}

// LivingEntity.computeModifiedFriction.
float computeModifiedFriction(float friction, float modifier) { return std::clamp(1.0f - (1.0f - friction) * modifier, 0.0f, 1.0f); }

// A block's special behaviour in movement, found by name once per registry (the game uses block tags and classes).
enum Trait : std::uint32_t {
    kClimbable = 1u << 0,
    kScaffolding = 1u << 1,
    kTrapdoor = 1u << 2,
    kLadder = 1u << 3,
    kCobweb = 1u << 4,
    kSweetBerry = 1u << 5,
    kPowderSnow = 1u << 6,
    kBubbleColumn = 1u << 7,
    kWaterBlock = 1u << 8,
    kHay = 1u << 9,
    kHoney = 1u << 10,
    kSlime = 1u << 11,
    kFence = 1u << 12,
    kWall = 1u << 13,
    kFenceGate = 1u << 14,
};

bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::uint32_t traitsOf(const BlockRegistry& registry, Block b) {
    struct Cache {
        const BlockRegistry* registry = nullptr;
        std::size_t blocks = 0;
        std::vector<std::uint32_t> flags;
    };
    static Cache cache;
    if (cache.registry != &registry || cache.blocks != registry.blocks().size()) {
        cache.registry = &registry;
        cache.blocks = registry.blocks().size();
        cache.flags.assign(cache.blocks, 0);
        for (std::size_t i = 0; i < cache.blocks; ++i) {
            const std::string& n = registry.blocks()[i].name;
            std::uint32_t f = 0;
            if (n == "ladder" || n == "vine" || n == "scaffolding" || n == "weeping_vines" || n == "weeping_vines_plant" || n == "twisting_vines" ||
                n == "twisting_vines_plant" || n == "cave_vines" || n == "cave_vines_plant")
                f |= kClimbable;
            if (n == "scaffolding") f |= kScaffolding;
            if (endsWith(n, "_trapdoor")) f |= kTrapdoor;
            if (n == "ladder") f |= kLadder;
            if (n == "cobweb") f |= kCobweb;
            if (n == "sweet_berry_bush") f |= kSweetBerry;
            if (n == "powder_snow") f |= kPowderSnow;
            if (n == "bubble_column") f |= kBubbleColumn;
            if (n == "water") f |= kWaterBlock;
            if (n == "hay_block") f |= kHay;
            if (n == "honey_block") f |= kHoney;
            if (n == "slime_block") f |= kSlime;
            if (endsWith(n, "_fence")) f |= kFence;
            if (endsWith(n, "_wall")) f |= kWall;
            if (endsWith(n, "_fence_gate")) f |= kFenceGate;
            cache.flags[i] = f;
        }
    }
    return cache.flags[registry.state(b).block];
}

struct Dims {
    float width, height, eye;
};
Dims dimensionsOf(PlayerPose pose) {
    switch (pose) {
        case PlayerPose::Crouching: return {0.6f, 1.5f, 1.27f};
        case PlayerPose::Swimming: return {0.6f, 0.6f, 0.4f};
        case PlayerPose::Standing: break;
    }
    return {0.6f, 1.8f, 1.62f};
}

// EntityDimensions.makeBoundingBox: half the (float) width either side, the (float) height up.
Aabb makeBox(double px, double py, double pz, const Dims& d) {
    const double half = static_cast<double>(d.width / 2.0f);
    return {px - half, py, pz - half, px + half, py + static_cast<double>(d.height), pz + half};
}

// Every block shape the box may touch (BlockCollisions): the box grown by a block on every side, but the outer shell
// only counts for shapes that stick out of their block (fences). A chunk that is not loaded is a solid cube, so
// nobody falls through the world while it is arriving. `fn` returns true to stop; the result says whether it did.
template <typename Collider, typename Fn>
bool forEachCollider(const Level& level, const Aabb& box, Fn&& fn) {
    const CollisionShapes& shapes = collisionShapes();
    const int x0 = mthFloor(box.minX - 1.0E-7) - 1;
    const int y0 = mthFloor(box.minY - 1.0E-7) - 1;
    const int z0 = mthFloor(box.minZ - 1.0E-7) - 1;
    const int x1 = mthFloor(box.maxX + 1.0E-7) + 1;
    const int y1 = mthFloor(box.maxY + 1.0E-7) + 1;
    const int z1 = mthFloor(box.maxZ + 1.0E-7) + 1;
    for (int bz = z0; bz <= z1; ++bz) {
        for (int by = y0; by <= y1; ++by) {
            for (int bx = x0; bx <= x1; ++bx) {
                const int type = (bx == x0 || bx == x1 ? 1 : 0) + (by == y0 || by == y1 ? 1 : 0) + (bz == z0 || bz == z1 ? 1 : 0);
                if (type == 3) continue;
                VoxelShape shape = shapes.shape(level.block(bx, by, bz));
                if (shape.empty()) {
                    if (type != 0 || by < kMinY || by >= kMaxY || level.isLoaded(bx, bz)) continue;
                    shape = shapes.unitCube();
                } else if (type == 2 || (type == 1 && !shape.isLarge())) {
                    continue;
                }
                if (shape.isUnitCube()) {
                    if (!box.intersects({static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz), static_cast<double>(bx) + 1.0,
                                         static_cast<double>(by) + 1.0, static_cast<double>(bz) + 1.0}))
                        continue;
                } else if (!shape.intersects(box, bx, by, bz)) {
                    continue;
                }
                if (fn(Collider{shape, bx, by, bz})) return true;
            }
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Blocks and the world

Block Player::blockAt(int bx, int by, int bz) const { return level_->block(bx, by, bz); }

Block Player::blockAtFeet() const { return blockAt(mthFloor(x), mthFloor(y), mthFloor(z)); }

// Entity.getInBlockState: the block at the feet, looked up once per tick.
Block Player::inBlockState() {
    if (!inBlockValid_) {
        inBlock_ = blockAtFeet();
        inBlockValid_ = true;
    }
    return inBlock_;
}

Aabb Player::boundingBox() const { return makeBox(x, y, z, dimensionsOf(pose_)); }

void Player::collectColliders(const Aabb& box, std::vector<Collider>& out) const {
    forEachCollider<Collider>(*level_, box, [&](const Collider& c) {
        out.push_back(c);
        return false;
    });
}

bool Player::noCollision(const Aabb& box) const {
    return !forEachCollider<Collider>(*level_, box, [](const Collider&) { return true; });
}

bool Player::collides(const Level& level, double px, double py, double pz) {
    return forEachCollider<Collider>(level, makeBox(px, py, pz, dimensionsOf(PlayerPose::Standing)).deflated(1.0E-7), [](const Collider&) { return true; });
}

bool Player::isFree(double dx, double dy, double dz) const {
    const Aabb box = boundingBox().moved(dx, dy, dz);
    if (!noCollision(box)) return false;
    // Level.containsAnyLiquid
    const BlockRegistry& registry = blockRegistry();
    for (int bz = mthFloor(box.minZ); bz < mthCeil(box.maxZ); ++bz) {
        for (int by = mthFloor(box.minY); by < mthCeil(box.maxY); ++by) {
            for (int bx = mthFloor(box.minX); bx < mthCeil(box.maxX); ++bx) {
                const StateInfo& s = registry.state(blockAt(bx, by, bz));
                if (s.has(kStateWater) || s.has(kStateLava)) return false;
            }
        }
    }
    return true;
}

bool Player::canFitIn(PlayerPose p) const { return noCollision(makeBox(x, y, z, dimensionsOf(p)).deflated(1.0E-7)); }

// Level.findSupportingBlock: of the blocks the probe touches, the one whose middle is closest to the player; a tie goes
// to the one with the larger (y, z, x).
std::optional<Player::BlockPos> Player::findSupportingBlock(const Aabb& probe) const {
    std::optional<BlockPos> best;
    double bestDistance = 1.7976931348623157e308;
    forEachCollider<Collider>(*level_, probe, [&](const Collider& c) {
        const double dx = static_cast<double>(c.x) + 0.5 - x;
        const double dy = static_cast<double>(c.y) + 0.5 - y;
        const double dz = static_cast<double>(c.z) + 0.5 - z;
        const double d = dx * dx + dy * dy + dz * dz;
        bool better = d < bestDistance;
        if (!better && d == bestDistance && best) {
            better = best->y != c.y ? best->y < c.y : best->z != c.z ? best->z < c.z : best->x < c.x;
        }
        if (better) {
            best = BlockPos{c.x, c.y, c.z};
            bestDistance = d;
        }
        return false;
    });
    return best;
}

void Player::checkSupportingBlock(bool ground, const Vec3* movement) {
    if (ground) {
        const Aabb box = boundingBox();
        const Aabb probe{box.minX, box.minY - 1.0E-6, box.minZ, box.maxX, box.minY, box.maxZ};
        auto found = findSupportingBlock(probe);
        if (found || onGroundNoBlocks_) {
            supporting_ = found;
        } else if (movement != nullptr) {
            found = findSupportingBlock(probe.moved(-movement->x, 0.0, -movement->z));
            supporting_ = found;
        }
        onGroundNoBlocks_ = !found.has_value();
    } else {
        onGroundNoBlocks_ = false;
        supporting_.reset();
    }
}

// Entity.getOnPos: the block under the feet that counts for friction and the like.
Player::BlockPos Player::getOnPos(float offset) const {
    if (supporting_) {
        const BlockPos p = *supporting_;
        if (offset > 1.0E-5f) {
            const std::uint32_t t = traitsOf(blockRegistry(), blockAt(p.x, p.y, p.z));
            if ((static_cast<double>(offset) <= 0.5 && (t & kFence) != 0) || (t & kWall) != 0 || (t & kFenceGate) != 0) return p;
            return {p.x, mthFloor(y - static_cast<double>(offset)), p.z};
        }
        return p;
    }
    return {mthFloor(x), mthFloor(y - static_cast<double>(offset)), mthFloor(z)};
}

// ---------------------------------------------------------------------------------------------------------------
// Collision

Player::Vec3 Player::collideWithShapes(Vec3 v, const Aabb& box, const std::vector<Collider>& shapes) const {
    if (shapes.empty()) return v;
    Vec3 allowed;
    const bool zFirst = std::abs(v.x) < std::abs(v.z);
    const int order[3] = {1, zFirst ? 2 : 0, zFirst ? 0 : 2};  // Direction.axisStepOrder: Y first, then the longer of X and Z
    for (const int axis : order) {
        double d = v.get(axis);
        if (d == 0.0) continue;
        const Aabb moved = box.moved(allowed.x, allowed.y, allowed.z);
        for (const Collider& c : shapes) {
            if (std::abs(d) < 1.0E-7) {
                d = 0.0;
                break;
            }
            d = c.shape.collide(axis, moved, d, c.x, c.y, c.z);
        }
        allowed.set(axis, d);
    }
    return allowed;
}

// Entity.collide: the move the blocks allow, and a step up onto a ledge of up to the step height.
Player::Vec3 Player::collide(Vec3 delta) const {
    const Aabb box = boundingBox();
    std::vector<Collider> shapes;
    Vec3 r = delta;
    if (delta.lengthSqr() != 0.0) {
        collectColliders(box.expandedTowards(delta.x, delta.y, delta.z), shapes);
        r = collideWithShapes(delta, box, shapes);
    }
    const bool xBlocked = delta.x != r.x;
    const bool yBlocked = delta.y != r.y;
    const bool zBlocked = delta.z != r.z;
    const bool landing = yBlocked && delta.y < 0.0;
    const float maxUp = static_cast<float>(attributes.stepHeight);
    if (maxUp > 0.0f && (landing || onGround) && (xBlocked || zBlocked)) {
        const Aabb base = landing ? box.moved(0.0, r.y, 0.0) : box;
        Aabb sweep = base.expandedTowards(delta.x, static_cast<double>(maxUp), delta.z);
        if (!landing) sweep = sweep.expandedTowards(0.0, -9.999999747378752E-6, 0.0);
        std::vector<Collider> around;
        collectColliders(sweep, around);
        // collectCandidateStepUpHeights: the heights of the shapes' grid lines above the feet, up to the step height.
        const float ry = static_cast<float>(r.y);
        std::vector<float> heights;
        for (const Collider& c : around) {
            for (int i = 0; i <= c.shape.cells(1); ++i) {
                const float h = static_cast<float>((c.shape.coord(1, i) + static_cast<double>(c.y)) - base.minY);
                if (h < 0.0f) continue;
                if (h == ry) continue;
                if (h > maxUp) break;
                heights.push_back(h);
            }
        }
        std::sort(heights.begin(), heights.end());
        heights.erase(std::unique(heights.begin(), heights.end()), heights.end());
        for (const float h : heights) {
            const Vec3 stepped = collideWithShapes({delta.x, static_cast<double>(h), delta.z}, base, around);
            if (stepped.horizontalDistanceSqr() > r.horizontalDistanceSqr()) {
                const double dropBack = box.minY - base.minY;
                return {stepped.x, stepped.y - dropBack, stepped.z};
            }
        }
    }
    return r;
}

bool Player::canFallAtLeast(double dx, double dz, double h) const {
    const Aabb box = boundingBox();
    return noCollision({box.minX + 1.0E-7 + dx, box.minY - h - 1.0E-7, box.minZ + 1.0E-7 + dz, box.maxX - 1.0E-7 + dx, box.minY, box.maxZ - 1.0E-7 + dz});
}

// Player.maybeBackOffFromEdge: sneaking never walks off a ledge higher than the step height.
Player::Vec3 Player::maybeBackOffFromEdge(Vec3 delta) const {
    const float step = static_cast<float>(attributes.stepHeight);
    if (flying || delta.y > 0.0 || !keys_.shift) return delta;
    const bool aboveGround = onGround || (fallDistance < static_cast<double>(step) && !canFallAtLeast(0.0, 0.0, static_cast<double>(step) - fallDistance));
    if (!aboveGround) return delta;
    double dx = delta.x;
    double dz = delta.z;
    const double sx = signum(dx) * 0.05;
    const double sz = signum(dz) * 0.05;
    const double h = static_cast<double>(step);
    while (dx != 0.0 && canFallAtLeast(dx, 0.0, h)) {
        if (std::abs(dx) <= 0.05) {
            dx = 0.0;
            break;
        }
        dx -= sx;
    }
    while (dz != 0.0 && canFallAtLeast(0.0, dz, h)) {
        if (std::abs(dz) <= 0.05) {
            dz = 0.0;
            break;
        }
        dz -= sz;
    }
    while (dx != 0.0 && dz != 0.0 && canFallAtLeast(dx, dz, h)) {
        dx = std::abs(dx) <= 0.05 ? 0.0 : dx - sx;
        dz = std::abs(dz) <= 0.05 ? 0.0 : dz - sz;
    }
    return {dx, delta.y, dz};
}

// LocalPlayer.isHorizontalCollisionMinor: a bump that still leaves the player moving (within 8 degrees) along the
// direction of the keys does not stop a sprint.
bool Player::isHorizontalCollisionMinor(const Vec3& actual) const {
    const float s = mthSin(yaw * kDegToRad);
    const float c = mthCos(yaw * kDegToRad);
    const float inX = xxa_ * c - zza_ * s;
    const float inZ = zza_ * c + xxa_ * s;
    const float inLen2 = inX * inX + inZ * inZ;
    const float ax = static_cast<float>(actual.x);
    const float az = static_cast<float>(actual.z);
    const float aLen2 = ax * ax + az * az;
    if (inLen2 < 1.0E-5f || aLen2 < 1.0E-5f) return false;
    const double angle = std::acos(static_cast<double>((inX * ax + inZ * az) / std::sqrt(inLen2 * aLen2)));
    return angle < 0.13962633907794952;
}

// Entity.getBlockSpeedFactor (soul sand, honey) as the player sees it.
float Player::blockSpeedFactor() {
    if (flying) return 1.0f;
    const BlockRegistry& registry = blockRegistry();
    const Block here = blockAtFeet();
    float f = registry.state(here).speedFactor;
    const bool inWaterBlock = (traitsOf(registry, here) & (kWaterBlock | kBubbleColumn)) != 0;
    if (!inWaterBlock && static_cast<double>(f) == 1.0) {
        const BlockPos below = getOnPos(0.500001f);
        f = registry.state(blockAt(below.x, below.y, below.z)).speedFactor;
    }
    const float efficiency = static_cast<float>(attributes.movementEfficiency);
    return f + efficiency * (1.0f - f);
}

// Entity.move(SELF, delta): collide, set the position and the flags, land, and slow down on soul sand.
void Player::moveSelf(Vec3 delta) {
    const double x0 = x;
    const double z0 = z;
    if (spectator) {  // noPhysics: no collision at all
        setPos(x + delta.x, y + delta.y, z + delta.z);
        horizontalCollision = verticalCollision = verticalCollisionBelow_ = minorHorizontalCollision_ = false;
    } else {
        if (stuck_.lengthSqr() > 1.0E-7) {  // cobweb, berries: the next move is squeezed and the velocity is lost
            delta = {delta.x * stuck_.x, delta.y * stuck_.y, delta.z * stuck_.z};
            stuck_ = {};
            vx = vy = vz = 0.0;
        }
        delta = maybeBackOffFromEdge(delta);
        const Vec3 actual = collide(delta);
        const double actualLen2 = actual.lengthSqr();
        if (actualLen2 > 1.0E-7 || delta.lengthSqr() - actualLen2 < 1.0E-7) setPos(x + actual.x, y + actual.y, z + actual.z);
        const bool xCol = !mthEqual(delta.x, actual.x);
        const bool zCol = !mthEqual(delta.z, actual.z);
        horizontalCollision = xCol || zCol;
        const bool yMoved = std::abs(delta.y) > 0.0;
        verticalCollision = delta.y != actual.y;
        verticalCollisionBelow_ = verticalCollision && delta.y < 0.0;
        onGround = verticalCollisionBelow_;
        checkSupportingBlock(onGround, horizontalCollision ? &actual : nullptr);
        minorHorizontalCollision_ = horizontalCollision ? isHorizontalCollisionMinor(actual) : false;
        const BlockPos onPos = getOnPos(0.2f);
        const Block landedOn = blockAt(onPos.x, onPos.y, onPos.z);
        checkFallDamage(actual.y, landedOn);
        if ((yMoved && verticalCollision) || horizontalCollision) restituteMovementAfterCollisions(landedOn, xCol, zCol, actual);
        if (onGround && (traitsOf(blockRegistry(), landedOn) & kSlime) != 0) {  // SlimeBlock.stepOn: a slow walk on slime
            const double d = std::abs(vy);
            if (d < 0.1 && !keys_.shift) {
                const double e = 0.4 + d * 0.2;
                vx *= e;
                vz *= e;
            }
        }
        const float f = blockSpeedFactor();
        vx *= static_cast<double>(f);
        vz *= static_cast<double>(f);
    }
    // LocalPlayer.move: the distance walked, for the view bob.
    const float dxf = static_cast<float>(x - x0);
    const float dzf = static_cast<float>(z - z0);
    walkDist_ += std::sqrt(dxf * dxf + dzf * dzf) * 0.6f;
}

// ---------------------------------------------------------------------------------------------------------------
// Falling and landing

void Player::checkFallDamage(double movedY, Block landedOn) {
    if (!inWater) updateFluidInteraction();  // landing in water resets the fall before it can hurt
    if (!inWater && movedY < 0.0) fallDistance -= static_cast<double>(static_cast<float>(movedY));
    if (!onGround) return;
    if (fallDistance > 0.0) {
        // Block.fallOn and its overrides: beds soften by their reduction, hay and honey take 20%, slime none (unless sneaking).
        const BlockRegistry& registry = blockRegistry();
        const std::uint32_t t = traitsOf(registry, landedOn);
        double distance = fallDistance;
        float multiplier = 1.0f;
        bool hurts = true;
        if ((t & kPowderSnow) != 0) {
            hurts = false;
        } else if ((t & (kHay | kHoney)) != 0) {
            multiplier = 0.2f;
        } else if ((t & kSlime) != 0 && !keys_.shift) {
            multiplier = 0.0f;
        } else {
            distance = fallDistance * static_cast<double>(1.0f - collisionShapes().fallDistanceReduction(landedOn));
        }
        // Player.causeFallDamage: creative and spectator take none. LivingEntity.calculateFallDamage:
        if (hurts && !canFly) {
            const int damage = mthFloor((distance + 1.0E-6 - attributes.safeFallDistance) * static_cast<double>(multiplier) * attributes.fallDamageMultiplier);
            if (damage > 0) lastFallDamage = damage;
        }
    }
    resetFallDistance();
}

// Entity.restituteMovementAfterCollisions: what a collision does to the velocity (0 on the blocked axes, or a bounce).
void Player::restituteMovementAfterCollisions(Block landedOn, bool xCol, bool zCol, const Vec3& actual) {
    const bool suppress = keys_.shift;
    double bounce = suppress ? 0.0 : attributes.bounciness;
    const Vec3 v{vx, vy, vz};
    Vec3 nv = v;
    if (xCol) nv.x = -v.x * bounce;
    if (zCol) nv.z = -v.z * bounce;
    const double gravity = attributes.gravity;
    if (verticalCollision) {
        if (verticalCollisionBelow_) {
            const bool honey = (traitsOf(blockRegistry(), landedOn) & kHoney) != 0;
            bounce = (-v.y <= gravity || suppress || honey) ? 0.0 : std::max(bounce, static_cast<double>(collisionShapes().bounceRestitution(landedOn) * 1.0f));
        }
        double newY = 0.0;
        double drag = 1.0;
        if (bounce > 0.0) {
            const double ratio = actual.y / v.y;
            newY = ratio * gravity;
            const double airDrag = static_cast<double>(computeModifiedFriction(0.98f, static_cast<float>(attributes.airDragModifier)));
            drag = 1.0 + ratio * (airDrag - 1.0);
        }
        nv.y = (newY - v.y) * drag * bounce;
    }
    vx = nv.x;
    vy = nv.y;
    vz = nv.z;
}

// Cobweb, sweet berry bushes and powder snow squeeze whoever is inside them (not while flying).
void Player::applyEffectsFromBlocks() {
    if (flying || spectator) return;
    const BlockRegistry& registry = blockRegistry();
    const Aabb box = boundingBox();
    const int x0 = mthFloor(box.minX + 1.0E-7), x1 = mthFloor(box.maxX - 1.0E-7);
    const int y0 = mthFloor(box.minY + 1.0E-7), y1 = mthFloor(box.maxY - 1.0E-7);
    const int z0 = mthFloor(box.minZ + 1.0E-7), z1 = mthFloor(box.maxZ - 1.0E-7);
    const Block here = inBlockState();
    for (int bz = z0; bz <= z1; ++bz) {
        for (int by = y0; by <= y1; ++by) {
            for (int bx = x0; bx <= x1; ++bx) {
                const std::uint32_t t = traitsOf(registry, blockAt(bx, by, bz));
                if ((t & kCobweb) != 0) {
                    resetFallDistance();
                    stuck_ = {0.25, 0.05000000074505806, 0.25};
                } else if ((t & kSweetBerry) != 0) {
                    resetFallDistance();
                    stuck_ = {0.800000011920929, 0.75, 0.800000011920929};
                } else if ((t & kPowderSnow) != 0 && (traitsOf(registry, here) & kPowderSnow) != 0) {
                    resetFallDistance();
                    stuck_ = {0.8999999761581421, 1.5, 0.8999999761581421};
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Fluids

namespace {
// FlowingFluid.getHeight: a full block under another of the same fluid, else the fluid's own level (8/9 for a source).
float fluidHeightAt(const Level& level, const BlockRegistry& registry, int bx, int by, int bz, const StateInfo& s, bool lava) {
    const StateInfo& above = registry.state(level.block(bx, by + 1, bz));
    if (lava ? above.has(kStateLava) : above.has(kStateWater)) return 1.0f;
    const int level8 = s.fluidLevel;
    const int amount = level8 == 0 || level8 >= 8 ? 8 : 8 - level8;
    return static_cast<float>(amount) / 9.0f;
}
}  // namespace

// EntityFluidInteraction.update: how deep the body is in water and in lava, and whether the eyes are in water.
// (The pushing of currents is not part of this port.)
void Player::updateFluidInteraction() {
    const Level& lvl = *level_;
    const BlockRegistry& registry = blockRegistry();
    const Aabb body = boundingBox();
    const Aabb box = body.deflated(0.001);
    FluidTrack water;
    FluidTrack lava;
    currentSum_ = {};
    currentCount_ = 0;
    double currentHeight = 0.0;  // the deepest the water has been so far among the blocks looked at (the game's accumulator height)
    const double eyeLevel = eyeY();
    const int feetX = mthFloor(x);
    const int feetZ = mthFloor(z);
    for (int bz = mthFloor(box.minZ); bz <= mthCeil(box.maxZ) - 1; ++bz) {
        for (int by = mthFloor(box.minY); by <= mthCeil(box.maxY) - 1; ++by) {
            for (int bx = mthFloor(box.minX); bx <= mthCeil(box.maxX) - 1; ++bx) {
                const StateInfo& s = registry.state(lvl.block(bx, by, bz));
                const bool isLava = s.has(kStateLava);
                if (!isLava && !s.has(kStateWater)) continue;
                const float h = fluidHeightAt(lvl, registry, bx, by, bz, s, isLava);
                const double surface = static_cast<double>(by) + static_cast<double>(h);
                if (surface < box.minY) continue;
                FluidTrack& t = isLava ? lava : water;
                if (bx == feetX && bz == feetZ && eyeLevel >= static_cast<double>(by) && eyeLevel <= static_cast<double>(by) + static_cast<double>(h)) t.eyesInside = true;
                t.height = std::max(t.height, surface - body.minY);
                if (!isLava) {  // the water's current: this block's flow, counted for less where the water is shallow (EntityFluidInteraction)
                    currentHeight = std::max(t.height, currentHeight);
                    std::array<double, 3> flow = FluidSimulator::flowAt(lvl, bx, by, bz);
                    if (currentHeight < 0.4) {
                        for (double& c : flow) c *= currentHeight;
                    }
                    for (std::size_t i = 0; i < 3; ++i) currentSum_[i] += flow[i];
                    ++currentCount_;
                }
            }
        }
    }
    waterHeight_ = water.height;
    lavaHeight_ = lava.height;
    eyesInWater_ = water.eyesInside;
    eyesInLava_ = lava.eyesInside;
    const bool touching = waterHeight_ > 0.0;
    if (touching) resetFallDistance();
    inWater = touching;
}

// EntityFluidInteraction.CurrentAccumulator.applyTo for a player: the average flow of the water they are in, times 0.014, is added to
// their motion - and a player at rest in a current too weak to notice is still given a nudge of 0.0045, so they drift.
void Player::applyWaterCurrent() {
    if (currentCount_ == 0) return;
    const double lengthSquared = currentSum_[0] * currentSum_[0] + currentSum_[1] * currentSum_[1] + currentSum_[2] * currentSum_[2];
    if (lengthSquared < 9.999999747378752e-6) return;
    constexpr double kWaterPush = 0.014;
    std::array<double, 3> push{};
    for (std::size_t i = 0; i < 3; ++i) push[i] = currentSum_[i] * (1.0 / static_cast<double>(currentCount_)) * kWaterPush;
    const double length = std::sqrt(push[0] * push[0] + push[1] * push[1] + push[2] * push[2]);
    if (std::abs(vx) < 0.003 && std::abs(vz) < 0.003 && length < 0.0045000000000000005 && length > 0.0) {
        for (double& c : push) c = c / length * 0.0045000000000000005;
    }
    vx += push[0];
    vy += push[1];
    vz += push[2];
}

// Entity.updateSwimming: sprinting in water swims (the body lies down); it ends when the water does.
void Player::updateSwimming() {
    if (flying) {
        swimming_ = false;
        return;
    }
    if (isSwimming()) {
        swimming_ = sprinting && inWater;
    } else {
        const bool inWaterBlock = blockRegistry().state(blockAtFeet()).has(kStateWater);
        swimming_ = sprinting && isUnderWater() && inWaterBlock;
    }
}

void Player::baseTick() {
    inBlockValid_ = false;
    updateFluidInteraction();
    applyWaterCurrent();
    updateSwimming();
    if (isInLava()) fallDistance *= 0.5;
    firstTick_ = false;
}

// ---------------------------------------------------------------------------------------------------------------
// Pose

void Player::setPose(PlayerPose p) {
    if (p == pose_) return;
    pose_ = p;
    eyeHeight_ = dimensionsOf(p).eye;  // refreshDimensions: the body changes at once; the camera eases
}

// Player.updatePlayerPose: crouch while shift is held (or while there is no room to stand), swim when swimming, and
// crawl where even a crouch does not fit.
void Player::updatePlayerPose() {
    if (!canFitIn(PlayerPose::Swimming)) return;
    PlayerPose want = PlayerPose::Standing;
    if (isSwimming()) {
        want = PlayerPose::Swimming;
    } else if (keys_.shift && !flying) {
        want = PlayerPose::Crouching;
    }
    PlayerPose chosen = want;
    if (!spectator && !canFitIn(want)) chosen = canFitIn(PlayerPose::Crouching) ? PlayerPose::Crouching : PlayerPose::Swimming;
    setPose(chosen);
}

// ---------------------------------------------------------------------------------------------------------------
// Input and the tick

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

void Player::tick(const PlayerInput& input, const Level& level) {
    level_ = &level;
    // Camera.tick: the camera's eye height eases halfway to the body's every tick.
    cameraEyeHeightOld_ = cameraEyeHeight_;
    cameraEyeHeight_ += (eyeHeight_ - cameraEyeHeight_) * 0.5f;
    // Entity.commonTick: remember where we were.
    xo = x;
    yo = y;
    zo = z;
    yawO = yaw;
    pitchO = pitch;
    walkDistO_ = walkDist_;
    // Player.tick
    if (spectator) {
        canFly = true;
        onGround = false;
    }
    if (!canFly) flying = false;
    wasUnderwater_ = eyesInWater_;
    baseTick();
    aiStep(input);
    updatePlayerPose();
    sneaking = pose_ == PlayerPose::Crouching;
    level_ = nullptr;
}

void Player::turn(double deltaYaw, double deltaPitch) {
    const float dp = static_cast<float>(deltaPitch) * 0.15f;
    const float dy = static_cast<float>(deltaYaw) * 0.15f;
    pitch = std::clamp(pitch + dp, -90.0f, 90.0f);
    yaw += dy;
    pitchO = std::clamp(pitchO + dp, -90.0f, 90.0f);
    yawO += dy;
}

void Player::setPosition(double newX, double newY, double newZ) {
    x = xo = newX;
    y = yo = newY;
    z = zo = newZ;
}

void Player::toggleFlying() {
    if (!canFly) return;
    flying = !flying;
}

// ---------------------------------------------------------------------------------------------------------------
// Between ticks

std::array<double, 3> Player::position(double partial) const {
    return {xo + partial * (x - xo), yo + partial * (y - yo), zo + partial * (z - zo)};
}

std::array<double, 3> Player::eyePosition(double partial) const {
    const auto p = position(partial);
    return {p[0], p[1] + static_cast<double>(eyeHeight_), p[2]};
}

std::array<double, 3> Player::cameraPosition(double partial) const {
    const auto p = position(partial);
    const float t = static_cast<float>(partial);
    const float easedEye = cameraEyeHeightOld_ + t * (cameraEyeHeight_ - cameraEyeHeightOld_);
    return {p[0], p[1] + static_cast<double>(easedEye), p[2]};
}

// GameRenderer.bobView.
ViewBob Player::viewBob(double partial) const {
    const float t = static_cast<float>(partial);
    constexpr float kPi = 3.1415927f;
    const float walk = -(walkDist_ + (walkDist_ - walkDistO_) * t);
    const float strength = bobO_ + t * (bob_ - bobO_);
    ViewBob b;
    b.x = mthSin(walk * kPi) * strength * 0.5f;
    b.y = -std::abs(mthCos(walk * kPi) * strength);
    b.rollDegrees = mthSin(walk * kPi) * strength * 3.0f;
    b.pitchDegrees = std::abs(mthCos(walk * kPi - 0.2f) * strength) * 5.0f;
    return b;
}

}  // namespace falcon
