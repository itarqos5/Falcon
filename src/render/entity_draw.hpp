#pragma once

// What the renderers of models have in common (internal to src/render): the shader, the vertex, the affine maps the model parts are posed with, and the walk down a model's parts
// that turns their boxes into triangles in the game's lighting. The player's body and the mobs both draw with it.
#include "falcon/render/entity_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace falcon::entity_draw {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kDegrees = kPi / 180.0f;

inline constexpr char kVertex[] = R"(#version 330 core
layout(location = 0) in vec3 aPos;   // relative to the camera
layout(location = 1) in vec2 aUv;    // 0..1 on the 64x64 skin
layout(location = 2) in float aShade;
uniform mat4 uProj;
out vec2 vUv;
out float vShade;
void main() {
    gl_Position = uProj * vec4(aPos, 1.0);
    vUv = aUv;
    vShade = aShade;
}
)";

inline constexpr char kFragment[] = R"(#version 330 core
in vec2 vUv;
in float vShade;
uniform sampler2D uSkin;
uniform vec3 uLight;
uniform vec3 uTint;
out vec4 frag;
void main() {
    vec4 c = texture(uSkin, vUv);
    if (c.a < 0.1) discard;
    frag = vec4(c.rgb * uTint * vShade * uLight, 1.0);
}
)";

struct Vertex {
    float x, y, z, u, v, shade;
};

// An affine map p -> M p + t in doubles (the same arithmetic as the inventory preview; mul(a, b) applies b first).
struct V3 {
    double x = 0, y = 0, z = 0;
};
struct Xf {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    V3 t;
};
inline Xf mul(const Xf& a, const Xf& b) {
    Xf r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    }
    r.t = {a.m[0][0] * b.t.x + a.m[0][1] * b.t.y + a.m[0][2] * b.t.z + a.t.x, a.m[1][0] * b.t.x + a.m[1][1] * b.t.y + a.m[1][2] * b.t.z + a.t.y,
           a.m[2][0] * b.t.x + a.m[2][1] * b.t.y + a.m[2][2] * b.t.z + a.t.z};
    return r;
}
inline V3 apply(const Xf& f, V3 p) {
    return {f.m[0][0] * p.x + f.m[0][1] * p.y + f.m[0][2] * p.z + f.t.x, f.m[1][0] * p.x + f.m[1][1] * p.y + f.m[1][2] * p.z + f.t.y,
            f.m[2][0] * p.x + f.m[2][1] * p.y + f.m[2][2] * p.z + f.t.z};
}
inline V3 applyLinear(const Xf& f, V3 p) {
    return {f.m[0][0] * p.x + f.m[0][1] * p.y + f.m[0][2] * p.z, f.m[1][0] * p.x + f.m[1][1] * p.y + f.m[1][2] * p.z, f.m[2][0] * p.x + f.m[2][1] * p.y + f.m[2][2] * p.z};
}
inline Xf translation(V3 t) {
    Xf f;
    f.t = t;
    return f;
}
inline Xf scaling(V3 s) {
    Xf f;
    f.m[0][0] = s.x;
    f.m[1][1] = s.y;
    f.m[2][2] = s.z;
    return f;
}
inline Xf rotX(double a) {
    Xf f;
    const double c = std::cos(a), s = std::sin(a);
    f.m[1][1] = c;
    f.m[1][2] = -s;
    f.m[2][1] = s;
    f.m[2][2] = c;
    return f;
}
inline Xf rotY(double a) {
    Xf f;
    const double c = std::cos(a), s = std::sin(a);
    f.m[0][0] = c;
    f.m[0][2] = s;
    f.m[2][0] = -s;
    f.m[2][2] = c;
    return f;
}
inline Xf rotZ(double a) {
    Xf f;
    const double c = std::cos(a), s = std::sin(a);
    f.m[0][0] = c;
    f.m[0][1] = -s;
    f.m[1][0] = s;
    f.m[1][1] = c;
    return f;
}
// ModelPart.translateAndRotate: the part's offset, then Quaternionf.rotationZYX(z, y, x) (x first), then its scale.
inline Xf partTransform(const EntityPart& p) {
    return mul(translation({p.offset[0] / 16.0, p.offset[1] / 16.0, p.offset[2] / 16.0}),
               mul(rotZ(p.rotation[2]), mul(rotY(p.rotation[1]), mul(rotX(p.rotation[0]), scaling({p.scale[0], p.scale[1], p.scale[2]})))));
}

// Mth.rotLerp for radians (ModelPart: HumanoidModel.rotlerpRad): the short way round.
inline float rotlerp(float a, float from, float to) {
    float f = std::fmod(to - from, 2.0f * kPi);
    if (f < -kPi) f += 2.0f * kPi;
    if (f >= kPi) f -= 2.0f * kPi;
    return from + a * f;
}
inline void collect(const EntityPart& part, const Xf& parent, const Xf& toWorld, const std::array<bool, 7>& parts, std::vector<Vertex>& out) {
    if (part.name == "jacket" && !parts[1]) return;
    if (part.name == "left_sleeve" && !parts[2]) return;
    if (part.name == "right_sleeve" && !parts[3]) return;
    if (part.name == "left_pants" && !parts[4]) return;
    if (part.name == "right_pants" && !parts[5]) return;
    if (part.name == "hat" && !parts[6]) return;
    const Xf here = mul(parent, partTransform(part));
    const Xf full = mul(toWorld, here);
    for (const EntityPolygon& poly : part.polygons) {
        // The game's entity lighting (two lights from above, 0.4 of ambient), with the normal turned like the face, in the world's orientation.
        float shade = 1.0f;
        V3 n = applyLinear(full, {poly.normal[0], poly.normal[1], poly.normal[2]});
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-6) {
            n = {n.x / len, n.y / len, n.z / len};
            constexpr double k0 = 1.0 / 1.42828568, k1 = 1.0 / 1.01980390;
            const double d0 = std::max(0.0, (0.2 * n.x + 1.0 * n.y - 0.7 * n.z) * k0);
            const double d1 = std::max(0.0, (-0.2 * n.x + 1.0 * n.y + 0.7 * n.z) * k1);
            shade = static_cast<float>(std::min(1.0, (d0 + d1) * 0.6 + 0.4));
        }
        Vertex v[4];
        for (std::size_t i = 0; i < 4; ++i) {
            const V3 p = apply(full, {poly.vertex[i][0] / 16.0, poly.vertex[i][1] / 16.0, poly.vertex[i][2] / 16.0});
            v[i] = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), poly.vertex[i][3], poly.vertex[i][4], shade};
        }
        out.insert(out.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
    for (const EntityPart& child : part.children) collect(child, here, toWorld, parts, out);
}

}  // namespace falcon::entity_draw
