#include "falcon/render/block_icons.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

struct Vtx {
    double sx, sy, depth;  // screen position (pixels) and depth (bigger: nearer)
    double u, v;           // picture coordinates, 0..1
};

}  // namespace

Image renderBlockIcon(const BlockModels& models, Block block, int size) {
    constexpr int kSuper = 2;  // drawn at twice the size and averaged down
    const int w = size * kSuper;
    Image out;
    out.width = size;
    out.height = size;
    out.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);

    const std::vector<ModelQuad> quads = models.iconQuads(block);
    if (quads.empty()) return out;
    const StateRender& state = models.render(block);

    // The game's gui transform for block models: rotate 225 degrees about y, then 30 about x, scale 0.625, around the
    // middle of the block.
    constexpr double kPi = 3.14159265358979323846;
    const double ry = 225.0 * kPi / 180.0, rx = 30.0 * kPi / 180.0;
    const double cy = std::cos(ry), sy = std::sin(ry), cx = std::cos(rx), sx = std::sin(rx);
    const double pixelsPerBlock = w * 0.625;
    const auto project = [&](const std::array<float, 3>& p, double u, double v) {
        const double x = p[0] - 0.5, y = p[1] - 0.5, z = p[2] - 0.5;
        const double x1 = x * cy + z * sy;
        const double z1 = -x * sy + z * cy;
        const double y2 = y * cx - z1 * sx;
        const double z2 = y * sx + z1 * cx;
        return Vtx{w / 2.0 + x1 * pixelsPerBlock, w / 2.0 - y2 * pixelsPerBlock, z2, u, v};
    };

    std::vector<float> depth(static_cast<std::size_t>(w) * static_cast<std::size_t>(w), -1e9f);
    std::vector<unsigned char> color(static_cast<std::size_t>(w) * static_cast<std::size_t>(w) * 4, 0);

    for (const ModelQuad& quad : quads) {
        const Image tile = models.layerImage(quad.layer);
        if (tile.empty()) continue;
        Vtx v[4];
        for (int i = 0; i < 4; ++i) v[i] = project(quad.pos[static_cast<std::size_t>(i)], quad.uv[static_cast<std::size_t>(i)][0], quad.uv[static_cast<std::size_t>(i)][1]);
        // Facing: the sign of the screen-space area (the quad is counter-clockwise from outside).
        const double area = (v[1].sx - v[0].sx) * (v[2].sy - v[0].sy) - (v[1].sy - v[0].sy) * (v[2].sx - v[0].sx);
        const bool front = area < 0;  // screen y points down, so counter-clockwise on screen is negative here
        const bool twoSided = quad.type != RenderType::Opaque;
        if (!front && !twoSided) continue;
        // Which face of the cube this is on the picture: the top is bright, the left and right sides darker.
        float shade = 0.8f;
        if (front) shade = quad.face == kUp ? 1.0f : quad.face == kEast ? 0.8f : quad.face == kNorth ? 0.6f : 0.8f;
        if (!quad.shade) shade = 1.0f;
        std::uint32_t tint = 0xFFFFFF;
        if (quad.tint >= 0) tint = models.tintColor(state, 0, 0, 0);
        const double tr = ((tint >> 16) & 0xFF) / 255.0, tg = ((tint >> 8) & 0xFF) / 255.0, tb = (tint & 0xFF) / 255.0;

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

    // Average down (alpha-weighted so edges stay clean).
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

BlockIcons::BlockIcons(BlockModels& models)
    : models_(models), atlas_(Texture2D::empty(kSize * kColumns, kSize * kRows, FilterMode::Nearest)) {}

BlockIcons::Slot BlockIcons::slot(Block block) {
    auto it = cell_.find(stateId(block));
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
        atlas_.upload((index % kColumns) * kSize, (index / kColumns) * kSize, renderBlockIcon(models_, block, kSize));
        it = cell_.emplace(stateId(block), index).first;
    }
    const int index = it->second;
    return {static_cast<float>((index % kColumns) * kSize), static_cast<float>((index / kColumns) * kSize)};
}

}  // namespace falcon
