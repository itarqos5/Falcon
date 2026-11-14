#pragma once

// The few java.lang.Math / Mth behaviours the terrain code depends on, written out so they behave the same in C++
// (NaN handling, signed zeros, saturating double -> int conversion).

#include <climits>
#include <cmath>
#include <cstdint>

namespace falcon::worldgen {

// (int)d with Java's saturating d2i: NaN -> 0, >= 2^31 -> INT_MAX, <= -2^31 -> INT_MIN.
[[nodiscard]] inline std::int32_t javaD2I(double d) {
    if (d > -2147483648.0 && d < 2147483647.0) return static_cast<std::int32_t>(d);
    if (d != d) return 0;
    return d > 0.0 ? INT_MAX : INT_MIN;
}

// Mth.floor(double) = (int)Math.floor(d)
[[nodiscard]] inline std::int32_t javaFloor(double d) {
    // In range, floor(d) is the truncation corrected for negative fractions (no libm call); the rest saturates like d2i.
    if (d > -2147483648.0 && d < 2147483647.0) {
        const auto i = static_cast<std::int32_t>(d);
        return i - (d < static_cast<double>(i) ? 1 : 0);
    }
    return javaD2I(std::floor(d));
}
// Mth.floor(float) = (int)Math.floor((double)f)
[[nodiscard]] inline std::int32_t javaFloorF(float f) { return javaD2I(std::floor(static_cast<double>(f))); }

// Math.min / Math.max (float): NaN propagates, min(-0, +0) = -0, max(-0, +0) = +0.
[[nodiscard]] inline float jmin(float a, float b) {
    if (a < b) return a;
    if (b < a) return b;
    if (a != a) return a;
    if (b != b) return b;
    if (a == 0.0f) return std::signbit(a) ? a : b;  // both zero: the negative one wins
    return a;
}
[[nodiscard]] inline float jmax(float a, float b) {
    if (a > b) return a;
    if (b > a) return b;
    if (a != a) return a;
    if (b != b) return b;
    if (a == 0.0f) return std::signbit(a) ? b : a;  // both zero: the positive one wins
    return a;
}

// Mth.clamp(float, lo, hi) = (x < lo) ? lo : Math.min(x, hi)
[[nodiscard]] inline float clampf(float x, float lo, float hi) { return x < lo ? lo : jmin(x, hi); }

// Mth.lerp(float t, float a, float b) = a + t * (b - a), every operation rounded to float (build with -ffp-contract=off).
[[nodiscard]] inline float lerpf(float t, float a, float b) { return a + t * (b - a); }

[[nodiscard]] inline std::int32_t floorDiv(std::int32_t a, std::int32_t b) {
    std::int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
[[nodiscard]] inline std::int32_t floorMod(std::int32_t a, std::int32_t b) { return a - floorDiv(a, b) * b; }

}  // namespace falcon::worldgen
