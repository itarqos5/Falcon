#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/raycast.hpp"

namespace falcon {

// What a block item is about to place, and where: everything the game's BlockItem.getPlacementState looks at.
struct PlacementContext {
    const BlockRegistry& registry;
    int blockIndex;       // the block of the item (an index into BlockRegistry::blocks())
    const BlockHit& hit;  // the block and face aimed at (hit.placePosition() is where it goes)
    float yaw = 0.0f;     // the player's look, degrees (facing of stairs, logs' axis, ...)
    float pitch = 0.0f;
    bool sneaking = false;
};

// The one place that decides which STATE of a block is placed: stairs facing, logs along the face that was clicked, slabs
// top or bottom, doors, ... For now it is the block's default state; replace this function (it is the only caller's rule) to
// give the placed blocks their properties.
[[nodiscard]] Block blockStateForPlacement(const PlacementContext& context);

}  // namespace falcon
