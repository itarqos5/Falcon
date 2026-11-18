#pragma once

// The 26.3 density-function engine: worldgen/density_function/*.json (and inline definitions in noise settings) are
// loaded at runtime into one immutable, shared graph of nodes. Everything is IEEE float (only noise coordinates are
// double), bit-exact with the game; see docs in the .cpp files for the places where the game's own arithmetic has
// quirks (x / c becomes x * (1 / c), lerp shortcuts, incremental y interpolation ...).
//
// Two evaluation paths, as in the game:
//   * point:  DensityFunction::compute(x, y, z)  - `interpolated` nodes lerp between their 8 cell corners
//   * volume: DensityFunction::fillVolume(v, out) - `interpolated` nodes evaluate their input only at the cell corners
//             and fill the cells (z-lerp, x-lerp, then y by repeated addition). This is what chunk generation uses.
// The two differ by about one ulp for functions that contain `interpolated` (exactly like the game's).
//
// Thread safety: a built DensityGraph is immutable; any number of threads may evaluate it. Scratch memory is
// per-thread (thread_local) or in a DensityEvaluator owned by the caller; nothing allocates per evaluated point.

#include "falcon/worldgen/json_tree.hpp"
#include "falcon/worldgen/random_state.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace falcon::worldgen {

namespace detail {
struct GraphData;
struct Node;
}  // namespace detail

// A box of sample positions (net.minecraft DensityVolume). Sample (i, j, k) is at block
// (minX + i*stepX, minY + j*stepY, minZ + k*stepZ). Buffer index: y FASTEST, then x, then z.
struct DensityVolume {
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    int minX = 0, minY = 0, minZ = 0;
    int stepX = 1, stepY = 1, stepZ = 1;

    [[nodiscard]] std::size_t size() const { return static_cast<std::size_t>(sizeX) * static_cast<std::size_t>(sizeY) * static_cast<std::size_t>(sizeZ); }
    [[nodiscard]] std::size_t index(int i, int j, int k) const {
        return static_cast<std::size_t>(j) + static_cast<std::size_t>(sizeY) * (static_cast<std::size_t>(i) + static_cast<std::size_t>(sizeX) * static_cast<std::size_t>(k));
    }
    [[nodiscard]] int blockX(int i) const { return minX + i * stepX; }
    [[nodiscard]] int blockY(int j) const { return minY + j * stepY; }
    [[nodiscard]] int blockZ(int k) const { return minZ + k * stepZ; }
    [[nodiscard]] int maxX() const { return minX + sizeX * stepX - 1; }
    [[nodiscard]] int maxY() const { return minY + sizeY * stepY - 1; }
    [[nodiscard]] int maxZ() const { return minZ + sizeZ * stepZ - 1; }
};

class DensityGraph;
class DensityFunction;

enum DensityAxis : unsigned { kAxisX = 1, kAxisY = 2, kAxisZ = 4 };

// Per-thread (or per-worker) scratch for point evaluation. Values of nodes shared between functions are memoised
// per position, so evaluating several functions of one graph at the same position through one evaluator (e.g. the six
// climate functions) shares the work. Not thread-safe: one evaluator per thread.
class DensityEvaluator {
public:
    explicit DensityEvaluator(const DensityGraph& graph);

    // Starts a new position. Nodes that do not depend on y stay memoised while only y changes.
    void setPosition(int x, int y, int z);
    [[nodiscard]] float value(const DensityFunction& f);                              // at the current position
    [[nodiscard]] float value(const DensityFunction& f, int x, int y, int z);         // setPosition + value

    [[nodiscard]] std::uint64_t graphId() const { return graphId_; }

private:
    friend struct EvaluatorAccess;
    [[nodiscard]] float eval(int node);
    [[nodiscard]] float compute(const detail::Node& n);
    [[nodiscard]] float evalAt(int node, int x, int y, int z);
    void bumpAll();
    void bumpXZ();

    // The eight corner values of the last two cells an `interpolated` node was read in (per node; made on its first use). A search for the top of the ground probes one column a few
    // blocks apart, nearly always inside the same cell, and the corners (each a whole noise evaluation) were worked out again for every probe.
    struct CornerSet {
        int x0 = 0, y0 = 0, z0 = 0;
        bool valid = false;
        float c[8] = {};
    };
    const detail::GraphData* g_;
    std::uint64_t graphId_;
    std::vector<CornerSet> corners_;      // two for each node
    std::vector<std::uint8_t> cornerNext_;  // which of a node's two to overwrite next
    std::vector<float> val_;
    std::vector<std::uint32_t> stamp_;
    std::uint32_t stampAll_ = 1;
    std::uint32_t stampXZ_ = 1;
    int x_ = 0, y_ = 0, z_ = 0;
    int keyX_ = 0, keyZ_ = 0;
    bool keyValid_ = false;
};

// A handle to one function of a built graph (cheap to copy; valid while the graph lives).
class DensityFunction {
public:
    DensityFunction() = default;
    [[nodiscard]] bool valid() const { return graph_ != nullptr; }

    // Point evaluation. Thread-safe (uses a thread_local evaluator); no allocation after a thread's first call.
    [[nodiscard]] float compute(int x, int y, int z) const;
    [[nodiscard]] float compute(DensityEvaluator& ev, int x, int y, int z) const { return ev.value(*this, x, y, z); }

    // Volume evaluation into `out` (v.size() floats, index order of DensityVolume). `interpolated` nodes use the
    // game's cell interpolation. Thread-safe.
    void fillVolume(const DensityVolume& v, float* out) const;

    // Which of x/y/z the function depends on (DensityAxis bits; the game's domainAxes, conservative).
    [[nodiscard]] unsigned axes() const;
    [[nodiscard]] bool dependsOnY() const { return (axes() & kAxisY) != 0; }

    [[nodiscard]] const DensityGraph* graph() const { return graph_; }
    [[nodiscard]] int nodeId() const { return node_; }

private:
    friend class DensityGraph;
    friend class DensityEvaluator;
    const DensityGraph* graph_ = nullptr;
    int node_ = -1;
    int plan_ = -1;
};

class DensityGraph {
public:
    ~DensityGraph();
    DensityGraph(const DensityGraph&) = delete;
    DensityGraph& operator=(const DensityGraph&) = delete;

    // The handle for a root added to the builder (the id the builder returned).
    [[nodiscard]] DensityFunction function(int rootId) const;
    [[nodiscard]] std::size_t nodeCount() const;
    [[nodiscard]] std::uint64_t id() const;
    [[nodiscard]] const detail::GraphData& data() const { return *d_; }

private:
    friend class DensityGraphBuilder;
    DensityGraph();
    std::unique_ptr<detail::GraphData> d_;
};

// Builds a graph from JSON. Roots are added one by one (each returns the root's id, or -1 and error()); build()
// compiles them all into one graph that shares every common sub-function.
class DensityGraphBuilder {
public:
    explicit DensityGraphBuilder(std::shared_ptr<const RandomState> state);
    ~DensityGraphBuilder();
    DensityGraphBuilder(const DensityGraphBuilder&) = delete;
    DensityGraphBuilder& operator=(const DensityGraphBuilder&) = delete;

    // "minecraft:overworld/offset" -> <worldgen>/density_function/overworld/offset.json
    [[nodiscard]] int addRef(std::string_view ref);
    // A density-function value: a number (constant), a string (reference) or an object with a "type".
    [[nodiscard]] int addJson(const JsonValue& json);

    [[nodiscard]] const std::string& error() const;
    [[nodiscard]] std::unique_ptr<DensityGraph> build();  // nullptr (and error()) on failure

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace falcon::worldgen
