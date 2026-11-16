#pragma once

// The compiled form of a density-function graph (shared by the builder and the evaluators). Private to src/worldgen.

#include "falcon/worldgen/density.hpp"
#include "falcon/worldgen/legacy_noise.hpp"
#include "falcon/worldgen/noise.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace falcon::worldgen::detail {

enum class Op : std::uint8_t {
    Constant,
    // two operands (a, b): the game's compile-time specialisation for a constant operand gives the *C forms
    Add, Sub, Mul, Div, Min, Max,
    AddC,    // a + f0           (add either side; sub with a constant right side is AddC with -c)
    SubCL,   // f0 - a           (sub with a constant LEFT side)
    MulC,    // a * f0           (mul either side; div with a constant right side is MulC with 1.0f / c)
    DivCL,   // f0 / a           (div with a constant left side)
    MinC, MaxC,
    Abs, Square, Cube, HalfNegative, QuarterNegative, Squeeze, Negate,
    Clamp,           // a, f0 = min, f1 = max
    Lerp,            // a = alpha, b = first, c = second
    RangeChoice,     // a = input, b = when in range, c = when out of range, f0 = min inclusive, f1 = max exclusive
    IntervalSelect,  // a = input, i0 = first threshold, i1 = threshold count N, i2 = first function in `extra`, i3 = N + 1
    Gradient,        // i0 = axis (0 x, 1 y, 2 z), i1 = from coordinate, i2/i3 = min/max coordinate, f0 = from value, f1 = factor
    Noise,           // i0 = noise slot, i1 = mode (0 plain, 1 shifted xz, 2 shifted xyz), d0 = xz scale, d1 = y scale,
                     // a/b/c = shift x/y/z (-1 when absent)
    ShiftA, ShiftB, Shift,  // i0 = noise slot
    BlendedNoise,    // i0 = slot in GraphData::blended
    Spline,          // i0 = root multipoint, i2 = first coordinate node in `extra`, i3 = count
    FindTopSurface,  // a = density, b = upper bound, i0 = lower bound, i1 = cell height
    Interpolated,    // a = input, i0 = cell size xz, i1 = cell size y, f0 = 1 / cell xz, f1 = 1 / cell y
    DistanceToPoint, // the End's island: i0 = metric (0 euclidean, 1 manhattan, 2 chebyshev), d0, d1, f0 = the point's x, y, z
    Slice,           // a = input, i0 = axis, i1 = coordinate: the input as it is at that coordinate of the axis
    EndOuterIslands, // i0 = slot in GraphData::simplex: the small islands out beyond the main one (EndIslandDensityFunction)
};

struct Node {
    Op op = Op::Constant;
    std::uint8_t axes = 0;     // DensityAxis bits
    bool memo = false;         // value memoised per position in point evaluation
    bool hasInterp = false;    // an Interpolated node is this node or below it
    std::int32_t a = -1, b = -1, c = -1;
    std::int32_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
    float f0 = 0.0f, f1 = 0.0f;
    double d0 = 0.0, d1 = 0.0;
};

struct SplineMulti {
    std::int32_t coordNode;
    std::int32_t firstPoint;
    std::int32_t count;
};
struct SplinePoint {
    float location;
    float derivative;
    float constant;   // the value when `sub` < 0
    std::int32_t sub; // index of a nested SplineMulti, or -1
};

// How a function is evaluated over a volume: the nodes above the `interpolated` nodes (the "region") are computed
// column by column in small buffers; everything below an `interpolated` node is evaluated at its corner points.
struct RootPlan {
    std::int32_t root = -1;
    std::vector<std::int32_t> region;   // post-order; constants are not listed (they are operands)
    std::vector<std::int32_t> slotOf;   // node id -> buffer slot (-1: none)
    std::int32_t slotCount = 0;
    std::vector<std::int32_t> interps;  // Interpolated nodes of the region
};

struct GraphData {
    std::uint64_t id = 0;
    std::shared_ptr<const RandomState> state;  // owns the NormalNoise instances
    std::vector<Node> nodes;
    std::vector<const NormalNoise*> noises;
    std::vector<std::unique_ptr<BlendedNoise>> blended;
    std::vector<std::unique_ptr<SimplexNoise>> simplex;
    std::vector<SplineMulti> multis;
    std::vector<SplinePoint> points;
    std::vector<float> thresholds;
    std::vector<std::int32_t> extra;  // interval_select functions, spline coordinate nodes
    std::vector<RootPlan> plans;
    std::vector<std::int32_t> planOfNode;  // node id -> index into plans (-1: not a root)
};

// A node above an `interpolated` node whose value is combined elementwise from its children's columns. Everything
// else in a plan's region (Interpolated itself, subtrees without one, and the few nodes that cannot combine child
// buffers) is a "leaf": evaluated per position (an Interpolated one from its corner grid).
[[nodiscard]] inline bool isElementwise(const Node& n) {
    return n.hasInterp && n.op != Op::Interpolated && n.op != Op::FindTopSurface && n.op != Op::Noise;
}

// Calls f(childNodeId) for every child edge of n (a spline's coordinates are listed once per reference).
template <class F>
void forEachChild(const GraphData& g, const Node& n, F&& f) {
    switch (n.op) {
        case Op::Constant:
        case Op::Gradient:
        case Op::ShiftA:
        case Op::ShiftB:
        case Op::Shift:
        case Op::BlendedNoise:
        case Op::DistanceToPoint:
        case Op::EndOuterIslands:
            break;
        case Op::Noise:
            if (n.a >= 0) f(n.a);
            if (n.b >= 0) f(n.b);
            if (n.c >= 0) f(n.c);
            break;
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Min: case Op::Max:
        case Op::FindTopSurface:
            f(n.a);
            f(n.b);
            break;
        case Op::Lerp:
        case Op::RangeChoice:
            f(n.a);
            f(n.b);
            f(n.c);
            break;
        case Op::IntervalSelect:
            f(n.a);
            for (int i = 0; i < n.i3; ++i) f(g.extra[static_cast<std::size_t>(n.i2 + i)]);
            break;
        case Op::Spline:
            for (int i = 0; i < n.i3; ++i) f(g.extra[static_cast<std::size_t>(n.i2 + i)]);
            break;
        default:  // one operand
            f(n.a);
            break;
    }
}

}  // namespace falcon::worldgen::detail
