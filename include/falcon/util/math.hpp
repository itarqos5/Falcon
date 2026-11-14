#pragma once

#include <array>

namespace falcon {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;

[[nodiscard]] inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Mth.clampedMap: map `value` from [inMin, inMax] to [outMin, outMax], clamped.
[[nodiscard]] float clampedMap(float value, float inMin, float inMax, float outMin, float outMax);

// Mth.wrapDegrees: wrap an angle into [-180, 180).
[[nodiscard]] float wrapDegrees(float degrees);

// 4x4 matrix, column-major (OpenGL layout).
struct Mat4 {
    std::array<float, 16> m{};

    [[nodiscard]] static Mat4 identity();
    [[nodiscard]] static Mat4 perspective(float fovyRadians, float aspect, float zNear, float zFar);
    [[nodiscard]] static Mat4 ortho(float left, float right, float bottom, float top, float zNear,
                                    float zFar);
    [[nodiscard]] static Mat4 rotationX(float radians);
    [[nodiscard]] static Mat4 rotationY(float radians);
    [[nodiscard]] Mat4 operator*(const Mat4& rhs) const;
};

// 2D affine transform  [a c e; b d f]  with post-multiplying translate/rotate/scale,
// i.e. the same composition order as a matrix-stack "translate(); rotate(); scale();".
struct Pose2D {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

    Pose2D& translate(float x, float y);
    Pose2D& rotate(float radians);
    Pose2D& scale(float s);
    void apply(float x, float y, float& outX, float& outY) const;
};

}  // namespace falcon
