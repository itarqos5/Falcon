#pragma once

#include <array>
#include <cmath>

namespace apex {

// Column-major 4x4 matrix, the layout OpenGL wants.
struct Matrix4f {
    std::array<float, 16> m{};
    [[nodiscard]] static Matrix4f identity() {
        Matrix4f r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    [[nodiscard]] Matrix4f operator*(const Matrix4f& b) const {
        Matrix4f r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[static_cast<std::size_t>(k * 4 + row)] * b.m[static_cast<std::size_t>(col * 4 + k)];
                r.m[static_cast<std::size_t>(col * 4 + row)] = sum;
            }
        }
        return r;
    }
};

// Which way the player faces. Yaw 0 looks south (+Z) and grows clockwise seen from above (90 looks west, -X);
// pitch is positive when looking down (-90 straight up, 90 straight down): the game's own convention.
struct Look {
    float yaw = 0.0f;    // degrees
    float pitch = 0.0f;  // degrees
    [[nodiscard]] std::array<double, 3> forward() const {
        const double y = static_cast<double>(yaw) * 0.017453292519943295;
        const double p = static_cast<double>(pitch) * 0.017453292519943295;
        return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
    }
};

// A planar view frustum: a point is inside when it is on the positive side of all six planes.
struct Frustum {
    std::array<std::array<float, 4>, 6> planes{};  // a, b, c, d with a*x + b*y + c*z + d >= 0 inside
    // False only if the box (min..max) is completely outside one plane, so it cannot be seen.
    [[nodiscard]] bool mayContain(const std::array<float, 3>& lo, const std::array<float, 3>& hi) const {
        for (const auto& p : planes) {
            const float x = p[0] >= 0 ? hi[0] : lo[0];
            const float y = p[1] >= 0 ? hi[1] : lo[1];
            const float z = p[2] >= 0 ? hi[2] : lo[2];
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f) return false;
        }
        return true;
    }
};

// Perspective projection (vertical field of view in degrees), OpenGL clip space.
[[nodiscard]] inline Matrix4f perspective(float fovDegrees, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / std::tan(fovDegrees * 0.5f * 0.017453292f);
    Matrix4f r;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
    return r;
}

// The rotation that turns world directions into view space (no movement: the renderer draws everything relative
// to the camera, which keeps the numbers small far from the origin).
[[nodiscard]] inline Matrix4f viewRotation(const Look& look) {
    const auto f = look.forward();
    // right = forward x up(0,1,0)
    double rx = -f[2];
    double rz = f[0];
    const double rl = std::sqrt(rx * rx + rz * rz);
    if (rl < 1e-9) {  // looking straight up or down: keep the yaw's right
        const double y = static_cast<double>(look.yaw) * 0.017453292519943295;
        rx = -std::cos(y);
        rz = -std::sin(y);
    } else {
        rx /= rl;
        rz /= rl;
    }
    // true up = right x forward
    const double ux = 0 * f[2] - rz * f[1];
    const double uy = rz * f[0] - rx * f[2];
    const double uz = rx * f[1] - 0 * f[0];
    Matrix4f v = Matrix4f::identity();
    v.m[0] = static_cast<float>(rx);  v.m[4] = 0.0f;                    v.m[8] = static_cast<float>(rz);
    v.m[1] = static_cast<float>(ux);  v.m[5] = static_cast<float>(uy);  v.m[9] = static_cast<float>(uz);
    v.m[2] = static_cast<float>(-f[0]); v.m[6] = static_cast<float>(-f[1]); v.m[10] = static_cast<float>(-f[2]);
    return v;
}

// The game's view bobbing (GameRenderer.bobView): a shift in view space, a roll about the view direction and a tilt about
// the horizontal axis, applied before the camera's own rotation. Angles in degrees.
[[nodiscard]] inline Matrix4f bobTransform(float x, float y, float rollDegrees, float pitchDegrees) {
    const float roll = rollDegrees * 0.017453292f;
    const float tilt = pitchDegrees * 0.017453292f;
    Matrix4f shift = Matrix4f::identity();
    shift.m[12] = x;
    shift.m[13] = y;
    Matrix4f rz = Matrix4f::identity();
    rz.m[0] = std::cos(roll);
    rz.m[1] = std::sin(roll);
    rz.m[4] = -std::sin(roll);
    rz.m[5] = std::cos(roll);
    Matrix4f rx = Matrix4f::identity();
    rx.m[5] = std::cos(tilt);
    rx.m[6] = std::sin(tilt);
    rx.m[9] = -std::sin(tilt);
    rx.m[10] = std::cos(tilt);
    return shift * rz * rx;
}

// The planes of the view volume of `viewProjection`, in the space the matrix takes points from.
[[nodiscard]] inline Frustum extractFrustum(const Matrix4f& vp) {
    const auto row = [&](int i) { return std::array<float, 4>{vp.m[static_cast<std::size_t>(i)], vp.m[static_cast<std::size_t>(4 + i)], vp.m[static_cast<std::size_t>(8 + i)], vp.m[static_cast<std::size_t>(12 + i)]}; };
    const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Frustum f;
    for (int k = 0; k < 4; ++k) {
        f.planes[0][static_cast<std::size_t>(k)] = r3[static_cast<std::size_t>(k)] + r0[static_cast<std::size_t>(k)];  // left
        f.planes[1][static_cast<std::size_t>(k)] = r3[static_cast<std::size_t>(k)] - r0[static_cast<std::size_t>(k)];  // right
        f.planes[2][static_cast<std::size_t>(k)] = r3[static_cast<std::size_t>(k)] + r1[static_cast<std::size_t>(k)];  // bottom
        f.planes[3][static_cast<std::size_t>(k)] = r3[static_cast<std::size_t>(k)] - r1[static_cast<std::size_t>(k)];  // top
        f.planes[4][static_cast<std::size_t>(k)] = r3[static_cast<std::size_t>(k)] + r2[static_cast<std::size_t>(k)];  // near
        f.planes[5][static_cast<std::size_t>(k)] = r3[static_cast<std::size_t>(k)] - r2[static_cast<std::size_t>(k)];  // far
    }
    return f;
}

}  // namespace apex
