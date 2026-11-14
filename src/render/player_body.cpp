#include "falcon/render/player_body.hpp"

#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_manager.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace falcon {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegrees = kPi / 180.0f;

constexpr char kVertex[] = R"(#version 330 core
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

constexpr char kFragment[] = R"(#version 330 core
in vec2 vUv;
in float vShade;
uniform sampler2D uSkin;
uniform vec3 uLight;
out vec4 frag;
void main() {
    vec4 c = texture(uSkin, vUv);
    if (c.a < 0.1) discard;
    frag = vec4(c.rgb * vShade * uLight, 1.0);
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
Xf mul(const Xf& a, const Xf& b) {
    Xf r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    }
    r.t = {a.m[0][0] * b.t.x + a.m[0][1] * b.t.y + a.m[0][2] * b.t.z + a.t.x, a.m[1][0] * b.t.x + a.m[1][1] * b.t.y + a.m[1][2] * b.t.z + a.t.y,
           a.m[2][0] * b.t.x + a.m[2][1] * b.t.y + a.m[2][2] * b.t.z + a.t.z};
    return r;
}
V3 apply(const Xf& f, V3 p) {
    return {f.m[0][0] * p.x + f.m[0][1] * p.y + f.m[0][2] * p.z + f.t.x, f.m[1][0] * p.x + f.m[1][1] * p.y + f.m[1][2] * p.z + f.t.y,
            f.m[2][0] * p.x + f.m[2][1] * p.y + f.m[2][2] * p.z + f.t.z};
}
V3 applyLinear(const Xf& f, V3 p) {
    return {f.m[0][0] * p.x + f.m[0][1] * p.y + f.m[0][2] * p.z, f.m[1][0] * p.x + f.m[1][1] * p.y + f.m[1][2] * p.z, f.m[2][0] * p.x + f.m[2][1] * p.y + f.m[2][2] * p.z};
}
Xf translation(V3 t) {
    Xf f;
    f.t = t;
    return f;
}
Xf scaling(V3 s) {
    Xf f;
    f.m[0][0] = s.x;
    f.m[1][1] = s.y;
    f.m[2][2] = s.z;
    return f;
}
Xf rotX(double a) {
    Xf f;
    const double c = std::cos(a), s = std::sin(a);
    f.m[1][1] = c;
    f.m[1][2] = -s;
    f.m[2][1] = s;
    f.m[2][2] = c;
    return f;
}
Xf rotY(double a) {
    Xf f;
    const double c = std::cos(a), s = std::sin(a);
    f.m[0][0] = c;
    f.m[0][2] = s;
    f.m[2][0] = -s;
    f.m[2][2] = c;
    return f;
}
Xf rotZ(double a) {
    Xf f;
    const double c = std::cos(a), s = std::sin(a);
    f.m[0][0] = c;
    f.m[0][1] = -s;
    f.m[1][0] = s;
    f.m[1][1] = c;
    return f;
}
// ModelPart.translateAndRotate: the part's offset, then Quaternionf.rotationZYX(z, y, x) (x first), then its scale.
Xf partTransform(const EntityPart& p) {
    return mul(translation({p.offset[0] / 16.0, p.offset[1] / 16.0, p.offset[2] / 16.0}),
               mul(rotZ(p.rotation[2]), mul(rotY(p.rotation[1]), mul(rotX(p.rotation[0]), scaling({p.scale[0], p.scale[1], p.scale[2]})))));
}

void collect(const EntityPart& part, const Xf& parent, const Xf& toWorld, const std::array<bool, 7>& parts, std::vector<Vertex>& out) {
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

}  // namespace

std::unique_ptr<PlayerBodyRenderer> PlayerBodyRenderer::create(const ResourceManager& resources, const std::filesystem::path& dir, std::string& error) {
    auto out = std::unique_ptr<PlayerBodyRenderer>(new PlayerBodyRenderer());
    auto model = loadEntityModel(dir, "minecraft:player#main", error);
    if (!model) return nullptr;
    out->model_ = std::move(*model);
    const auto bytes = resources.readBytes(ResourceLocation("minecraft", "textures/entity/player/wide/steve.png"));
    auto skin = bytes ? decodePng(*bytes) : std::nullopt;
    if (!skin || skin->empty()) {
        error = "the default skin (entity/player/wide/steve.png) is not readable";
        return nullptr;
    }
    out->skin_ = Texture2D(*skin, FilterMode::Nearest);
    out->shader_ = ShaderProgram("player_body", kVertex, kFragment);
    out->uProj_ = out->shader_.uniform("uProj");
    out->uLight_ = out->shader_.uniform("uLight");
    out->uSkin_ = out->shader_.uniform("uSkin");
    gl::GenVertexArrays(1, &out->vao_);
    gl::GenBuffers(1, &out->vbo_);
    gl::BindVertexArray(out->vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, out->vbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, sizeof(Vertex), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, gl::FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(5 * sizeof(float)));
    gl::BindVertexArray(0);
    return out;
}

PlayerBodyRenderer::~PlayerBodyRenderer() {
    if (vao_ != 0) gl::DeleteVertexArrays(1, &vao_);
    if (vbo_ != 0) gl::DeleteBuffers(1, &vbo_);
}

void PlayerBodyRenderer::render(const Matrix4f& viewProj, const std::array<double, 3>& cam, const BodyPose& pose) {
    // HumanoidModel.setupAnim on a copy of the model.
    EntityPart model = model_;
    EntityPart* head = model.find("head");
    EntityPart* body = model.find("body");
    EntityPart* rightArm = model.find("right_arm");
    EntityPart* leftArm = model.find("left_arm");
    EntityPart* rightLeg = model.find("right_leg");
    EntityPart* leftLeg = model.find("left_leg");
    if (head != nullptr) {
        head->rotation[1] = pose.headYaw * kDegrees;
        head->rotation[0] = pose.headPitch * kDegrees;
    }
    if (rightArm != nullptr && leftArm != nullptr && rightLeg != nullptr && leftLeg != nullptr && body != nullptr) {
        const float p = pose.walkPosition * 0.6662f, s = pose.walkSpeed;
        rightArm->rotation[0] = std::cos(p + kPi) * 2.0f * s * 0.5f;
        leftArm->rotation[0] = std::cos(p) * 2.0f * s * 0.5f;
        rightLeg->rotation[0] = std::cos(p) * 1.4f * s;
        leftLeg->rotation[0] = std::cos(p + kPi) * 1.4f * s;
        rightArm->rotation[1] = leftArm->rotation[1] = 0.0f;
        rightArm->rotation[2] = leftArm->rotation[2] = 0.0f;
        rightLeg->rotation[1] = leftLeg->rotation[1] = 0.0f;
        rightLeg->rotation[2] = leftLeg->rotation[2] = 0.0f;
        body->rotation[0] = body->rotation[1] = body->rotation[2] = 0.0f;
        // setupAttackAnim: the body turns a little and the right arm swings across.
        const float t = pose.attackProgress;
        if (t > 0.0f) {
            body->rotation[1] = std::sin(std::sqrt(t) * kPi * 2.0f) * 0.2f;
            const float by = body->rotation[1];
            rightArm->offset[2] = std::sin(by) * 5.0f;
            rightArm->offset[0] = -std::cos(by) * 5.0f;
            leftArm->offset[2] = -std::sin(by) * 5.0f;
            leftArm->offset[0] = std::cos(by) * 5.0f;
            rightArm->rotation[1] += by;
            leftArm->rotation[1] += by;
            leftArm->rotation[0] += by;
            float f = 1.0f - t;
            f = 1.0f - f * f * f * f;
            const float g = std::sin(f * kPi);
            const float h = std::sin(t * kPi) * -(head != nullptr ? head->rotation[0] - 0.7f : -0.7f) * 0.75f;
            rightArm->rotation[0] -= g * 1.2f + h;
            rightArm->rotation[1] += by * 2.0f;
            rightArm->rotation[2] += std::sin(t * kPi) * -0.4f;
        } else {
            rightArm->offset[0] = -5.0f;
            leftArm->offset[0] = 5.0f;
            rightArm->offset[2] = leftArm->offset[2] = 0.0f;
        }
        if (pose.crouching) {
            body->rotation[0] = 0.5f;
            rightArm->rotation[0] += 0.4f;
            leftArm->rotation[0] += 0.4f;
            rightLeg->offset[2] = leftLeg->offset[2] = 4.0f;
            rightLeg->offset[1] = leftLeg->offset[1] = 12.2f;
            if (head != nullptr) head->offset[1] = 4.2f;
            body->offset[1] = 3.2f;
            leftArm->offset[1] = rightArm->offset[1] = 5.2f;
        } else {
            rightLeg->offset[2] = leftLeg->offset[2] = 0.1f;
            rightLeg->offset[1] = leftLeg->offset[1] = 12.0f;
            if (head != nullptr) head->offset[1] = 0.0f;
            body->offset[1] = 0.0f;
            leftArm->offset[1] = rightArm->offset[1] = 2.0f;
        }
        // AnimationUtils.bobModelPart: the arms drift a little out and forward and back while standing.
        const float age = static_cast<float>(pose.ageTicks);
        rightArm->rotation[2] += std::cos(age * 0.09f) * 0.05f + 0.05f;
        rightArm->rotation[0] += std::sin(age * 0.067f) * 0.05f;
        leftArm->rotation[2] -= std::cos(age * 0.09f) * 0.05f + 0.05f;
        leftArm->rotation[0] -= std::sin(age * 0.067f) * 0.05f;
    }

    // LivingEntityRenderer: to the player's place (a crouching player is 2/16 lower), turned about Y, the model's y-down mirrored space flipped,
    // shrunk to 0.9375 and dropped by 1.501.
    const double crouchDrop = pose.crouching ? -2.0 / 16.0 : 0.0;
    Xf entity = mul(scaling({-0.9375, -0.9375, 0.9375}), translation({0.0, -1.501, 0.0}));
    entity = mul(rotY((180.0 - static_cast<double>(pose.bodyYaw)) * static_cast<double>(kDegrees)), entity);
    entity = mul(translation({pose.position[0] - cam[0], pose.position[1] - cam[1] + crouchDrop, pose.position[2] - cam[2]}), entity);
    std::vector<Vertex> vertices;
    vertices.reserve(2048);
    collect(model, Xf{}, entity, pose.parts, vertices);
    if (vertices.empty()) return;

    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    shader_.use();
    gl::UniformMatrix4fv(uProj_, 1, 0, viewProj.m.data());
    gl::Uniform3f(uLight_, pose.light[0], pose.light[1], pose.light[2]);
    skin_.bind(0);
    gl::Uniform1i(uSkin_, 0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), gl::STREAM_DRAW);
    gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(vertices.size()));
    gl::BindVertexArray(0);
}

}  // namespace falcon
