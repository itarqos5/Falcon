// JSON -> compiled density-function graph.
//
// The compile-time rewrites of the game that change float results are reproduced (spec section 4.2):
//   add:  x + c             sub: c - x (constant left)   or   x + (-c) (constant right)
//   mul:  x * c             div: c / x (constant left)   or   x * (1.0f / c) (constant right)   <- NOT x / c
//   min / max with a constant: Math.min(x, c) / Math.max(x, c)
// References ("minecraft:overworld/offset") are inlined by sharing one node per file; cache / blend_density are
// transparent; blend_alpha = 1.0, blend_offset = 0.0 and an absent beardifier = 0.0 (a world without old chunks or
// structures). Structurally identical nodes are shared, so a function that several others use is computed once.

#include "falcon/worldgen/java_math.hpp"
#include "density_internal.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <set>

namespace falcon::worldgen {

using namespace detail;

namespace {
std::atomic<std::uint64_t> g_nextGraphId{1};

constexpr std::string_view kNs = "minecraft:";

std::string_view stripNs(std::string_view s) {
    if (s.substr(0, kNs.size()) == kNs) s.remove_prefix(kNs.size());
    return s;
}

bool expensive(Op op) {
    switch (op) {
        case Op::Noise: case Op::ShiftA: case Op::ShiftB: case Op::Shift: case Op::BlendedNoise: case Op::Spline:
        case Op::FindTopSurface: case Op::Interpolated:
            return true;
        default:
            return false;
    }
}
}  // namespace

struct DensityGraphBuilder::Impl {
    std::shared_ptr<const RandomState> state;
    std::filesystem::path dfDir;
    std::unique_ptr<GraphData> data = std::make_unique<GraphData>();
    std::map<std::string, int> interned;
    std::map<std::string, int> refCache;
    std::set<std::string> inProgress;
    std::map<const NormalNoise*, int> noiseSlot;
    std::map<std::array<double, 5>, int> blendedSlot;
    std::vector<int> roots;
    std::string error;
    int depth = 0;

    int fail(const std::string& msg) {
        if (error.empty()) error = msg;
        return -1;
    }

    [[nodiscard]] static std::string keyOf(const Node& n) {
        std::string k;
        const auto put = [&k](const void* p, std::size_t s) { k.append(static_cast<const char*>(p), s); };
        put(&n.op, sizeof(n.op));
        put(&n.a, 4); put(&n.b, 4); put(&n.c, 4);
        put(&n.i0, 4); put(&n.i1, 4); put(&n.i2, 4); put(&n.i3, 4);
        put(&n.f0, 4); put(&n.f1, 4);
        put(&n.d0, 8); put(&n.d1, 8);
        return k;
    }

    // Derives axes / hasInterp, then adds the node (sharing an identical earlier one when possible).
    int add(Node n) {
        unsigned axes = 0;
        bool interp = n.op == Op::Interpolated;
        forEachChild(*data, n, [&](int c) {
            axes |= data->nodes[static_cast<std::size_t>(c)].axes;
            interp = interp || data->nodes[static_cast<std::size_t>(c)].hasInterp;
        });
        switch (n.op) {
            case Op::Gradient: axes = 1u << n.i0; break;
            case Op::Noise: {
                unsigned base = kAxisX | kAxisY | kAxisZ;
                if (n.d1 == 0.0) base &= ~static_cast<unsigned>(kAxisY);
                if (n.d0 == 0.0) base &= ~static_cast<unsigned>(kAxisX | kAxisZ);
                axes |= base;
                break;
            }
            case Op::ShiftA: case Op::ShiftB: axes = kAxisX | kAxisZ; break;
            case Op::Shift: case Op::BlendedNoise: case Op::Interpolated: axes = kAxisX | kAxisY | kAxisZ; break;
            case Op::FindTopSurface: axes &= ~static_cast<unsigned>(kAxisY); break;
            default: break;
        }
        n.axes = static_cast<std::uint8_t>(axes);
        n.hasInterp = interp;
        const bool shareable = n.op != Op::Spline && n.op != Op::IntervalSelect;
        std::string key;
        if (shareable) {
            key = keyOf(n);
            const auto it = interned.find(key);
            if (it != interned.end()) return it->second;
        }
        const int id = static_cast<int>(data->nodes.size());
        data->nodes.push_back(n);
        if (shareable) interned.emplace(std::move(key), id);
        return id;
    }

    int constant(float v) {
        Node n;
        n.op = Op::Constant;
        n.f0 = v;
        return add(n);
    }
    [[nodiscard]] bool isConst(int id) const { return data->nodes[static_cast<std::size_t>(id)].op == Op::Constant; }
    [[nodiscard]] float constValue(int id) const { return data->nodes[static_cast<std::size_t>(id)].f0; }
    [[nodiscard]] bool isZero(int id) const { return id < 0 || (isConst(id) && constValue(id) == 0.0f); }

    int unary(Op op, int a) {
        Node n;
        n.op = op;
        n.a = a;
        return add(n);
    }

    int binary(Op op, int l, int r) {
        const bool lc = isConst(l), rc = isConst(r);
        const float lv = lc ? constValue(l) : 0.0f, rv = rc ? constValue(r) : 0.0f;
        if (lc && rc) {
            switch (op) {
                case Op::Add: return constant(lv + rv);
                case Op::Sub: return constant(lv - rv);
                case Op::Mul: return constant(lv * rv);
                case Op::Div: return constant(lv / rv);
                case Op::Min: return constant(jmin(lv, rv));
                default: return constant(jmax(lv, rv));
            }
        }
        Node n;
        if (lc || rc) {
            const int x = lc ? r : l;
            const float c = lc ? lv : rv;
            n.a = x;
            n.f0 = c;
            switch (op) {
                case Op::Add: n.op = Op::AddC; break;
                case Op::Sub:
                    if (lc) {
                        n.op = Op::SubCL;
                    } else {
                        n.op = Op::AddC;
                        n.f0 = -c;
                    }
                    break;
                case Op::Mul: n.op = Op::MulC; break;
                case Op::Div:
                    if (lc) {
                        n.op = Op::DivCL;
                    } else {
                        n.op = Op::MulC;
                        n.f0 = 1.0f / c;
                    }
                    break;
                case Op::Min: n.op = Op::MinC; break;
                default: n.op = Op::MaxC; break;
            }
            return add(n);
        }
        n.op = op;
        n.a = l;
        n.b = r;
        return add(n);
    }

    // ---- field helpers ----
    bool floatField(const JsonValue& o, const char* key, float& out, const char* what) {
        const JsonValue* v = o.find(key);
        if (!v || !v->toFloat(out)) {
            fail(std::string(what) + ": '" + key + "' missing or not a number");
            return false;
        }
        return true;
    }
    bool doubleField(const JsonValue& o, const char* key, double& out, const char* what) {
        const JsonValue* v = o.find(key);
        if (!v || !v->toDouble(out)) {
            fail(std::string(what) + ": '" + key + "' missing or not a number");
            return false;
        }
        return true;
    }
    bool intField(const JsonValue& o, const char* key, std::int32_t& out, const char* what) {
        const JsonValue* v = o.find(key);
        if (!v || !v->toInt(out)) {
            fail(std::string(what) + ": '" + key + "' missing or not an integer");
            return false;
        }
        return true;
    }
    int child(const JsonValue& o, const char* key, const char* what) {
        const JsonValue* v = o.find(key);
        if (!v) return fail(std::string(what) + ": '" + key + "' missing");
        return fromValue(*v);
    }

    int noiseSlotFor(const std::string& key) {
        std::string err;
        const NormalNoise* noise = state->noise(key, err);
        if (!noise) return fail(err);
        const auto it = noiseSlot.find(noise);
        if (it != noiseSlot.end()) return it->second;
        const int slot = static_cast<int>(data->noises.size());
        data->noises.push_back(noise);
        noiseSlot.emplace(noise, slot);
        return slot;
    }

    // ---- splines ----
    bool splineValue(const JsonValue& v, SplinePoint& pt, std::vector<int>& coords);
    int splineMulti(const JsonValue& v, std::vector<int>& coords);

    int fromRef(std::string_view ref);
    int fromValue(const JsonValue& j);
    int fromObject(const JsonValue& j, std::string_view type);
};

bool DensityGraphBuilder::Impl::splineValue(const JsonValue& v, SplinePoint& pt, std::vector<int>& coords) {
    if (v.isNumber()) {
        float f = 0.0f;
        if (!v.toFloat(f)) {
            fail("spline: bad constant");
            return false;
        }
        pt.sub = -1;
        pt.constant = f;
        return true;
    }
    const int m = splineMulti(v, coords);
    if (m < 0) return false;
    pt.sub = m;
    return true;
}

int DensityGraphBuilder::Impl::splineMulti(const JsonValue& v, std::vector<int>& coords) {
    if (!v.isObject()) return fail("spline: expected an object or a number");
    const int coordNode = child(v, "coordinate", "spline");
    if (coordNode < 0) return -1;
    coords.push_back(coordNode);
    const JsonValue* pts = v.find("points");
    if (!pts || !pts->isArray() || pts->items.empty()) return fail("spline: 'points' missing or empty");
    std::vector<SplinePoint> local;
    local.reserve(pts->items.size());
    for (const JsonValue& p : pts->items) {
        SplinePoint sp{0.0f, 0.0f, 0.0f, -1};
        if (!floatField(p, "location", sp.location, "spline point") || !floatField(p, "derivative", sp.derivative, "spline point")) return -1;
        const JsonValue* value = p.find("value");
        if (!value) return fail("spline point: 'value' missing");
        if (!splineValue(*value, sp, coords)) return -1;
        if (!local.empty() && !(local.back().location < sp.location)) return fail("spline: points must be sorted by location");
        local.push_back(sp);
    }
    const int first = static_cast<int>(data->points.size());
    data->points.insert(data->points.end(), local.begin(), local.end());
    data->multis.push_back({coordNode, first, static_cast<std::int32_t>(local.size())});
    return static_cast<int>(data->multis.size()) - 1;
}

int DensityGraphBuilder::Impl::fromRef(std::string_view ref) {
    if (ref.substr(0, kNs.size()) != kNs && ref.find(':') != std::string_view::npos) return fail("density function '" + std::string(ref) + "': only the minecraft namespace is supported");
    const std::string name(stripNs(ref));
    const auto cached = refCache.find(name);
    if (cached != refCache.end()) return cached->second;
    if (!inProgress.insert(name).second) return fail("density function '" + name + "' refers to itself");
    JsonValue json;
    std::string err;
    int id = -1;
    if (!parseJsonFile(dfDir / (name + ".json"), json, err)) {
        fail(err);
    } else {
        id = fromValue(json);
    }
    inProgress.erase(name);
    if (id >= 0) refCache.emplace(name, id);
    return id;
}

int DensityGraphBuilder::Impl::fromValue(const JsonValue& j) {
    if (++depth > 96) {
        --depth;
        return fail("density function nesting too deep");
    }
    int id = -1;
    if (j.isNumber()) {
        float f = 0.0f;
        id = j.toFloat(f) ? constant(f) : fail("density function: bad constant");
    } else if (j.isString()) {
        id = fromRef(j.text);
    } else if (j.isObject()) {
        const JsonValue* t = j.find("type");
        id = (t && t->isString()) ? fromObject(j, stripNs(t->text)) : fail("density function: object without a 'type'");
    } else {
        id = fail("density function: expected a number, a reference or an object");
    }
    --depth;
    return id;
}

int DensityGraphBuilder::Impl::fromObject(const JsonValue& j, std::string_view type) {
    const std::string what(type);
    const char* w = what.c_str();

    if (type == "add" || type == "sub" || type == "mul" || type == "div" || type == "min" || type == "max") {
        const int l = child(j, "left", w);
        if (l < 0) return -1;
        const int r = child(j, "right", w);
        if (r < 0) return -1;
        const Op op = type == "add" ? Op::Add : type == "sub" ? Op::Sub : type == "mul" ? Op::Mul : type == "div" ? Op::Div : type == "min" ? Op::Min : Op::Max;
        return binary(op, l, r);
    }
    if (type == "abs" || type == "square" || type == "cube" || type == "half_negative" || type == "quarter_negative" || type == "squeeze" ||
        type == "negate") {
        const int a = child(j, "input", w);
        if (a < 0) return -1;
        const Op op = type == "abs" ? Op::Abs : type == "square" ? Op::Square : type == "cube" ? Op::Cube : type == "half_negative" ? Op::HalfNegative
                      : type == "quarter_negative" ? Op::QuarterNegative : type == "squeeze" ? Op::Squeeze : Op::Negate;
        return unary(op, a);
    }
    if (type == "constant") {
        float f = 0.0f;
        return floatField(j, "argument", f, w) ? constant(f) : -1;
    }
    if (type == "cache" || type == "blend_density") return child(j, "input", w);
    if (type == "blend_alpha") return constant(1.0f);
    if (type == "blend_offset" || type == "beardifier") return constant(0.0f);
    if (type == "clamp") {
        Node n;
        n.op = Op::Clamp;
        n.a = child(j, "input", w);
        if (n.a < 0 || !floatField(j, "min", n.f0, w) || !floatField(j, "max", n.f1, w)) return -1;
        return add(n);
    }
    if (type == "lerp") {
        Node n;
        n.op = Op::Lerp;
        n.a = child(j, "alpha", w);
        if (n.a < 0) return -1;
        n.b = child(j, "first", w);
        if (n.b < 0) return -1;
        n.c = child(j, "second", w);
        if (n.c < 0) return -1;
        return add(n);
    }
    if (type == "range_choice") {
        Node n;
        n.op = Op::RangeChoice;
        n.a = child(j, "input", w);
        if (n.a < 0 || !floatField(j, "min_inclusive", n.f0, w) || !floatField(j, "max_exclusive", n.f1, w)) return -1;
        n.b = child(j, "when_in_range", w);
        if (n.b < 0) return -1;
        n.c = child(j, "when_out_of_range", w);
        if (n.c < 0) return -1;
        return add(n);
    }
    if (type == "interval_select") {
        Node n;
        n.op = Op::IntervalSelect;
        n.a = child(j, "input", w);
        if (n.a < 0) return -1;
        const JsonValue* th = j.find("thresholds");
        const JsonValue* fns = j.find("functions");
        if (!th || !th->isArray() || !fns || !fns->isArray() || fns->items.size() != th->items.size() + 1) {
            return fail("interval_select: needs N thresholds and N + 1 functions");
        }
        std::vector<float> ths;
        for (const JsonValue& t : th->items) {
            float f = 0.0f;
            if (!t.toFloat(f)) return fail("interval_select: bad threshold");
            ths.push_back(f);
        }
        std::vector<int> fnIds;
        for (const JsonValue& f : fns->items) {
            const int id = fromValue(f);
            if (id < 0) return -1;
            fnIds.push_back(id);
        }
        n.i0 = static_cast<std::int32_t>(data->thresholds.size());
        n.i1 = static_cast<std::int32_t>(ths.size());
        data->thresholds.insert(data->thresholds.end(), ths.begin(), ths.end());
        n.i2 = static_cast<std::int32_t>(data->extra.size());
        n.i3 = static_cast<std::int32_t>(fnIds.size());
        data->extra.insert(data->extra.end(), fnIds.begin(), fnIds.end());
        return add(n);
    }
    if (type == "gradient") {
        Node n;
        n.op = Op::Gradient;
        const JsonValue* axis = j.find("axis");
        if (!axis || !axis->isString() || (axis->text != "x" && axis->text != "y" && axis->text != "z")) return fail("gradient: bad 'axis'");
        n.i0 = axis->text == "x" ? 0 : axis->text == "y" ? 1 : 2;
        std::int32_t from = 0, to = 0;
        float fromV = 0.0f, toV = 0.0f;
        if (!intField(j, "from_coordinate", from, w) || !intField(j, "to_coordinate", to, w) || !floatField(j, "from_value", fromV, w) ||
            !floatField(j, "to_value", toV, w)) {
            return -1;
        }
        if (const JsonValue* tiling = j.find("tiling")) {
            if (!tiling->isString() || stripNs(tiling->text) != "clamp_to_edge") return fail("gradient: only the clamp_to_edge tiling is supported");
        }
        if (from == to) return fail("gradient: from_coordinate equals to_coordinate");
        n.i1 = from;
        n.i2 = from < to ? from : to;
        n.i3 = from < to ? to : from;
        n.f0 = fromV;
        n.f1 = (toV - fromV) / static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(to) - static_cast<std::uint32_t>(from)));
        return add(n);
    }
    if (type == "noise") {
        Node n;
        n.op = Op::Noise;
        const JsonValue* key = j.find("noise");
        if (!key || !key->isString()) return fail("noise: 'noise' missing");
        if (!doubleField(j, "xz_scale", n.d0, w) || !doubleField(j, "y_scale", n.d1, w)) return -1;
        n.i0 = noiseSlotFor(key->text);
        if (n.i0 < 0) return -1;
        int sx = -1, sy = -1, sz = -1;
        if (j.find("shift_x") && (sx = child(j, "shift_x", w)) < 0) return -1;
        if (j.find("shift_y") && (sy = child(j, "shift_y", w)) < 0) return -1;
        if (j.find("shift_z") && (sz = child(j, "shift_z", w)) < 0) return -1;
        if (isZero(sx) && isZero(sy) && isZero(sz)) {
            n.i1 = 0;
        } else if (isZero(sy)) {
            n.i1 = 1;
            n.a = sx >= 0 ? sx : constant(0.0f);
            n.c = sz >= 0 ? sz : constant(0.0f);
        } else {
            n.i1 = 2;
            n.a = sx >= 0 ? sx : constant(0.0f);
            n.b = sy;
            n.c = sz >= 0 ? sz : constant(0.0f);
        }
        return add(n);
    }
    if (type == "shift_a" || type == "shift_b" || type == "shift") {
        Node n;
        n.op = type == "shift_a" ? Op::ShiftA : type == "shift_b" ? Op::ShiftB : Op::Shift;
        const JsonValue* key = j.find("noise");
        if (!key || !key->isString()) return fail(what + ": 'noise' missing");
        n.i0 = noiseSlotFor(key->text);
        return n.i0 < 0 ? -1 : add(n);
    }
    if (type == "old_blended_noise") {
        BlendedNoiseParams p;
        std::string err;
        if (!parseBlendedNoiseParams(j, p, err)) return fail(err);
        const std::array<double, 5> key = {p.xzScale, p.yScale, p.xzFactor, p.yFactor, p.smearScaleMultiplier};
        int slot;
        const auto it = blendedSlot.find(key);
        if (it != blendedSlot.end()) {
            slot = it->second;
        } else {
            XoroshiroRandomSource rnd = state->terrainRandom();
            slot = static_cast<int>(data->blended.size());
            data->blended.push_back(std::make_unique<BlendedNoise>(p, rnd));
            blendedSlot.emplace(key, slot);
        }
        Node n;
        n.op = Op::BlendedNoise;
        n.i0 = slot;
        return add(n);
    }
    if (type == "spline") {
        const JsonValue* s = j.find("spline");
        if (!s) return fail("spline: 'spline' missing");
        if (s->isNumber()) {
            float f = 0.0f;
            return s->toFloat(f) ? constant(f) : fail("spline: bad constant");
        }
        std::vector<int> coords;
        const int root = splineMulti(*s, coords);
        if (root < 0) return -1;
        Node n;
        n.op = Op::Spline;
        n.i0 = root;
        n.i2 = static_cast<std::int32_t>(data->extra.size());
        n.i3 = static_cast<std::int32_t>(coords.size());
        data->extra.insert(data->extra.end(), coords.begin(), coords.end());
        return add(n);
    }
    if (type == "interpolated") {
        Node n;
        n.op = Op::Interpolated;
        std::int32_t cxz = 0, cy = 0;
        if (!intField(j, "cell_size_xz", cxz, w) || !intField(j, "cell_size_y", cy, w)) return -1;
        if (cxz <= 0 || cy <= 0) return fail("interpolated: cell sizes must be positive");
        n.a = child(j, "input", w);
        if (n.a < 0) return -1;
        n.i0 = cxz;
        n.i1 = cy;
        n.f0 = 1.0f / static_cast<float>(cxz);
        n.f1 = 1.0f / static_cast<float>(cy);
        return add(n);
    }
    if (type == "find_top_surface") {
        Node n;
        n.op = Op::FindTopSurface;
        n.a = child(j, "density", w);
        if (n.a < 0) return -1;
        n.b = child(j, "upper_bound", w);
        if (n.b < 0) return -1;
        std::int32_t lower = 0, cell = 0;
        if (!intField(j, "lower_bound", lower, w) || !intField(j, "cell_height", cell, w)) return -1;
        if (cell <= 0) return fail("find_top_surface: cell_height must be positive");
        n.i0 = lower;
        n.i1 = cell;
        return add(n);
    }
    return fail("unsupported density function type 'minecraft:" + what + "'");
}

// ---------------------------------------------------------------------------------------------------------------
DensityGraphBuilder::DensityGraphBuilder(std::shared_ptr<const RandomState> state) : impl_(std::make_unique<Impl>()) {
    impl_->dfDir = state->worldgenDir() / "density_function";
    impl_->data->state = state;
    impl_->state = std::move(state);
}
DensityGraphBuilder::~DensityGraphBuilder() = default;

const std::string& DensityGraphBuilder::error() const { return impl_->error; }

int DensityGraphBuilder::addRef(std::string_view ref) {
    if (!impl_->data) return -1;
    const int id = impl_->fromRef(ref);
    if (id >= 0) impl_->roots.push_back(id);
    return id;
}

int DensityGraphBuilder::addJson(const JsonValue& json) {
    if (!impl_->data) return -1;
    const int id = impl_->fromValue(json);
    if (id >= 0) impl_->roots.push_back(id);
    return id;
}

namespace {

// The nodes above the `interpolated` nodes of a root, in evaluation order (see RootPlan).
void planVisit(const GraphData& g, RootPlan& plan, std::vector<char>& seen, int id) {
    if (seen[static_cast<std::size_t>(id)]) return;
    seen[static_cast<std::size_t>(id)] = 1;
    const Node& n = g.nodes[static_cast<std::size_t>(id)];
    if (n.op == Op::Constant) return;
    if (!isElementwise(n)) {
        plan.region.push_back(id);
        if (n.op == Op::Interpolated) plan.interps.push_back(id);
        return;
    }
    forEachChild(g, n, [&](int c) { planVisit(g, plan, seen, c); });
    plan.region.push_back(id);
}

}  // namespace

std::unique_ptr<DensityGraph> DensityGraphBuilder::build() {
    Impl& s = *impl_;
    if (!s.data) return nullptr;
    if (!s.error.empty()) return nullptr;
    GraphData& g = *s.data;

    // Memoise what is shared or costly.
    std::vector<int> parents(g.nodes.size(), 0);
    for (const Node& n : g.nodes) forEachChild(g, n, [&](int c) { ++parents[static_cast<std::size_t>(c)]; });
    for (const int r : s.roots) ++parents[static_cast<std::size_t>(r)];
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        Node& n = g.nodes[i];
        n.memo = n.op != Op::Constant && (parents[i] >= 2 || expensive(n.op));
    }

    // One plan per root.
    g.planOfNode.assign(g.nodes.size(), -1);
    for (const int r : s.roots) {
        if (g.planOfNode[static_cast<std::size_t>(r)] >= 0) continue;
        RootPlan plan;
        plan.root = r;
        std::vector<char> seen(g.nodes.size(), 0);
        planVisit(g, plan, seen, r);
        plan.slotOf.assign(g.nodes.size(), -1);
        for (const int id : plan.region) plan.slotOf[static_cast<std::size_t>(id)] = plan.slotCount++;
        g.planOfNode[static_cast<std::size_t>(r)] = static_cast<std::int32_t>(g.plans.size());
        g.plans.push_back(std::move(plan));
    }

    g.id = g_nextGraphId.fetch_add(1);
    auto graph = std::unique_ptr<DensityGraph>(new DensityGraph());
    graph->d_ = std::move(s.data);
    return graph;
}

}  // namespace falcon::worldgen
