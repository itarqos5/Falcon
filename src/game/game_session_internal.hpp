#pragma once

// The small things the parts of GameSession share (game_session*.cpp): the length of a tick, a printf into a string, a little statistics, the light a held item gives.
#include "falcon/game/game_session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace falcon {
namespace session {

inline constexpr double kReach = 5.0;
inline constexpr double kRepeatDelay = 0.25;  // holding break or place repeats this often
inline constexpr int kLoadRadius = 4;         // chunks around the spawn that must be ready before play starts
inline constexpr double kLoadTimeoutSeconds = 30.0;  // the game's client wait timeout (LevelLoadTracker.CLIENT_WAIT_TIMEOUT_MS)
inline constexpr double kTickSeconds = 0.05;  // the game runs at 20 ticks a second
inline constexpr int kMaxRemoteTicksPerFrame = 2;  // the same on a server, where a burst of ticks is what an anti-cheat flags as a fast client
inline constexpr int kMaxTicksPerFrame = 10;  // after a stall the game catches up by at most this many ticks and drops the rest

inline std::string format(const char* pattern, ...) {
    char buffer[512];
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(buffer, sizeof(buffer), pattern, args);
    va_end(args);
    return buffer;
}

// Chunks are made and meshed on this many threads: about half the logical cores (min 3, max 8), so loading and
// terrain fill-in are quick while the main and render threads - and anything else running - still keep cores.
inline unsigned workerCount() { return std::clamp(std::thread::hardware_concurrency() / 2, 3u, 8u); }
// ... and while the world loads (the loading screen: nothing else is going on) all but two of them, at below-normal priority.
inline unsigned loadWorkerCount() { return std::clamp(std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1u, workerCount(), 16u); }

// A body that glides with elytra (AvatarRenderer.setupRotations, HumanoidModel.setupAnim): the body leans over as it picks up speed (in the first 10 ticks), turns toward where it
// is going when that is not where it looks, and the limbs hardly swing at speed. `flown` is how long it has glided (ticks, with the part of this one), the look is in degrees, the
// motion is per tick.
inline void poseGliding(BodyPose& pose, float flown, float lookYaw, float lookPitch, double deltaX, double deltaY, double deltaZ) {
    pose.fallFlying = true;
    pose.glideLean = std::clamp(flown * flown / 100.0f, 0.0f, 1.0f) * (-90.0f - lookPitch);
    const double yawRad = static_cast<double>(lookYaw) * 0.017453292519943295, pitchRad = static_cast<double>(lookPitch) * 0.017453292519943295;
    const double viewX = -std::sin(yawRad) * std::cos(pitchRad), viewZ = std::cos(yawRad) * std::cos(pitchRad);
    const double moving = deltaX * deltaX + deltaZ * deltaZ, looking = viewX * viewX + viewZ * viewZ;
    pose.glideTurn = 0.0f;
    if (moving > 1.0E-5 && looking > 1.0E-5) {
        const double dot = std::clamp((deltaX * viewX + deltaZ * viewZ) / std::sqrt(moving * looking), -1.0, 1.0);
        const double cross = deltaX * viewZ - deltaZ * viewX;
        pose.glideTurn = static_cast<float>((cross > 0.0 ? 1.0 : cross < 0.0 ? -1.0 : 0.0) * std::acos(dot));
    }
    double k = (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) / 0.2;
    k = k * k * k;
    pose.limbScale = static_cast<float>(std::max(1.0, k));
}

inline double megabytes(std::size_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

inline float percentile(std::vector<float> values, double p) {
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(p * static_cast<double>(values.size())))];
}

inline float average(const std::vector<float>& values) {
    return values.empty() ? 0.0f : std::accumulate(values.begin(), values.end(), 0.0f) / static_cast<float>(values.size());
}

inline const char* facing(float yaw) {
    float y = std::fmod(yaw, 360.0f);
    if (y < 0) y += 360.0f;
    static constexpr const char* kNames[4] = {"south (towards +Z)", "west (towards -X)", "north (towards -Z)", "east (towards +X)"};
    return kNames[static_cast<int>((y + 45.0f) / 90.0f) % 4];
}

// The block light (0..15) an item gives off while it is held: the light of the block it places (a torch 14, a lantern 15, glowstone
// 15, a redstone torch 7, ...), 0 for everything else. A block that only glows in some states (a furnace) is judged by its default.
inline int carriedLightLevel(const ItemStack& stack) {
    if (stack.isEmpty()) return 0;
    const ItemType& type = stack.type();
    if (!type.isBlockItem()) return 0;
    const BlockRegistry& registry = blockRegistry();
    const auto& blocks = registry.blocks();
    if (static_cast<std::size_t>(type.blockIndex) >= blocks.size()) return 0;
    return registry.state(static_cast<Block>(blocks[static_cast<std::size_t>(type.blockIndex)].defaultState)).emission;
}

}  // namespace session
using namespace session;
}  // namespace falcon
