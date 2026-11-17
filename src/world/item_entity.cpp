#include "falcon/world/item_entity.hpp"

#include "falcon/world/player.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

int blockFloor(double v) { return static_cast<int>(std::floor(v)); }

}  // namespace

ItemEntity& ItemEntities::spawn(ItemStack stack, double x, double y, double z, double vx, double vy, double vz, int pickupDelay, float bobOffset) {
    ItemEntity e;
    e.stack = std::move(stack);
    e.x = e.xo = x;
    e.y = e.yo = y;
    e.z = e.zo = z;
    e.vx = vx;
    e.vy = vy;
    e.vz = vz;
    e.pickupDelay = pickupDelay;
    e.bobOffset = bobOffset;
    e.id = nextId_++;
    items_.push_back(std::move(e));
    return items_.back();
}

// ItemEntity.tick.
void ItemEntities::tick(const Level& level, const std::function<bool(int chunkX, int chunkZ)>& isChunkRendered) {
    ++tickCount_;
    const BlockRegistry& registry = blockRegistry();
    for (ItemEntity& e : items_) {
        if (e.stack.isEmpty()) continue;
        if (isChunkRendered && !isChunkRendered(chunkOf(blockFloor(e.x)), chunkOf(blockFloor(e.z)))) {
            e.stack = ItemStack();
            continue;
        }
        if (e.pickupDelay > 0 && e.pickupDelay != ItemEntity::kNeverPickUp) --e.pickupDelay;
        e.xo = e.x;
        e.yo = e.y;
        e.zo = e.z;
        const StateInfo& here = registry.state(level.block(blockFloor(e.x), blockFloor(e.y + 0.1), blockFloor(e.z)));
        if (here.has(kStateLava)) {  // the lava burns it up
            e.stack = ItemStack();
            continue;
        }
        if (here.has(kStateWater)) {  // setUnderwaterMovement: floats up slowly
            e.vx *= 0.99;
            e.vz *= 0.99;
            e.vy += e.vy < 0.06 ? 5.0E-4 : 0.0;
        } else {
            e.vy -= 0.04;  // gravity
        }
        // Stuck inside a block (one was placed on it): the game pushes it to the nearest free space, here it is lifted out.
        if (aabbCollides(level, e.box().deflated(1.0E-7))) {
            e.y += 0.1;
            e.vy = 0.0;
            e.onGround = false;
            continue;
        }
        if (!e.onGround || e.vx * e.vx + e.vz * e.vz > 1.0E-5 || (tickCount_ + e.id) % 4 == 0) {
            const auto actual = collideAabb(level, e.box(), e.vx, e.vy, e.vz);
            e.x += actual[0];
            e.y += actual[1];
            e.z += actual[2];
            if (std::abs(e.vx - actual[0]) > 1.0E-5) e.vx = 0.0;  // a wall stops it
            if (std::abs(e.vz - actual[2]) > 1.0E-5) e.vz = 0.0;
            const bool verticalHit = std::abs(e.vy - actual[1]) > 1.0E-5;
            e.onGround = verticalHit && e.vy < 0.0;
            if (verticalHit) e.vy = 0.0;
            float friction = 0.98f;
            if (e.onGround) friction = registry.state(level.block(blockFloor(e.x), blockFloor(e.y - 0.500001), blockFloor(e.z))).friction * 0.98f;
            e.vx *= static_cast<double>(friction);
            e.vy *= 0.98;
            e.vz *= static_cast<double>(friction);
        }
        const bool moved = blockFloor(e.xo) != blockFloor(e.x) || blockFloor(e.yo) != blockFloor(e.y) || blockFloor(e.zo) != blockFloor(e.z);
        if ((tickCount_ + e.id) % (moved ? 2 : 40) == 0) mergeWithNeighbours(e);
        ++e.age;
        if (e.age >= ItemEntity::kLifetimeTicks || e.y < static_cast<double>(kMinY) - 64.0) e.stack = ItemStack();
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const ItemEntity& e) { return e.stack.isEmpty(); }), items_.end());
}

// ItemEntity.mergeWithNeighbours: a stack of the same item within half a block either way (not above or below) joins this one while there is room;
// the bigger stack stays, the age and the pickup delay are the ones that suit both.
void ItemEntities::mergeWithNeighbours(ItemEntity& item) {
    if (item.pickupDelay == ItemEntity::kNeverPickUp || item.age >= ItemEntity::kLifetimeTicks || item.stack.count >= item.stack.maxStackSize()) return;
    const Aabb near{item.x - 0.125 - 0.5, item.y, item.z - 0.125 - 0.5, item.x + 0.125 + 0.5, item.y + 0.25, item.z + 0.125 + 0.5};
    for (ItemEntity& other : items_) {
        if (&other == &item || other.stack.isEmpty() || !other.box().intersects(near)) continue;
        if (other.pickupDelay == ItemEntity::kNeverPickUp || other.age >= ItemEntity::kLifetimeTicks) continue;
        if (!isSameItemSameComponents(item.stack, other.stack)) continue;
        ItemEntity& big = other.stack.count < item.stack.count ? item : other;
        ItemEntity& small = &big == &item ? other : item;
        const int room = big.stack.maxStackSize() - big.stack.count;
        const int moved = std::min(small.stack.count, room);
        if (moved <= 0) continue;
        big.stack.grow(moved);
        small.stack.shrink(moved);
        big.pickupDelay = std::max(big.pickupDelay, small.pickupDelay);
        big.age = std::min(big.age, small.age);
        if (small.stack.count <= 0) small.stack = ItemStack();
        if (&small == &item) return;  // this one has gone into the other
    }
}

}  // namespace falcon
