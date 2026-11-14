#include "falcon/world/raycast.hpp"

#include "falcon/world/block_outlines.hpp"
#include "falcon/world/block_position.hpp"

#include <cmath>
#include <limits>

namespace falcon {

namespace {

// Where a ray first enters a box (world space), if it does within `reach`: the distance, and the axis of the face it comes
// through. A ray that starts inside the box does not hit it (nothing is hit inside a block you are in).
bool rayEntersBox(const std::array<double, 3>& origin, const std::array<double, 3>& direction, const double (&lo)[3], const double (&hi)[3],
                  double reach, double& distance, int& axis) {
    double tNear = -std::numeric_limits<double>::infinity();
    double tFar = std::numeric_limits<double>::infinity();
    int entering = -1;
    for (int a = 0; a < 3; ++a) {
        const auto i = static_cast<std::size_t>(a);
        const double d = direction[i];
        if (d == 0.0) {
            if (origin[i] < lo[a] || origin[i] > hi[a]) return false;  // parallel to the slab and outside it
            continue;
        }
        double t1 = (lo[a] - origin[i]) / d;
        double t2 = (hi[a] - origin[i]) / d;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tNear) {
            tNear = t1;
            entering = a;
        }
        tFar = std::min(tFar, t2);
        if (tNear > tFar) return false;
    }
    if (entering < 0 || tNear < 0.0 || tNear > reach) return false;
    distance = tNear;
    axis = entering;
    return true;
}

}  // namespace

std::optional<BlockHit> raycastBlocks(const Level& level, const std::array<double, 3>& origin, const std::array<double, 3>& direction,
                                      double reach) {
    int cell[3];
    int step[3];
    double tMax[3];
    double tDelta[3];
    for (int a = 0; a < 3; ++a) {
        cell[a] = static_cast<int>(std::floor(origin[static_cast<std::size_t>(a)]));
        const double d = direction[static_cast<std::size_t>(a)];
        step[a] = d > 0 ? 1 : d < 0 ? -1 : 0;
        if (step[a] == 0) {
            tMax[a] = std::numeric_limits<double>::infinity();
            tDelta[a] = std::numeric_limits<double>::infinity();
        } else {
            const double boundary = step[a] > 0 ? cell[a] + 1 : cell[a];
            tMax[a] = (boundary - origin[static_cast<std::size_t>(a)]) / d;
            tDelta[a] = std::abs(1.0 / d);
        }
    }
    double travelled = 0.0;
    while (travelled <= reach) {
        int axis = 0;
        if (tMax[1] < tMax[axis]) axis = 1;
        if (tMax[2] < tMax[axis]) axis = 2;
        travelled = tMax[axis];
        if (travelled > reach) break;
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
        if (cell[1] < kMinY || cell[1] >= kMaxY) {
            if ((cell[1] < kMinY && step[1] < 0) || (cell[1] >= kMaxY && step[1] > 0)) break;  // left the world
            continue;
        }
        const Block block = level.block(cell[0], cell[1], cell[2]);
        const StateInfo& info = blockRegistry().state(block);
        if (info.has(kStateAir) || info.has(kStateLiquid)) continue;
        // Aim at the block's outline, not its cell: a torch is a small post, a slab half a block, a fence its posts and rails. A ray
        // that passes through the empty part of the cell goes on to whatever is behind.
        const OutlineShape& outline = blockOutlines().shape(block);
        double dx = 0.0, dz = 0.0;
        if (info.has(kStateHasOffset)) {  // flowers and grass are drawn shifted, and so is what you aim at
            const std::array<double, 2> offset = blockOffset(cell[0], cell[2]);
            dx = offset[0];
            dz = offset[1];
        }
        double best = std::numeric_limits<double>::infinity();
        int bestAxis = -1;
        for (const Aabb& box : outline.boxes) {
            const double lo[3] = {cell[0] + box.minX + dx, cell[1] + box.minY, cell[2] + box.minZ + dz};
            const double hi[3] = {cell[0] + box.maxX + dx, cell[1] + box.maxY, cell[2] + box.maxZ + dz};
            double t = 0.0;
            int a = -1;
            if (rayEntersBox(origin, direction, lo, hi, reach, t, a) && t < best) {
                best = t;
                bestAxis = a;
            }
        }
        if (bestAxis < 0) continue;
        // The face of the box the ray comes through: moving +x enters through the -x face, and so on.
        const auto i = static_cast<std::size_t>(bestAxis);
        const bool positive = direction[i] > 0.0;
        const int face = bestAxis == 0 ? (positive ? kWest : kEast) : bestAxis == 1 ? (positive ? kDown : kUp) : (positive ? kNorth : kSouth);
        return BlockHit{cell[0], cell[1], cell[2], face, best};
    }
    return std::nullopt;
}

}  // namespace falcon
