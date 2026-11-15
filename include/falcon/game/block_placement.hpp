#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/raycast.hpp"

#include <array>

namespace falcon {

// What a block item is about to place, and where: everything the game's BlockItem.getPlacementState looks at.
struct PlacementContext {
    const BlockRegistry& registry;
    int blockIndex;       // the block of the item (an index into BlockRegistry::blocks())
    const BlockHit& hit;  // the block and face aimed at (hit.placePosition() is where it goes)
    float yaw = 0.0f;     // the player's look, degrees (facing of stairs, logs' axis, ...)
    float pitch = 0.0f;
    bool sneaking = false;
    std::array<double, 3> cursor{0.5, 0.5, 0.5};  // where in the block the aim hit it, 0..1 on each axis (slabs and trapdoors go top or bottom by it)
};

// The one place that decides which STATE of a block is placed (BlockItem.getPlacementState): stairs facing the player and upside down from the upper
// half, logs along the face that was clicked, slabs top or bottom, doors and trapdoors, levers and buttons on the face they were put on, repeaters,
// pistons, observers, furnaces and the like facing the way the game turns them.
[[nodiscard]] Block blockStateForPlacement(const PlacementContext& context);

}  // namespace falcon
