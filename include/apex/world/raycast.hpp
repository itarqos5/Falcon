#pragma once

#include "apex/world/block_render.hpp"
#include "apex/world/chunk.hpp"

#include <array>
#include <optional>

namespace apex {

// The block a ray from the player's eyes first hits, and which of its faces (see Face) it hits.
struct BlockHit {
    int x, y, z;
    int face;           // the face that was hit: where a block would be placed is next to it
    double distance;
    // The block next to the hit face (where a new block would go).
    [[nodiscard]] std::array<int, 3> placePosition() const {
        constexpr int kNormals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        return {x + kNormals[face][0], y + kNormals[face][1], z + kNormals[face][2]};
    }
};

// Walks the ray block by block (Amanatides and Woo) up to `reach` blocks. Air and water are looked through, as the game does when
// aiming. Any other block is hit only where its OUTLINE is (see BlockOutlines): a torch is a small post, a slab half a block, a fence
// its posts and rails, and a ray through the empty part of the block's cell goes on to what is behind. `distance` is how far along the
// ray the outline is hit and `face` the side of it. Nothing is hit inside a block the ray starts in.
[[nodiscard]] std::optional<BlockHit> raycastBlocks(const Level& level, const std::array<double, 3>& origin,
                                                    const std::array<double, 3>& direction, double reach);

}  // namespace apex
