#pragma once

#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/block_models.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/render/lod_renderer.hpp"
#include "falcon/render/sprite_renderer.hpp"
#include "falcon/world/day_cycle.hpp"
#include "falcon/util/priority_pool.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/chunk_cache.hpp"
#include "falcon/world/mesher.hpp"
#include "falcon/world/world_save.hpp"
#include "falcon/world/terrain.hpp"
#include "falcon/world/decoration.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace falcon {
namespace jvm {
struct WorldSubmit;
}

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
    // What the worker threads spent so far, summed over every job (where the time of loading chunks goes). Wall-clock time of one job on one thread.
    struct StageTime {
        double milliseconds = 0.0;
        long jobs = 0;
    };
    StageTime terrainTime, decorateTime, lightTime, meshTime, packTime;
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

    // Another dimension: every chunk of the one that was here goes, and the chunks that come next are made by this generator (null: the simple terrain), decorated by this
    // decorator, with these edits put back. `skylight` false (the Nether) has no sky light at all. Main thread only; it waits for the workers to finish what they were doing.
    struct DimensionWorld {
        std::shared_ptr<const worldgen::OverworldGenerator> generator;
        std::shared_ptr<const Decorator> decorator;  // (the Overworld's: what the LODs read the trees from)
        std::function<void(Chunk&, int, int, const worldgen::ChunkExtras&)> decorate;  // what finishes a chunk after its terrain (null: the decorator's)
        std::shared_ptr<WorldEdits> edits;
        bool skylight = true;
        bool lods = false;  // levels of detail beyond the chunks (only the Overworld has them)
    };
    void enterDimension(DimensionWorld world);
    [[nodiscard]] Level& level() { return level_; }
    [[nodiscard]] const Level& level() const { return level_; }
    [[nodiscard]] const TerrainGenerator& generator() const { return generator_; }
    // Where a new player stands, and the height of the ground in a column, from whichever generator makes the chunks.
    [[nodiscard]] TerrainGenerator::Spawn findSpawn() const;
    [[nodiscard]] int surfaceHeight(int x, int z) const;
    [[nodiscard]] const worldgen::OverworldGenerator* overworld() const { return overworld_.get(); }
    [[nodiscard]] std::shared_ptr<const Decorator> decoratorPtr() const { return decorator_; }
    // A thick fog of the dimension (the Nether: from 10 blocks to 96, whatever the render distance): blocks. Call every frame; a start beyond the end (or 0, 0) is none.
    void setEnvironmentFog(float start, float end) {
        envFogStart_ = start;
        envFogEnd_ = end;
    }
    // The Brightness option (0..1) that lifts the dark end of the light map.
    void setBrightness(float brightness) { brightness_ = brightness; }
    // The sky of this moment (world/day_cycle.hpp): its colours, the sun, the moon, the stars, and how much of the sky's light the night takes away. Call every frame.
    void setSky(const daycycle::Sky& sky) { skyState_ = sky; }
    // Which parts of the sky are drawn (the Sky page of Video Settings). With the sky off nothing above the horizon is drawn at all, and the world is drawn over the clear
    // colour. The sun and the moon are two pictures in one texture that the sky samples: turning one off rebuilds that texture without it, so the sky's shader (and its Vulkan
    // twin) is the same either way. Call whenever an option changes; a call with the same values does nothing.
    void setSkyElements(bool sky, bool sun, bool moon, bool stars);
    // The clouds (the Clouds option): off, a flat layer (fast) or that layer with a second one under it (fancy), out to `distanceChunks` chunks. Call every frame with the options.
    // The bits a broken block throws (Video Settings' Particles: 0 for none, 4 for Decreased, 8 for All), out of the block's own picture at (x, y, z). They fly, fall and are
    // gone in about a second, drawn with the world's fog and light like the world.
    void spawnBlockBits(int x, int y, int z, const std::string& blockName, int count);
    // Mipmap Levels (0: the block pictures are drawn at their own size, the sharpest and the cheapest; 1 to 4: the smaller copies of themselves the game makes, which far away
    // blocks are drawn from). Call with the option; it costs nothing when the level has not changed.
    void setBlockTextureMipmaps(int levels);
    // Chunk Fade: how long a section that has just been built takes to fade in, in seconds (0: it appears at once, the cheapest).
    void setChunkFade(double seconds) { chunkFadeSeconds_ = static_cast<float>(std::max(0.0, seconds)); }
    // The soft shadow under a mob, on the ground at its feet: the session adds one for each mob it draws and the renderer empties the list every frame.
    void addEntityShadow(double x, double y, double z, double width) { shadows_.push_back({x, y, z, width}); }
    // Rain (or snow, but that is the picture's business): drops drawn round the player out to `radius` blocks, over the world. Off: none are drawn and the ones that were are gone.
    void setWeather(bool rain, int radius) {
        rain_ = rain;
        rainRadius_ = radius;
        if (!rain_) rainQuads_.clear();
    }
    void setClouds(bool on, bool thick, int distanceChunks) {
        cloudsOn_ = on;
        cloudsThick_ = thick;
        cloudDistance_ = distanceChunks;
    }
    // How many levels of sky light the night takes away just now (0 by day): what the rules of the game (spawning, sleeping) subtract from the sky light they read.
    [[nodiscard]] float skyDarkSteps() const { return skyState_.darkSteps; }
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
    // Levels of detail beyond the loaded chunks (render/lod_renderer.hpp): call every frame with the options. Only a world this client generates itself has them (not a
    // server's: its terrain is not known here).
    void setLods(const LodSettings& settings);
    [[nodiscard]] LodStats lodStats() const { return lod_ ? lod_->stats() : LodStats{}; }
    // The Smooth Lighting option: per-corner light and ambient occlusion, or flat per-face light. Changing it re-meshes
    // everything (the light is baked into the vertices).
    void setSmoothLighting(bool on);
    // How many of the worker threads make chunks (the rest sleep): all of them while the world loads, fewer while it is played.
    void setActiveWorkers(unsigned n) { pool_.setActive(n); }
    [[nodiscard]] unsigned activeWorkers() const { return pool_.activeCount(); }
    void reloadChunks() { remeshAll(); }  // F3 + A: every section is built again
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
    // A zoom (the spyglass) looks far, and what is far is a LOD. While it lasts, the chunks in the cone the camera looks into, out to `reachChunks`, are generated, lit and
    // drawn as real chunks (the ones beyond the render distance; the rest is as it was). `dirX`, `dirZ`: where the camera looks (horizontally), `halfAngleDegrees`: how wide
    // the cone is on each side. Call it every frame; a reach of 0 (or no further than the render distance) ends it, and the chunks go back to the cache.
    void setZoom(double x, double z, float dirX, float dirZ, float halfAngleDegrees, int reachChunks);
    // How far, in chunks, the world is drawn as chunks now: the render distance, or the reach of the zoom's cone when that is further.
    [[nodiscard]] int drawRadius(int renderDistance) const { return renderDistance > zoomReach_ ? renderDistance : zoomReach_; }
    // The far plane of the chunks' pictures: across, the render distance (and a bit more); up and down, the farthest the world goes from the eyes. A camera at y 430 looks at chunks
    // 360 blocks below it, which a far plane of the render distance alone cut off (with LODs on, a hole in the ground under the player).
    [[nodiscard]] static float farPlaneFor(int radiusChunks, double cameraY);
    // 0..1: how much of the area within `radius` chunks is generated and meshed. 1 means everything is on screen.
    [[nodiscard]] float loadProgress(int radius) const;
    // How far one chunk is, for the loading screen's chunk map (main thread).
    [[nodiscard]] ChunkLoadState chunkLoadState(ChunkPos pos) const;
    // True once the section holding the block (x, y, z) is generated and its mesh is on the GPU (or it has nothing to draw):
    // what the game waits for before it takes the loading screen away, so the player never opens their eyes in empty space.
    [[nodiscard]] bool sectionReady(int x, int y, int z) const;
    // True if the chunk is currently loaded and within the active render distance (not in LOD territory).
    [[nodiscard]] bool isChunkRendered(int chunkX, int chunkZ) const;
    [[nodiscard]] bool isBlockInRenderedChunk(int x, int z) const;
    // True if the chunk is rendered and currently in the view frustum / visible to the camera.
    [[nodiscard]] bool isChunkVisible(int chunkX, int chunkZ) const;

    // Draws the sky and the blocks. The framebuffer must be cleared (depth too); this leaves depth test on.
    void render(const Camera& camera, int framebufferWidth, int framebufferHeight, int renderDistance);
    // Coloured lines in the world (the F3 + G chunk borders, the F3 + B hitboxes). Positions are world coordinates; `width` is in pixels (1 is a plain line, more is drawn as a strip
    // that keeps its width on the screen); `onTop` lines are not hidden by the world.
    struct DebugLine {
        double ax, ay, az, bx, by, bz;
        float r, g, b, a;
        float width = 1.0f;
        bool onTop = false;
    };
    void drawDebugLines(const Camera& camera, int framebufferWidth, int framebufferHeight, const std::vector<DebugLine>& lines);
    // What mods drew in the level (their render pipelines' vertices, camera-relative): lines of their width, filled quads and triangles, with the depth test, the blending and the depth writing
    // they asked for, after the entities. See jvm/vm.hpp (WorldSubmit): the translation of a mod's pipeline into this renderer's own drawing, which runs on OpenGL and on Vulkan alike.
    void drawModGeometry(const Camera& camera, int framebufferWidth, int framebufferHeight, const std::vector<jvm::WorldSubmit>& submits);
    // Black outline of one block (the one the player looks at).
    void drawSelection(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z);
    // The ten pictures of a block being dug (destroy_stage_0..9); without them no cracks are drawn.
    void setBreakImages(const std::vector<Image>& stages);
    // The cracks on the block being dug, `stage` 0..9 (LevelRenderer: the picture is multiplied into the block's colours, so grey is no change).
    void drawBreakOverlay(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z, int stage);
    // Turns off what render() turned on, so the GUI can draw.
    void endFrame();

    // A world a server sends: chunks come from receiveChunk (any thread) and are let go by forgetChunk; none are made here.
    void setRemote(bool on) { remote_ = on; }
    [[nodiscard]] bool remote() const { return remote_; }
    void receiveChunk(ChunkPos pos, std::shared_ptr<Chunk> chunk);
    void forgetChunk(ChunkPos pos);
    void forgetAllChunks();  // another world: every chunk of the old one goes (in order with the chunks that come after)

    // Changes a block and remeshes the sections it touches at once.
    bool setBlock(int x, int y, int z, Block block);
    // Between these two, setBlock only records what it touched and the meshes and the light are redone once, at the end (an explosion changes hundreds of blocks).
    void beginBatch() { ++batchDepth_; }
    void endBatch();

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
        // The Chunk Fade option: a section that has just been built is drawn blended, from nothing (fade 0) to itself (1) over that many seconds.
        bool fadeSeen = false;    // it has been through a frame since it was built
        float fade = 1.0f;
        bool lodFar = false;      // built without the leaf faces between leaf blocks (it was far from the player)
        bool lodMatters = false;  // it has such faces: worth building again when it crosses the line
    };
    struct VisibleSection {
        const GpuSection* section;
        float ox, oy, oz;
        float distance2;
        int chunkX = 0, chunkZ = 0;
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
    bool skylight_ = true;                        // false in the Nether
    std::function<void(Chunk&, int, int, const worldgen::ChunkExtras&)> decorate_;  // another dimension's decoration (see enterDimension)
    float envFogStart_ = 1.0e9f, envFogEnd_ = 2.0e9f;
    bool lodsAllowed_ = true;
    Level level_;
    std::unique_ptr<LodRenderer> lod_;  // (before the pool: the workers that make its tiles are joined first)
    LodSettings lodSettings_;

    ShaderProgram world_;
    ShaderProgram sky_;
    ShaderProgram lines_;
    ShaderProgram cracks_;
    std::vector<Texture2D> breakTextures_;
    Texture2D celestial_;     // the sun and the eight faces of the moon, one under another (each 32 x 32): the sky draws them from it
    Image celestialImage_;    // the atlas the texture above was made from: setSkyElements puts the sun or the moon back into it
    bool celestialSun_ = true, celestialMoon_ = true;
    bool skyVisible_ = true, skyStarsOn_ = true;
    gl::GLint crackViewProj_ = -1, crackTex_ = -1;
    gl::GLuint crackVao_ = 0, crackVbo_ = 0;
    gl::GLint uViewProj_ = -1, uOffset_ = -1, uFogColor_ = -1, uFogRange_ = -1, uAlphaCut_ = -1, uTex_ = -1, uGamma_ = -1, uFade_ = -1;
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
    gl::GLint skySunDir_ = -1, skyAngle_ = -1, skyStars_ = -1, skyGlow_ = -1, skyMoon_ = -1, skyCelestial_ = -1;
    daycycle::Sky skyState_;
    // The clouds and the world's little quads (a broken block's bits): the game's own cloud picture, laid over the world 192 blocks up.
    SpriteRenderer sprites_;
    Texture2D cloudTexture_;
    Texture2D rainTexture_;
    // A bit of a block thrown by breaking one: where it is, how it moves, how long it has left and which part of the picture it shows.
    struct BlockBit {
        std::array<float, 3> pos, velocity;
        float life = 0.0f, maxLife = 1.0f, size = 0.1f, u0 = 0.0f, v0 = 0.0f, layer = 0.0f;
        std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
    };
    std::vector<BlockBit> bits_;
    std::vector<std::array<float, 3>> rainQuads_;  // (where each drop is this frame: x, y, z of its top)
    bool rain_ = false;
    int rainRadius_ = 10;
    double lastFrameSeconds_ = 0.0;  // (the frame before this one: what the bits fall by)
    unsigned bitSeed_ = 12345u;
    int blockMipLevels_ = -1;  // (the Mipmap Levels the block pictures were set to: -1, not set yet)
    float chunkFadeSeconds_ = 0.0f;
    Texture2D shadowTexture_;                         // (a soft round blob, made in code: white with an alpha that fades to nothing at its edge)
    std::vector<std::array<double, 4>> shadows_;      // x, y, z of where a mob stands, and how wide its shadow is
    [[nodiscard]] float bitRandom();
    bool cloudsOn_ = false, cloudsThick_ = false;
    int cloudDistance_ = 128;
    [[nodiscard]] bool lightmapSkyChanged() const;
    std::array<float, 7> lightmapSky_{};  // the sky factor and colour and the ambient light the light map picture was made for
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
    int batchDepth_ = 0;
    bool remote_ = false;
    std::mutex remoteMutex_;
    std::vector<std::pair<ChunkPos, std::shared_ptr<Chunk>>> remoteChunks_;
    std::vector<ChunkPos> remoteForgets_;
    std::vector<SectionKey> batchTouched_;
    std::unordered_set<ChunkPos, ChunkPosHash> batchLight_;

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
    enum Stage { kStageTerrain, kStageDecorate, kStageLight, kStageMesh, kStagePack, kStageCount };
    std::array<std::atomic<std::int64_t>, kStageCount> stageNanos_{};  // time the workers spent in each stage, and how many times
    std::array<std::atomic<long>, kStageCount> stageJobs_{};
    void addStageTime(Stage stage, std::chrono::steady_clock::time_point since) {
        stageNanos_[stage] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - since).count();
        ++stageJobs_[stage];
    }
    float brightness_ = 0.5f;
    bool smoothLighting_ = true;
    bool cutoutLeaves_ = true;
    gl::GLuint heldVao_ = 0, heldVbo_ = 0;
    int lastChunkX_ = 1 << 30;
    int lastChunkZ_ = 1 << 30;
    int lastRadius_ = -1;
    int readyCx_ = 1 << 30, readyCz_ = 1 << 30, readyFor_ = -1, readyRadius2_ = -1, readyAge_ = 0;  // (chunkCircleReady's last answer)
    // The zoom cone (setZoom). The request is the last call's; the sets are made from it when it changed (main thread) and read by the workers, who check a chunk against them
    // before they spend time on it.
    struct ZoomCone {
        double x = 0.0, z = 0.0;
        float dirX = 0.0f, dirZ = 1.0f, halfAngle = 0.0f;
        int reach = 0;
    };
    using ChunkSet = std::unordered_set<ChunkPos, ChunkPosHash>;
    ZoomCone zoomRequest_;
    int zoomReach_ = 0;                                   // the reach of the cone in force, in chunks: 0 when there is none
    std::array<int, 6> zoomKey_{-1, 0, 0, 0, 0, 0};       // what the sets were made for
    std::vector<std::pair<ChunkPos, int>> zoomOrder_;     // the chunks to generate, nearest first, with their squared distance (main thread)
    mutable std::mutex zoomMutex_;
    std::shared_ptr<const ChunkSet> zoomWanted_;          // the cone beyond the render distance: drawn
    std::shared_ptr<const ChunkSet> zoomGenerate_;        // and a ring of two chunks round it: generated, so that the light and the faces at the edges have their neighbours
    [[nodiscard]] bool inZoom(ChunkPos pos, bool generate) const;
    [[nodiscard]] bool drawable(int chunkX, int chunkZ) const;  // whether the chunk's sections are drawn (see the zoom)
    // The radius squared, in chunks, of the circle round the player's chunk in which every chunk has all its meshes (the LODs are cut out of that circle and no wider: a chunk the player
    // has just come to, which the workers have not finished, shows the LOD instead of a hole). At most `radius`; -1 when the player's own chunk is not there. Remembered for a few frames.
    [[nodiscard]] int chunkCircleReady(int radius);
    [[nodiscard]] bool chunkMeshed(ChunkPos pos) const;
    bool refreshZoom(int playerChunkX, int playerChunkZ, int renderDistance);  // true when the cone changed
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
    std::array<double, 3> lastCameraPos_{};
    Frustum lastFrustum_{};
    std::unordered_set<ChunkPos, ChunkPosHash> visibleChunks_;
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
        bool entry;          // the camera's own section: every face is open, and nothing led to it
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
