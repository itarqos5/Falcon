#include "falcon/util/math.hpp"

#include <cmath>

namespace falcon {

float clampedMap(float value, float inMin, float inMax, float outMin, float outMax) {
    const float t = clampf((value - inMin) / (inMax - inMin), 0.0f, 1.0f);
    return outMin + t * (outMax - outMin);
}

float wrapDegrees(float degrees) {
    float f = std::fmod(degrees, 360.0f);
    if (f >= 180.0f) f -= 360.0f;
    if (f < -180.0f) f += 360.0f;
    return f;
}

Mat4 Mat4::identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::perspective(float fovyRadians, float aspect, float zNear, float zFar) {
    Mat4 r;
    const float f = 1.0f / std::tan(fovyRadians * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

Mat4 Mat4::ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
    Mat4 r;
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (zFar - zNear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zFar + zNear) / (zFar - zNear);
    r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::rotationX(float radians) {
    Mat4 r = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

Mat4 Mat4::rotationY(float radians) {
    Mat4 r = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

Mat4 Mat4::operator*(const Mat4& rhs) const {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[static_cast<std::size_t>(k * 4 + row)] *
                       rhs.m[static_cast<std::size_t>(col * 4 + k)];
            }
            r.m[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return r;
}

Pose2D& Pose2D::translate(float x, float y) {
    e += a * x + c * y;
    f += b * x + d * y;
    return *this;
}

Pose2D& Pose2D::rotate(float radians) {
    const float cs = std::cos(radians);
    const float sn = std::sin(radians);
    const float na = a * cs + c * sn;
    const float nb = b * cs + d * sn;
    const float nc = a * -sn + c * cs;
    const float nd = b * -sn + d * cs;
    a = na;
    b = nb;
    c = nc;
    d = nd;
    return *this;
}

Pose2D& Pose2D::scale(float s) {
    a *= s;
    b *= s;
    c *= s;
    d *= s;
    return *this;
}

void Pose2D::apply(float x, float y, float& outX, float& outY) const {
    outX = a * x + c * y + e;
    outY = b * x + d * y + f;
}

}  // namespace falcon
