#pragma once

#include "apex/world/chunk.hpp"

#include <array>
#include <functional>
#include <map>
#include <unordered_set>

namespace apex {

// Water that flows: the rules of the game's FlowingFluid / WaterFluid, in game ticks. A source block spreads sideways, losing one level
// of height a block (7 blocks at most), falls straight down through air and plants, turns into a source where two sources meet over
// something solid (the infinite pool), and a flowing block that no source feeds any more dries up. Water that meets a block it can hold
// fluid in (air, a flower, a torch ...) replaces it; it stops at solid blocks and never flows into lava (lava and water do not mix yet).
//
// Nothing happens until something changes: blockChanged() is called when a block was placed or broken, and every water block next to
// it looks at its surroundings again, five game ticks later, then those it changes look at theirs. tick() runs the updates that are due.
class FluidSimulator {
public:
    using SetBlock = std::function<bool(int x, int y, int z, Block block)>;
    static constexpr int kWaterDelay = 5;  // game ticks between a water block's update and the next ones (WaterFluid.getTickDelay)

    // A block at (x, y, z) was placed, broken or replaced: the water in it and next to it will be looked at again.
    void blockChanged(const Level& level, int x, int y, int z);
    // One game tick: runs the water updates that are due. At most `maxChanges` blocks are changed in a tick (the rest wait for the
    // next one), so a flood never costs a whole frame. `setBlock` places the block in the world (and redraws it) and says if it did.
    void tick(const Level& level, const SetBlock& setBlock, int maxChanges = 48);

    [[nodiscard]] std::size_t pending() const { return due_.size(); }
    [[nodiscard]] long long gameTime() const { return time_; }

    // What the water at a block is (exposed for tests and for the swimming code).
    struct Water {
        bool water = false;     // the block holds water at all
        bool source = false;    // a source (level 0, a waterlogged block, kelp, seagrass)
        bool falling = false;   // water falling down
        int amount = 0;         // 1..8: how high it stands; a source and falling water are 8
        bool operator==(const Water&) const = default;
    };
    [[nodiscard]] static Water waterAt(Block block);
    // The block of water with this state (flowing water at a level 1..7 of height, falling water, or a source).
    [[nodiscard]] static Block waterBlock(const Water& water);
    // The way the water at block (x, y, z) pulls whatever is in it (FlowingFluid.getFlow): a unit vector pointing towards where the
    // surface is lower (a drop-off, the end of a stream), straight down for falling water against a wall; zero for still water.
    [[nodiscard]] static std::array<double, 3> flowAt(const Level& level, int x, int y, int z);

private:
    struct Pos {
        int x, y, z;
    };
    static std::int64_t keyOf(const Pos& p) {
        return (static_cast<std::int64_t>(p.x) & 0x3FFFFFF) << 38 | (static_cast<std::int64_t>(p.z) & 0x3FFFFFF) << 12 | (static_cast<std::int64_t>(p.y + 2048) & 0xFFF);
    }
    void schedule(const Pos& p, int delay);
    void neighboursChanged(const Level& level, const Pos& p);

    // One water block's update.
    void update(const Level& level, const SetBlock& setBlock, const Pos& p, int& changes);
    [[nodiscard]] Water newLiquid(const Level& level, const Pos& p, Block block) const;
    void spread(const Level& level, const SetBlock& setBlock, const Pos& p, const Water& water, int& changes);
    void spreadToSides(const Level& level, const SetBlock& setBlock, const Pos& p, const Water& water, Block below, int& changes);
    [[nodiscard]] std::array<std::pair<bool, Water>, 4> getSpread(const Level& level, const Pos& p, Block below) const;
    [[nodiscard]] int slopeDistance(const Level& level, const Pos& p, int distance, int from, Block block) const;
    [[nodiscard]] bool isWaterHole(const Level& level, const Pos& below, Block belowBlock, const Pos& at) const;
    [[nodiscard]] bool canHoldFluid(Block block) const;
    [[nodiscard]] bool canPassThroughWall(Block to) const;
    [[nodiscard]] bool canPassThrough(const Level& level, const Pos& np, Block nb) const;
    [[nodiscard]] int sourceNeighbours(const Level& level, const Pos& p) const;
    void spreadTo(const Level& level, const SetBlock& setBlock, const Pos& p, const Water& water, int& changes);
    [[nodiscard]] Block blockAt(const Level& level, const Pos& p) const;

    long long time_ = 0;
    std::multimap<long long, Pos> due_;       // when -> the block to update
    std::unordered_set<std::int64_t> scheduled_;  // the blocks in due_, so none is there twice
};

}  // namespace apex
