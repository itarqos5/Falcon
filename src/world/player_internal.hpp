#pragma once

// What the parts of the player's physics share (player*.cpp): the game's Mth functions, the traits of blocks that movement looks at, the size of the body
// in each pose and the walk over the shapes a box may touch.
#include "falcon/world/player.hpp"

#include "falcon/world/fluids.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace falcon {
namespace player_detail {

inline constexpr float kDegToRad = 0.017453292f;

inline int mthFloor(double v) {
    const int i = static_cast<int>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}
inline int mthCeil(double v) {
    const int i = static_cast<int>(v);
    return v > static_cast<double>(i) ? i + 1 : i;
}
inline bool mthEqual(double a, double b) { return std::abs(b - a) < 9.999999747378752E-6; }
inline double signum(double v) { return v > 0.0 ? 1.0 : v < 0.0 ? -1.0 : 0.0; }

// Mth.sin / Mth.cos: a 65536-entry float table, indexed by the angle in 1/10430.378 radians.
struct SinTable {
    std::array<float, 65536> value;
    SinTable() {
        for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<float>(std::sin(static_cast<double>(i) / 10430.378350470453));
    }
};
inline const SinTable& sinTable() {
    static const SinTable table;
    return table;
}
inline float mthSin(double angle) {
    return sinTable().value[static_cast<std::uint32_t>(static_cast<std::int64_t>(angle * 10430.378350470453)) & 65535u];
}
inline float mthCos(double angle) {
    return sinTable().value[static_cast<std::uint32_t>(static_cast<std::int64_t>(angle * 10430.378350470453 + 16384.0)) & 65535u];
}

// LivingEntity.computeModifiedFriction.
inline float computeModifiedFriction(float friction, float modifier) { return std::clamp(1.0f - (1.0f - friction) * modifier, 0.0f, 1.0f); }

// A block's special behaviour in movement, found by name once per registry (the game uses block tags and classes).
enum Trait : std::uint32_t {
    kClimbable = 1u << 0,
    kScaffolding = 1u << 1,
    kTrapdoor = 1u << 2,
    kLadder = 1u << 3,
    kCobweb = 1u << 4,
    kSweetBerry = 1u << 5,
    kPowderSnow = 1u << 6,
    kBubbleColumn = 1u << 7,
    kWaterBlock = 1u << 8,
    kHay = 1u << 9,
    kHoney = 1u << 10,
    kSlime = 1u << 11,
    kFence = 1u << 12,
    kWall = 1u << 13,
    kFenceGate = 1u << 14,
};

inline bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

inline std::uint32_t traitsOf(const BlockRegistry& registry, Block b) {
    struct Cache {
        const BlockRegistry* registry = nullptr;
        std::size_t blocks = 0;
        std::vector<std::uint32_t> flags;
    };
    static Cache cache;
    if (cache.registry != &registry || cache.blocks != registry.blocks().size()) {
        cache.registry = &registry;
        cache.blocks = registry.blocks().size();
        cache.flags.assign(cache.blocks, 0);
        for (std::size_t i = 0; i < cache.blocks; ++i) {
            const std::string& n = registry.blocks()[i].name;
            std::uint32_t f = 0;
            if (n == "ladder" || n == "vine" || n == "scaffolding" || n == "weeping_vines" || n == "weeping_vines_plant" || n == "twisting_vines" ||
                n == "twisting_vines_plant" || n == "cave_vines" || n == "cave_vines_plant")
                f |= kClimbable;
            if (n == "scaffolding") f |= kScaffolding;
            if (endsWith(n, "_trapdoor")) f |= kTrapdoor;
            if (n == "ladder") f |= kLadder;
            if (n == "cobweb") f |= kCobweb;
            if (n == "sweet_berry_bush") f |= kSweetBerry;
            if (n == "powder_snow") f |= kPowderSnow;
            if (n == "bubble_column") f |= kBubbleColumn;
            if (n == "water") f |= kWaterBlock;
            if (n == "hay_block") f |= kHay;
            if (n == "honey_block") f |= kHoney;
            if (n == "slime_block") f |= kSlime;
            if (endsWith(n, "_fence")) f |= kFence;
            if (endsWith(n, "_wall")) f |= kWall;
            if (endsWith(n, "_fence_gate")) f |= kFenceGate;
            cache.flags[i] = f;
        }
    }
    return cache.flags[registry.state(b).block];
}

struct Dims {
    float width, height, eye;
};
inline Dims dimensionsOf(PlayerPose pose) {
    switch (pose) {
        case PlayerPose::Crouching: return {0.6f, 1.5f, 1.27f};
        case PlayerPose::Swimming: return {0.6f, 0.6f, 0.4f};
        case PlayerPose::Standing: break;
    }
    return {0.6f, 1.8f, 1.62f};
}

// EntityDimensions.makeBoundingBox: half the (float) width either side, the (float) height up.
inline Aabb makeBox(double px, double py, double pz, const Dims& d) {
    const double half = static_cast<double>(d.width / 2.0f);
    return {px - half, py, pz - half, px + half, py + static_cast<double>(d.height), pz + half};
}

// Every block shape the box may touch (BlockCollisions): the box grown by a block on every side, but the outer shell
// only counts for shapes that stick out of their block (fences). A chunk that is not loaded is a solid cube, so
// nobody falls through the world while it is arriving. `fn` returns true to stop; the result says whether it did.
template <typename Collider, typename Fn>
inline bool forEachCollider(const Level& level, const Aabb& box, Fn&& fn) {
    const CollisionShapes& shapes = collisionShapes();
    const int x0 = mthFloor(box.minX - 1.0E-7) - 1;
    const int y0 = mthFloor(box.minY - 1.0E-7) - 1;
    const int z0 = mthFloor(box.minZ - 1.0E-7) - 1;
    const int x1 = mthFloor(box.maxX + 1.0E-7) + 1;
    const int y1 = mthFloor(box.maxY + 1.0E-7) + 1;
    const int z1 = mthFloor(box.maxZ + 1.0E-7) + 1;
    for (int bz = z0; bz <= z1; ++bz) {
        for (int by = y0; by <= y1; ++by) {
            for (int bx = x0; bx <= x1; ++bx) {
                const int type = (bx == x0 || bx == x1 ? 1 : 0) + (by == y0 || by == y1 ? 1 : 0) + (bz == z0 || bz == z1 ? 1 : 0);
                if (type == 3) continue;
                VoxelShape shape = shapes.shape(level.block(bx, by, bz));
                if (shape.empty()) {
                    if (type != 0 || by < kMinY || by >= kMaxY || level.isLoaded(bx, bz)) continue;
                    shape = shapes.unitCube();
                } else if (type == 2 || (type == 1 && !shape.isLarge())) {
                    continue;
                }
                if (shape.isUnitCube()) {
                    if (!box.intersects({static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz), static_cast<double>(bx) + 1.0,
                                         static_cast<double>(by) + 1.0, static_cast<double>(bz) + 1.0}))
                        continue;
                } else if (!shape.intersects(box, bx, by, bz)) {
                    continue;
                }
                if (fn(Collider{shape, bx, by, bz})) return true;
            }
        }
    }
    return false;
}

}  // namespace player_detail
using namespace player_detail;
}  // namespace falcon
