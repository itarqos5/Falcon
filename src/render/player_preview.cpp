#include "falcon/render/player_preview.hpp"


#include "falcon/assets/resource_manager.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {

namespace {

constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x = 0, y = 0, z = 0;
};

// An affine map p -> M p + t. mul(a, b) applies b first, then a (so chains read like the game's pose stack, outermost first).
struct Xf {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
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
// Rotations about each axis, counter-clockwise by `a` radians (joml's rotateX / rotateY / rotateZ).
Xf rotX(float a) {
    Xf f;
    const float c = std::cos(a), s = std::sin(a);
    f.m[1][1] = c;
    f.m[1][2] = -s;
    f.m[2][1] = s;
    f.m[2][2] = c;
    return f;
}
Xf rotY(float a) {
    Xf f;
    const float c = std::cos(a), s = std::sin(a);
    f.m[0][0] = c;
    f.m[0][2] = s;
    f.m[2][0] = -s;
    f.m[2][2] = c;
    return f;
}
Xf rotZ(float a) {
    Xf f;
    const float c = std::cos(a), s = std::sin(a);
    f.m[0][0] = c;
    f.m[0][1] = -s;
    f.m[1][0] = s;
    f.m[1][1] = c;
    return f;
}

// ModelPart.translateAndRotate: move to the part's offset, rotate (Quaternionf.rotationZYX(z, y, x): x first, then y, then z), scale.
Xf partTransform(const EntityPart& p) {
    return mul(translation({p.offset[0] / 16.0f, p.offset[1] / 16.0f, p.offset[2] / 16.0f}),
               mul(rotZ(p.rotation[2]), mul(rotY(p.rotation[1]), mul(rotX(p.rotation[0]), scaling({p.scale[0], p.scale[1], p.scale[2]})))));
}

// A face ready to be filled: corners in pixels (and a depth, larger = nearer the viewer), the picture's corners and its brightness.
struct Face {
    std::array<float, 4> x{}, y{}, z{}, u{}, v{};
    float shade = 1.0f;
};

void collect(const EntityPart& part, const Xf& parent, const Xf& toView, const std::array<bool, 7>& parts, float originX, float originY, float pixelsPerBlock,
             std::vector<Face>& out) {
    // The overlay layers answer to the Skin Customization options.
    if (part.name == "jacket" && !parts[1]) return;
    if (part.name == "left_sleeve" && !parts[2]) return;
    if (part.name == "right_sleeve" && !parts[3]) return;
    if (part.name == "left_pants" && !parts[4]) return;
    if (part.name == "right_pants" && !parts[5]) return;
    if (part.name == "hat" && !parts[6]) return;
    const Xf here = mul(parent, partTransform(part));
    const Xf full = mul(toView, here);
    for (const EntityPolygon& poly : part.polygons) {
        Face f;
        for (std::size_t i = 0; i < 4; ++i) {
            const V3 p = apply(full, {poly.vertex[i][0] / 16.0f, poly.vertex[i][1] / 16.0f, poly.vertex[i][2] / 16.0f});
            f.x[i] = originX + pixelsPerBlock * p.x;
            f.y[i] = originY + pixelsPerBlock * p.y;
            f.z[i] = -pixelsPerBlock * p.z;  // the GUI's scale(s, s, -s): the nearer to the viewer, the larger
            f.u[i] = poly.vertex[i][3];
            f.v[i] = poly.vertex[i][4];
        }
        // The shading of Lighting.Entry.ENTITY_IN_UI (light.glsl's minecraft_mix_light) with the normal in the view the game shades in:
        // min(1, (max(0, L0.n) + max(0, L1.n)) * 0.6 + 0.4). The normal is turned like the face, and the GUI's z flip turns it round.
        V3 n = applyLinear(full, {poly.normal[0], poly.normal[1], poly.normal[2]});
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-6f) {
            n = {n.x / len, n.y / len, -n.z / len};
            constexpr float k0 = 1.0f / 1.42828568f, k1 = 1.0f / 1.01980390f;
            const float d0 = std::max(0.0f, (0.2f * n.x - 1.0f * n.y + 1.0f * n.z) * k0);
            const float d1 = std::max(0.0f, (-0.2f * n.x - 1.0f * n.y + 0.0f * n.z) * k1);
            f.shade = std::min(1.0f, (d0 + d1) * 0.6f + 0.4f);
        }
        out.push_back(f);
    }
    for (const EntityPart& child : part.children) collect(child, here, toView, parts, originX, originY, pixelsPerBlock, out);
}

}  // namespace

std::unique_ptr<PlayerPreview> PlayerPreview::create(const ResourceManager& resources, const std::filesystem::path& dir, std::string& error) {
    auto out = std::unique_ptr<PlayerPreview>(new PlayerPreview());
    auto model = loadEntityModel(dir, "minecraft:player#main", error);
    if (!model) return nullptr;
    out->model_ = std::move(*model);
    std::string slimError;
    if (auto slim = loadEntityModel(dir, "minecraft:player_slim#main", slimError)) {  // (a game file that lacks it only costs the slim arms)
        out->slimModel_ = std::move(*slim);
        out->hasSlim_ = true;
    }
    const auto bytes = resources.readBytes(ResourceLocation("minecraft", "textures/entity/player/wide/steve.png"));
    auto skin = bytes ? decodePng(*bytes) : std::nullopt;
    if (!skin || skin->empty()) {
        error = "the default skin (entity/player/wide/steve.png) is not readable";
        return nullptr;
    }
    out->skin_ = std::move(*skin);
    return out;
}

Image PlayerPreview::render(const View& view) const {
    const int gs = std::max(view.guiScale, 1);
    Image image;
    image.width = std::max(view.x1 - view.x0, 1) * gs;
    image.height = std::max(view.y1 - view.y0, 1) * gs;
    image.rgba.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4, 0);

    // The follow-the-mouse numbers of InventoryScreen.extractEntityInInventoryFollowsMouse.
    const float cx = static_cast<float>(view.x0 + view.x1) / 2.0f;
    const float cy = static_cast<float>(view.y0 + view.y1) / 2.0f;
    const float xAngle = std::atan((cx - view.mouseX) / 40.0f);
    const float yAngle = std::atan((cy - view.mouseY) / 40.0f);
    const float degrees = kPi / 180.0f;
    const float bodyYawDegrees = 180.0f + xAngle * 20.0f;
    const float headYawDegrees = xAngle * 20.0f;       // the head's turn against the body (state.yRot)
    const float headPitchDegrees = -yAngle * 20.0f;    // state.xRot

    // The pose of the figure in the game: LivingEntityRenderer turns it (180 - bodyRot about Y), flips the model's y-down, mirrored space
    // to the world (scale(-1, -1, 1)), shrinks the player to 0.9375 and drops it by 1.501; the GUI then puts a half-turn about Z and
    // the tilt about X (the mouse's up and down) on it and moves it by half the player's height (1.8) plus the box's offset.
    EntityPart model = view.slim && hasSlim_ ? slimModel_ : model_;
    if (EntityPart* head = model.find("head")) {
        head->rotation[1] = headYawDegrees * degrees;
        head->rotation[0] = headPitchDegrees * degrees;
    }
    // HumanoidModel's idle sway of the arms (AnimationUtils.bobModelPart): they drift a little out and forward and back.
    const float age = static_cast<float>(view.ageTicks);
    if (EntityPart* right = model.find("right_arm")) {
        right->rotation[2] += std::cos(age * 0.09f) * 0.05f + 0.05f;
        right->rotation[0] += std::sin(age * 0.067f) * 0.05f;
    }
    if (EntityPart* left = model.find("left_arm")) {
        left->rotation[2] -= std::cos(age * 0.09f) * 0.05f + 0.05f;
        left->rotation[0] -= std::sin(age * 0.067f) * 0.05f;
    }
    Xf entity = scaling({-0.9375f, -0.9375f, 0.9375f});
    entity = mul(entity, translation({0.0f, -1.501f, 0.0f}));
    const Xf bodyTurn = rotY((180.0f - bodyYawDegrees) * degrees);
    const Xf pose = mul(rotZ(kPi), rotX(yAngle * 20.0f * degrees));
    const Xf toView = mul(translation({0.0f, 1.8f / 2.0f + view.offsetY, 0.0f}), mul(pose, mul(bodyTurn, entity)));

    std::vector<Face> faces;
    const float pixelsPerBlock = view.scale * static_cast<float>(gs);
    collect(model, Xf{}, toView, view.parts, (cx - static_cast<float>(view.x0)) * static_cast<float>(gs), (cy - static_cast<float>(view.y0)) * static_cast<float>(gs),
            pixelsPerBlock, faces);

    // Fill them with a depth test, no culling, nearest texels and the skin's transparency cut out. The picture is the player's own skin when it is
    // known, and Steve until it has been downloaded (or for a player who has none).
    const Image& skin = view.skin != nullptr && !view.skin->empty() ? *view.skin : skin_;
    std::vector<float> depth(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), -1e30f);
    const auto texel = [&](float u, float v, unsigned char out[4]) {
        const int tx = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(skin.width))), 0, skin.width - 1);
        const int ty = std::clamp(static_cast<int>(std::floor(v * static_cast<float>(skin.height))), 0, skin.height - 1);
        const unsigned char* p = &skin.rgba[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(skin.width) + static_cast<std::size_t>(tx)) * 4];
        for (int i = 0; i < 4; ++i) out[i] = p[i];
    };
    const auto triangle = [&](const Face& f, int a, int b, int c) {
        const float det = (f.x[b] - f.x[a]) * (f.y[c] - f.y[a]) - (f.y[b] - f.y[a]) * (f.x[c] - f.x[a]);
        if (std::abs(det) < 1e-6f) return;
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({f.x[a], f.x[b], f.x[c]}))));
        const int maxX = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({f.x[a], f.x[b], f.x[c]}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({f.y[a], f.y[b], f.y[c]}))));
        const int maxY = std::min(image.height - 1, static_cast<int>(std::ceil(std::max({f.y[a], f.y[b], f.y[c]}))));
        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                const float x = static_cast<float>(px) + 0.5f, y = static_cast<float>(py) + 0.5f;
                const float w1 = ((x - f.x[a]) * (f.y[c] - f.y[a]) - (y - f.y[a]) * (f.x[c] - f.x[a])) / det;
                const float w2 = ((f.x[b] - f.x[a]) * (y - f.y[a]) - (f.y[b] - f.y[a]) * (x - f.x[a])) / det;
                const float w0 = 1.0f - w1 - w2;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                const float z = w0 * f.z[a] + w1 * f.z[b] + w2 * f.z[c];
                const std::size_t index = static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(px);
                if (z <= depth[index]) continue;
                unsigned char t[4];
                texel(w0 * f.u[a] + w1 * f.u[b] + w2 * f.u[c], w0 * f.v[a] + w1 * f.v[b] + w2 * f.v[c], t);
                if (t[3] < 128) continue;
                depth[index] = z;
                unsigned char* out = &image.rgba[index * 4];
                for (int i = 0; i < 3; ++i) out[i] = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(static_cast<float>(t[i]) * f.shade)), 0, 255));
                out[3] = 255;
            }
        }
    };
    for (const Face& f : faces) {
        triangle(f, 0, 1, 2);
        triangle(f, 0, 2, 3);
    }
    return image;
}

}  // namespace falcon
