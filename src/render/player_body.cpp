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

// Mth.rotLerp for radians (ModelPart: HumanoidModel.rotlerpRad): the short way round.
float rotlerp(float a, float from, float to) {
    float f = std::fmod(to - from, 2.0f * kPi);
    if (f < -kPi) f += 2.0f * kPi;
    if (f >= kPi) f -= 2.0f * kPi;
    return from + a * f;
}
float quadraticArmUpdate(float f) { return -65.0f * f + f * f; }

// The armor layers follow the body's limbs: every part takes the pose of the part of the same name.
void copyPose(EntityPart& dst, const EntityPart& whole) {
    for (EntityPart& part : dst.children) {
        if (const EntityPart* src = const_cast<EntityPart&>(whole).find(part.name)) {
            part.offset = src->offset;
            part.rotation = src->rotation;
            part.scale = src->scale;
        }
    }
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
    out->resources_ = &resources;
    {  // the model with slim arms, and its armor layers (without them the slim skins are drawn with the wide model)
        std::string slimError;
        if (auto slim = loadEntityModel(dir, "minecraft:player_slim#main", slimError)) {
            out->slimModel_ = std::move(*slim);
            out->haveSlim_ = true;
        }
        static const char* const kSlimLayers[4] = {"minecraft:player_slim#helmet", "minecraft:player_slim#chestplate", "minecraft:player_slim#leggings", "minecraft:player_slim#boots"};
        for (std::size_t i = 0; i < 4; ++i) {
            std::string armorError;
            if (auto armorModel = loadEntityModel(dir, kSlimLayers[i], armorError)) {
                out->armorModelsSlim_[i] = std::move(*armorModel);
                out->haveArmorSlim_[i] = true;
            }
        }
    }
    {  // the armor layers of the model (without them nothing is worn)
        static const char* const kLayers[4] = {"minecraft:player#helmet", "minecraft:player#chestplate", "minecraft:player#leggings", "minecraft:player#boots"};
        for (std::size_t i = 0; i < 4; ++i) {
            std::string armorError;
            if (auto armorModel = loadEntityModel(dir, kLayers[i], armorError)) {
                out->armorModels_[i] = std::move(*armorModel);
                out->haveArmorModel_[i] = true;
            }
        }
    }
    out->shader_ = ShaderProgram("player_body", kVertex, kFragment);
    out->uProj_ = out->shader_.uniform("uProj");
    out->uLight_ = out->shader_.uniform("uLight");
    out->uSkin_ = out->shader_.uniform("uSkin");
    out->uTint_ = out->shader_.uniform("uTint");
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

// One of the default skins (see BodyPose::skin); the plain Steve for -1 or for a picture the game's files do not have.
const Texture2D& PlayerBodyRenderer::skinTexture(int index) {
    if (index < 0 || index > 17) return skin_;
    auto it = skins_.find(index);
    if (it == skins_.end()) {
        static const char* const kNames[9] = {"alex", "ari", "efe", "kai", "makena", "noor", "steve", "sunny", "zuri"};
        std::unique_ptr<Texture2D> texture;
        if (resources_ != nullptr) {
            const std::string path = std::string("textures/entity/player/") + (index < 9 ? "slim/" : "wide/") + kNames[index % 9] + ".png";
            const auto bytes = resources_->readBytes(ResourceLocation("minecraft", path));
            if (const auto image = bytes ? decodePng(*bytes) : std::nullopt) texture = std::make_unique<Texture2D>(*image, FilterMode::Nearest);
        }
        it = skins_.emplace(index, std::move(texture)).first;
    }
    return it->second ? *it->second : skin_;
}

// The picture of an armor layer ("humanoid" or "humanoid_leggings") for a material ("iron"); null when the game has none.
const Texture2D* PlayerBodyRenderer::armorTexture(const std::string& layer, const std::string& material) {
    const std::string key = layer + "/" + material;
    auto it = armorTextures_.find(key);
    if (it == armorTextures_.end()) {
        std::unique_ptr<Texture2D> texture;
        if (resources_ != nullptr) {
            const auto bytes = resources_->readBytes(ResourceLocation("minecraft", "textures/entity/equipment/" + key + ".png"));
            if (const auto image = bytes ? decodePng(*bytes) : std::nullopt) texture = std::make_unique<Texture2D>(*image, FilterMode::Nearest);
        }
        it = armorTextures_.emplace(key, std::move(texture)).first;
    }
    return it->second.get();
}

void PlayerBodyRenderer::render(const Matrix4f& viewProj, const std::array<double, 3>& cam, const BodyPose& pose) {
    // HumanoidModel.setupAnim on a copy of the model.
    const bool slim = haveSlim_ && (pose.customSkin != nullptr ? pose.customSlim : (pose.skin >= 0 && pose.skin < 9));
    EntityPart model = slim ? slimModel_ : model_;
    EntityPart* head = model.find("head");
    EntityPart* body = model.find("body");
    EntityPart* rightArm = model.find("right_arm");
    EntityPart* leftArm = model.find("left_arm");
    EntityPart* rightLeg = model.find("right_leg");
    EntityPart* leftLeg = model.find("left_leg");
    if (head != nullptr) {
        head->rotation[1] = pose.headYaw * kDegrees;
        head->rotation[0] = pose.headPitch * kDegrees;
        if (pose.swimAmount > 0.0f && pose.visuallySwimming) head->rotation[0] = rotlerp(pose.swimAmount, head->rotation[0], -kPi / 4.0f);  // looks along the swim
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
        // HumanoidModel.setupAnim, swimAmount > 0: the arms go over the head and make the crawl stroke (a 26-step cycle), the legs kick.
        if (pose.swimAmount > 0.0f) {
            const float sw = pose.swimAmount, m = std::fmod(pose.walkPosition, 26.0f);
            const float n = pose.attackProgress > 0.0f ? 0.0f : sw, o = sw;  // (the right arm keeps swinging when it attacks)
            const auto lerp = [](float a, float from, float to) { return from + a * (to - from); };
            const auto arms = [&](float xr, float zl, float zr) {
                leftArm->rotation[0] = rotlerp(o, leftArm->rotation[0], xr);
                rightArm->rotation[0] = lerp(n, rightArm->rotation[0], xr);
                leftArm->rotation[1] = rotlerp(o, leftArm->rotation[1], kPi);
                rightArm->rotation[1] = lerp(n, rightArm->rotation[1], kPi);
                leftArm->rotation[2] = rotlerp(o, leftArm->rotation[2], zl);
                rightArm->rotation[2] = lerp(n, rightArm->rotation[2], zr);
            };
            if (m < 14.0f) {
                const float q = quadraticArmUpdate(m) / quadraticArmUpdate(14.0f);
                arms(0.0f, kPi + 1.8707964f * q, kPi - 1.8707964f * q);
            } else if (m < 22.0f) {
                const float ph = (m - 14.0f) / 8.0f;
                arms(kPi / 2.0f * ph, 5.012389f - 1.8707964f * ph, 1.2707963f + 1.8707964f * ph);
            } else {
                const float ph = (m - 22.0f) / 4.0f;
                arms(kPi / 2.0f - kPi / 2.0f * ph, kPi, kPi);
            }
            leftLeg->rotation[0] = lerp(sw, leftLeg->rotation[0], 0.3f * std::cos(pose.walkPosition * 0.33333334f + kPi));
            rightLeg->rotation[0] = lerp(sw, rightLeg->rotation[0], 0.3f * std::cos(pose.walkPosition * 0.33333334f));
        }
    }

    // LivingEntityRenderer: to the player's place (a crouching player is 2/16 lower), turned about Y, the model's y-down mirrored space flipped,
    // shrunk to 0.9375 and dropped by 1.501.
    const double crouchDrop = pose.crouching ? -2.0 / 16.0 : 0.0;
    Xf entity = mul(scaling({-0.9375, -0.9375, 0.9375}), translation({0.0, -1.501, 0.0}));
    if (pose.swimAmount > 0.0f) {  // PlayerRenderer.setupRotations: lying down by the pitch of the view (straight down out of the water), and lower and forward
        const float target = pose.inWater ? -90.0f - pose.headPitch : -90.0f;
        const float lie = target * pose.swimAmount;
        Xf lay = rotX(static_cast<double>(lie) * static_cast<double>(kDegrees));
        if (pose.visuallySwimming) lay = mul(lay, translation({0.0, -1.0, 0.3}));
        entity = mul(lay, entity);
    }
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
    gl::Uniform1i(uSkin_, 0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto draw = [&](const std::vector<Vertex>& list, const Texture2D& texture, float r, float g, float b) {
        if (list.empty()) return;
        texture.bind(0);
        gl::Uniform3f(uTint_, r, g, b);
        gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(list.size() * sizeof(Vertex)), list.data(), gl::STREAM_DRAW);
        gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(list.size()));
    };
    draw(vertices, pose.customSkin != nullptr ? *pose.customSkin : skinTexture(pose.skin), 1.0f, 1.0f, 1.0f);
    // The armor worn: each layer of the model in the posed body's pose, with its own picture (leather is brown unless dyed, with a plain overlay on top).
    for (std::size_t slot = 0; slot < 4; ++slot) {
        const std::string& item = pose.armor[slot];
        if (item.empty() || !haveArmorModel_[slot]) continue;
        const std::size_t cut = item.rfind('_');
        if (cut == std::string::npos) continue;
        std::string material = item.substr(0, cut);
        if (material == "golden") material = "gold";
        if (material == "turtle") material = "turtle_scute";
        const Texture2D* texture = armorTexture(slot == 2 ? "humanoid_leggings" : "humanoid", material);
        if (texture == nullptr) continue;
        EntityPart worn = slim && haveArmorSlim_[slot] ? armorModelsSlim_[slot] : armorModels_[slot];
        copyPose(worn, model);
        std::vector<Vertex> list;
        collect(worn, Xf{}, entity, pose.parts, list);
        const bool leather = material == "leather";
        draw(list, *texture, leather ? 0.627f : 1.0f, leather ? 0.396f : 1.0f, leather ? 0.251f : 1.0f);  // (0xA06540: the colour of undyed leather)
        if (leather) {
            if (const Texture2D* overlay = armorTexture(slot == 2 ? "humanoid_leggings" : "humanoid", "leather_overlay")) draw(list, *overlay, 1.0f, 1.0f, 1.0f);
        }
    }
    gl::BindVertexArray(0);
}

}  // namespace falcon
