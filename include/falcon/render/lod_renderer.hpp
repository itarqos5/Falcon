#pragma once

// Levels of detail for the terrain beyond the chunks that are really loaded: the ground only, from the seed's noise (worldgen/lod_surface.hpp), drawn the way Distant Horizons
// draws it: every cell of a tile is a column like a block, with a flat top and one flat colour, and a wall where the next cell is lower; coarser the further away it is.
//
// The world is cut into tiles of 32 x 32 cells. A tile of level L has cells of cmin * 2^L blocks, so the tiles of the next level are twice as wide, and which level a place gets
// is decided by a quadtree: a tile is split into its four halves while its cells would look too coarse from the camera (a cell of more than k times its distance), so the
// detail falls off smoothly out to the LOD radius. Tiles are made by the world's worker threads (after the chunks, the coarse ones first so that the horizon fills in
// quickly), kept while they might be needed again, and drawn in a pass of their own: its own near and far plane, so that the depth buffer stays precise to 130 000 blocks,
// and then the depth is cleared and the real chunks are drawn over it.

#include "falcon/gfx/gl.hpp"
#include "falcon/gfx/shader.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/util/priority_pool.hpp"
#include "falcon/worldgen/lod_surface.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace falcon {

inline constexpr int kLodMinChunks = 64;
inline constexpr int kLodMaxChunks = 8192;
// "LOD View Strength": how fine the cells are, and so what the LODs cost.
inline constexpr int kLodDetailLow = 0, kLodDetailMedium = 1, kLodDetailFull = 2;

struct LodSettings {
    bool enabled = false;
    int radiusChunks = 128;  // how far out LODs reach, in chunks (kLodMinChunks .. kLodMaxChunks)
    int detail = kLodDetailMedium;
    bool operator==(const LodSettings&) const = default;
};

struct LodStats {
    int tilesDrawn = 0;
    int tilesLoaded = 0;    // on the GPU
    int tilesBuilding = 0;  // asked for, not back yet
    long trianglesDrawn = 0;
    int levels = 0;         // how many levels of detail the view uses
    double megabytes = 0.0; // video memory the tiles take
};

class LodRenderer {
public:
    // `trees`: what each biome grows, by biome id (Decorator::treeFacts), so that forests are drawn.
    LodRenderer(std::shared_ptr<const worldgen::OverworldGenerator> generator, std::vector<worldgen::LodTrees> trees, PriorityPool& pool);
    ~LodRenderer();
    LodRenderer(const LodRenderer&) = delete;
    LodRenderer& operator=(const LodRenderer&) = delete;

    // A change of strength makes every tile again; a change of radius only changes which ones are wanted.
    void setSettings(const LodSettings& settings);
    [[nodiscard]] const LodSettings& settings() const { return settings_; }

    // The fog of the world while LODs are on: start and end (blocks). The real chunks are not fogged at their own edge, the fog is for the far end of the LODs.
    [[nodiscard]] static std::pair<float, float> fogRange(int radiusChunks, int nearChunks);

    // Main thread, every frame: decides which tiles are wanted round the camera, asks for the ones that are missing, and puts finished ones on the GPU for at most
    // `uploadBudgetMs`. `nearChunks` is the radius (chunks) of the real world, which the LODs do not draw over.
    void update(double camX, double camZ, int nearChunks, double uploadBudgetMs);
    // Draws the LODs (the framebuffer's depth is left with them in it: the caller clears it before drawing the chunks). `view` is the camera's view matrix (rotation; the
    // world is drawn relative to the camera), the camera's place is in blocks.
    void render(const Matrix4f& view, float fovDegrees, float aspect, double camX, double camY, double camZ, int nearChunks, const std::array<float, 3>& fogColor,
                const std::array<float, 2>& envFog);

    [[nodiscard]] LodStats stats() const { return stats_; }

    // A tile is 32 x 32 cells.
    static constexpr int kCells = 32;

    // 12 bytes: the place in the tile (x and z in cells, y in blocks) and the colour, with the face's shade already in it.
    struct Vertex {
        std::int16_t x, y, z, unused;
        std::uint8_t r, g, b, a;
    };
    // The columns of a tile as vertices (four to a quad), and the lowest and highest y among them. (Public for the tests.)
    [[nodiscard]] static std::vector<Vertex> buildColumns(const worldgen::LodGrid& grid, int cell, float& minY, float& maxY);

private:
    struct TileKey {
        int level, tx, tz;
        bool operator==(const TileKey&) const = default;
    };
    struct TileKeyHash {
        std::size_t operator()(const TileKey& k) const {
            return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.tx)) * 73856093u) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.tz)) * 19349663u) ^
                                              (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.level)) * 83492791u));
        }
    };
    enum class State : std::uint8_t { Requested, Uploaded };
    struct Tile {
        State state = State::Requested;
        int buffer = -1;                // where its vertices are (Uploaded): the buffer, the first vertex and how many
        std::uint32_t first = 0, count = 0;
        float minY = 0.0f, maxY = 0.0f; // the height range, skirt included
        std::uint64_t lastUsed = 0;     // the frame that last wanted it
    };
    struct Result {
        TileKey key;
        int epoch;
        std::vector<Vertex> vertices;
        float minY, maxY;
    };
    // What the worker jobs share with the renderer; held by every job, so a job that finishes after the renderer is gone only finds its results dropped.
    struct Shared {
        std::mutex mutex;
        std::vector<Result> done;
        std::atomic<int> epoch{0};
        std::atomic<int> building{0};
    };
    struct Detail {
        int cellMin;        // the finest cell, in blocks
        float k;            // a cell may be at most k times its distance
        int chunkMax;       // cells up to this size read the real height of every column (from whole chunks: exact, and the dearest)
        bool residual;      // coarser ones are corrected by the real height on a sparse grid (else the preliminary surface alone)
    };
    struct Wanted {
        TileKey key;
        double distance;
    };

    [[nodiscard]] static Detail detailOf(int detail);
    [[nodiscard]] int cellOf(int level) const { return detail_.cellMin << level; }
    [[nodiscard]] double tileSize(int level) const { return static_cast<double>(kCells) * cellOf(level); }
    void select(double camX, double camZ, double nearBlocks, double maxBlocks);
    void split(const TileKey& key, double camX, double camZ, double nearBlocks, double maxBlocks);
    void request(const TileKey& key, double priority);
    void upload(Result&& result);
    void evict();
    void releaseAll();
    // Room for `count` vertices in one of the big buffers (a new one when none has it): first fit, the free ranges joined when they touch.
    bool allocate(std::uint32_t count, int& buffer, std::uint32_t& first);
    void release(int buffer, std::uint32_t first, std::uint32_t count);

    std::shared_ptr<const worldgen::LodSurface> surface_;
    PriorityPool& pool_;
    std::shared_ptr<Shared> shared_;
    LodSettings settings_;
    Detail detail_{2, 1.0f / 100.0f, 2, true};
    int topLevel_ = 0;
    std::uint64_t frame_ = 0;

    ShaderProgram program_;
    gl::GLint uViewProj_ = -1, uOffset_ = -1, uCell_ = -1, uFogColor_ = -1, uFogRange_ = -1, uEnvFog_ = -1;
    gl::GLuint vao_ = 0, indexBuffer_ = 0;
    // The vertices of every tile sit in a few big buffers (a buffer for each tile would be thousands of allocations).
    static constexpr std::uint32_t kVerticesPerBuffer = 1u << 20;
    struct Buffer {
        gl::GLuint id = 0;
        std::map<std::uint32_t, std::uint32_t> free;  // first vertex -> how many, the ranges nobody uses
    };
    std::vector<Buffer> buffers_;
    std::size_t uploadedVertices_ = 0;

    std::unordered_map<TileKey, Tile, TileKeyHash> tiles_;
    std::vector<Wanted> wanted_;  // the leaves of the quadtree this frame
    LodStats stats_;
};

}  // namespace falcon
