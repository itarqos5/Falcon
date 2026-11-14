#pragma once

#include "falcon/world/chunk.hpp"
#include "falcon/world/collision_shapes.hpp"
#include "falcon/world/item_stack.hpp"

#include <vector>

namespace falcon {

// An item lying in the world (the game's ItemEntity): a stack in a 0.25 box that falls, slides and settles, can be picked up by the player after a
// short delay, joins a neighbouring stack of the same item, and is gone after five minutes. Positions are doubles, velocities blocks per tick.
struct ItemEntity {
    static constexpr double kSize = 0.25;
    static constexpr int kLifetimeTicks = 6000;
    static constexpr int kNeverPickUp = 32767;

    ItemStack stack;
    double x = 0.0, y = 0.0, z = 0.0;  // the middle of the bottom of the box
    double vx = 0.0, vy = 0.0, vz = 0.0;
    double xo = 0.0, yo = 0.0, zo = 0.0;  // where it was a tick ago, for drawing between ticks
    int age = 0;
    int pickupDelay = 10;  // ticks until the player may take it (a dropped block 10, a thrown item 40)
    int id = 0;
    bool onGround = false;
    float bobOffset = 0.0f;  // radians: where in its bobbing and spinning this item starts

    [[nodiscard]] Aabb box() const { return {x - kSize / 2.0, y, z - kSize / 2.0, x + kSize / 2.0, y + kSize, z + kSize / 2.0}; }
};

class ItemEntities {
public:
    // Puts an item in the world. `bobOffset` is a random angle in radians (the caller owns the random numbers).
    ItemEntity& spawn(ItemStack stack, double x, double y, double z, double vx, double vy, double vz, int pickupDelay, float bobOffset);
    // One game tick for every item: gravity, water and lava, the move and its friction, merging, ageing, despawning.
    void tick(const Level& level);
    [[nodiscard]] std::vector<ItemEntity>& items() { return items_; }
    [[nodiscard]] const std::vector<ItemEntity>& items() const { return items_; }
    void clear() { items_.clear(); }

    // How many copies of an item the game draws for a stack of `count` (ItemEntity.getRenderedAmount).
    [[nodiscard]] static int renderedAmount(int count) { return count <= 1 ? 1 : count <= 16 ? 2 : count <= 32 ? 3 : count <= 48 ? 4 : 5; }

private:
    void mergeWithNeighbours(ItemEntity& item);
    std::vector<ItemEntity> items_;
    int nextId_ = 1;
    long long tickCount_ = 0;
};

}  // namespace falcon
