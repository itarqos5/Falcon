// Density-function evaluation: the point path (memoised, lazy) and the volume path (corner grids + column passes).
//
// All float arithmetic is written exactly as the game's samplers do it (no fused multiply-add, Java Math.min/max,
// lerp = a + t * (b - a), ...); this file must be compiled with -ffp-contract=off.
//
// Volume path, in the spirit of the game's DensityVolume sampling (spec section 6):
//   1. every `interpolated` node evaluates its input at the cell corners of the volume (five-by-forty-nine-by-five
//      corners for a chunk), with the point evaluator: lazy, and memoised per (x, z) for everything that does not
//      depend on y (climate noises, splines ...).
//   2. the volume is then processed one (x, z) column at a time: each `interpolated` node fills its column (z-lerp,
//      x-lerp, then y by repeated addition of a step), and the nodes above them combine columns elementwise. A column
//      is 384 floats, so all the temporaries stay in the L1 cache.

#include "falcon/worldgen/java_math.hpp"
#include "density_internal.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace falcon::worldgen {

using namespace detail;

struct EvaluatorAccess {
    static float eval(DensityEvaluator& e, int node) { return e.eval(node); }
};

// ---------------------------------------------------------------------------------------------------------------
// Splines (util.CubicSpline.Multipoint.sample). `coord(node)` returns a coordinate function's value.
namespace {

template <class CoordFn>
float splineSample(const GraphData& g, int multiIndex, CoordFn& coord) {
    const SplineMulti& m = g.multis[static_cast<std::size_t>(multiIndex)];
    const SplinePoint* pts = &g.points[static_cast<std::size_t>(m.firstPoint)];
    const int n = m.count;
    const float x = coord(m.coordNode);
    // Mth.binarySearch: the first index whose location is greater than x
    int lo = 0, len = n;
    while (len > 0) {
        const int half = len / 2;
        const int mid = lo + half;
        if (x < pts[mid].location) {
            len = half;
        } else {
            lo = mid + 1;
            len -= half + 1;
        }
    }
    const int i = lo - 1;
    const int last = n - 1;
    const auto value = [&](const SplinePoint& p) { return p.sub >= 0 ? splineSample(g, p.sub, coord) : p.constant; };
    if (i < 0) {
        const float y = value(pts[0]);
        const float d = pts[0].derivative;
        return d == 0.0f ? y : y + d * (x - pts[0].location);
    }
    if (i == last) {
        const float y = value(pts[last]);
        const float d = pts[last].derivative;
        return d == 0.0f ? y : y + d * (x - pts[last].location);
    }
    const float x0 = pts[i].location;
    const float x1 = pts[i + 1].location;
    const float t = (x - x0) / (x1 - x0);
    const float y0 = value(pts[i]);
    const float y1 = value(pts[i + 1]);
    const float d0 = pts[i].derivative;
    const float d1 = pts[i + 1].derivative;
    const float a = d0 * (x1 - x0) - (y1 - y0);
    const float b = (-d1) * (x1 - x0) + (y1 - y0);
    return lerpf(t, y0, y1) + (t * (1.0f - t)) * lerpf(t, a, b);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Point evaluation
DensityEvaluator::DensityEvaluator(const DensityGraph& graph)
    : g_(&graph.data()), graphId_(graph.data().id), val_(graph.data().nodes.size(), 0.0f), stamp_(graph.data().nodes.size(), 0u) {}

void DensityEvaluator::bumpAll() {
    if (++stampAll_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        stampAll_ = 1;
    }
}
void DensityEvaluator::bumpXZ() {
    if (++stampXZ_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        stampXZ_ = 1;
    }
}

void DensityEvaluator::setPosition(int x, int y, int z) {
    x_ = x;
    y_ = y;
    z_ = z;
    if (!keyValid_ || x != keyX_ || z != keyZ_) {
        bumpXZ();
        keyX_ = x;
        keyZ_ = z;
        keyValid_ = true;
    }
    bumpAll();
}

float DensityEvaluator::value(const DensityFunction& f) { return eval(f.node_); }

float DensityEvaluator::value(const DensityFunction& f, int x, int y, int z) {
    setPosition(x, y, z);
    return eval(f.node_);
}

float DensityEvaluator::eval(int id) {
    const Node& n = g_->nodes[static_cast<std::size_t>(id)];
    if (n.op == Op::Constant) return n.f0;
    if (!n.memo) return compute(n);
    const bool yDep = (n.axes & kAxisY) != 0;
    const auto idx = static_cast<std::size_t>(id);
    if (stamp_[idx] == (yDep ? stampAll_ : stampXZ_)) return val_[idx];
    const float v = compute(n);
    stamp_[idx] = yDep ? stampAll_ : stampXZ_;  // re-read: nested re-positioning (find_top_surface, interpolated) bumps the stamps
    val_[idx] = v;
    return v;
}

float DensityEvaluator::evalAt(int id, int x, int y, int z) {
    const int sx = x_, sy = y_, sz = z_;
    setPosition(x, y, z);
    const float v = eval(id);
    setPosition(sx, sy, sz);
    return v;
}

float DensityEvaluator::compute(const Node& n) {
    switch (n.op) {
        case Op::Constant: return n.f0;
        case Op::Add: return eval(n.a) + eval(n.b);
        case Op::Sub: return eval(n.a) - eval(n.b);
        case Op::Mul: {
            const float l = eval(n.a);
            if (l == 0.0f) return 0.0f;  // MulSampler: left == 0 -> 0 without evaluating the right side
            return l * eval(n.b);
        }
        case Op::Div: {
            const float l = eval(n.a);
            if (l == 0.0f) return 0.0f;
            return l / eval(n.b);
        }
        case Op::Min: return jmin(eval(n.a), eval(n.b));
        case Op::Max: return jmax(eval(n.a), eval(n.b));
        case Op::AddC: return eval(n.a) + n.f0;
        case Op::SubCL: return n.f0 - eval(n.a);
        case Op::MulC: return eval(n.a) * n.f0;
        case Op::DivCL: return n.f0 / eval(n.a);
        case Op::MinC: return jmin(eval(n.a), n.f0);
        case Op::MaxC: return jmax(eval(n.a), n.f0);
        case Op::Abs: return std::fabs(eval(n.a));
        case Op::Square: {
            const float x = eval(n.a);
            return x * x;
        }
        case Op::Cube: {
            const float x = eval(n.a);
            return x * x * x;
        }
        case Op::HalfNegative: {
            const float x = eval(n.a);
            return x > 0.0f ? x : x * 0.5f;
        }
        case Op::QuarterNegative: {
            const float x = eval(n.a);
            return x > 0.0f ? x : x * 0.25f;
        }
        case Op::Squeeze: {
            const float c = clampf(eval(n.a), -1.0f, 1.0f);
            return c / 2.0f - (c * c * c) / 24.0f;
        }
        case Op::Negate: return -eval(n.a);
        case Op::Clamp: return clampf(eval(n.a), n.f0, n.f1);
        case Op::Lerp: {
            const float alpha = eval(n.a);
            if (alpha == 0.0f) return eval(n.b);
            if (alpha == 1.0f) return eval(n.c);
            const float first = eval(n.b);
            const float second = eval(n.c);
            return lerpf(alpha, first, second);
        }
        case Op::RangeChoice: {
            const float v = eval(n.a);
            return (v >= n.f0 && v < n.f1) ? eval(n.b) : eval(n.c);
        }
        case Op::IntervalSelect: {
            const float v = eval(n.a);
            for (int i = 0; i < n.i1; ++i) {
                if (v < g_->thresholds[static_cast<std::size_t>(n.i0 + i)]) return eval(g_->extra[static_cast<std::size_t>(n.i2 + i)]);
            }
            return eval(g_->extra[static_cast<std::size_t>(n.i2 + n.i1)]);
        }
        case Op::Gradient: {
            const int c = n.i0 == 0 ? x_ : n.i0 == 1 ? y_ : z_;
            const int cc = c < n.i2 ? n.i2 : (c > n.i3 ? n.i3 : c);
            return n.f0 + static_cast<float>(cc - n.i1) * n.f1;
        }
        case Op::Noise: {
            const NormalNoise& nz = *g_->noises[static_cast<std::size_t>(n.i0)];
            if (n.i1 == 0) return nz.get(static_cast<double>(x_) * n.d0, static_cast<double>(y_) * n.d1, static_cast<double>(z_) * n.d0);
            const double sx = static_cast<double>(eval(n.a));
            const double sz = static_cast<double>(eval(n.c));
            const double a = static_cast<double>(x_) * n.d0 + sx;
            double b = static_cast<double>(y_) * n.d1;
            if (n.i1 == 2) b = b + static_cast<double>(eval(n.b));
            const double c = static_cast<double>(z_) * n.d0 + sz;
            return nz.get(a, b, c);
        }
        case Op::ShiftA: {
            const NormalNoise& nz = *g_->noises[static_cast<std::size_t>(n.i0)];
            return nz.get(static_cast<double>(x_) * 0.25, static_cast<double>(y_) * 0.0, static_cast<double>(z_) * 0.25) * 4.0f;
        }
        case Op::ShiftB: {
            const NormalNoise& nz = *g_->noises[static_cast<std::size_t>(n.i0)];
            return nz.get(static_cast<double>(z_) * 0.25, static_cast<double>(x_) * 0.25, 0.0) * 4.0f;
        }
        case Op::Shift: {
            const NormalNoise& nz = *g_->noises[static_cast<std::size_t>(n.i0)];
            return nz.get(static_cast<double>(x_) * 0.25, static_cast<double>(y_) * 0.25, static_cast<double>(z_) * 0.25) * 4.0f;
        }
        case Op::BlendedNoise: return g_->blended[static_cast<std::size_t>(n.i0)]->sample(x_, y_, z_);
        case Op::DistanceToPoint: {
            // DistanceMetric.compute(float, float, float) on the integer differences from the point
            const float dx = static_cast<float>(x_ - static_cast<int>(n.d0)), dy = static_cast<float>(y_ - static_cast<int>(n.d1)), dz = static_cast<float>(z_ - static_cast<int>(n.f0));
            if (n.i0 == 1) return std::fabs(dx) + std::fabs(dy) + std::fabs(dz);
            if (n.i0 == 2) return std::max(std::max(std::fabs(dx), std::fabs(dy)), std::fabs(dz));
            const float lengthSquared = dx * dx + dy * dy + dz * dz;
            if (n.i0 == 3) return lengthSquared;
            return static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));  // Mth.length: Mth.sqrt(lengthSquared)
        }
        case Op::Slice: {
            const int sx = x_, sy = y_, sz = z_;
            setPosition(n.i0 == 0 ? n.i1 : sx, n.i0 == 1 ? n.i1 : sy, n.i0 == 2 ? n.i1 : sz);
            const float v = eval(n.a);
            setPosition(sx, sy, sz);
            return v;
        }
        case Op::EndOuterIslands: {
            // EndIslandDensityFunction.getHeightValue without the main island (the distance function has it now): the best of the small islands round this spot.
            const SimplexNoise& noise = *g_->simplex[static_cast<std::size_t>(n.i0)];
            const int x = x_ / 8, z = z_ / 8;  // (Java's division: towards zero)
            const int i = x / 2, j = z / 2, k = x % 2, l = z % 2;
            float f = -100.0f;
            for (int m = -12; m <= 12; ++m) {
                for (int q = -12; q <= 12; ++q) {
                    const long long o = static_cast<long long>(i) + m, p = static_cast<long long>(j) + q;
                    if (o * o + p * p > 4096LL && static_cast<float>(noise.getValue(static_cast<double>(o), static_cast<double>(p))) < -0.9f) {
                        const float g = std::fmod(std::fabs(static_cast<float>(o)) * 3439.0f + std::fabs(static_cast<float>(p)) * 147.0f, 13.0f) + 9.0f;
                        const float h = static_cast<float>(k - m * 2), r2 = static_cast<float>(l - q * 2);
                        float r = 100.0f - std::sqrt(h * h + r2 * r2) * g;
                        r = std::clamp(r, -100.0f, 80.0f);
                        f = std::max(f, r);
                    }
                }
            }
            return static_cast<float>((static_cast<double>(f) - 8.0) / 128.0);
        }
        case Op::Spline: {
            auto coord = [this](int node) { return eval(node); };
            return splineSample(*g_, n.i0, coord);
        }
        case Op::FindTopSurface: {
            // YSampler(sampler, 0): independent of y; the input is sampled at y = 0 (bound) and at y = start, start - cell ...
            const int sx = x_, sy = y_, sz = z_;
            const int lower = n.i0;
            const int cell = n.i1;
            setPosition(sx, 0, sz);
            const float up = eval(n.b);
            const int start = static_cast<std::int32_t>(static_cast<std::uint32_t>(javaFloorF(up / static_cast<float>(cell))) * static_cast<std::uint32_t>(cell));
            float result = static_cast<float>(lower);
            if (start > lower) {
                for (int yy = start; yy >= lower; yy -= cell) {
                    setPosition(sx, yy, sz);
                    if (eval(n.a) > 0.0f) {
                        result = static_cast<float>(yy);
                        break;
                    }
                }
            }
            setPosition(sx, sy, sz);
            return result;
        }
        case Op::Interpolated: {
            const int cxz = n.i0, cy = n.i1;
            const int fx = floorMod(x_, cxz), fy = floorMod(y_, cy), fz = floorMod(z_, cxz);
            if (fx == 0 && fy == 0 && fz == 0) return eval(n.a);
            const int sx = x_, sy = y_, sz = z_;
            const int x0 = sx - fx, y0 = sy - fy, z0 = sz - fz;
            float c[8];  // index dx + 2 dy + 4 dz
            for (int dz = 0; dz < 2; ++dz) {
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        setPosition(x0 + dx * cxz, y0 + dy * cy, z0 + dz * cxz);
                        c[dx + 2 * dy + 4 * dz] = eval(n.a);
                    }
                }
            }
            setPosition(sx, sy, sz);
            const float tx = static_cast<float>(fx) / static_cast<float>(cxz);
            const float ty = static_cast<float>(fy) / static_cast<float>(cy);
            const float tz = static_cast<float>(fz) / static_cast<float>(cxz);
            // Mth.lerp3: x, then y, then z
            const float l0 = lerpf(ty, lerpf(tx, c[0], c[1]), lerpf(tx, c[2], c[3]));
            const float l1 = lerpf(ty, lerpf(tx, c[4], c[5]), lerpf(tx, c[6], c[7]));
            return lerpf(tz, l0, l1);
        }
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
namespace {

// One evaluator per (thread, graph), so DensityFunction::compute never allocates after its first call on a thread.
DensityEvaluator& threadEvaluator(const DensityGraph& graph) {
    thread_local std::vector<std::unique_ptr<DensityEvaluator>> cache;
    for (const auto& e : cache) {
        if (e->graphId() == graph.id()) return *e;
    }
    if (cache.size() >= 4) cache.erase(cache.begin());
    cache.push_back(std::make_unique<DensityEvaluator>(graph));
    return *cache.back();
}

struct InterpState {
    enum class Mode { Interpolate, Passthrough, Fallback };
    Mode mode = Mode::Passthrough;
    int cxz = 1, cy = 1;
    int cMinX = 0, cMinY = 0, cMinZ = 0;  // corner grid origin in cells
    int nX = 0, nY = 0, nZ = 0;           // number of cells
    int sizeX = 0, sizeY = 0, sizeZ = 0;  // number of corners
    float invXz = 1.0f, invY = 1.0f;
    std::vector<float> corners;           // index y + sizeY * (x + sizeX * z)
};

struct VolumeScratch {
    std::vector<float> arena;
    std::vector<InterpState> interps;
};

VolumeScratch& volumeScratch() {
    thread_local VolumeScratch s;
    return s;
}

// The corner grid of an `interpolated` node over volume v (spec 6.2).
void prepareInterp(DensityEvaluator& ev, const Node& n, const DensityVolume& v, InterpState& st) {
    const int cxz = n.i0, cy = n.i1;
    const bool alignedX = (v.stepX == cxz || v.sizeX == 1) && floorMod(v.minX, cxz) == 0;
    const bool alignedY = (v.stepY == cy || v.sizeY == 1) && floorMod(v.minY, cy) == 0;
    const bool alignedZ = (v.stepZ == cxz || v.sizeZ == 1) && floorMod(v.minZ, cxz) == 0;
    if (alignedX && alignedY && alignedZ) {  // a corner volume itself: the input is sampled as it is
        st.mode = InterpState::Mode::Passthrough;
        return;
    }
    if (v.stepX != 1 || v.stepY != 1 || v.stepZ != 1) {  // not specified by the game's data: fall back to the point path
        st.mode = InterpState::Mode::Fallback;
        return;
    }
    st.mode = InterpState::Mode::Interpolate;
    st.cxz = cxz;
    st.cy = cy;
    st.invXz = n.f0;
    st.invY = n.f1;
    st.cMinX = floorDiv(v.minX, cxz);
    st.cMinY = floorDiv(v.minY, cy);
    st.cMinZ = floorDiv(v.minZ, cxz);
    const int cMaxX = floorDiv(v.maxX(), cxz), cMaxY = floorDiv(v.maxY(), cy), cMaxZ = floorDiv(v.maxZ(), cxz);
    st.nX = cMaxX - st.cMinX + 1;
    st.nY = cMaxY - st.cMinY + 1;
    st.nZ = cMaxZ - st.cMinZ + 1;
    st.sizeX = floorMod(v.maxX(), cxz) == 0 ? st.nX : st.nX + 1;
    st.sizeY = floorMod(v.maxY(), cy) == 0 ? st.nY : st.nY + 1;
    st.sizeZ = floorMod(v.maxZ(), cxz) == 0 ? st.nZ : st.nZ + 1;
    const std::size_t total = static_cast<std::size_t>(st.sizeX) * static_cast<std::size_t>(st.sizeY) * static_cast<std::size_t>(st.sizeZ);
    if (st.corners.size() < total) st.corners.resize(total);
    // Columns outermost: everything that does not depend on y is computed once per corner column.
    for (int kz = 0; kz < st.sizeZ; ++kz) {
        const int z = (st.cMinZ + kz) * cxz;
        for (int kx = 0; kx < st.sizeX; ++kx) {
            const int x = (st.cMinX + kx) * cxz;
            float* col = &st.corners[static_cast<std::size_t>(st.sizeY) * (static_cast<std::size_t>(kx) + static_cast<std::size_t>(st.sizeX) * static_cast<std::size_t>(kz))];
            for (int ky = 0; ky < st.sizeY; ++ky) {
                ev.setPosition(x, (st.cMinY + ky) * cy, z);
                col[ky] = EvaluatorAccess::eval(ev, n.a);
            }
        }
    }
}

// The interpolated values of one (bx, bz) column (the game's fillCell: z-lerp, x-lerp, then y by repeated addition).
void interpColumn(const InterpState& st, const DensityVolume& v, int bx, int bz, float* out) {
    const int cx = floorDiv(bx, st.cxz) - st.cMinX;
    const int cz = floorDiv(bz, st.cxz) - st.cMinZ;
    const float tx = static_cast<float>(floorMod(bx, st.cxz)) * st.invXz;
    const float tz = static_cast<float>(floorMod(bz, st.cxz)) * st.invXz;
    const int cx1 = std::min(cx + 1, st.sizeX - 1);
    const int cz1 = std::min(cz + 1, st.sizeZ - 1);
    const std::size_t sY = static_cast<std::size_t>(st.sizeY);
    const float* c00 = &st.corners[sY * (static_cast<std::size_t>(cx) + static_cast<std::size_t>(st.sizeX) * static_cast<std::size_t>(cz))];
    const float* c10 = &st.corners[sY * (static_cast<std::size_t>(cx1) + static_cast<std::size_t>(st.sizeX) * static_cast<std::size_t>(cz))];
    const float* c01 = &st.corners[sY * (static_cast<std::size_t>(cx) + static_cast<std::size_t>(st.sizeX) * static_cast<std::size_t>(cz1))];
    const float* c11 = &st.corners[sY * (static_cast<std::size_t>(cx1) + static_cast<std::size_t>(st.sizeX) * static_cast<std::size_t>(cz1))];
    for (int cyi = 0; cyi < st.nY; ++cyi) {
        const int cy1 = std::min(cyi + 1, st.sizeY - 1);
        const float p000 = c00[cyi], p100 = c10[cyi], p001 = c01[cyi], p101 = c11[cyi];
        const float p010 = c00[cy1], p110 = c10[cy1], p011 = c01[cy1], p111 = c11[cy1];
        const int y0 = (st.cMinY + cyi) * st.cy - v.minY;
        const int startY = std::max(0, -y0);
        const int endY = std::min(st.cy, v.sizeY - y0) - 1;
        const float v27 = lerpf(tz, p000, p001);
        const float v28 = lerpf(tz, p010, p011);
        const float v29 = lerpf(tz, p100, p101);
        const float v30 = lerpf(tz, p110, p111);
        const float v34 = lerpf(tx, v27, v29);  // bottom plane at this (x, z)
        const float v35 = lerpf(tx, v28, v30);  // top plane
        const float step = (v35 - v34) * st.invY;
        float val = v34 + step * static_cast<float>(startY);
        float* o = out + (y0 + startY);
        for (int yy = startY; yy <= endY; ++yy) {
            *o++ = val;
            val = val + step;  // repeated float addition, NOT a lerp in y
        }
    }
}

struct Opnd {
    const float* p;
    int s;
    [[nodiscard]] float operator[](int i) const { return p[i * s]; }
};

template <class F>
inline void map1(float* o, Opnd a, int n, F f) {
    for (int i = 0; i < n; ++i) o[i] = f(a[i]);
}
template <class F>
inline void map2(float* o, Opnd a, Opnd b, int n, F f) {
    for (int i = 0; i < n; ++i) o[i] = f(a[i], b[i]);
}
template <class F>
inline void map3(float* o, Opnd a, Opnd b, Opnd c, int n, F f) {
    for (int i = 0; i < n; ++i) o[i] = f(a[i], b[i], c[i]);
}

void runVolume(const GraphData& g, const RootPlan& plan, DensityEvaluator& ev, const DensityVolume& v, float* out) {
    const Node& rootNode = g.nodes[static_cast<std::size_t>(plan.root)];
    if (rootNode.op == Op::Constant) {
        std::fill(out, out + v.size(), rootNode.f0);
        return;
    }
    const int sy = v.sizeY;
    VolumeScratch& sc = volumeScratch();
    const std::size_t need = static_cast<std::size_t>(plan.slotCount) * static_cast<std::size_t>(sy);
    if (sc.arena.size() < need) sc.arena.resize(need);
    if (sc.interps.size() < plan.interps.size()) sc.interps.resize(plan.interps.size());
    for (std::size_t q = 0; q < plan.interps.size(); ++q) prepareInterp(ev, g.nodes[static_cast<std::size_t>(plan.interps[q])], v, sc.interps[q]);
    float* const arena = sc.arena.data();

    for (int k = 0; k < v.sizeZ; ++k) {
        const int bz = v.blockZ(k);
        for (int i = 0; i < v.sizeX; ++i) {
            const int bx = v.blockX(i);
            float* const colOut = out + v.index(i, 0, k);
            const auto slot = [&](int id) { return id == plan.root ? colOut : arena + static_cast<std::size_t>(plan.slotOf[static_cast<std::size_t>(id)]) * static_cast<std::size_t>(sy); };
            const auto operand = [&](int id) -> Opnd {
                const Node& c = g.nodes[static_cast<std::size_t>(id)];
                if (c.op == Op::Constant) return {&c.f0, 0};
                return {slot(id), 1};
            };
            // A subtree without interpolation, evaluated at each block of the column.
            const auto leaf = [&](int id, float* o) {
                const Node& nd = g.nodes[static_cast<std::size_t>(id)];
                if ((nd.axes & kAxisY) == 0) {
                    ev.setPosition(bx, v.minY, bz);
                    const float val = EvaluatorAccess::eval(ev, id);
                    std::fill(o, o + sy, val);
                } else {
                    for (int j = 0; j < sy; ++j) {
                        ev.setPosition(bx, v.blockY(j), bz);
                        o[j] = EvaluatorAccess::eval(ev, id);
                    }
                }
            };

            for (const std::int32_t id : plan.region) {
                const Node& n = g.nodes[static_cast<std::size_t>(id)];
                float* const o = slot(id);
                if (n.op == Op::Interpolated) {
                    std::size_t q = 0;
                    while (plan.interps[q] != id) ++q;
                    const InterpState& st = sc.interps[q];
                    if (st.mode == InterpState::Mode::Interpolate) interpColumn(st, v, bx, bz, o);
                    else if (st.mode == InterpState::Mode::Passthrough) leaf(n.a, o);
                    else leaf(id, o);
                    continue;
                }
                if (!isElementwise(n)) {
                    leaf(id, o);
                    continue;
                }
                switch (n.op) {
                    case Op::Add: map2(o, operand(n.a), operand(n.b), sy, [](float a, float b) { return a + b; }); break;
                    case Op::Sub: map2(o, operand(n.a), operand(n.b), sy, [](float a, float b) { return a - b; }); break;
                    case Op::Mul: map2(o, operand(n.a), operand(n.b), sy, [](float a, float b) { return a * b; }); break;
                    case Op::Div: map2(o, operand(n.a), operand(n.b), sy, [](float a, float b) { return a / b; }); break;
                    case Op::Min: map2(o, operand(n.a), operand(n.b), sy, [](float a, float b) { return jmin(a, b); }); break;
                    case Op::Max: map2(o, operand(n.a), operand(n.b), sy, [](float a, float b) { return jmax(a, b); }); break;
                    case Op::AddC: { const float c = n.f0; map1(o, operand(n.a), sy, [c](float a) { return a + c; }); break; }
                    case Op::SubCL: { const float c = n.f0; map1(o, operand(n.a), sy, [c](float a) { return c - a; }); break; }
                    case Op::MulC: { const float c = n.f0; map1(o, operand(n.a), sy, [c](float a) { return a * c; }); break; }
                    case Op::DivCL: { const float c = n.f0; map1(o, operand(n.a), sy, [c](float a) { return c / a; }); break; }
                    case Op::MinC: { const float c = n.f0; map1(o, operand(n.a), sy, [c](float a) { return jmin(a, c); }); break; }
                    case Op::MaxC: { const float c = n.f0; map1(o, operand(n.a), sy, [c](float a) { return jmax(a, c); }); break; }
                    case Op::Abs: map1(o, operand(n.a), sy, [](float x) { return std::fabs(x); }); break;
                    case Op::Square: map1(o, operand(n.a), sy, [](float x) { return x * x; }); break;
                    case Op::Cube: map1(o, operand(n.a), sy, [](float x) { return x * x * x; }); break;
                    case Op::HalfNegative: map1(o, operand(n.a), sy, [](float x) { return x > 0.0f ? x : x * 0.5f; }); break;
                    case Op::QuarterNegative: map1(o, operand(n.a), sy, [](float x) { return x > 0.0f ? x : x * 0.25f; }); break;
                    case Op::Squeeze:
                        map1(o, operand(n.a), sy, [](float x) {
                            const float c = clampf(x, -1.0f, 1.0f);
                            return c / 2.0f - (c * c * c) / 24.0f;
                        });
                        break;
                    case Op::Negate: map1(o, operand(n.a), sy, [](float x) { return -x; }); break;
                    case Op::Clamp: { const float lo = n.f0, hi = n.f1; map1(o, operand(n.a), sy, [lo, hi](float x) { return clampf(x, lo, hi); }); break; }
                    case Op::Lerp:
                        map3(o, operand(n.a), operand(n.b), operand(n.c), sy, [](float alpha, float first, float second) {
                            if (alpha == 0.0f) return first;
                            if (alpha == 1.0f) return second;
                            return lerpf(alpha, first, second);
                        });
                        break;
                    case Op::RangeChoice: {
                        const float lo = n.f0, hi = n.f1;
                        map3(o, operand(n.a), operand(n.b), operand(n.c), sy, [lo, hi](float x, float in, float outside) { return (x >= lo && x < hi) ? in : outside; });
                        break;
                    }
                    case Op::IntervalSelect: {
                        const Opnd in = operand(n.a);
                        for (int j = 0; j < sy; ++j) {
                            const float x = in[j];
                            int pick = n.i1;
                            for (int t = 0; t < n.i1; ++t) {
                                if (x < g.thresholds[static_cast<std::size_t>(n.i0 + t)]) {
                                    pick = t;
                                    break;
                                }
                            }
                            o[j] = operand(g.extra[static_cast<std::size_t>(n.i2 + pick)])[j];
                        }
                        break;
                    }
                    case Op::Spline: {
                        for (int j = 0; j < sy; ++j) {
                            auto coord = [&](int node) { return operand(node)[j]; };
                            o[j] = splineSample(g, n.i0, coord);
                        }
                        break;
                    }
                    default: leaf(id, o); break;
                }
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
DensityGraph::DensityGraph() = default;
DensityGraph::~DensityGraph() = default;

DensityFunction DensityGraph::function(int rootId) const {
    DensityFunction f;
    if (rootId < 0 || static_cast<std::size_t>(rootId) >= d_->planOfNode.size() || d_->planOfNode[static_cast<std::size_t>(rootId)] < 0) return f;
    f.graph_ = this;
    f.node_ = rootId;
    f.plan_ = d_->planOfNode[static_cast<std::size_t>(rootId)];
    return f;
}

std::size_t DensityGraph::nodeCount() const { return d_->nodes.size(); }
std::uint64_t DensityGraph::id() const { return d_->id; }

unsigned DensityFunction::axes() const { return graph_ ? graph_->data().nodes[static_cast<std::size_t>(node_)].axes : 0u; }

float DensityFunction::compute(int x, int y, int z) const {
    if (!graph_) return 0.0f;
    return threadEvaluator(*graph_).value(*this, x, y, z);
}

void DensityFunction::fillVolume(const DensityVolume& v, float* out) const {
    if (!graph_ || v.size() == 0) return;
    const GraphData& g = graph_->data();
    runVolume(g, g.plans[static_cast<std::size_t>(plan_)], threadEvaluator(*graph_), v, out);
}

}  // namespace falcon::worldgen
