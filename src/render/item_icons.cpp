#include "falcon/render/item_icons.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace falcon {
using nlohmann::json;

namespace {

std::string stripNamespace(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return name;
}

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------------------------
// Resolving an item definition

std::uint32_t rgbOf(const json& v, std::uint32_t fallback = 0xFFFFFF) {
    if (v.is_number_integer()) return static_cast<std::uint32_t>(v.get<std::int64_t>()) & 0xFFFFFFu;  // the game writes ARGB as a signed int
    return fallback;
}

std::vector<std::uint32_t> readTints(const json& node, const BlockModels* colors) {
    std::vector<std::uint32_t> out;
    const auto tints = node.find("tints");
    if (tints == node.end() || !tints->is_array()) return out;
    for (const json& t : *tints) {
        const std::string type = stripNamespace(t.value("type", std::string()));
        std::uint32_t rgb = 0xFFFFFF;
        if (type == "constant") {
            rgb = rgbOf(t.value("value", json()));
        } else if (type == "grass") {
            const double temperature = t.value("temperature", 0.5);
            const double downfall = t.value("downfall", 1.0);
            rgb = colors != nullptr ? colors->grassColor(temperature, downfall) : 0x91BD59;
        } else if (type == "foliage") {
            rgb = colors != nullptr ? colors->foliageColor(0.8, 0.4) : 0x77AB2F;
        } else {  // dye, potion, firework, map_color, team, custom_model_data: what the item looks like without the component
            rgb = rgbOf(t.value("default", json()));
        }
        out.push_back(rgb);
    }
    return out;
}

bool contextMatches(const json& when, const std::string& context) {
    if (when.is_string()) return stripNamespace(when.get<std::string>()) == context;
    if (when.is_array()) {
        for (const json& w : when) {
            if (w.is_string() && stripNamespace(w.get<std::string>()) == context) return true;
        }
    }
    return false;
}

void resolveNode(const json& node, const BlockModels* colors, ResolvedItemModel& out, int depth, const std::string& context) {
    if (!node.is_object() || depth > 12) return;
    const std::string type = stripNamespace(node.value("type", std::string()));
    if (type == "model") {
        IconPart part;
        part.model = stripNamespace(node.value("model", std::string()));
        part.tints = readTints(node, colors);
        if (!part.model.empty()) out.parts.push_back(std::move(part));
    } else if (type == "condition") {
        // Nothing is being used, selected or broken: the "off" branch.
        if (const auto f = node.find("on_false"); f != node.end()) resolveNode(*f, colors, out, depth + 1, context);
    } else if (type == "select") {
        const std::string property = stripNamespace(node.value("property", std::string()));
        const json* chosen = nullptr;
        const auto cases = node.find("cases");
        // A select on the display context (e.g. a spear: a flat icon in the menu, a 3D model in the hand) picks the case that
        // lists the context we are rendering for; the fallback covers the rest (held items are the fallback for spears).
        if (property == "display_context" && cases != node.end() && cases->is_array()) {
            for (const json& c : *cases) {
                if (c.contains("when") && contextMatches(c["when"], context) && c.contains("model")) {
                    chosen = &c["model"];
                    break;
                }
            }
        }
        if (chosen == nullptr) {
            if (const auto f = node.find("fallback"); f != node.end()) {
                chosen = &*f;
            } else if (cases != node.end() && cases->is_array() && !cases->empty() && (*cases)[0].contains("model")) {
                chosen = &(*cases)[0]["model"];
            }
        }
        if (chosen != nullptr) resolveNode(*chosen, colors, out, depth + 1, context);
    } else if (type == "range_dispatch") {
        const json* chosen = nullptr;
        if (const auto f = node.find("fallback"); f != node.end()) {
            chosen = &*f;
        } else if (const auto entries = node.find("entries"); entries != node.end() && entries->is_array()) {
            double best = 1e300;  // the lowest threshold: a compass pointing one way, a clock at the start
            for (const json& e : *entries) {
                const double threshold = e.value("threshold", 0.0);
                if (e.contains("model") && threshold < best) {
                    best = threshold;
                    chosen = &e["model"];
                }
            }
        }
        if (chosen != nullptr) resolveNode(*chosen, colors, out, depth + 1, context);
    } else if (type == "composite") {
        if (const auto models = node.find("models"); models != node.end() && models->is_array()) {
            for (const json& m : *models) resolveNode(m, colors, out, depth + 1, context);
        }
    } else if (type == "special") {
        IconPart part;
        part.model = stripNamespace(node.value("base", std::string()));
        if (const auto m = node.find("model"); m != node.end() && m->is_object()) {
            part.special = stripNamespace(m->value("type", std::string()));
            part.specialDetail = stripNamespace(m->value("color", m->value("kind", std::string())));
            part.specialTexture = stripNamespace(m->value("texture", std::string()));  // a chest's: "normal", "trapped", "ender", "copper" ...
        }
        if (!part.model.empty()) out.parts.push_back(std::move(part));
    } else if (type == "empty") {
        out.empty = true;
    }
    // bundle/selected_item and anything unknown draws nothing
}

// ---------------------------------------------------------------------------------------------------------------
// Pictures

Image blankImage(int size) {
    Image image;
    image.width = size;
    image.height = size;
    image.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);
    return image;
}

Image resizeNearest(const Image& image, int size) {
    if (image.width == size && image.height == size) return image;
    Image out = blankImage(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int sx = std::min(image.width - 1, x * image.width / size);
            const int sy = std::min(image.height - 1, y * image.height / size);
            std::copy_n(&image.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(sx)) * 4], 4,
                        &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4]);
        }
    }
    return out;
}

// The top square of a picture that may be a strip of animation frames.
Image firstFrame(const Image& image) {
    if (image.width == image.height || image.width <= 0 || image.height < image.width) return image;
    Image frame;
    frame.width = image.width;
    frame.height = image.width;
    frame.rgba.assign(image.rgba.begin(), image.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.width) * 4));
    return frame;
}

// `top` drawn over `bottom` (straight alpha).
void drawOver(Image& bottom, const Image& top) {
    for (std::size_t i = 0; i + 3 < bottom.rgba.size() && i + 3 < top.rgba.size(); i += 4) {
        const double sa = top.rgba[i + 3] / 255.0;
        if (sa <= 0.0) continue;
        const double da = bottom.rgba[i + 3] / 255.0;
        const double oa = sa + da * (1.0 - sa);
        for (int c = 0; c < 3; ++c) {
            const double v = (top.rgba[i + static_cast<std::size_t>(c)] * sa + bottom.rgba[i + static_cast<std::size_t>(c)] * da * (1.0 - sa)) / oa;
            bottom.rgba[i + static_cast<std::size_t>(c)] = static_cast<unsigned char>(std::clamp(v + 0.5, 0.0, 255.0));
        }
        bottom.rgba[i + 3] = static_cast<unsigned char>(std::clamp(oa * 255.0 + 0.5, 0.0, 255.0));
    }
}

void tintImage(Image& image, std::uint32_t rgb) {
    if (rgb == 0xFFFFFF) return;
    const double r = ((rgb >> 16) & 0xFF) / 255.0, g = ((rgb >> 8) & 0xFF) / 255.0, b = (rgb & 0xFF) / 255.0;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        image.rgba[i] = static_cast<unsigned char>(image.rgba[i] * r);
        image.rgba[i + 1] = static_cast<unsigned char>(image.rgba[i + 1] * g);
        image.rgba[i + 2] = static_cast<unsigned char>(image.rgba[i + 2] * b);
    }
}

void fillRect(Image& image, int x0, int y0, int x1, int y1, std::uint32_t rgb) {
    for (int y = std::max(0, y0); y < std::min(image.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(image.width, x1); ++x) {
            unsigned char* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4];
            p[0] = static_cast<unsigned char>(rgb >> 16);
            p[1] = static_cast<unsigned char>(rgb >> 8);
            p[2] = static_cast<unsigned char>(rgb);
            p[3] = 255;
        }
    }
}

// DyeColor.getTextureDiffuseColor.
std::uint32_t dyeColor(const std::string& name) {
    static const struct {
        const char* name;
        std::uint32_t rgb;
    } kDyes[] = {{"white", 0xF9FFFE},  {"orange", 0xF9801D},     {"magenta", 0xC74EBD}, {"light_blue", 0x3AB3DA}, {"yellow", 0xFED83D}, {"lime", 0x80C71F},
                 {"pink", 0xF38BAA},   {"gray", 0x474F52},       {"light_gray", 0x9D9D97}, {"cyan", 0x169C9C},    {"purple", 0x8932B8}, {"blue", 0x3C44AA},
                 {"brown", 0x835432},  {"green", 0x5E7C16},      {"red", 0xB02E26},     {"black", 0x1D1D21}};
    for (const auto& d : kDyes) {
        if (name == d.name) return d.rgb;
    }
    return 0xF9FFFE;
}

// A banner: a cloth hanging from a crossbar, in its dye colour (the game draws the real one with code).
Image bannerStandIn(int size, const std::string& color) {
    Image image = blankImage(size);
    const auto u = [&](double f) { return static_cast<int>(std::lround(f * size)); };
    const std::uint32_t cloth = dyeColor(color);
    fillRect(image, u(0.28), u(0.12), u(0.72), u(0.84), cloth);
    for (int y = u(0.66); y < u(0.84); ++y) {  // the notch at the bottom
        const int cut = (y - u(0.66)) * 2;
        fillRect(image, u(0.5) - cut / 2, y, u(0.5) + cut / 2, y + 1, 0);
        for (int x = u(0.5) - cut / 2; x < u(0.5) + cut / 2; ++x) {
            if (x >= 0 && x < size) image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4 + 3] = 0;
        }
    }
    fillRect(image, u(0.22), u(0.06), u(0.78), u(0.13), 0x7A5C34);  // the crossbar
    return image;
}

// The rectangle [x0,x0+w) x [y0,y0+h) of a picture, as a new w x h picture (out-of-bounds pixels are transparent).
Image cropImage(const Image& src, int x0, int y0, int w, int h) {
    Image out;
    out.width = w;
    out.height = h;
    out.rgba.assign(static_cast<std::size_t>(std::max(0, w)) * static_cast<std::size_t>(std::max(0, h)) * 4, 0);
    for (int y = 0; y < h; ++y) {
        const int sy = y0 + y;
        if (sy < 0 || sy >= src.height) continue;
        for (int x = 0; x < w; ++x) {
            const int sx = x0 + x;
            if (sx < 0 || sx >= src.width) continue;
            std::copy_n(&src.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(src.width) + static_cast<std::size_t>(sx)) * 4], 4,
                        &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4]);
        }
    }
    return out;
}

// A shield (drawn by code in the game): its real front face — the wooden board with its metal rim and boss — standing upright,
// its true 12:22 proportions kept, filling most of the icon. `front` is that face already cut from the shield texture; if it is
// empty (the texture could not be read) a plain wood-and-metal board is drawn instead.
Image shieldStandIn(int size, const Image& front) {
    Image image = blankImage(size);
    if (!front.empty() && front.width > 0 && front.height > 0) {
        const int h = std::max(1, static_cast<int>(std::lround(size * 0.94)));
        const int w = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * front.width / front.height)));
        const int ox = (size - w) / 2, oy = (size - h) / 2;
        for (int y = 0; y < h; ++y) {
            const int sy = std::min(front.height - 1, y * front.height / h);
            for (int x = 0; x < w; ++x) {
                const int sx = std::min(front.width - 1, x * front.width / w);
                const std::size_t si = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(front.width) + static_cast<std::size_t>(sx)) * 4;
                if (front.rgba[si + 3] == 0) continue;
                const int dx = ox + x, dy = oy + y;
                if (dx < 0 || dy < 0 || dx >= size || dy >= size) continue;
                std::copy_n(&front.rgba[si], 4, &image.rgba[(static_cast<std::size_t>(dy) * static_cast<std::size_t>(size) + static_cast<std::size_t>(dx)) * 4]);
            }
        }
        return image;
    }
    const auto u = [&](double f) { return static_cast<int>(std::lround(f * size)); };
    for (int y = u(0.10); y < u(0.92); ++y) {
        const double t = static_cast<double>(y - u(0.10)) / static_cast<double>(u(0.82));
        const double half = t < 0.55 ? 0.30 : 0.30 * (1.0 - (t - 0.55) / 0.45) + 0.02;
        const int x0 = u(0.5 - half), x1 = u(0.5 + half);
        fillRect(image, x0, y, x1, y + 1, 0xA0A0A8);
        fillRect(image, x0 + 2, y, x1 - 2, y + 1, 0x8F6A3D);
    }
    fillRect(image, u(0.20), u(0.10), u(0.80), u(0.14), 0xA0A0A8);
    return image;
}

struct Vtx {
    double sx, sy, depth;  // screen position (pixels) and depth (bigger: nearer)
    double u, v;           // picture coordinates, 0..1
};

using Vec3 = std::array<double, 3>;

// Quaternionf.rotationXYZ(x, y, z): the matrix Rx * Ry * Rz, so z is turned first.
Vec3 rotateXYZ(Vec3 p, const std::array<float, 3>& degrees) {
    const double ax = degrees[0] * kPi / 180.0, ay = degrees[1] * kPi / 180.0, az = degrees[2] * kPi / 180.0;
    const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az), sz = std::sin(az);
    double x = p[0] * cz - p[1] * sz;
    double y = p[0] * sz + p[1] * cz;
    double z = p[2];
    const double x2 = x * cy + z * sy;
    const double z2 = -x * sy + z * cy;
    x = x2;
    z = z2;
    const double y3 = y * cx - z * sx;
    const double z3 = y * sx + z * cx;
    return {x, y3, z3};
}

// Draws baked quads like the GUI draws an item with a display transform, into a `size` x `size` picture. The image is made at
// twice the size and averaged down so the edges are smooth.
Image rasterize(const BlockModels& models, const std::vector<ModelQuad>& quads, const DisplayTransform& display, bool litLikeBlock,
                const std::vector<std::uint32_t>& tints, int size, const Image* textureOverride = nullptr) {
    constexpr int kSuper = 2;
    const int w = size * kSuper;
    Image out = blankImage(size);
    if (quads.empty()) return out;

    const auto project = [&](const std::array<float, 3>& p, double u, double v) {
        Vec3 c{(p[0] - 0.5) * display.scale[0], (p[1] - 0.5) * display.scale[1], (p[2] - 0.5) * display.scale[2]};
        c = rotateXYZ(c, display.rotation);
        c[0] += display.translation[0] / 16.0;
        c[1] += display.translation[1] / 16.0;
        c[2] += display.translation[2] / 16.0;
        return Vtx{w / 2.0 + c[0] * w, w / 2.0 - c[1] * w, c[2], u, v};
    };

    std::vector<float> depth(static_cast<std::size_t>(w) * static_cast<std::size_t>(w), -1e9f);
    std::vector<unsigned char> color(static_cast<std::size_t>(w) * static_cast<std::size_t>(w) * 4, 0);

    for (const ModelQuad& quad : quads) {
        Image layerCopy;
        const Image* tilePtr = textureOverride;  // an entity model brings its own picture (a 64 x 64 chest texture), not a block texture layer
        if (tilePtr == nullptr) {
            layerCopy = models.layerImage(quad.layer);
            tilePtr = &layerCopy;
        }
        const Image& tile = *tilePtr;
        if (tile.empty()) continue;
        Vtx v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = project(quad.pos[static_cast<std::size_t>(i)], quad.uv[static_cast<std::size_t>(i)][0], quad.uv[static_cast<std::size_t>(i)][1]);
        }
        // The face's normal in view space (y up, z towards the viewer) from its corners: counter-clockwise seen from outside.
        const double e1x = v[1].sx - v[0].sx, e1y = -(v[1].sy - v[0].sy), e1z = v[1].depth - v[0].depth;
        const double e2x = v[2].sx - v[0].sx, e2y = -(v[2].sy - v[0].sy), e2z = v[2].depth - v[0].depth;
        double nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length < 1e-9) continue;
        nx /= length;
        ny /= length;
        nz /= length;
        const bool front = nz > 0;
        const bool twoSided = quad.type != RenderType::Opaque;
        if (!front && !twoSided) continue;
        // The game's own GUI lighting for block models (Lighting.ITEMS_3D with light.glsl): two lamps,
        // normalize(0.2, 1, -0.7) and normalize(-0.2, 1, 0.7), placed in the model's own space, and
        //   shade = min(1, (max(0, dot(L0, n)) + max(0, dot(L1, n))) * 0.6 + 0.4).
        // A cube comes out at 1.0 on top, 0.74 on north and south, 0.50 on east and west and 0.4 underneath.
        double shade = 1.0;
        if (litLikeBlock && quad.shade) {
            const auto& q0 = quad.pos[0];
            const auto& q1 = quad.pos[1];
            const auto& q2 = quad.pos[2];
            const double a[3] = {q1[0] - q0[0], q1[1] - q0[1], q1[2] - q0[2]};
            const double b[3] = {q2[0] - q0[0], q2[1] - q0[1], q2[2] - q0[2]};
            double m[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
            const double mlen = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            if (mlen > 1e-9) {
                for (double& c : m) c /= mlen;
                constexpr double kL0[3] = {0.161696, 0.808479, -0.565936};
                constexpr double kL1[3] = {-0.161696, 0.808479, 0.565936};
                const double d0 = std::max(0.0, kL0[0] * m[0] + kL0[1] * m[1] + kL0[2] * m[2]);
                const double d1 = std::max(0.0, kL1[0] * m[0] + kL1[1] * m[1] + kL1[2] * m[2]);
                shade = std::min(1.0, (d0 + d1) * 0.6 + 0.4);
            }
        }
        double tr = 1.0, tg = 1.0, tb = 1.0;
        if (quad.tint >= 0 && static_cast<std::size_t>(quad.tint) < tints.size()) {
            const std::uint32_t t = tints[static_cast<std::size_t>(quad.tint)];
            tr = ((t >> 16) & 0xFF) / 255.0;
            tg = ((t >> 8) & 0xFF) / 255.0;
            tb = (t & 0xFF) / 255.0;
        }

        const auto triangle = [&](const Vtx& a, const Vtx& b, const Vtx& c) {
            const double minX = std::min({a.sx, b.sx, c.sx}), maxX = std::max({a.sx, b.sx, c.sx});
            const double minY = std::min({a.sy, b.sy, c.sy}), maxY = std::max({a.sy, b.sy, c.sy});
            const double det = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
            if (std::abs(det) < 1e-9) return;
            for (int py = std::max(0, static_cast<int>(std::floor(minY))); py <= std::min(w - 1, static_cast<int>(std::ceil(maxY))); ++py) {
                for (int px = std::max(0, static_cast<int>(std::floor(minX))); px <= std::min(w - 1, static_cast<int>(std::ceil(maxX))); ++px) {
                    const double x = px + 0.5, y = py + 0.5;
                    const double l1 = ((x - a.sx) * (c.sy - a.sy) - (y - a.sy) * (c.sx - a.sx)) / det;
                    const double l2 = ((b.sx - a.sx) * (y - a.sy) - (b.sy - a.sy) * (x - a.sx)) / det;
                    const double l0 = 1.0 - l1 - l2;
                    if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6) continue;
                    const double z = l0 * a.depth + l1 * b.depth + l2 * c.depth;
                    const std::size_t at = static_cast<std::size_t>(py) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px);
                    if (z <= static_cast<double>(depth[at]) - 1e-6) continue;
                    const double u = l0 * a.u + l1 * b.u + l2 * c.u;
                    const double t = l0 * a.v + l1 * b.v + l2 * c.v;
                    const int tx = std::clamp(static_cast<int>(std::floor(u * tile.width)), 0, tile.width - 1);
                    const int ty = std::clamp(static_cast<int>(std::floor(t * tile.height)), 0, tile.height - 1);
                    const unsigned char* s = &tile.rgba[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(tile.width) + static_cast<std::size_t>(tx)) * 4];
                    if (s[3] < 26) continue;
                    depth[at] = static_cast<float>(z);
                    unsigned char* d = &color[at * 4];
                    d[0] = static_cast<unsigned char>(std::min(255.0, s[0] * shade * tr));
                    d[1] = static_cast<unsigned char>(std::min(255.0, s[1] * shade * tg));
                    d[2] = static_cast<unsigned char>(std::min(255.0, s[2] * shade * tb));
                    d[3] = s[3];
                }
            }
        };
        triangle(v[0], v[1], v[2]);
        triangle(v[0], v[2], v[3]);
    }

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy2 = 0; sy2 < kSuper; ++sy2) {
                for (int sx2 = 0; sx2 < kSuper; ++sx2) {
                    const unsigned char* p = &color[(static_cast<std::size_t>(y * kSuper + sy2) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x * kSuper + sx2)) * 4];
                    r += p[0] * p[3];
                    g += p[1] * p[3];
                    b += p[2] * p[3];
                    a += p[3];
                }
            }
            unsigned char* d = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
            if (a > 0) {
                d[0] = static_cast<unsigned char>(r / a);
                d[1] = static_cast<unsigned char>(g / a);
                d[2] = static_cast<unsigned char>(b / a);
                d[3] = static_cast<unsigned char>(a / (kSuper * kSuper));
            }
        }
    }
    return out;
}

// A unit cube whose six faces show one picture: the stand-in for models the game draws with code.
std::vector<ModelQuad> cubeQuads(std::uint16_t layer) {
    struct FaceCorners {
        int sel[4][3];
    };
    static constexpr FaceCorners kCorners[kFaceCount] = {
        /* east  */ {{{1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}}},
        /* west  */ {{{0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}}},
        /* up    */ {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
        /* down  */ {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}},
        /* south */ {{{0, 1, 1}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}}},
        /* north */ {{{1, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}}},
    };
    static constexpr float kUv[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    std::vector<ModelQuad> out;
    for (int face = 0; face < kFaceCount; ++face) {
        ModelQuad q;
        q.layer = layer;
        q.face = static_cast<std::uint8_t>(face);
        q.cullFace = -1;
        for (int i = 0; i < 4; ++i) {
            q.pos[static_cast<std::size_t>(i)] = {static_cast<float>(kCorners[face].sel[i][0]), static_cast<float>(kCorners[face].sel[i][1]),
                                                  static_cast<float>(kCorners[face].sel[i][2])};
            q.uv[static_cast<std::size_t>(i)] = {kUv[i][0], kUv[i][1]};
        }
        out.push_back(q);
    }
    return out;
}

// The gui transform of block models that do not say: the block default (30, 225, 0, scale 0.625).
DisplayTransform defaultGui() {
    DisplayTransform t;
    t.rotation = {30.0f, 225.0f, 0.0f};
    t.scale = {0.625f, 0.625f, 0.625f};
    t.present = true;
    return t;
}

}  // namespace

ResolvedItemModel resolveItemModel(const json& definition, const BlockModels* colors, const std::string& context) {
    ResolvedItemModel out;
    if (!definition.is_object() || !definition.contains("model")) {
        out.missing = true;
        return out;
    }
    resolveNode(definition["model"], colors, out, 0, context);
    if (out.parts.empty()) out.empty = true;
    return out;
}

// ---------------------------------------------------------------------------------------------------------------

ItemIcons::ItemIcons(BlockModels& models) : models_(models) {}

ItemIcons::~ItemIcons() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ItemIcons::workerLoop() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);  // pictures can wait; a game running beside this one cannot
#endif
    for (;;) {
        ItemStack stack;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            idle_.notify_all();
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            stack = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }
        const std::string key = stack.type().itemModel;
        Image image = render(stack, iconPixels_.load(std::memory_order_relaxed));
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            ready_[key] = std::move(image);
            queued_.erase(key);
            busy_ = false;
        }
    }
}

void ItemIcons::prefetch(const ItemStack& stack) {
    if (stack.isEmpty()) return;
    const std::string key = stack.type().itemModel;
    if (cell_.count(key) != 0) return;
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (ready_.count(key) != 0 || !queued_.insert(key).second) return;
    queue_.push_back(stack);
    if (!worker_.joinable()) worker_ = std::thread([this] { workerLoop(); });
    wake_.notify_one();
}

void ItemIcons::waitForPrefetch() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

std::size_t ItemIcons::readyCount() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return ready_.size();
}

// (Callers hold renderMutex_, or are the only ones: tests.)
const ResolvedItemModel& ItemIcons::resolved(const std::string& itemModel, const std::string& context) {
    const std::string cacheKey = context == "gui" ? itemModel : itemModel + "|" + context;
    const auto found = resolved_.find(cacheKey);
    if (found != resolved_.end()) return found->second;
    ResolvedItemModel result;
    const auto text = models_.resources().readText(ResourceLocation("minecraft", "items/" + itemModel + ".json"));
    if (!text) {
        result.missing = true;
    } else {
        const json doc = json::parse(*text, nullptr, false, true);
        if (doc.is_discarded()) {
            result.missing = true;
        } else {
            result = resolveItemModel(doc, &models_, context);
        }
    }
    return resolved_.emplace(cacheKey, std::move(result)).first->second;
}

struct ItemIcons::Rendered {
    Image image;
    std::string kind;  // "flat", "block", "stand-in", "missing"
};

ItemIcons::Rendered ItemIcons::renderPart(const IconPart& part, int size) {
    Rendered r;
    r.image = blankImage(size);
    const auto info = models_.itemModelInfo(part.model);

    const auto missingCube = [&] {
        r.kind = "missing";
        r.image = rasterize(models_, cubeQuads(models_.layerFor("missingno")), defaultGui(), true, {}, size);
    };
    if (!info) {
        missingCube();
        return r;
    }
    if (info->generated) {
        r.kind = "flat";
        for (std::size_t i = 0; i < info->layers.size(); ++i) {
            const std::string& name = info->layers[i];
            auto it = textures_.find(name);
            if (it == textures_.end()) {
                Image image;
                if (const auto bytes = models_.resources().readBytes(ResourceLocation("minecraft", "textures/" + name + ".png"))) {
                    if (auto decoded = decodePng(*bytes); decoded && !decoded->empty()) image = firstFrame(*decoded);
                }
                if (image.empty()) image = models_.layerImage(models_.layerFor("missingno"));  // (the game's magenta and black squares)
                it = textures_.emplace(name, std::move(image)).first;
            }
            Image layer = resizeNearest(it->second, size);
            if (i < part.tints.size()) tintImage(layer, part.tints[i]);
            drawOver(r.image, layer);
        }
        return r;
    }
    if (!part.special.empty() || info->builtinEntity || !info->quads || info->quads->empty()) {
        // The chests: the game draws the chest model itself (ChestSpecialRenderer: the closed chest, as it stands in the world with its front on
        // the south side, which the item's display transform turns to face the viewer). The model comes from entity-models.json and its
        // picture from textures/entity/chest/<name>.png.
        if (part.special == "chest") {
            if (!chestModelTried_) {
                chestModelTried_ = true;
                std::string error;
                chestModel_ = loadEntityModel(models_.resources().dataRoot(), "minecraft:chest#main", error);
                if (!chestModel_) std::printf("[falcon] chest icon: %s\n", error.c_str());
            }
            const std::string textureName = "entity/chest/" + (part.specialTexture.empty() ? std::string("normal") : part.specialTexture);
            auto it = textures_.find(textureName);
            if (it == textures_.end()) {
                Image image;
                if (const auto bytes = models_.resources().readBytes(ResourceLocation("minecraft", "textures/" + textureName + ".png"))) {
                    if (auto decoded = decodePng(*bytes); decoded && !decoded->empty()) image = firstFrame(*decoded);
                }
                it = textures_.emplace(textureName, std::move(image)).first;
            }
            if (chestModel_ && !it->second.empty()) {
                std::vector<ModelQuad> quads;
                const auto addPart = [&](const auto& self, const EntityPart& p, float ox, float oy, float oz) -> void {
                    // (The lid is shut, so the parts only need their offsets; block entity models are y-up and in 1/16 blocks.)
                    const float px = ox + p.offset[0], py = oy + p.offset[1], pz = oz + p.offset[2];
                    for (const EntityPolygon& poly : p.polygons) {
                        ModelQuad q;
                        for (std::size_t i = 0; i < 4; ++i) {
                            q.pos[i] = {(poly.vertex[i][0] + px) / 16.0f, (poly.vertex[i][1] + py) / 16.0f, (poly.vertex[i][2] + pz) / 16.0f};
                            q.uv[i] = {poly.vertex[i][3], poly.vertex[i][4]};
                        }
                        quads.push_back(q);
                    }
                    for (const EntityPart& c : p.children) self(self, c, px, py, pz);
                };
                addPart(addPart, *chestModel_, 0.0f, 0.0f, 0.0f);
                r.kind = "entity";
                r.image = rasterize(models_, quads, info->display.count("gui") != 0 ? info->display.at("gui") : defaultGui(), true, {}, size, &it->second);
                return r;
            }
        }
        // A model the game draws with code (or one without any geometry): a stand-in picture.
        r.kind = "stand-in";
        if (part.special == "banner") {
            r.image = bannerStandIn(size, part.specialDetail);
            return r;
        }
        if (part.special == "shield") {
            Image front;  // the shield's front face: pixels [1,13) x [1,23) of the 64x64 shield texture (iron rim + wood grain)
            if (const auto bytes = models_.resources().readBytes(ResourceLocation("minecraft", "textures/entity/shield/shield_base_nopattern.png"))) {
                if (auto decoded = decodePng(*bytes); decoded && !decoded->empty()) front = cropImage(*decoded, 1, 1, 12, 22);
            }
            r.image = shieldStandIn(size, front);
            return r;
        }
        std::uint16_t layer = info->particle.empty() ? std::uint16_t{0} : models_.layerFor(info->particle);
        if (layer == 0) {  // nothing to take a picture from
            missingCube();
            return r;
        }
        r.image = rasterize(models_, cubeQuads(layer), defaultGui(), true, part.tints, size);
        return r;
    }
    r.kind = "block";
    const DisplayTransform gui = info->display.count("gui") != 0 ? info->display.at("gui") : DisplayTransform{};
    r.image = rasterize(models_, *info->quads, gui, info->guiLight != "front", part.tints, size);
    return r;
}

Image ItemIcons::render(const ItemStack& stack, int size) {
    if (stack.isEmpty()) return blankImage(size);
    return renderModel(stack.type().itemModel, size);
}

Image ItemIcons::renderModel(const std::string& itemModel, int size) {
    Image out = blankImage(size);
    std::lock_guard<std::mutex> lock(renderMutex_);
    const ResolvedItemModel& model = resolved(itemModel);
    if (model.missing) {
        IconPart part;
        part.model = "builtin/missing";
        return renderPart(part, size).image;
    }
    for (const IconPart& part : model.parts) drawOver(out, renderPart(part, size).image);
    return out;
}

std::string ItemIcons::kindOf(const ItemStack& stack) {
    if (stack.isEmpty()) return "empty";
    return kindOfModel(stack.type().itemModel);
}

std::string ItemIcons::kindOfModel(const std::string& itemModel) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    const ResolvedItemModel& model = resolved(itemModel);
    if (model.missing) return "missing";
    if (model.parts.empty()) return "empty";
    std::string kind;
    for (const IconPart& part : model.parts) {
        const std::string k = renderPart(part, 8).kind;
        if (kind.empty() || k == "missing" || (k == "stand-in" && kind != "missing")) kind = k;
    }
    return kind;
}

void ItemIcons::setScale(int guiScale) {
    const int s = std::clamp(16 * guiScale, 16, kMaxSize);
    if (s == iconPixels_.load(std::memory_order_relaxed)) return;
    iconPixels_.store(s, std::memory_order_relaxed);
    // The icons were made at the old size; drop them so they are re-made at the new one (crisp at 1:1 again).
    cell_.clear();
    next_ = 0;
    std::lock_guard<std::mutex> lock(queueMutex_);
    ready_.clear();
    queued_.clear();
    queue_.clear();
}

ItemIcons::Slot ItemIcons::slot(const ItemStack& stack) {
    if (atlas_.id() == 0) atlas_ = Texture2D::empty(kMaxSize * kColumns, kMaxSize * kRows, FilterMode::Nearest);  // (the first picture: now there is a GL context)
    const std::string key = stack.type().itemModel;
    auto it = cell_.find(key);
    if (it == cell_.end()) {
        const int capacity = kColumns * kRows;
        const int index = next_ % capacity;
        if (next_ >= capacity) {  // reuse the oldest cell
            for (auto e = cell_.begin(); e != cell_.end(); ++e) {
                if (e->second == index) {
                    cell_.erase(e);
                    break;
                }
            }
        }
        ++next_;
        Image picture;
        bool prepared = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (const auto r = ready_.find(key); r != ready_.end()) {  // made in the background already
                picture = std::move(r->second);
                ready_.erase(r);
                prepared = true;
            }
        }
        if (!prepared || picture.width != iconPixels_.load(std::memory_order_relaxed)) picture = render(stack, iconPixels_.load(std::memory_order_relaxed));
        atlas_.upload((index % kColumns) * kMaxSize, (index / kColumns) * kMaxSize, picture);
        it = cell_.emplace(key, index).first;
    }
    const int index = it->second;
    return {static_cast<float>((index % kColumns) * kMaxSize), static_cast<float>((index / kColumns) * kMaxSize)};
}

}  // namespace falcon
