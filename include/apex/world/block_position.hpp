#pragma once

#include <array>
#include <cstdint>

namespace apex {

// The game's Mth.getSeed(x, y, z): a number made from a block position, the same everywhere. It picks between alternative
// models of a block (the variants of grass) and shifts plants sideways.
[[nodiscard]] inline std::int64_t positionSeed(int x, int y, int z) {
    std::int64_t l = static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(x) * 3129871u)) ^
                     (static_cast<std::int64_t>(z) * 116129781LL) ^ static_cast<std::int64_t>(y);
    const auto ul = static_cast<std::uint64_t>(l);
    l = static_cast<std::int64_t>(ul * ul * 42317861ULL + ul * 11ULL);
    return l >> 16;
}

// How far a block that is drawn shifted (flowers, grass, ferns: the offset flag of its state) is moved sideways at the block
// position (x, z), up to a quarter of a block either way. Both the picture and the outline you aim at use it, so they agree.
[[nodiscard]] inline std::array<double, 2> blockOffset(int x, int z) {
    const std::int64_t seed = positionSeed(x, 0, z);
    const auto clamp = [](double v) { return v < -0.25 ? -0.25 : v > 0.25 ? 0.25 : v; };
    return {clamp(((seed & 15) / 15.0 - 0.5) * 0.5), clamp((((seed >> 8) & 15) / 15.0 - 0.5) * 0.5)};
}

}  // namespace apex
