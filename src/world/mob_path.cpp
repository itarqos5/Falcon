// Falcon's own path finding for walking mobs: A* over the cells a mob can stand in. The game's PathNavigation looks at much more (node types, door and fence rules, penalties by
// biome); this keeps what makes a mob look sensible: it walks round walls and water, steps up one block, drops a few and never walks into lava or fire.
#include "falcon/world/mob.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace falcon {
namespace {

// Hazard flags by block, found once: lava, fire, magma, cactus, berry bushes, campfires, wither roses.
const std::vector<std::uint8_t>& hazardTable() {
    static const std::vector<std::uint8_t> table = [] {
        const BlockRegistry& registry = blockRegistry();
        std::vector<std::uint8_t> t(registry.stateCount(), 0);
        for (std::size_t i = 0; i < t.size(); ++i) {
            const StateInfo& info = registry.state(static_cast<Block>(i));
            const std::string& name = registry.blocks()[info.block].name;
            t[i] = info.has(kStateLava) || name == "fire" || name == "soul_fire" || name == "magma_block" || name == "cactus" || name == "sweet_berry_bush" || name == "campfire" ||
                   name == "soul_campfire" || name == "wither_rose" || name == "powder_snow" || name == "cobweb";
        }
        return t;
    }();
    return table;
}

struct Key {
    int x, y, z;
    bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct KeyHash {
    std::size_t operator()(const Key& k) const {
        return static_cast<std::size_t>(k.x) * 73856093u ^ static_cast<std::size_t>(k.y) * 19349663u ^ static_cast<std::size_t>(k.z) * 83492791u;
    }
};
struct Node {
    Key key;
    float g = 0.0f, f = 0.0f;
    Key parent{0, 0, 0};
    bool hasParent = false;
};

}  // namespace

bool mobBlocks(Block b) {
    if (b == Block::Air) return false;
    const StateInfo& s = blockRegistry().state(b);
    return s.has(kStateFullCollision) || s.has(kStateOpaqueCube);
}

bool mobHazard(Block b) {
    if (b == Block::Air) return false;
    const auto& table = hazardTable();
    const std::size_t i = static_cast<std::size_t>(stateId(b));
    return i < table.size() && table[i] != 0;
}

namespace {

// Whether a mob of `rows` cells of height can have its feet in the cell (x, y, z): the cells it fills are open and none is dangerous.
bool roomFor(const Level& level, int x, int y, int z, int rows, bool canSwim) {
    for (int i = 0; i < rows; ++i) {
        const Block b = level.block(x, y + i, z);
        if (mobBlocks(b) || mobHazard(b)) return false;
        if (!canSwim && i == 0 && blockRegistry().state(b).has(kStateWater) && blockRegistry().state(level.block(x, y + 1, z)).has(kStateWater)) return false;  // deep water
    }
    return true;
}

// Ground under the feet: a block that holds a mob up (water does not, unless it can swim).
bool groundBelow(const Level& level, int x, int y, int z, bool canSwim) {
    const Block below = level.block(x, y - 1, z);
    if (mobBlocks(below)) return true;
    const StateInfo& s = blockRegistry().state(below);
    if (s.has(kStateWater)) return canSwim;
    return !s.has(kStateAir) && !s.has(kStateReplaceable) && !s.has(kStateLiquid) && !mobHazard(below);  // slabs, stairs, fences and the like
}

}  // namespace

std::vector<std::array<int, 3>> findMobPath(const Level& level, const std::array<int, 3>& from, const std::array<int, 3>& goal, float width, float height, int maxNodes, bool canSwim) {
    const int rows = std::max(2, static_cast<int>(std::ceil(static_cast<double>(height) - 1.0e-6)));
    (void)width;  // (a mob wider than a block is treated as one block wide: it still has to get through a one-block gap here)
    const auto heuristic = [&](const Key& k) {
        const float dx = static_cast<float>(std::abs(k.x - goal[0])), dy = static_cast<float>(std::abs(k.y - goal[1])), dz = static_cast<float>(std::abs(k.z - goal[2]));
        return (dx + dz + dy) * 1.1f;
    };
    struct Cmp {
        bool operator()(const Node& a, const Node& b) const { return a.f > b.f; }
    };
    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    std::unordered_map<Key, Node, KeyHash> closed;
    std::unordered_map<Key, float, KeyHash> best;
    const Key start{from[0], from[1], from[2]};
    Node first;
    first.key = start;
    first.f = heuristic(start);
    open.push(first);
    best[start] = 0.0f;
    Key nearest = start;
    float nearestH = heuristic(start);
    bool found = false;
    Key end = start;
    int expanded = 0;
    while (!open.empty() && expanded < maxNodes) {
        const Node n = open.top();
        open.pop();
        if (closed.count(n.key) != 0) continue;
        closed[n.key] = n;
        ++expanded;
        const float h = heuristic(n.key);
        if (h < nearestH) {
            nearestH = h;
            nearest = n.key;
        }
        if (std::abs(n.key.x - goal[0]) <= 1 && std::abs(n.key.z - goal[2]) <= 1 && std::abs(n.key.y - goal[1]) <= 1) {
            found = true;
            end = n.key;
            break;
        }
        static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : dirs) {
            const int nx = n.key.x + d[0], nz = n.key.z + d[1];
            if (!level.isLoaded(nx, nz)) continue;
            const auto consider = [&](int ny, float cost) {
                const Key k{nx, ny, nz};
                if (closed.count(k) != 0) return;
                const float g = n.g + cost;
                const auto it = best.find(k);
                if (it != best.end() && it->second <= g) return;
                best[k] = g;
                Node next;
                next.key = k;
                next.g = g;
                next.f = g + heuristic(k);
                next.parent = n.key;
                next.hasParent = true;
                open.push(next);
            };
            // Level: the cell beside is open with ground under it.
            if (roomFor(level, nx, n.key.y, nz, rows, canSwim)) {
                if (groundBelow(level, nx, n.key.y, nz, canSwim)) {
                    const bool wet = blockRegistry().state(level.block(nx, n.key.y, nz)).has(kStateWater);
                    consider(n.key.y, wet ? 3.0f : 1.0f);
                } else {  // nothing to stand on: it would fall, up to three blocks
                    for (int drop = 1; drop <= 3; ++drop) {
                        const int ny = n.key.y - drop;
                        if (!roomFor(level, nx, ny, nz, rows, canSwim)) break;
                        if (groundBelow(level, nx, ny, nz, canSwim)) {
                            consider(ny, 1.0f + static_cast<float>(drop) * 0.6f);
                            break;
                        }
                    }
                }
            } else if (roomFor(level, n.key.x, n.key.y + rows, n.key.z, 1, canSwim) && roomFor(level, nx, n.key.y + 1, nz, rows, canSwim) && groundBelow(level, nx, n.key.y + 1, nz, canSwim)) {
                consider(n.key.y + 1, 1.8f);  // a step up of one block (it jumps)
            }
        }
    }
    if (!found) end = nearest;
    std::vector<std::array<int, 3>> path;
    Key at = end;
    for (int guard = 0; guard < 4096; ++guard) {
        path.push_back({at.x, at.y, at.z});
        const auto it = closed.find(at);
        if (it == closed.end() || !it->second.hasParent) break;
        at = it->second.parent;
    }
    std::reverse(path.begin(), path.end());
    if (!path.empty()) path.erase(path.begin());  // (the cell it stands in)
    return path;
}

}  // namespace falcon
