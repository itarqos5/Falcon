#include "falcon/world/fluids.hpp"

#include "falcon/world/block_registry.hpp"
#include "falcon/world/collision_shapes.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace falcon {

namespace {

constexpr int kHorizontal[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};  // east, west, south, north
constexpr int kSlopeFindDistance = 4;                                  // WaterFluid.getSlopeFindDistance
constexpr int kNoSlope = 1000;

constexpr int opposite(int direction) { return direction ^ 1; }

bool endsWith(const std::string& s, const char* suffix) {
    const std::string tail(suffix);
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

}  // namespace

FluidSimulator::Water FluidSimulator::waterAt(Block block) {
    const StateInfo& info = blockRegistry().state(block);
    if (!info.has(kStateWater)) return {};
    if (info.has(kStateLiquid)) {  // the water block itself: its level says what it is
        const int level = info.fluidLevel;
        if (level == 0) return {true, true, false, 8};
        if (level >= 8) return {true, false, true, 8};
        return {true, false, false, 8 - level};
    }
    return {true, true, false, 8};  // a waterlogged block, kelp, seagrass: a source
}

Block FluidSimulator::waterBlock(const Water& water) {
    const BlockRegistry& registry = blockRegistry();
    const int level = water.source ? 0 : water.falling ? 8 : std::clamp(8 - water.amount, 1, 7);
    return registry.with(commonBlocks().water, "level", std::to_string(level));
}

std::array<double, 3> FluidSimulator::flowAt(const Level& level, int x, int y, int z) {
    const BlockRegistry& registry = blockRegistry();
    const auto blockOf = [&](int bx, int by, int bz) {
        if (by < kMinY || by >= kMaxY || level.chunk({chunkOf(bx), chunkOf(bz)}) == nullptr) return commonBlocks().bedrock;
        return level.block(bx, by, bz);
    };
    const Water self = waterAt(blockOf(x, y, z));
    if (!self.water) return {0.0, 0.0, 0.0};
    const auto ownHeight = [](const Water& w) { return w.water ? static_cast<double>(w.amount) / 9.0 : 0.0; };  // FluidState.getOwnHeight
    const auto affectsFlow = [&](Block b) { return waterAt(b).water || !registry.state(b).has(kStateLava); };    // empty fluid, or water
    const auto solid = [&](Block b) { return registry.state(b).has(kStateFullCollision); };
    double dx = 0.0, dz = 0.0;
    for (const auto& d : kHorizontal) {
        const Block nb = blockOf(x + d[0], y, z + d[1]);
        if (!affectsFlow(nb)) continue;
        double f = ownHeight(waterAt(nb));
        double f1 = 0.0;
        if (f == 0.0) {
            if (!solid(nb)) {  // no water there and nothing in the way: if water is lower down that side, it pulls this way
                const Block below = blockOf(x + d[0], y - 1, z + d[1]);
                if (affectsFlow(below)) {
                    f = ownHeight(waterAt(below));
                    if (f > 0.0) f1 = ownHeight(self) - (f - 0.8888889);
                }
            }
        } else {
            f1 = ownHeight(self) - f;
        }
        if (f1 != 0.0) {
            dx += d[0] * f1;
            dz += d[1] * f1;
        }
    }
    std::array<double, 3> v = {dx, 0.0, dz};
    const auto normalize = [](std::array<double, 3> a) {
        const double len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (len < 1.0e-5) return std::array<double, 3>{0.0, 0.0, 0.0};
        return std::array<double, 3>{a[0] / len, a[1] / len, a[2] / len};
    };
    if (self.falling) {  // falling water against a solid face is pulled down
        const auto solidFace = [&](Block b) { return !waterAt(b).water && solid(b); };
        for (const auto& d : kHorizontal) {
            if (solidFace(blockOf(x + d[0], y, z + d[1])) || solidFace(blockOf(x + d[0], y + 1, z + d[1]))) {
                v = normalize(v);
                v[1] += -6.0;
                break;
            }
        }
    }
    return normalize(v);
}

Block FluidSimulator::blockAt(const Level& level, const Pos& p) const {
    // Outside the world and in chunks that are not loaded there is "something solid": water never flows there.
    if (p.y < kMinY || p.y >= kMaxY || level.chunk({chunkOf(p.x), chunkOf(p.z)}) == nullptr) return commonBlocks().bedrock;
    return level.block(p.x, p.y, p.z);
}

void FluidSimulator::schedule(const Pos& p, int delay) {
    if (!scheduled_.insert(keyOf(p)).second) return;
    due_.emplace(time_ + delay, p);
}

void FluidSimulator::neighboursChanged(const Level& level, const Pos& p) {
    constexpr int kSides[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& s : kSides) {
        const Pos n{p.x + s[0], p.y + s[1], p.z + s[2]};
        if (waterAt(blockAt(level, n)).water) schedule(n, kWaterDelay);
    }
}

void FluidSimulator::blockChanged(const Level& level, int x, int y, int z) {
    const Pos p{x, y, z};
    if (waterAt(blockAt(level, p)).water) schedule(p, kWaterDelay);
    neighboursChanged(level, p);
}

void FluidSimulator::tick(const Level& level, const SetBlock& setBlock, int maxChanges) {
    ++time_;
    int changes = 0;
    while (!due_.empty() && due_.begin()->first <= time_ && changes < maxChanges) {
        const Pos p = due_.begin()->second;
        due_.erase(due_.begin());
        scheduled_.erase(keyOf(p));
        update(level, setBlock, p, changes);
    }
}

// Whether a block can have flowing water put in it (FlowingFluid.canHoldFluid): air, and anything with no collision to it that
// water can wash away - flowers, grass, torches, rails - but not doors, signs, ladders, sugar cane, portals, and not lava or water
// that is already a source.
bool FluidSimulator::canHoldFluid(Block block) const {
    const BlockRegistry& registry = blockRegistry();
    const StateInfo& info = registry.state(block);
    if (info.has(kStateLava)) return false;
    if (info.has(kStateWater)) return info.has(kStateLiquid) && info.fluidLevel != 0;  // flowing water is replaced by what flows in
    if (info.has(kStateAir)) return true;
    const std::string& name = registry.type(block).name;
    if (endsWith(name, "_door") || name.find("sign") != std::string::npos || name == "ladder" || name == "sugar_cane" || name == "bubble_column" ||
        name.find("portal") != std::string::npos || name == "end_gateway" || name == "structure_void") {
        return false;
    }
    return collisionShapes().shape(block).empty();
}

// Whether water can enter the block `to` through the face it shares: not through a full block (FlowingFluid.canPassThroughWall, for the
// shapes that matter here).
bool FluidSimulator::canPassThroughWall(Block to) const { return !blockRegistry().state(to).has(kStateFullCollision); }

bool FluidSimulator::canPassThrough(const Level&, const Pos&, Block nb) const {
    const Water w = waterAt(nb);
    return !(w.water && w.source) && canPassThroughWall(nb) && canHoldFluid(nb);
}

int FluidSimulator::sourceNeighbours(const Level& level, const Pos& p) const {
    int count = 0;
    for (const auto& d : kHorizontal) {
        const Water w = waterAt(blockAt(level, {p.x + d[0], p.y, p.z + d[1]}));
        if (w.water && w.source) ++count;
    }
    return count;
}

// FlowingFluid.getNewLiquid: what the water at p should be, from what is round it.
FluidSimulator::Water FluidSimulator::newLiquid(const Level& level, const Pos& p, Block) const {
    int highest = 0, sources = 0;
    for (const auto& d : kHorizontal) {
        const Block nb = blockAt(level, {p.x + d[0], p.y, p.z + d[1]});
        const Water w = waterAt(nb);
        if (w.water && canPassThroughWall(nb)) {
            if (w.source) ++sources;
            highest = std::max(highest, w.amount);
        }
    }
    if (sources >= 2) {  // two sources side by side make a third: the infinite pool (over something solid, or another source)
        const Block below = blockAt(level, {p.x, p.y - 1, p.z});
        const StateInfo& bi = blockRegistry().state(below);
        const Water bw = waterAt(below);
        if (bi.has(kStateFullCollision) || bi.has(kStateOpaqueCube) || (bw.water && bw.source)) return {true, true, false, 8};
    }
    const Block above = blockAt(level, {p.x, p.y + 1, p.z});
    if (waterAt(above).water && canPassThroughWall(above)) return {true, false, true, 8};  // water above: this is a waterfall
    const int amount = highest - 1;
    if (amount <= 0) return {};
    return {true, false, false, amount};
}

// A hole the water would pour into: the block below it is water already, or can hold water.
bool FluidSimulator::isWaterHole(const Level&, const Pos&, Block belowBlock, const Pos&) const {
    if (!canPassThroughWall(belowBlock)) return false;
    return waterAt(belowBlock).water ? true : canHoldFluid(belowBlock);
}

// FlowingFluid.getSlopeDistance: how many blocks sideways (searching up to 4) until the water could fall.
int FluidSimulator::slopeDistance(const Level& level, const Pos& p, int distance, int from, Block) const {
    int best = kNoSlope;
    for (int d = 0; d < 4; ++d) {
        if (d == from) continue;
        const Pos np{p.x + kHorizontal[d][0], p.y, p.z + kHorizontal[d][1]};
        const Block nb = blockAt(level, np);
        if (!canPassThrough(level, np, nb)) continue;
        const Pos below{np.x, np.y - 1, np.z};
        if (isWaterHole(level, below, blockAt(level, below), np)) return distance;
        if (distance < kSlopeFindDistance) best = std::min(best, slopeDistance(level, np, distance + 1, opposite(d), nb));
    }
    return best;
}

// FlowingFluid.getSpread: the sides water spreads to - the ones with the shortest way to a drop (all of them, when none has one).
std::array<std::pair<bool, FluidSimulator::Water>, 4> FluidSimulator::getSpread(const Level& level, const Pos& p, Block) const {
    int best = kNoSlope;
    std::array<std::pair<bool, Water>, 4> result{};
    for (int d = 0; d < 4; ++d) {
        const Pos np{p.x + kHorizontal[d][0], p.y, p.z + kHorizontal[d][1]};
        const Block nb = blockAt(level, np);
        if (!canPassThrough(level, np, nb)) continue;
        const Water fluid = newLiquid(level, np, nb);
        if (!fluid.water) continue;
        const Pos below{np.x, np.y - 1, np.z};
        const int slope = isWaterHole(level, below, blockAt(level, below), np) ? 0 : slopeDistance(level, np, 1, opposite(d), nb);
        if (slope < best) result = {};
        if (slope <= best) {
            result[static_cast<std::size_t>(d)] = {true, fluid};
            best = slope;
        }
    }
    return result;
}

void FluidSimulator::spreadTo(const Level& level, const SetBlock& setBlock, const Pos& p, const Water& water, int& changes) {
    const Block wanted = waterBlock(water);
    if (blockAt(level, p) == wanted) return;  // already so: setting the same state changes nothing (and wakes nothing)
    if (!setBlock(p.x, p.y, p.z, wanted)) return;
    ++changes;
    schedule(p, kWaterDelay);  // the new water looks at its own surroundings in turn
    neighboursChanged(level, p);
}

void FluidSimulator::spreadToSides(const Level& level, const SetBlock& setBlock, const Pos& p, const Water& water, Block below, int& changes) {
    const int newAmount = water.falling ? 7 : water.amount - 1;  // WaterFluid.getDropOff is 1; falling water spreads at 7
    if (newAmount <= 0) return;
    const auto spreads = getSpread(level, p, below);
    for (int d = 0; d < 4; ++d) {
        if (!spreads[static_cast<std::size_t>(d)].first) continue;
        spreadTo(level, setBlock, {p.x + kHorizontal[d][0], p.y, p.z + kHorizontal[d][1]}, spreads[static_cast<std::size_t>(d)].second, changes);
    }
}

// FlowingFluid.spread: down if it can, else sideways.
void FluidSimulator::spread(const Level& level, const SetBlock& setBlock, const Pos& p, const Water& water, int& changes) {
    const Pos below{p.x, p.y - 1, p.z};
    const Block belowBlock = blockAt(level, below);
    const Water belowWater = waterAt(belowBlock);
    if (!(belowWater.water && belowWater.source) && canPassThroughWall(belowBlock) && canHoldFluid(belowBlock)) {
        const Water newBelow = newLiquid(level, below, belowBlock);
        if (!belowWater.water && newBelow.water) {  // (water is never replaced by water coming down on it)
            spreadTo(level, setBlock, below, newBelow, changes);
            if (sourceNeighbours(level, p) >= 3) spreadToSides(level, setBlock, p, water, belowBlock, changes);
            return;
        }
    }
    if (water.source || !isWaterHole(level, below, belowBlock, p)) spreadToSides(level, setBlock, p, water, belowBlock, changes);
}

// FlowingFluid.tick: a flowing block works out what it should be now and changes into it (or dries up); then it spreads.
void FluidSimulator::update(const Level& level, const SetBlock& setBlock, const Pos& p, int& changes) {
    const Block block = blockAt(level, p);
    Water water = waterAt(block);
    if (!water.water) return;
    const StateInfo& info = blockRegistry().state(block);
    if (!info.has(kStateLiquid)) return;  // a waterlogged block or kelp: a source that has nothing to do
    if (!water.source) {
        const Water next = newLiquid(level, p, block);
        if (!next.water) {  // nothing feeds it any more
            if (setBlock(p.x, p.y, p.z, commonBlocks().air)) {
                ++changes;
                neighboursChanged(level, p);
            }
            return;
        }
        if (!(next == water)) {
            if (setBlock(p.x, p.y, p.z, waterBlock(next))) {
                ++changes;
                schedule(p, kWaterDelay);
                neighboursChanged(level, p);
            }
            water = next;
        }
    }
    spread(level, setBlock, p, water, changes);
}

}  // namespace falcon
