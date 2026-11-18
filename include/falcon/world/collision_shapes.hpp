#pragma once

#include "falcon/world/block_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace falcon {

// An axis-aligned box in doubles, like the game's AABB (axis 0 is x, 1 is y, 2 is z).
struct Aabb {
    double minX = 0.0, minY = 0.0, minZ = 0.0, maxX = 0.0, maxY = 0.0, maxZ = 0.0;

    [[nodiscard]] double min(int axis) const { return axis == 0 ? minX : axis == 1 ? minY : minZ; }
    [[nodiscard]] double max(int axis) const { return axis == 0 ? maxX : axis == 1 ? maxY : maxZ; }
    [[nodiscard]] Aabb moved(double dx, double dy, double dz) const { return {minX + dx, minY + dy, minZ + dz, maxX + dx, maxY + dy, maxZ + dz}; }
    // Grown only on the side the delta points to (AABB.expandTowards).
    [[nodiscard]] Aabb expandedTowards(double dx, double dy, double dz) const {
        Aabb r = *this;
        (dx < 0.0 ? r.minX : r.maxX) += dx;
        (dy < 0.0 ? r.minY : r.maxY) += dy;
        (dz < 0.0 ? r.minZ : r.maxZ) += dz;
        return r;
    }
    [[nodiscard]] Aabb deflated(double d) const { return {minX + d, minY + d, minZ + d, maxX - d, maxY - d, maxZ - d}; }
    // Positive overlap on all three axes (AABB.intersects: touching faces do not count).
    [[nodiscard]] bool intersects(const Aabb& o) const {
        return minX < o.maxX && maxX > o.minX && minY < o.maxY && maxY > o.minY && minZ < o.maxZ && maxZ > o.minZ;
    }
};

class CollisionShapes;

// One block state's collision shape: the game's VoxelShape, a grid whose lines sit at listed coordinates (in block
// space, so up to 1.5 high for a fence) with some of its cells filled. A cheap view into the CollisionShapes table
// that made it; it is only valid while that table lives.
class VoxelShape {
public:
    [[nodiscard]] bool empty() const { return (flags_ & kEmpty) != 0; }
    [[nodiscard]] bool isUnitCube() const { return (flags_ & kUnitCube) != 0; }
    // The shape sticks out of its block (fences, walls and gates are taller than 1): the game then also looks at the
    // blocks one step further out when it collects colliders.
    [[nodiscard]] bool isLarge() const { return (flags_ & kLarge) != 0; }
    // Cells and grid lines along an axis: there are cells(axis) + 1 coordinates.
    [[nodiscard]] int cells(int axis) const { return n_[axis]; }
    [[nodiscard]] double coord(int axis, int i) const { return static_cast<double>(coords_[axisBase_[axis] + static_cast<std::size_t>(i)]); }
    [[nodiscard]] bool filled(int ix, int iy, int iz) const;

    // How far `box` (in world space) may move along `axis` by `d` before it hits a filled cell of this shape when the
    // block sits at (bx, by, bz): VoxelShape.collide of the game, including its 1e-7 tolerances.
    [[nodiscard]] double collide(int axis, const Aabb& box, double d, int bx, int by, int bz) const;
    // Whether `box` overlaps a filled cell of the shape placed at (bx, by, bz) by more than a sliver.
    [[nodiscard]] bool intersects(const Aabb& box, int bx, int by, int bz) const;
    // The filled cells merged into as few boxes as a greedy sweep finds, in block space (a stair is two boxes).
    void boxes(std::vector<Aabb>& out) const;
    // The smallest box around the filled cells, in block space (empty shape: all zero).
    [[nodiscard]] Aabb bounds() const;

private:
    friend class CollisionShapes;
    enum : std::uint8_t { kEmpty = 1, kUnitCube = 2, kLarge = 4 };
    [[nodiscard]] int findIndex(int axis, double position, int offset) const;

    const float* coords_ = nullptr;
    const std::uint8_t* bits_ = nullptr;
    std::size_t axisBase_[3] = {0, 0, 0};  // where each axis' coordinates start in coords_
    std::size_t bitBase_ = 0;              // bit index of cell (0,0,0)
    int n_[3] = {0, 0, 0};
    std::uint8_t flags_ = kEmpty;
};

// The collision shapes of every block state, from the game's cached-collision.nbt (6,093 distinct shapes for the
// 35,723 states), plus the two per-block numbers falls and landings need (bounce and fall reduction, from
// movement-properties.nbt). Held compactly: a 16-byte record and a few floats per shape, 2 bytes per state.
class CollisionShapes {
public:
    // Reads <dataDir>/cached-collision.nbt and movement-properties.nbt; the result is kept as a small binary file in
    // `cacheDir` so the next start-up skips the 10 MB of NBT. Null and `error` when the files are unusable.
    [[nodiscard]] static std::shared_ptr<const CollisionShapes> loadFromGameData(const std::filesystem::path& dataDir,
                                                                                 const std::filesystem::path& cacheDir,
                                                                                 const BlockRegistry& registry, std::string& error);
    // For tests and when the game's files are missing: every state flagged as a full-cube collision is a unit cube,
    // everything else has no collision at all.
    [[nodiscard]] static std::shared_ptr<const CollisionShapes> makeMinimal(const BlockRegistry& registry);
    // A table of full cubes plus the given states with their own boxes (block space) and bounce numbers: lets tests
    // have slabs, stairs and slime without the game's files.
    struct Override {
        Block block;
        std::vector<Aabb> boxes;
        float bounceRestitution = 0.0f;
        float fallDistanceReduction = 0.0f;
    };
    [[nodiscard]] static std::shared_ptr<const CollisionShapes> makeFromBoxes(const BlockRegistry& registry, const std::vector<Override>& overrides);
    // The table with full cubes for every state from stateCount() up to `states` (the states a mod's blocks added to the registry).
    [[nodiscard]] static std::shared_ptr<const CollisionShapes> withCubes(const CollisionShapes& base, std::size_t states);

    [[nodiscard]] bool isFromGameData() const { return fromGameData_; }
    [[nodiscard]] std::size_t stateCount() const { return stateShape_.size(); }
    [[nodiscard]] std::size_t shapeCount() const { return shapes_.size(); }

    [[nodiscard]] VoxelShape shape(Block b) const;
    [[nodiscard]] VoxelShape unitCube() const { return view(unitCube_); }
    // Convenience for selection and tests: the block's collision as merged boxes in block space.
    [[nodiscard]] std::vector<Aabb> boxes(Block b) const;

    // Block.getBounceRestitution / getFallDistanceReduction: slime 1.0, beds 0.75 and 0.5, everything else 0.
    [[nodiscard]] float bounceRestitution(Block b) const;
    [[nodiscard]] float fallDistanceReduction(Block b) const;

    [[nodiscard]] std::size_t memoryBytes() const;

private:
    friend class CollisionShapesBuilder;
    struct Record {
        std::uint32_t coordBase = 0;  // index into coords_: x lines, then y lines, then z lines
        std::uint32_t bitBase = 0;    // bit index into bits_ of the first cell (cells are ordered (x * ny + y) * nz + z)
        std::uint8_t n[3] = {0, 0, 0};
        std::uint8_t flags = 1;
        std::uint16_t reserved = 0;
    };
    struct Special {  // a block with a bounce or fall reduction
        std::uint16_t firstState = 0;
        std::uint16_t stateCount = 0;
        float bounce = 0.0f;
        float reduction = 0.0f;
    };
    [[nodiscard]] VoxelShape view(std::uint16_t index) const;
    [[nodiscard]] const Special* special(Block b) const;

    bool fromGameData_ = false;
    std::vector<Record> shapes_;
    std::vector<float> coords_;
    std::vector<std::uint8_t> bits_;
    std::vector<std::uint16_t> stateShape_;  // state id -> index into shapes_
    std::vector<Special> specials_;          // sorted by firstState
    std::uint16_t unitCube_ = 0;
    std::uint16_t emptyShape_ = 0;
};

// The table the world code uses: installed next to the block registry when a world opens (the game's real shapes) or
// built on demand from the installed registry (full cubes only, for tests). Main thread only.
[[nodiscard]] const CollisionShapes& collisionShapes();
void installCollisionShapes(std::shared_ptr<const CollisionShapes> shapes);
// Whether the installed table is the game's real one (not the full-cubes-only stand-in).
[[nodiscard]] bool hasCollisionShapesFromGameData();

}  // namespace falcon
