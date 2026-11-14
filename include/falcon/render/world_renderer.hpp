#pragma once

#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/block_models.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/util/priority_pool.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/chunk_cache.hpp"
#include "falcon/world/mesher.hpp"
#include "falcon/world/world_save.hpp"
#include "falcon/world/terrain.hpp"
#include "falcon/world/decoration.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace falcon {

// Where the player's eyes are and what they look at.
struct Camera {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    Look look;
    float fov = 70.0f;  // vertical, degrees
    // View bobbing from walking (zero when the option is off): see bobTransform().
    float bobX = 0.0f;
    float bobY = 0.0f;
    float bobRollDegrees = 0.0f;
    float bobPitchDegrees = 0.0f;
};

// The view matrix of a camera: its rotation, with the bobbing in front.
[[nodiscard]] inline Matrix4f cameraView(const Camera& camera) {
    const Matrix4f rotation = viewRotation(camera.look);
    if (camera.bobX == 0.0f && camera.bobY == 0.0f && camera.bobRollDegrees == 0.0f && camera.bobPitchDegrees == 0.0f) return rotation;
    return bobTransform(camera.bobX, camera.bobY, camera.bobRollDegrees, camera.bobPitchDegrees) * rotation;
}

// What a frame cost and what is loaded, for the debug screen and the benchmark.
struct RenderStats {
    int chunksLoaded = 0;
    int sectionsLoaded = 0;     // sections with a mesh on the GPU
    int sectionsDrawn = 0;      // ... that passed culling this frame
    std::size_t quadsDrawn = 0;
    std::size_t quadsTotal = 0;
    std::size_t meshBytes = 0;   // vertex data on the GPU
    std::size_t levelBytes = 0;  // block data in RAM
    std::size_t cachedChunks = 0;  // chunks that left the loaded area and are kept as small compressed copies
    std::size_t cacheBytes = 0;    // what those copies take
    std::size_t textureBytes = 0;
    int jobsWaiting = 0;
    int jobsInFlight = 0;       // chunks being made or meshed (running or waiting)
    int uploadsThisFrame = 0;
    int drawCalls = 0;          // OpenGL draw calls the world took this frame
    int drawCommands = 0;       // runs of quads drawn (with multi-draw, many commands share one call)
    bool occlusionCulling = false;
    bool multiDraw = false;
};

// How far around the player (in chunks) chunks are generated and kept, for a render distance: the drawn disk of radius R plus a
// margin. A chunk is ready once it and its 8 neighbours are lit, and a chunk is lit once ITS 8 neighbours exist, so a chunk in
// the drawn disk needs chunks up to 2 further out in each axis. On a diagonal the farthest one needed is at squared distance
// R^2 + 4*sqrt(2)*R + 8, which is more than (R + 2)^2 (the old margin) but at most (R + 3)^2 = R^2 + 6R + 9 for every R. With
// R + 2 the diagonal chunks at the rim were never made, so at a small render distance the loading area could never be declared
// ready: the loading screen never went away at render distance 2.
[[nodiscard]] constexpr int streamRadius(int renderDistance) { return renderDistance + 3; }

// How far along one chunk of the loading area is, for the loading screen's chunk map (like the game's chunk statuses).
enum class ChunkLoadState : std::uint8_t {
    None,       // not asked for yet
    Queued,     // being generated
    Generated,  // the blocks exist, no light yet
    Lit,        // lit, but its neighbours are not all lit yet
    Ready,      // it and its 8 neighbours are lit: it can be meshed and drawn
};

// Draws the blocks of a level and keeps the right chunks loaded around the player.
//
// Everything that can be done off the main thread is: chunks are generated and meshed by a small pool of worker
// threads, nearest to the player first. The main thread only uploads finished meshes (a few per frame, within a
// time budget, so loading never causes a hitch) and draws. Drawing is cheap by construction: merged quads, 20
// bytes a vertex, one draw call per visible section, sections outside the view or beyond the render distance
// skipped, opaque sections drawn front to back so hidden pixels are rejected early.
class WorldRenderer {
public:
    // `overworld`: the game-exact generator (null: the simple built-in terrain, which Superflat always uses).
    WorldRenderer(BlockModels& models, std::int64_t seed, TerrainStyle style, unsigned workerThreads, std::shared_ptr<const worldgen::OverworldGenerator> overworld = nullptr);
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    [[nodiscard]] Level& level() { return level_; }
    [[nodiscard]] const Level& level() const { return level_; }
    [[nodiscard]] const TerrainGenerator& generator() const { return generator_; }
    // Where a new player stands, and the height of the ground in a column, from whichever generator makes the chunks.
    [[nodiscard]] TerrainGenerator::Spawn findSpawn() const;
    [[nodiscard]] int surfaceHeight(int x, int z) const;
    [[nodiscard]] const worldgen::OverworldGenerator* overworld() const { return overworld_.get(); }
    // The Brightness option (0..1) that lifts the dark end of the light map.
    void setBrightness(float brightness) { brightness_ = brightness; }
    // The light the player carries (a torch in the hand): `level` (0..15; 0 is none) shines from the block (x, y, z), spreading and
    // fading one level a block and stopping at walls like block light does, but follows the player without touching the chunks'
    // light. Cheap to call every frame: the light is only worked out again when the block, the level or the blocks around change.
    void setCarriedLight(int x, int y, int z, int level);
    // The eyes are under water (on = true): no sky, and the world fades into `color` (0..1) from `fogStart` to `fogEnd` blocks away.
    // Call every frame; `on = false` goes back to the sky and the render distance's fog.
    void setUnderwater(bool on, const std::array<float, 3>& color = {0.0f, 0.0f, 0.0f}, float fogStart = 0.0f, float fogEnd = 0.0f) {
        underwater_ = on;
        waterFogColor_ = color;
        waterFogStart_ = fogStart;
        waterFogEnd_ = fogEnd;
    }
    // The Smooth Lighting option: per-corner light and ambient occlusion, or flat per-face light. Changing it re-meshes
    // everything (the light is baked into the vertices).
    void setSmoothLighting(bool on);
    // The Leaves video option: true draws leaves see-through (Cutout), false solid (Opaque). Re-meshes the world on a change.
    void setCutoutLeaves(bool on);
    // The world's edits: every block changed through setBlock() is recorded in them, and every chunk made (again) has its edits put back before it is
    // used. Set once, before the first update().
    void setEdits(std::shared_ptr<WorldEdits> edits) { edits_ = std::move(edits); }
    // A small model drawn on top of the world with the world's shader (the held item): `mvp` takes its 0..1 space to the screen.
    void drawHeld(const Matrix4f& mvp, const std::vector<MeshVertex>& vertices);
    // The view and projection of the last frame drawn (the world is drawn relative to the camera): for drawing other things in it.
    [[nodiscard]] const Matrix4f& viewProjection() const { return lastViewProj_; }
    // The sky and block light (0..15) of a block (full sky where no light is known yet).
    void lightAt(int x, int y, int z, int& sky, int& block) const;
    // The same with the light the player carries (a held torch) counted in: what lights things drawn near the player that are not part of a mesh (dropped items).
    void lightAtWithCarried(int x, int y, int z, int& sky, int& block) const;

    // Loads and unloads chunks around the player (block coordinates) for a render distance in chunks, and uploads
    // finished meshes for at most `uploadBudgetMs`.
    void update(double playerX, double playerZ, int renderDistance, double uploadBudgetMs);
    // 0..1: how much of the area within `radius` chunks is generated and meshed. 1 means everything is on screen.
    [[nodiscard]] float loadProgress(int radius) const;
    // How far one chunk is, for the loading screen's chunk map (main thread).
    [[nodiscard]] ChunkLoadState chunkLoadState(ChunkPos pos) const;
    // True once the section holding the block (x, y, z) is generated and its mesh is on the GPU (or it has nothing to draw):
    // what the game waits for before it takes the loading screen away, so the player never opens their eyes in empty space.
    [[nodiscard]] bool sectionReady(int x, int y, int z) const;

    // Draws the sky and the blocks. The framebuffer must be cleared (depth too); this leaves depth test on.
    void render(const Camera& camera, int framebufferWidth, int framebufferHeight, int renderDistance);
    // Black outline of one block (the one the player looks at).
    void drawSelection(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z);
    // The ten pictures of a block being dug (destroy_stage_0..9); without them no cracks are drawn.
    void setBreakImages(const std::vector<Image>& stages);
    // The cracks on the block being dug, `stage` 0..9 (LevelRenderer: the picture is multiplied into the block's colours, so grey is no change).
    void drawBreakOverlay(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z, int stage);
    // Turns off what render() turned on, so the GUI can draw.
    void endFrame();

    // Changes a block and remeshes the sections it touches at once.
    bool setBlock(int x, int y, int z, Block block);

    [[nodiscard]] RenderStats stats() const { return stats_; }
    // GPU time of the last finished world pass, in milliseconds (0 until a measurement is back).
    [[nodiscard]] double gpuMillis() const { return gpuMillis_; }
    // The GPU timer queries cost the driver something every frame, so they only run while somebody looks at the number (F3, the benchmark).
    void setGpuTiming(bool on) { gpuTiming_ = on; }

private:
    struct SectionKey {
        int x, z, y;
        bool operator==(const SectionKey&) const = default;
    };
    struct SectionKeyHash {
        std::size_t operator()(const SectionKey& k) const {
            return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.x)) * 73856093u) ^
                                              (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.z)) * 19349663u) ^
                                              (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.y)) * 83492791u));
        }
    };
    // A section's mesh lives in one big shared vertex buffer (the arena) at `arenaBase` (in vertices), so the whole world is
    // drawn with one bound vertex array and no per-section buffer switches.
    struct GpuSection {
        std::uint32_t arenaBase = 0;   // first vertex in the arena
        std::uint32_t arenaVerts = 0;  // vertices reserved there (opaque + cutout + translucent)
        int opaqueVertices = 0;
        int cutoutVertices = 0;
        int translucentVertices = 0;
        // How many vertices of the opaque and of the cutout part face each way (east, west, up, down, south, north, other): the parts are
        // stored in that order, so the draw can skip the directions that turn away from the camera.
        std::array<int, kFacingGroups> opaqueGroups{}, cutoutGroups{};
        bool lodFar = false;      // built without the leaf faces between leaf blocks (it was far from the player)
        bool lodMatters = false;  // it has such faces: worth building again when it crosses the line
    };
    struct VisibleSection {
        const GpuSection* section;
        float ox, oy, oz;
        float distance2;
    };
    struct MeshResult {
        SectionKey key;
        std::uint32_t stamp;
        std::vector<MeshVertex> vertices;  // opaque first, then cutout, then translucent
        int opaqueVertices;
        int cutoutVertices;
        int translucentVertices;
        std::uint32_t gen = 0;  // which request of this section made it
        bool lodFar = false;
        bool leafInterior = false;
        std::array<int, kFacingGroups> opaqueGroups{}, cutoutGroups{};
        std::uint64_t visibility = kVisibilityOpen;  // how the faces of the section connect (occlusion culling)
    };
    struct ChunkResult {
        ChunkPos pos;
        std::shared_ptr<Chunk> chunk;
    };

    void requestChunk(ChunkPos pos, float priority);
    void requestMeshes(ChunkPos pos);
    [[nodiscard]] bool neighborsLoaded(ChunkPos pos) const;
    [[nodiscard]] bool neighborsLit(ChunkPos pos) const;  // the chunk and the 8 around it have their light
    struct LightResult {
        ChunkPos pos;
        std::uint32_t stamp;
        std::shared_ptr<const ChunkLight> light;
    };
    void requestLight(ChunkPos pos, bool force);
    void applyLight(LightResult&& result);
    void requestSectionMesh(const SectionKey& key, float priority);
    void remeshAll();  // re-mesh every section on the GPU (after a change to smooth lighting or the leaves option)
    // Leaf level of detail: leaf faces hidden between two leaf blocks are only made within this many chunks of the player (a forest has
    // several times more of them than anything else, and from further than this they cannot be told apart). Sections that cross the
    // line are built again, with a margin so that walking along it does not build them over and over.
    static constexpr int kLeafDetailChunks = 6;
    static constexpr int kLeafDetailMargin = 2;
    [[nodiscard]] bool leafLodFor(const SectionKey& key) const;
    void refreshLeafDetail();
    void acceptChunk(ChunkPos pos, std::shared_ptr<Chunk> chunk);
    void uploadMesh(MeshResult&& result);
    void dropSections(ChunkPos pos);
    void meshNow(const SectionKey& key);
    [[nodiscard]] std::uint32_t stampOf(ChunkPos pos) const;
    void destroySection(GpuSection& section);
    void measureGpu(bool begin);

    BlockModels& models_;
    TerrainGenerator generator_;
    std::shared_ptr<const worldgen::OverworldGenerator> overworld_;
    std::shared_ptr<WorldEdits> edits_;            // what the player changed (see setEdits)
    std::shared_ptr<const Decorator> decorator_;  // trees, plants and snow on the generated terrain (null without the generator)
    Level level_;

    ShaderProgram world_;
    ShaderProgram sky_;
    ShaderProgram lines_;
    ShaderProgram cracks_;
    std::vector<Texture2D> breakTextures_;
    gl::GLint crackViewProj_ = -1, crackTex_ = -1;
    gl::GLuint crackVao_ = 0, crackVbo_ = 0;
    gl::GLint uViewProj_ = -1, uOffset_ = -1, uFogColor_ = -1, uFogRange_ = -1, uAlphaCut_ = -1, uTex_ = -1, uGamma_ = -1;
    gl::GLint uHeldLight_ = -1, uHeldOrigin_ = -1, uHeldOn_ = -1, uEnvFog_ = -1;
    bool underwater_ = false;
    std::array<float, 3> waterFogColor_{};
    float waterFogStart_ = 0.0f, waterFogEnd_ = 0.0f;
    // The carried light: levels of the 32 x 32 x 32 blocks around the player, as a 3D texture (see setCarriedLight).
    static constexpr int kHeldLightSize = 32;
    gl::GLuint heldLightTex_ = 0;
    std::vector<std::uint8_t> heldLightVolume_;
    int heldLightX_ = 0, heldLightY_ = 0, heldLightZ_ = 0;  // the block it shines from; the volume's corner is 16 blocks off in each axis
    int heldLightLevel_ = 0;
    unsigned heldLightEdits_ = 0;   // blockEdits_ when the volume was last made
    int heldLightAge_ = 0;          // frames since it was last made (chunks load and change around the player)
    unsigned blockEdits_ = 0;       // counts setBlock calls
    gl::GLint skyForward_ = -1, skyRight_ = -1, skyUp_ = -1, skyTan_ = -1, skyHorizon_ = -1, skyZenith_ = -1;
    gl::GLint lineViewProj_ = -1;
    gl::GLuint indexBuffer_ = 0;
    gl::GLuint emptyVao_ = 0;
    // The shared vertex arena: one buffer and one vertex array for every section.
    gl::GLuint arenaVao_ = 0;
    gl::GLuint arenaBuffer_ = 0;
    gl::GLuint stagingBuffer_ = 0;  // new meshes are written here and copied into the arena by the GPU, so the CPU never waits for draws that use the arena
    std::size_t arenaCapacity_ = 0;  // vertices the buffer holds
    std::size_t arenaUsed_ = 0;      // bump pointer (vertices), including dead space
    std::size_t arenaDead_ = 0;      // freed vertices not yet reclaimed by a compaction
    void setupArenaVao();
    [[nodiscard]] std::uint32_t arenaAllocate(std::uint32_t verts);
    void arenaFree(std::uint32_t base, std::uint32_t verts);
    void arenaCompact(std::size_t newCapacity = 0);  // 0: keep the size
    void arenaMaybeShrink();                          // gives the memory back when far fewer vertices are live than the buffer holds
    void arenaGrow(std::size_t needed);
    gl::GLuint lineVao_ = 0;
    gl::GLuint lineVbo_ = 0;

    std::unordered_map<SectionKey, GpuSection, SectionKeyHash> sections_;
    std::unordered_set<SectionKey, SectionKeyHash> meshRequested_;  // asked for, or done (empty meshes included)
    std::unordered_set<SectionKey, SectionKeyHash> meshPending_;    // asked for and not yet back from the workers (main thread only)
    std::unordered_map<SectionKey, std::uint32_t, SectionKeyHash> meshGen_;  // latest mesh request of each section: older results are dropped
    std::unordered_set<ChunkPos, ChunkPosHash> chunkRequested_;
    std::unordered_set<ChunkPos, ChunkPosHash> lightRequested_;                // light job asked for, not applied yet
    std::unordered_map<ChunkPos, std::uint32_t, ChunkPosHash> lightStamps_;     // latest light request of each chunk
    std::unordered_map<ChunkPos, std::uint32_t, ChunkPosHash> editStamps_;

    mutable std::mutex resultsMutex_;
    std::vector<ChunkResult> chunkResults_;
    std::vector<LightResult> lightResults_;
    std::deque<MeshResult> meshResults_;

    std::atomic<int> playerChunkX_{0};
    std::atomic<int> playerChunkZ_{0};
    std::atomic<int> wantedRadius_{0};
    std::atomic<int> generating_{0};   // chunk jobs not finished
    std::atomic<int> meshing_{0};      // mesh jobs not finished
    std::atomic<int> lighting_{0};     // light jobs not finished
    float brightness_ = 0.5f;
    bool smoothLighting_ = true;
    bool cutoutLeaves_ = true;
    gl::GLuint heldVao_ = 0, heldVbo_ = 0;
    int lastChunkX_ = 1 << 30;
    int lastChunkZ_ = 1 << 30;
    int lastRadius_ = -1;
    std::size_t meshBytes_ = 0;
    std::size_t quadsTotal_ = 0;
    RenderStats stats_;

    gl::GLuint queries_[3] = {0, 0, 0};
    int queryWrite_ = 0;
    int queriesInFlight_ = 0;
    bool measuring_ = false;
    int statTick_ = 0;
    // Chunks that were let go of, kept compressed (a few KB each) for when the player comes back or raises the Render Distance.
    ChunkCache chunkCache_{128u * 1024u * 1024u};
    std::vector<VisibleSection> visible_;  // reused every frame
    double gpuMillis_ = 0.0;
    bool gpuTiming_ = false;

    // ---- Occlusion culling ------------------------------------------------------------------------------------------------------
    // Every section of the area around the player has a cell in a dense grid (x and z wrap, 128 chunks across, which is more than the
    // largest render distance needs) holding what the mesher said about how its six faces connect, and a pointer to its mesh when it
    // has one. Each frame the sections are walked from the camera's own outwards, through the faces that connect (a section that is
    // solid rock lets nothing pass), so a cave below the ground or a valley behind a hill is never drawn. A section that has not been
    // meshed yet counts as open, so nothing is ever hidden by something that is not known.
    static constexpr int kGridSize = 128;
    [[nodiscard]] static std::size_t gridIndex(int chunkX, int sectionY, int chunkZ) {
        return (static_cast<std::size_t>(chunkZ & (kGridSize - 1)) * kGridSize + static_cast<std::size_t>(chunkX & (kGridSize - 1))) * kSectionCount +
               static_cast<std::size_t>(sectionY);
    }
    std::vector<const GpuSection*> gridSection_;
    std::vector<std::uint64_t> gridVisibility_;
    std::vector<std::uint32_t> gridStamp_;  // the traversal that last reached each cell
    std::vector<std::uint8_t> gridIncoming_;   // faces a traversal entered the cell through (a set)
    std::vector<std::uint8_t> gridTravelled_;  // directions it has moved in to get there (a set): it never turns back on them
    std::uint32_t traversalStamp_ = 0;
    struct TraversalNode {
        std::uint32_t cell;  // index into the grid
        std::int32_t x, z;   // chunk
        std::int8_t y;       // section
    };
    std::vector<TraversalNode> traversal_;
    std::vector<const VisibleSection*> translucentOrder_;  // sections with see-through blocks, farthest first
    bool useOcclusion_ = true;
    void setGrid(const SectionKey& key, const GpuSection* section, std::uint64_t visibility);
    void collectVisibleByTraversal(const Camera& camera, const Frustum& frustum, float reach);
    void collectVisibleByScan(const Camera& camera, const Frustum& frustum, float reach);

    // ---- Drawing the lists with a few calls (OpenGL 4.3) --------------------------------------------------------------------------
    // One command per run of quads of a section (the same runs the one-by-one way draws), and one section offset per command, which the
    // vertex shader gets as an instanced attribute (the command's base instance picks it). All three passes go into the two buffers
    // before the first of them is drawn.
    struct IndirectCommand {
        std::uint32_t count, instanceCount, firstIndex;
        std::int32_t baseVertex;
        std::uint32_t baseInstance;
    };
    bool useMultiDraw_ = false;
    gl::GLuint arenaVaoInstanced_ = 0;
    gl::GLuint commandBuffer_ = 0;
    gl::GLuint offsetBuffer_ = 0;
    std::size_t commandBufferCapacity_ = 0, offsetBufferCapacity_ = 0;  // bytes
    std::vector<IndirectCommand> commands_;
    std::vector<float> commandOffsets_;
    gl::GLint uInstanced_ = -1;

    // ---- The light map as a 16 x 16 picture (sky light x block light), made again when the Brightness option changes ----------------
    gl::GLuint lightmapTex_ = 0;
    gl::GLint uLightmap_ = -1;
    float lightmapBrightness_ = -1.0f;
    Matrix4f lastViewProj_ = Matrix4f::identity();
    void updateLightmap();

    // Last so the workers stop (and are joined) before anything above them is destroyed.
    PriorityPool pool_;
};

}  // namespace falcon
