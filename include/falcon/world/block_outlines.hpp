#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/collision_shapes.hpp"  // Aabb

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

// The shape of a block you aim at: its outline (the game's "shape" as opposed to its collision shape). A torch's outline is a small
// post although you can walk through it, a slab's is half a block, a fence's is its posts and rails. What the cursor hits, and the
// black lines drawn round it, come from here. Boxes are in block space (0..1 across the block).
struct OutlineShape {
    std::vector<Aabb> boxes;                    // what a ray can hit; empty: not aimable
    std::vector<std::array<float, 6>> edges;    // the line segments drawn round it: x0, y0, z0, x1, y1, z1
};

// The outline of every block state, from the game's gameplay-facts.json (read once, then kept as a small cache file).
class BlockOutlines {
public:
    // Reads <dataDir>/gameplay-facts.json (streamed: it is 55 MB); the result is kept as a small binary file in `cacheDir`. Null
    // and `error` when the file is unusable.
    [[nodiscard]] static std::shared_ptr<const BlockOutlines> loadFromGameData(const std::filesystem::path& dataDir,
                                                                               const std::filesystem::path& cacheDir,
                                                                               const BlockRegistry& registry, std::string& error);
    // For tests and when the game's file is missing: every state that is neither air nor liquid is a full cube.
    [[nodiscard]] static std::shared_ptr<const BlockOutlines> makeMinimal(const BlockRegistry& registry);
    // Like makeMinimal, but the given states have their own boxes (edges are made from them): for tests.
    struct Override {
        Block block;
        std::vector<Aabb> boxes;
    };
    [[nodiscard]] static std::shared_ptr<const BlockOutlines> makeFromBoxes(const BlockRegistry& registry, const std::vector<Override>& overrides);

    [[nodiscard]] bool isFromGameData() const { return fromGameData_; }
    [[nodiscard]] std::size_t stateCount() const { return stateShape_.size(); }
    [[nodiscard]] std::size_t shapeCount() const { return shapes_.size(); }
    [[nodiscard]] const OutlineShape& shape(Block b) const;
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    friend class BlockOutlinesBuilder;
    std::vector<OutlineShape> shapes_;          // shape 0 is the empty one
    std::vector<std::uint16_t> stateShape_;     // state id -> index into shapes_
    bool fromGameData_ = false;
};

// The 12 edges of a box as line segments (what the game draws for a box-shaped outline).
[[nodiscard]] std::vector<std::array<float, 6>> boxEdges(const std::vector<Aabb>& boxes);

// The table the world code uses (installed next to the block registry when a world opens; a full-cubes stand-in is made on demand
// from the installed registry otherwise). Main thread only.
[[nodiscard]] const BlockOutlines& blockOutlines();
void installBlockOutlines(std::shared_ptr<const BlockOutlines> outlines);
[[nodiscard]] bool hasBlockOutlinesFromGameData();

}  // namespace falcon
