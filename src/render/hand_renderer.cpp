#include "falcon/render/hand_renderer.hpp"

#include "falcon/render/world_renderer.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>

namespace falcon {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

// A 4x4 matrix (column-major like Matrix4f) in doubles, for the chain of translations and rotations of the first-person hand.
struct M4 {
    std::array<double, 16> m{};
    static M4 id() {
        M4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
        return r;
    }
    M4 operator*(const M4& b) const {
        M4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                double s = 0.0;
                for (int k = 0; k < 4; ++k) s += m[static_cast<std::size_t>(k * 4 + row)] * b.m[static_cast<std::size_t>(c * 4 + k)];
                r.m[static_cast<std::size_t>(c * 4 + row)] = s;
            }
        }
        return r;
    }
    [[nodiscard]] std::array<double, 3> point(double x, double y, double z) const {
        return {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]};
    }
    [[nodiscard]] std::array<double, 3> direction(double x, double y, double z) const {
        return {m[0] * x + m[4] * y + m[8] * z, m[1] * x + m[5] * y + m[9] * z, m[2] * x + m[6] * y + m[10] * z};
    }
    [[nodiscard]] Matrix4f toFloat() const {
        Matrix4f r;
        for (std::size_t i = 0; i < 16; ++i) r.m[i] = static_cast<float>(m[i]);
        return r;
    }
};
M4 translate(double x, double y, double z) {
    M4 r = M4::id();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}
M4 scaleBy(double x, double y, double z) {
    M4 r = M4::id();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}
M4 rotX(double deg) {
    const double c = std::cos(deg * kDeg), s = std::sin(deg * kDeg);
    M4 r = M4::id();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}
M4 rotY(double deg) {
    const double c = std::cos(deg * kDeg), s = std::sin(deg * kDeg);
    M4 r = M4::id();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}
M4 rotZ(double deg) {
    const double c = std::cos(deg * kDeg), s = std::sin(deg * kDeg);
    M4 r = M4::id();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

// ItemTransform.apply: the model's own placement for a display context (translation in 1/16, rotation X then Y then Z as a
// quaternion rotationXYZ = Rx * Ry * Rz, scale). For the left hand the y and z rotations and the x shift are mirrored.
M4 applyTransform(const DisplayTransform& t, bool leftHand) {
    double rx = t.rotation[0], ry = t.rotation[1], rz = t.rotation[2];
    if (leftHand) {
        ry = -ry;
        rz = -rz;
    }
    const double tx = (leftHand ? -t.translation[0] : t.translation[0]) / 16.0;
    return translate(tx, t.translation[1] / 16.0, t.translation[2] / 16.0) * rotX(rx) * rotY(ry) * rotZ(rz) *
           scaleBy(t.scale[0], t.scale[1], t.scale[2]);
}

// The game's own light map (assets/shaders/core/lightmap.fsh) for a sky and a block light level; the same formula the
// world shader uses, see lightColor() there.
std::array<float, 3> lightMapColor(int sky, int block, float gamma) {
    const auto curve = [](float l) { return l / (4.0f - 3.0f * l); };
    const float skyLevel = static_cast<float>(sky) / 15.0f;
    const float blockLevel = static_cast<float>(block) / 15.0f;
    const float skyBrightness = curve(skyLevel);
    const float blockBrightness = curve(blockLevel) * 1.4f;
    const float parabolic = (2.0f * blockLevel - 1.0f) * (2.0f * blockLevel - 1.0f);
    constexpr std::array<float, 3> kTint = {1.0f, 0.84705883f, 0.54901963f};  // #FFD88C
    std::array<float, 3> c{};
    for (int i = 0; i < 3; ++i) {
        const float tint = kTint[static_cast<std::size_t>(i)] + (1.0f - kTint[static_cast<std::size_t>(i)]) * (0.9f * parabolic);
        c[static_cast<std::size_t>(i)] = std::clamp(skyBrightness + tint * blockBrightness, 0.0f, 1.0f);
    }
    const float m = std::max({c[0], c[1], c[2]});
    if (m > 0.0f) {
        const float inverted = 1.0f - m;
        const float scaled = (1.0f - inverted * inverted * inverted * inverted) / m;
        for (float& v : c) v += (v * scaled - v) * gamma;
    }
    return c;
}

constexpr char kArmVertex[] = R"(#version 330 core
layout(location = 0) in vec3 aPos;   // camera space
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

constexpr char kArmFragment[] = R"(#version 330 core
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

struct ArmVertex {
    float x, y, z, u, v, shade;
};

// One face of a cuboid of the player model: its four corners (top-left, top-right, bottom-left, bottom-right as seen from
// outside with the model's "up"), its skin rectangle, and the direction it faces.
struct BoxFace {
    std::array<double, 3> tl, tr, bl, br;
    double u0, v0, u1, v1;  // skin pixels
    std::array<double, 3> normal;
};

// The six faces of ModelPart.Cube: `texOffs` (u, v), the box at (x, y, z) of size (w, h, d), inflated. Model space has y pointing
// down, the front of the model is -z.
std::vector<BoxFace> boxFaces(double u, double v, double x, double y, double z, double w, double h, double d, double inflate) {
    const double x0 = x - inflate, x1 = x + w + inflate, y0 = y - inflate, y1 = y + h + inflate, z0 = z - inflate, z1 = z + d + inflate;
    std::vector<BoxFace> f;
    // -z, the front: u grows with +x.
    f.push_back({{x0, y0, z0}, {x1, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, u + d, v + d, u + d + w, v + d + h, {0, 0, -1}});
    // +z, the back: u grows with -x.
    f.push_back({{x1, y0, z1}, {x0, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, u + d + w + d, v + d, u + d + w + d + w, v + d + h, {0, 0, 1}});
    // -x: u grows toward the front (-z).
    f.push_back({{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z1}, {x0, y1, z0}, u, v + d, u + d, v + d + h, {-1, 0, 0}});
    // +x: u grows toward the back (+z).
    f.push_back({{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z0}, {x1, y1, z1}, u + d + w, v + d, u + d + w + d, v + d + h, {1, 0, 0}});
    // y min (the shoulder end): v grows toward the front (-z), u with +x.
    f.push_back({{x0, y0, z1}, {x1, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, u + d, v, u + d + w, v + d, {0, -1, 0}});
    // y max (the hand end): v grows toward the back (+z).
    f.push_back({{x0, y1, z0}, {x1, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, u + d + w, v, u + d + w + w, v + d, {0, 1, 0}});
    return f;
}

}  // namespace

HandRenderer::HandRenderer(const ResourceManager& resources, BlockModels& models, ItemIcons& icons)
    : models_(models), icons_(icons), armShader_("hand", kArmVertex, kArmFragment) {
    uProj_ = armShader_.uniform("uProj");
    uLight_ = armShader_.uniform("uLight");
    uSkin_ = armShader_.uniform("uSkin");
    // The skin: the default Steve (wide arms). The assets hold the other default skins as well.
    Image skin;
    if (const auto bytes = resources.readBytes(ResourceLocation("minecraft", "textures/entity/player/wide/steve.png"))) {
        if (auto image = decodePng(*bytes); image && image->width == 64 && image->height >= 32) skin = std::move(*image);
    }
    if (skin.empty()) {  // no skin file: a plain skin-coloured arm
        skin.width = 64;
        skin.height = 64;
        skin.rgba.assign(64 * 64 * 4, 0);
        for (std::size_t i = 0; i < skin.rgba.size(); i += 4) {
            skin.rgba[i] = 0xC6;
            skin.rgba[i + 1] = 0x92;
            skin.rgba[i + 2] = 0x6E;
            skin.rgba[i + 3] = 255;
        }
    }
    skin_ = Texture2D(skin, FilterMode::Nearest);
    gl::GenVertexArrays(1, &armVao_);
    gl::GenBuffers(1, &armVbo_);
    gl::BindVertexArray(armVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, armVbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, sizeof(ArmVertex), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, sizeof(ArmVertex), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, gl::FLOAT, 0, sizeof(ArmVertex), reinterpret_cast<const void*>(5 * sizeof(float)));
    gl::BindVertexArray(0);
}

HandRenderer::~HandRenderer() {
    if (armVao_ != 0) gl::DeleteVertexArrays(1, &armVao_);
    if (armVbo_ != 0) gl::DeleteBuffers(1, &armVbo_);
}

// ---------------------------------------------------------------------------------------------------------------
// Animation

void HandRenderer::swing() {
    if (!swinging_ || swingTime_ >= 3 || swingTime_ < 0) {  // (a swing past its middle starts over)
        swingTime_ = -1;
        swinging_ = true;
    }
}

void HandRenderer::tick(const ItemStack& held, float yaw, float pitch) {
    held_ = held;
    if (!started_) {
        started_ = true;
        xBob_ = xBobO_ = pitch;
        yBob_ = yBobO_ = yaw;
    }
    // The swing: six ticks.
    constexpr int kSwingTicks = 6;
    oAttackAnim_ = attackAnim_;
    if (swinging_) {
        ++swingTime_;
        if (swingTime_ >= kSwingTicks) {
            swingTime_ = 0;
            swinging_ = false;
        }
    } else {
        swingTime_ = 0;
    }
    attackAnim_ = static_cast<float>(swingTime_) / static_cast<float>(kSwingTicks);
    // The arm follows the view slowly, so it sways when the player turns.
    xBobO_ = xBob_;
    yBobO_ = yBob_;
    xBob_ += (pitch - xBob_) * 0.5f;
    yBob_ += (yaw - yBob_) * 0.5f;
    ++attackTicker_;
    // The equip animation: the hand rises to the height the attack cooldown allows, or sinks when the item changed.
    oMainHandHeight_ = mainHandHeight_;
    const bool same = (shown_.isEmpty() && held.isEmpty()) || (!shown_.isEmpty() && !held.isEmpty() && shown_.item == held.item);
    const float speed = held.isEmpty() ? 4.0f : std::max(0.1f, held.type().attackSpeed());
    const float strength = std::clamp((static_cast<float>(attackTicker_) + 0.5f) / (20.0f / speed), 0.0f, 1.0f);
    const float target = same ? strength * strength * strength : 0.0f;
    mainHandHeight_ += std::clamp(target - mainHandHeight_, -0.4f, 0.4f);
    if (mainHandHeight_ < 0.1f) shown_ = held;
}

float HandRenderer::swingProgress(double partialTick) const {
    float f = attackAnim_ - oAttackAnim_;
    if (f < 0.0f) f += 1.0f;
    return oAttackAnim_ + f * static_cast<float>(partialTick);
}

float HandRenderer::equipProgress(double partialTick) const {
    return 1.0f - (oMainHandHeight_ + (mainHandHeight_ - oMainHandHeight_) * static_cast<float>(partialTick));
}

// ---------------------------------------------------------------------------------------------------------------
// Held items

HandRenderer::HeldModel HandRenderer::buildHeldModel(const ItemStack& stack) {
    HeldModel model;
    // Resolve the item for the hand, not the menu: a spear then gives its 3D in-hand model (with its own texture and transform)
    // instead of its flat icon. The in-hand model is the same for either hand; only its display transform differs (read below).
    const ResolvedItemModel& resolved = icons_.resolved(stack.type().itemModel, "firstperson_righthand");
    if (resolved.empty || resolved.parts.empty()) return model;
    const auto push = [&](std::vector<MeshVertex>& out, const std::array<std::array<float, 3>, 4>& pos, const std::array<std::array<float, 2>, 4>& uv,
                          int layer, std::uint32_t rgb, float shade) {
        for (int i = 0; i < 4; ++i) {
            MeshVertex v{};
            v.x = static_cast<std::int16_t>(std::lround(pos[static_cast<std::size_t>(i)][0] * kMeshScale));
            v.y = static_cast<std::int16_t>(std::lround(pos[static_cast<std::size_t>(i)][1] * kMeshScale));
            v.z = static_cast<std::int16_t>(std::lround(pos[static_cast<std::size_t>(i)][2] * kMeshScale));
            v.layer = static_cast<std::int16_t>(layer);
            v.u = static_cast<std::int16_t>(std::lround(uv[static_cast<std::size_t>(i)][0] * kMeshScale));
            v.v = static_cast<std::int16_t>(std::lround(uv[static_cast<std::size_t>(i)][1] * kMeshScale));
            v.r = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
            v.g = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
            v.b = static_cast<std::uint8_t>(rgb & 0xFF);
            v.shade = static_cast<std::uint8_t>(std::lround(std::clamp(shade, 0.0f, 1.0f) * 255.0f));
            out.push_back(v);
        }
    };
    bool first = true;
    for (const IconPart& part : resolved.parts) {
        if (!part.special.empty()) continue;  // drawn by code in the game: not held here
        const auto info = models_.itemModelInfo(part.model);
        if (!info) continue;
        if (first) {
            model.firstRight = info->transform("firstperson_righthand");
            model.firstLeft = info->transform("firstperson_lefthand");
            if (!model.firstLeft.present) model.firstLeft = model.firstRight;  // the game's fallback
            model.ground = info->transform("ground");
            model.thirdRight = info->transform("thirdperson_righthand");
            model.thirdLeft = info->transform("thirdperson_lefthand");
            if (!model.thirdLeft.present) model.thirdLeft = model.thirdRight;
            first = false;
        }
        if (info->generated) {
            // A flat item is its picture pushed out one pixel (ItemModelGenerator): a front and a back face, and a thin face
            // at every pixel edge next to a transparent pixel.
            constexpr float z0 = 7.5f / 16.0f, z1 = 8.5f / 16.0f;
            for (std::size_t li = 0; li < info->layers.size(); ++li) {
                const int layer = models_.layerFor(info->layers[li]);
                const Image tile = models_.layerImage(static_cast<std::uint16_t>(layer));
                const std::uint32_t rgb = li < part.tints.size() ? part.tints[li] : 0xFFFFFFu;
                const int n = tile.width;
                const float step = 1.0f / static_cast<float>(n);
                push(model.vertices, {{{0, 1, z1}, {0, 0, z1}, {1, 0, z1}, {1, 1, z1}}}, {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}}, layer, rgb, 0.9f);
                push(model.vertices, {{{1, 1, z0}, {1, 0, z0}, {0, 0, z0}, {0, 1, z0}}}, {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}}, layer, rgb, 0.9f);
                const auto opaque = [&](int x, int y) { return x >= 0 && y >= 0 && x < n && y < tile.height && tile.alphaAt(x, y) > 25; };
                for (int py = 0; py < tile.height; ++py) {
                    for (int px = 0; px < n; ++px) {
                        if (!opaque(px, py)) continue;
                        const float x0 = static_cast<float>(px) * step, x1 = x0 + step;
                        const float yTop = 1.0f - static_cast<float>(py) * step, yBottom = yTop - step;
                        const float cu = (static_cast<float>(px) + 0.5f) * step, cv = (static_cast<float>(py) + 0.5f) * step;
                        const std::array<std::array<float, 2>, 4> centre = {{{cu, cv}, {cu, cv}, {cu, cv}, {cu, cv}}};
                        if (!opaque(px - 1, py)) push(model.vertices, {{{x0, yTop, z0}, {x0, yBottom, z0}, {x0, yBottom, z1}, {x0, yTop, z1}}}, centre, layer, rgb, 0.7f);
                        if (!opaque(px + 1, py)) push(model.vertices, {{{x1, yTop, z1}, {x1, yBottom, z1}, {x1, yBottom, z0}, {x1, yTop, z0}}}, centre, layer, rgb, 0.7f);
                        if (!opaque(px, py - 1)) push(model.vertices, {{{x0, yTop, z0}, {x0, yTop, z1}, {x1, yTop, z1}, {x1, yTop, z0}}}, centre, layer, rgb, 1.0f);
                        if (!opaque(px, py + 1)) push(model.vertices, {{{x0, yBottom, z1}, {x0, yBottom, z0}, {x1, yBottom, z0}, {x1, yBottom, z1}}}, centre, layer, rgb, 0.5f);
                    }
                }
            }
        } else if (info->quads) {
            for (const ModelQuad& q : *info->quads) {
                const std::uint32_t rgb = q.tint >= 0 && static_cast<std::size_t>(q.tint) < part.tints.size() ? part.tints[static_cast<std::size_t>(q.tint)] : 0xFFFFFFu;
                push(model.vertices, q.pos, q.uv, q.layer, rgb, q.shade ? faceShade(q.face) : 1.0f);
            }
        }
    }
    model.empty = model.vertices.empty();
    return model;
}

const HandRenderer::HeldModel& HandRenderer::modelFor(const ItemStack& stack) {
    const std::string key = stack.type().itemModel;
    auto it = heldModels_.find(key);
    if (it == heldModels_.end()) it = heldModels_.emplace(key, buildHeldModel(stack)).first;
    return it->second;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

void HandRenderer::drawArm(const HandFrame& frame, float side, float swing, float equip, float swayX, float swayY) {
    const double f = side;
    const double g = std::sqrt(static_cast<double>(swing));
    const double h = -0.3 * std::sin(g * kPi);
    const double i = 0.4 * std::sin(g * kPi * 2.0);
    const double j = -0.4 * std::sin(static_cast<double>(swing) * kPi);
    const double k = std::sin(static_cast<double>(swing) * static_cast<double>(swing) * kPi);
    const double l = std::sin(g * kPi);
    const M4 pose = rotX(swayX * 0.1) * rotY(swayY * 0.1) * translate(f * (h + 0.64000005), i - 0.6 + static_cast<double>(equip) * -0.6, j - 0.71999997) *
                    rotY(f * 45.0) * rotY(f * l * 70.0) * rotZ(f * k * -20.0) * translate(f * -1.0, 3.6, 3.5) * rotZ(f * 120.0) * rotX(200.0) *
                    rotY(f * -135.0) * translate(f * 5.6, 0.0, 0.0);

    // The arm and its sleeve, from the player model: pivot (+-5, 2 or 2.5, 0), arm 4 (3 slim) wide, 12 tall, 4 deep.
    const bool right = side > 0.0f;
    const double w = slim_ ? 3.0 : 4.0;
    const double pivotY = slim_ ? 2.5 : 2.0;
    const double px = right ? -5.0 : 5.0;
    const double bx = right ? (slim_ ? -2.0 : -3.0) : -1.0;
    std::vector<BoxFace> faces = boxFaces(right ? 40 : 32, right ? 16 : 48, bx, -2.0, -2.0, w, 12.0, 4.0, 0.0);
    if (frame.sleeve) {
        const auto sleeve = boxFaces(right ? 40 : 48, right ? 32 : 48, bx, -2.0, -2.0, w, 12.0, 4.0, 0.25);
        faces.insert(faces.end(), sleeve.begin(), sleeve.end());
    }
    const M4 model = pose * translate(px / 16.0, pivotY / 16.0, 0.0);
    const std::array<double, 3> l0 = {0.2, 1.0, -0.7}, l1 = {-0.2, 1.0, 0.7};
    const auto norm = [](std::array<double, 3> v) {
        const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return std::array<double, 3>{v[0] / len, v[1] / len, v[2] / len};
    };
    const auto L0 = norm(l0), L1 = norm(l1);
    std::vector<ArmVertex> out;
    out.reserve(faces.size() * 6);
    for (const BoxFace& face : faces) {
        const auto place = [&](const std::array<double, 3>& p) { return model.point(p[0] / 16.0, p[1] / 16.0, p[2] / 16.0); };
        const auto a = place(face.tl), b = place(face.tr), c = place(face.bl), d = place(face.br);
        auto n = model.direction(face.normal[0], face.normal[1], face.normal[2]);
        n = norm(n);
        const double lit = std::min(1.0, 0.4 + 0.6 * (std::max(0.0, n[0] * L0[0] + n[1] * L0[1] + n[2] * L0[2]) + std::max(0.0, n[0] * L1[0] + n[1] * L1[1] + n[2] * L1[2])));
        const float shade = static_cast<float>(lit);
        const float u0 = static_cast<float>(face.u0 / 64.0), u1 = static_cast<float>(face.u1 / 64.0);
        const float v0 = static_cast<float>(face.v0 / 64.0), v1 = static_cast<float>(face.v1 / 64.0);
        const auto vert = [&](const std::array<double, 3>& p, float u, float v) {
            return ArmVertex{static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), u, v, shade};
        };
        // Two triangles per face; the arm is drawn with culling off, so the winding does not matter.
        out.push_back(vert(a, u0, v0));
        out.push_back(vert(c, u0, v1));
        out.push_back(vert(d, u1, v1));
        out.push_back(vert(a, u0, v0));
        out.push_back(vert(d, u1, v1));
        out.push_back(vert(b, u1, v0));
    }

    const float aspect = static_cast<float>(frame.width) / static_cast<float>(std::max(1, frame.height));
    const Matrix4f proj = perspective(70.0f, aspect, 0.05f, 128.0f) * frame.bob;
    armShader_.use();
    gl::UniformMatrix4fv(uProj_, 1, 0, proj.m.data());
    const auto color = lightMapColor(frame.sky, frame.block, frame.brightness);
    gl::Uniform3f(uLight_, color[0], color[1], color[2]);
    skin_.bind(0);
    gl::Uniform1i(uSkin_, 0);
    gl::BindVertexArray(armVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, armVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(out.size() * sizeof(ArmVertex)), out.data(), gl::STREAM_DRAW);
    gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(out.size()));
    gl::BindVertexArray(0);
}

void HandRenderer::render(WorldRenderer& world, const HandFrame& frame) {
    const float swing = swingProgress(frame.partialTick);
    const float equip = equipProgress(frame.partialTick);
    const float swayX = frame.pitch - (xBobO_ + (xBob_ - xBobO_) * static_cast<float>(frame.partialTick));
    const float swayY = frame.yaw - (yBobO_ + (yBob_ - yBobO_) * static_cast<float>(frame.partialTick));
    const float side = frame.rightHanded ? 1.0f : -1.0f;

    gl::Clear(gl::DEPTH_BUFFER_BIT);  // the hand is always in front of the world
    gl::Enable(gl::DEPTH_TEST);
    // A held item is drawn with its back faces culled, as the game does. A flat item (a pickaxe, a sword) is a front face, a back face and a
    // ring of edge faces; with both sides drawn the back face showed beside the front one, so the tool looked like two sprites on top of each
    // other. (The bare arm is drawn without culling: its faces are not all wound the same way.)
    if (!shown_.isEmpty()) {
        gl::Enable(gl::CULL_FACE);
        gl::CullFace(gl::BACK);
        gl::FrontFace(gl::CCW);
    } else {
        gl::Disable(gl::CULL_FACE);
    }
    gl::Disable(gl::BLEND);

    if (shown_.isEmpty()) {
        drawArm(frame, side, swing, equip, swayX, swayY);
        return;
    }
    const HeldModel& model = modelFor(shown_);
    if (model.empty) return;
    const double s = side;
    const double sq = std::sqrt(static_cast<double>(swing));
    // renderArmWithItem for an item that is just held: the attack moves it, then applyItemArmTransform and the attack rotation.
    const double a = -0.4 * std::sin(sq * kPi), b = 0.2 * std::sin(sq * kPi * 2.0), c = -0.2 * std::sin(static_cast<double>(swing) * kPi);
    const double f1 = std::sin(static_cast<double>(swing) * static_cast<double>(swing) * kPi);
    const double g1 = std::sin(sq * kPi);
    M4 pose = rotX(swayX * 0.1) * rotY(swayY * 0.1) * translate(s * a, b, c) * translate(s * 0.56, -0.52 + static_cast<double>(equip) * -0.6, -0.72) *
              rotY(s * (45.0 + f1 * -20.0)) * rotZ(s * g1 * -20.0) * rotX(g1 * -80.0) * rotY(s * -45.0);
    pose = pose * applyTransform(frame.rightHanded ? model.firstRight : model.firstLeft, !frame.rightHanded) * translate(-0.5, -0.5, -0.5);

    const float aspect = static_cast<float>(frame.width) / static_cast<float>(std::max(1, frame.height));
    const Matrix4f mvp = perspective(70.0f, aspect, 0.05f, 128.0f) * frame.bob * pose.toFloat();
    std::vector<MeshVertex> vertices = model.vertices;
    for (MeshVertex& v : vertices) {
        v.sky = static_cast<std::uint8_t>(frame.sky * 17);
        v.block = static_cast<std::uint8_t>(frame.block * 17);
    }
    world.drawHeld(mvp, vertices);
}


// ---------------------------------------------------------------------------------------------------------------
// Items lying in the world (ItemEntityRenderer)

namespace {
float hashUnit(int id, int salt) {  // a repeatable number in [0, 1) for an item and a copy of it
    std::uint32_t x = static_cast<std::uint32_t>(id) * 0x9E3779B1u + static_cast<std::uint32_t>(salt + 1) * 0x85EBCA6Bu;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}
}  // namespace

void HandRenderer::renderGroundItems(WorldRenderer& world, const Matrix4f& viewProj, const std::array<double, 3>& cam, const std::vector<ItemEntity>& items, double partialTick) {
    if (items.empty()) return;
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::FrontFace(gl::CCW);
    gl::Disable(gl::BLEND);
    for (const ItemEntity& e : items) {
        if (e.stack.isEmpty()) continue;
        const double px = e.xo + (e.x - e.xo) * partialTick - cam[0];
        const double py = e.yo + (e.y - e.yo) * partialTick - cam[1];
        const double pz = e.zo + (e.z - e.zo) * partialTick - cam[2];
        if (px * px + py * py + pz * pz > 64.0 * 64.0) continue;
        const HeldModel& model = modelFor(e.stack);
        if (model.empty) continue;
        const DisplayTransform ground = model.ground.present ? model.ground : DisplayTransform{{0.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, {0.25f, 0.25f, 0.25f}, true};
        // The bobbing and the spin of ItemEntityRenderer: up and down 0.1 blocks over about 63 ticks, a full turn every 125.
        const double age = static_cast<double>(e.age) + partialTick;
        const double bob = std::sin(age / 10.0 + static_cast<double>(e.bobOffset)) * 0.1 + 0.1;
        const double spinDegrees = (age / 20.0 + static_cast<double>(e.bobOffset)) / kDeg;
        const M4 base = translate(px, py + bob + 0.25 * static_cast<double>(ground.scale[1]), pz) * rotY(spinDegrees);
        int sky = 15, block = 0;
        world.lightAtWithCarried(static_cast<int>(std::floor(e.x)), static_cast<int>(std::floor(e.y + 0.1)), static_cast<int>(std::floor(e.z)), sky, block);
        std::vector<MeshVertex> vertices = model.vertices;
        for (MeshVertex& v : vertices) {
            v.sky = static_cast<std::uint8_t>(sky * 17);
            v.block = static_cast<std::uint8_t>(block * 17);
        }
        const int copies = ItemEntities::renderedAmount(e.stack.count);
        for (int i = 0; i < copies; ++i) {  // a bigger stack is a little heap: the copies sit a few pixels apart
            const double ox = i == 0 ? 0.0 : (static_cast<double>(hashUnit(e.id, i * 3)) * 2.0 - 1.0) * 0.15;
            const double oy = i == 0 ? 0.0 : (static_cast<double>(hashUnit(e.id, i * 3 + 1)) * 2.0 - 1.0) * 0.05;
            const double oz = i == 0 ? 0.0 : (static_cast<double>(hashUnit(e.id, i * 3 + 2)) * 2.0 - 1.0) * 0.15;
            const M4 pose = base * translate(ox, oy, oz) * applyTransform(ground, false) * translate(-0.5, -0.5, -0.5);
            world.drawHeld(viewProj * pose.toFloat(), vertices);
        }
    }
}

void HandRenderer::renderItemInHand(WorldRenderer& world, const Matrix4f& viewProj, const ItemStack& stack, const std::array<double, 16>& arm, bool left, int sky, int block) {
    if (stack.isEmpty()) return;
    const HeldModel& model = modelFor(stack);
    if (model.empty) return;
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::FrontFace(gl::CCW);
    gl::Disable(gl::BLEND);
    M4 armPlace;
    armPlace.m = arm;
    const DisplayTransform& display = left ? model.thirdLeft : model.thirdRight;
    // ItemInHandLayer.renderArmWithItem: on the arm, X -90, Y 180, to the hand (a sixteenth sideways, an eighth down, ten sixteenths forward); then the item's own display transform
    // and its 0..1 model centred.
    const M4 pose = armPlace * rotX(-90.0) * rotY(180.0) * translate(left ? -1.0 / 16.0 : 1.0 / 16.0, 0.125, -0.625) * applyTransform(display, left) * translate(-0.5, -0.5, -0.5);
    std::vector<MeshVertex> vertices = model.vertices;
    for (MeshVertex& v : vertices) {
        v.sky = static_cast<std::uint8_t>(sky * 17);
        v.block = static_cast<std::uint8_t>(block * 17);
    }
    world.drawHeld(viewProj * pose.toFloat(), vertices);
}

void HandRenderer::renderBlockEntities(WorldRenderer& world, const Matrix4f& viewProj, const std::vector<FallingBlock>& falling, const std::vector<PrimedTnt>& tnt,
                                       const std::array<double, 3>& cam, double partialTick) {
    if (falling.empty() && tnt.empty()) return;
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::FrontFace(gl::CCW);
    gl::Disable(gl::BLEND);
    const BlockRegistry& registry = blockRegistry();
    const auto lit = [&](std::vector<MeshVertex> vertices, double x, double y, double z) {
        int sky = 15, block = 0;
        world.lightAtWithCarried(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y + 0.5)), static_cast<int>(std::floor(z)), sky, block);
        for (MeshVertex& v : vertices) {
            v.sky = static_cast<std::uint8_t>(sky * 17);
            v.block = static_cast<std::uint8_t>(block * 17);
        }
        return vertices;
    };
    for (const FallingBlock& e : falling) {
        if (e.id < 0) continue;
        const Item item = itemRegistry().itemOfBlock(registry.state(e.block).block);
        if (item == Item::Air) continue;
        const double px = e.xo + (e.x - e.xo) * partialTick, py = e.yo + (e.y - e.yo) * partialTick, pz = e.zo + (e.z - e.zo) * partialTick;
        if ((px - cam[0]) * (px - cam[0]) + (py - cam[1]) * (py - cam[1]) + (pz - cam[2]) * (pz - cam[2]) > 96.0 * 96.0) continue;
        const HeldModel& model = modelFor(ItemStack(item, 1));
        if (model.empty) continue;
        const M4 pose = translate(px - 0.5 - cam[0], py - cam[1], pz - 0.5 - cam[2]);
        world.drawHeld(viewProj * pose.toFloat(), lit(model.vertices, px, py, pz));
    }
    static const auto tntItem = itemRegistry().find("tnt");
    if (!tnt.empty() && tntItem) {
        const HeldModel& model = modelFor(ItemStack(*tntItem, 1));
        const std::uint16_t whiteLayer = models_.layerFor("minecraft:block/white_concrete");
        for (const PrimedTnt& e : tnt) {
            if (e.id < 0 || model.empty) continue;
            const double px = e.xo + (e.x - e.xo) * partialTick, py = e.yo + (e.y - e.yo) * partialTick, pz = e.zo + (e.z - e.zo) * partialTick;
            if ((px - cam[0]) * (px - cam[0]) + (py - cam[1]) * (py - cam[1]) + (pz - cam[2]) * (pz - cam[2]) > 96.0 * 96.0) continue;
            // PrimedTntRenderer: it swells in the last half second, and every five ticks it is a white block.
            const double left = static_cast<double>(e.fuse) - partialTick + 1.0;
            double scale = 1.0;
            if (left < 10.0) {
                double g = std::clamp(1.0 - left / 10.0, 0.0, 1.0);
                g = g * g;
                g = g * g;
                scale = 1.0 + g * 0.3;
            }
            std::vector<MeshVertex> vertices = lit(model.vertices, px, py, pz);
            if ((e.fuse / 5) % 2 == 0) {
                for (MeshVertex& v : vertices) {
                    v.layer = static_cast<std::int16_t>(whiteLayer);
                    v.r = v.g = v.b = 255;
                    v.shade = 255;
                    v.sky = v.block = 255;
                }
            }
            const M4 pose = translate(px - cam[0], py + 0.5 - cam[1], pz - cam[2]) * scaleBy(scale, scale, scale) * translate(-0.5, -0.5, -0.5);
            world.drawHeld(viewProj * pose.toFloat(), vertices);
        }
    }
}

std::array<float, 3> entityLightColor(int sky, int block, float gamma) { return lightMapColor(sky, block, gamma); }

}  // namespace falcon
