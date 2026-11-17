#include "falcon/render/lod_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace falcon {
namespace {

// The shaders: a tile's vertices carry their place in the tile (x and z in cells, y in blocks), the picture of the face (a layer of the block texture array) and its tint, with the
// shade of the face already in it. The picture is laid on the face once a block (the coordinates are in blocks, and the array repeats), the way Distant Horizons and Voxy draw their
// distant blocks, and lit with the sky light of the world's own light map, so it follows the time of day. Fog is the world's, so LODs and chunks melt into the same haze.
// (The Vulkan twins are src/vulkan/shaders/lod.vert and lod.frag.)
constexpr char kVertexShader[] = R"(// vk: lod.vert
#version 330 core
layout(location = 0) in vec4 aPos;     // x and z in cells, y in blocks, inside the tile; w: the layer of the picture
layout(location = 1) in vec4 aColor;   // rgb: the tint with the shade of the face in it; a: the face (0 top, 1 north, 2 south, 3 west, 4 east) and, from bit 3, which corner of its quad this is
uniform mat4 uViewProj;
uniform vec3 uOffset;                  // where the tile's corner is, relative to the camera
uniform float uCell;                   // the width of a cell, in blocks
out vec3 vColor;
out float vDist;
out vec2 vUv;
flat out float vLayer;
void main() {
    vec3 block = vec3(aPos.x * uCell, aPos.y, aPos.z * uCell);  // in blocks from the tile's corner, which is a whole number of blocks: a picture lines up from tile to tile
    vec3 p = block + uOffset;
    float dist = length(p);
    int bits = int(aColor.a * 255.0 + 0.5);
    int face = bits & 7;
    int corner = bits >> 3;
    // Sub-pixel quad growth to seal T-junction cracks without inflating or deforming distant mountains:
    float grow = min(0.04, 0.005 + 0.00002 * dist);
    float alongX = corner >= 2 ? 1.0 : -1.0;                    // the corners run (low, low), (low, high), (high, high), (high, low) along the quad's two axes
    float alongZ = (corner == 1 || corner == 2) ? 1.0 : -1.0;
    float down = (corner == 0 || corner == 3) ? -1.0 : 0.0;     // a wall also grows down (never up, so it does not rise over the top it holds)
    vec3 grown = vec3(0.0);
    if (face == 0) grown = vec3(alongX, 0.0, alongZ) * grow;
    else if (face < 3) grown = vec3(alongX * grow, down * grow, 0.0);
    else grown = vec3(0.0, down * grow, alongX * grow);
    gl_Position = uViewProj * vec4(p + grown, 1.0);
    // Smooth ambient occlusion: the base of a cliff/wall (corners 0 and 3) is shaded compared to the top:
    float ao = (face != 0 && (corner == 0 || corner == 3)) ? 0.82 : 1.0;
    vColor = aColor.rgb * ao;
    vDist = dist;
    // A top is laid out along x and z; the sides of north and south along x and up, those of west and east along z and up (v runs down the picture).
    vUv = face == 0 ? block.xz : (face < 3 ? vec2(block.x, -block.y) : vec2(block.z, -block.y));
    vLayer = aPos.w;
}
)";

constexpr char kFragmentShader[] = R"(// vk: lod.frag
#version 330 core
in vec3 vColor;
in float vDist;
in vec2 vUv;
flat in float vLayer;
uniform sampler2DArray uTex;           // the block pictures, mip-mapped: far away a block is its average colour
uniform sampler2D uLightmap;           // the world's light map: sky light across, block light down
uniform vec3 uFogColor;
uniform vec2 uFogRange;                // the world's fog: start, end (distance from the camera)
uniform vec2 uEnvFog;                  // the fog of the medium the eyes are in (water); far away when there is none
out vec4 frag;
void main() {
    // One mip level sharper than the hardware would pick: preserves texture definition on distant terrain
    vec4 t = texture(uTex, vec3(vUv, vLayer), -1.0);
    vec3 rgb = t.rgb;
    // Foliage cutouts: darken only actual leaf holes (alpha near 0), not translucent water:
    if (t.a < 0.25) rgb *= 0.45;
    vec3 light = texture(uLightmap, vec2(1.0, 0.0) * (15.0 / 16.0) + vec2(0.5 / 16.0)).rgb;  // sky light 15, block light 0
    // Distant Horizons style exponential-squared atmospheric fog:
    float d = max(0.0, vDist - uFogRange.x);
    float span = max(1.0, uFogRange.y - uFogRange.x);
    float fogNorm = clamp(d / span, 0.0, 1.0);
    float fog = 1.0 - exp(-fogNorm * fogNorm * 3.5);
    float env = clamp((vDist - uEnvFog.x) / max(1.0, uEnvFog.y - uEnvFog.x), 0.0, 1.0);
    fog = max(fog, env);
    // Subtle horizon fresnel highlight on water surfaces:
    vec3 col = rgb * vColor;
    if (t.a < 0.85 && t.a > 0.25) {
        float fresnel = pow(clamp(vDist / max(100.0, uFogRange.y), 0.0, 1.0), 3.0) * 0.35;
        col = mix(col, uFogColor, fresnel);
    }
    frag = vec4(mix(col * light, uFogColor, fog), 1.0);
}
)";

constexpr int kCells = LodRenderer::kCells;
constexpr int kSea = worldgen::LodSurface::kSeaLevel;
// The most quads a tile can hold: a top for every cell and, on each of its four sides, a wall of as many bands as the ground has layers (see wallBands).
constexpr std::uint32_t kMaxQuads = kCells * kCells * LodRenderer::kMaxQuadsPerCell;

// The one index list every tile shares: quad n is vertices 4n..4n+3, drawn as two triangles.
std::vector<std::uint32_t> makeIndices() {
    std::vector<std::uint32_t> out;
    out.reserve(static_cast<std::size_t>(kMaxQuads) * 6);
    for (std::uint32_t q = 0; q < kMaxQuads; ++q) {
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) out.push_back(q * 4 + i);
    }
    return out;
}

using Colour = std::array<std::uint8_t, 3>;
constexpr Colour kWhite{255, 255, 255};

// A band of the side of a cliff: from y = low to y = high, one picture, one tint.
struct Band {
    int low, high;
    std::uint16_t layer;
    Colour tint;
};

// The picture of the top of a column: the leaves of its trees, the sea, or what the ground is made of.
std::uint16_t topLayer(const worldgen::LodColumn& c, const LodTextures& t) {
    using worldgen::LodGround;
    if (c.water) return c.ice ? t.ice : t.water;
    if (c.tree > 0) return t.leaves[std::min<std::size_t>(c.tree, t.leaves.size() - 1)];
    switch (c.ground) {
        case LodGround::Grass: return t.grassTop;
        case LodGround::Sand: return t.sand;
        case LodGround::Terracotta: return t.terracotta;
        case LodGround::Snow: return t.snow;
        case LodGround::Stone: return t.stone;
        case LodGround::Mycelium: return t.mycelium;
        case LodGround::Gravel: return t.gravel;
        case LodGround::Water: return t.sand;
    }
    return t.stone;
}

// The bands of the wall of column `c` between `low` and `high` (a wall is made by the higher of two columns; `low` is the top of the lower one), the highest first, each with the
// picture of the block it is: the leaves of a tree on top (they stand over the ground), the topmost block of the ground, the earth under it, then stone, and deepslate under y 4
// (the game mixes the two between y 8 and 0). At most kMaxBands.
constexpr int kMaxBands = LodRenderer::kMaxBands;
int wallBands(const worldgen::LodColumn& c, int low, int high, const LodTextures& t, std::array<Band, kMaxBands>& out) {
    using worldgen::LodGround;
    int n = 0;
    const auto add = [&](int from, int to, std::uint16_t layer, const Colour& tint) {  // the part of [to, from] inside the wall
        const int hi = std::min(from, high), lo = std::max(to, low);
        if (hi <= lo || n >= kMaxBands) return;
        if (n > 0) {
            Band& last = out[static_cast<std::size_t>(n - 1)];
            if (last.layer == layer && last.tint == tint && last.low == hi) {  // the same block as the band above: one band
                last.low = lo;
                return;
            }
        }
        out[static_cast<std::size_t>(n++)] = {lo, hi, layer, tint};
    };
    const Colour top{c.r, c.g, c.b};
    if (c.water) {  // a sea has no layers to show
        add(high, low, c.ice ? t.ice : t.water, top);
        return n;
    }
    const int ground = c.height - c.canopy;
    if (c.canopy > 0) add(c.height, ground, t.leaves[std::min<std::size_t>(c.tree, t.leaves.size() - 1)], top);
    // The topmost block, and the layers under it (thickness in blocks): earth, sand over sandstone, and so on.
    struct Layer {
        int thickness;
        std::uint16_t layer;
    };
    std::uint16_t strip = t.stone;
    std::array<Layer, 2> layers{};
    int layerCount = 0;
    switch (c.ground) {
        case LodGround::Grass: strip = t.grassSide, layers[0] = {3, t.dirt}, layerCount = 1; break;
        case LodGround::Mycelium: strip = t.myceliumSide, layers[0] = {3, t.dirt}, layerCount = 1; break;
        case LodGround::Sand: strip = t.sand, layers[0] = {3, t.sand}, layers[1] = {6, t.sandstone}, layerCount = 2; break;
        case LodGround::Terracotta: strip = t.terracotta, layers[0] = {10, t.terracotta}, layerCount = 1; break;
        case LodGround::Snow: strip = c.bareSnow ? t.snow : t.snowSide, layers[0] = {3, c.bareSnow ? t.snow : t.dirt}, layerCount = 1; break;
        case LodGround::Gravel: strip = t.gravel, layers[0] = {4, t.gravel}, layerCount = 1; break;
        case LodGround::Stone: strip = t.stone; break;  // bare rock: stone all the way
        case LodGround::Water: strip = t.sand; break;
    }
    int y = ground;
    add(y, y - 1, strip, kWhite);
    y -= 1;
    for (int i = 0; i < layerCount; ++i) {
        add(y, y - layers[static_cast<std::size_t>(i)].thickness, layers[static_cast<std::size_t>(i)].layer, kWhite);
        y -= layers[static_cast<std::size_t>(i)].thickness;
    }
    if (y > 4) {
        add(y, 4, t.stone, kWhite);
        y = 4;
    }
    add(y, low, t.deepslate, kWhite);
    return n;
}

LodTextures resolveTextures(const BlockModels& models) {
    const auto layer = [&](const char* name) { return models.layerFor(std::string("minecraft:block/") + name); };
    LodTextures t;
    t.grassTop = layer("grass_block_top");
    t.grassSide = layer("grass_block_side");
    t.dirt = layer("dirt");
    t.stone = layer("stone");
    t.deepslate = layer("deepslate");
    t.sand = layer("sand");
    t.sandstone = layer("sandstone");
    t.snow = layer("snow");
    t.snowSide = layer("grass_block_snow");
    t.terracotta = layer("terracotta");
    t.mycelium = layer("mycelium_top");
    t.myceliumSide = layer("mycelium_side");
    t.gravel = layer("gravel");
    t.water = layer("water_still");
    t.ice = layer("ice");
    t.leaves = {layer("oak_leaves"), layer("oak_leaves"), layer("birch_leaves"), layer("spruce_leaves"), layer("acacia_leaves"), layer("jungle_leaves"), layer("cherry_leaves"), layer("dark_oak_leaves")};
    return t;
}

}  // namespace

LodRenderer::Detail LodRenderer::detailOf(int detail) {
    switch (detail) {
        // The five types of Distant Horizons: a cell of 1, 2, 4, 8 or 16 blocks (a block, two blocks, four blocks, half a chunk, a whole chunk), the finest next to the loaded chunks
        // and the next one for every doubling of the distance (a cell at most k times as far as it is wide). Past the fifth the cells keep doubling, only so that a far radius is
        // reachable. Low starts at 8 blocks (types 4 and 5) and never reads a real chunk (but it does read the real height of every column).
        case kLodDetailLow: return {8, 1.0f / 40.0f, 0};
        default: return {1, detail == kLodDetailFull ? 1.0f / 200.0f : 1.0f / 100.0f, 2};
    }
}

std::pair<float, float> LodRenderer::fogRange(int radiusChunks, int nearChunks) {
    const float end = static_cast<float>(radiusChunks) * 16.0f;
    const float start = static_cast<float>(nearChunks) * 16.0f;
    return {start, end};
}

LodRenderer::LodRenderer(std::shared_ptr<const worldgen::OverworldGenerator> generator, std::vector<worldgen::LodTrees> trees, PriorityPool& pool, BlockModels& models)
    : surface_(std::make_shared<const worldgen::LodSurface>(
          std::move(generator), std::move(trees),
          worldgen::LodColourMaps{[&models](float t, float d) { return models.grassColor(t, d); }, [&models](float t, float d) { return models.foliageColor(t, d); }})),
      pool_(pool),
      shared_(std::make_shared<Shared>()),
      models_(models),
      textures_(resolveTextures(models)),
      program_("lod", kVertexShader, kFragmentShader) {
    uViewProj_ = program_.uniform("uViewProj");
    uOffset_ = program_.uniform("uOffset");
    uCell_ = program_.uniform("uCell");
    uFogColor_ = program_.uniform("uFogColor");
    uFogRange_ = program_.uniform("uFogRange");
    uEnvFog_ = program_.uniform("uEnvFog");
    uTex_ = program_.uniform("uTex");
    uLightmap_ = program_.uniform("uLightmap");

    const std::vector<std::uint32_t> indices = makeIndices();
    gl::GenVertexArrays(1, &vao_);
    gl::BindVertexArray(vao_);
    gl::GenBuffers(1, &indexBuffer_);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, indexBuffer_);  // (part of the vertex array's state)
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::BindVertexArray(0);
}

LodRenderer::~LodRenderer() {
    shared_->epoch.fetch_add(1);  // jobs that are still queued make nothing
    for (Buffer& b : buffers_) {
        if (b.id != 0) gl::DeleteBuffers(1, &b.id);
    }
    if (indexBuffer_ != 0) gl::DeleteBuffers(1, &indexBuffer_);
    if (vao_ != 0) gl::DeleteVertexArrays(1, &vao_);
}

void LodRenderer::releaseAll() {
    for (Buffer& b : buffers_) {
        b.free.clear();
        b.free[0] = kVerticesPerBuffer;
    }
    tiles_.clear();
    wanted_.clear();
    uploadedVertices_ = 0;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->done.clear();
}

void LodRenderer::setSettings(const LodSettings& requested) {
    LodSettings s = requested;
    s.radiusChunks = std::clamp(s.radiusChunks, kLodMinChunks, kLodMaxChunks);
    s.detail = std::clamp(s.detail, kLodDetailLow, kLodDetailFull);
    if (s == settings_) return;
    if (s.detail != settings_.detail) {
        shared_->epoch.fetch_add(1);  // the tiles of the old strength are not wanted any more, in the queue or on the way back
        releaseAll();
        detail_ = detailOf(s.detail);
    }
    settings_ = s;
    if (!settings_.enabled) releaseAll();  // off: give the memory back
    // The top level is the first whose cells are fine enough for the far end.
    const double far = static_cast<double>(settings_.radiusChunks) * 16.0;
    topLevel_ = 0;
    while (topLevel_ < 15 && static_cast<double>(cellOf(topLevel_)) * (1.0 / static_cast<double>(detail_.k)) < far) ++topLevel_;
}

// ---- which tiles --------------------------------------------------------------------------------------------------------------------------------------------------

void LodRenderer::split(const TileKey& key, double camX, double camZ, double nearBlocks, double maxBlocks) {
    const double size = tileSize(key.level);
    const double x0 = static_cast<double>(key.tx) * size, z0 = static_cast<double>(key.tz) * size;
    const double nx = std::clamp(camX, x0, x0 + size), nz = std::clamp(camZ, z0, z0 + size);
    const double near = std::hypot(camX - nx, camZ - nz);
    if (near >= maxBlocks) return;  // beyond the last LOD
    const double farX = std::max(std::abs(camX - x0), std::abs(camX - (x0 + size))), farZ = std::max(std::abs(camZ - z0), std::abs(camZ - (z0 + size)));
    if (std::hypot(farX, farZ) <= nearBlocks * 0.9) return;  // wholly inside the loaded chunks: they are drawn there
    // A cell is too coarse when it is more than k times as far as it is wide; inside the loaded chunks nothing is drawn, so nothing there asks for detail.
    const double distance = std::max(near, nearBlocks * 0.9);
    if (key.level > 0 && static_cast<double>(cellOf(key.level)) > static_cast<double>(detail_.k) * distance && wanted_.size() < 8000) {
        for (int b = 0; b < 2; ++b) {
            for (int a = 0; a < 2; ++a) split({key.level - 1, key.tx * 2 + a, key.tz * 2 + b}, camX, camZ, nearBlocks, maxBlocks);
        }
        return;
    }
    wanted_.push_back({key, near});
}

void LodRenderer::select(double camX, double camZ, double nearBlocks, double maxBlocks) {
    wanted_.clear();
    const double size = tileSize(topLevel_);
    const auto first = [&](double v) { return static_cast<int>(std::floor((v - maxBlocks) / size)); };
    const auto last = [&](double v) { return static_cast<int>(std::floor((v + maxBlocks) / size)); };
    for (int tz = first(camZ); tz <= last(camZ); ++tz) {
        for (int tx = first(camX); tx <= last(camX); ++tx) split({topLevel_, tx, tz}, camX, camZ, nearBlocks, maxBlocks);
    }
}

// ---- making tiles -------------------------------------------------------------------------------------------------------------------------------------------------

// A tile as columns: for every cell a flat top, and a wall wherever the next cell is lower (walls between the cells of a tile are made by the higher one, so none is made
// twice); a run of cells with the same top, picture and tint is one quad, so flat country (the sea above all) is a few quads. The edge of the tile also gets a wall that hangs a
// good way under it whatever its neighbour is, which hides the gaps where it meets a tile of another level (their cells do not line up).
std::vector<LodRenderer::Vertex> LodRenderer::buildColumns(const worldgen::LodGrid& grid, int cell, const LodTextures& textures, float& minY, float& maxY) {
    std::vector<Vertex> out;
    out.reserve(2048);
    int lowest = 100000, highest = -100000;
    const auto top = [&](int i, int j) {
        const worldgen::LodColumn& c = grid.at(i, j);
        return static_cast<int>(c.water ? kSea : c.height);
    };
    const auto colourOf = [&](int i, int j) {
        const worldgen::LodColumn& c = grid.at(i, j);
        return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
    };
    // Two columns whose walls look the same: the same tint on top, the same kind of ground under it, the same trees, and the ground at the same height.
    const auto sameStyle = [&](int i, int j, int ni, int nj) {
        const worldgen::LodColumn& a = grid.at(i, j);
        const worldgen::LodColumn& b = grid.at(ni, nj);
        return colourOf(i, j) == colourOf(ni, nj) && a.ground == b.ground && a.bareSnow == b.bareSnow && a.water == b.water && a.ice == b.ice && a.tree == b.tree &&
               a.height - a.canopy == b.height - b.canopy;
    };
    const int skirt = std::min(2 * cell + 4, 320);
    // The shade of each way a face looks, as the game gives its blocks: up 1, north and south 0.8, east and west 0.6.
    const auto shaded = [](const Colour& rgb, float shade) {
        const auto channel = [&](std::uint8_t c) { return static_cast<std::uint8_t>(std::min(255.0f, std::floor(static_cast<float>(c) * shade + 0.5f))); };
        return Colour{channel(rgb[0]), channel(rgb[1]), channel(rgb[2])};
    };
    const auto cornerShade = [&](int x, int z, int h, bool isWater) -> float {
        if (isWater) return 1.0f;

        const int h00 = top(x - 1, z - 1);
        const int h10 = top(x, z - 1);
        const int h01 = top(x - 1, z);
        const int h11 = top(x, z);

        // 1. Ambient Occlusion: occluded by higher neighboring columns
        int occluded = 0;
        if (h00 > h) ++occluded;
        if (h10 > h) ++occluded;
        if (h01 > h) ++occluded;
        if (h11 > h) ++occluded;
        const float ao = 1.0f - 0.12f * static_cast<float>(occluded);

        // 2. Directional Slope Lighting (Hillshading)
        const float cellF = static_cast<float>(std::max(1, cell));
        const float dhdx = static_cast<float>((h10 + h11) - (h00 + h01)) / (2.0f * cellF);
        const float dhdz = static_cast<float>((h01 + h11) - (h00 + h10)) / (2.0f * cellF);

        const float len = std::sqrt(dhdx * dhdx + 1.0f + dhdz * dhdz);
        const float nx = -dhdx / len;
        const float ny = 1.0f / len;
        const float nz = -dhdz / len;

        // Light direction: slightly from South-East (standard sun angle)
        // Scaled so flat horizontal (ny=1, nx=0, nz=0) is exactly 1.0f
        float slopeLight = 1.0f + (0.35f * nx - 0.40f * nz) * 0.45f - (1.0f - ny) * 0.25f;
        slopeLight = std::clamp(slopeLight, 0.65f, 1.25f);

        return std::clamp(ao * slopeLight, 0.4f, 1.25f);
    };
    const auto corner = [&](int x, int y, int z, std::uint16_t layer, const Colour& c, std::uint8_t face) {
        // (A quad is always four corners in a row: the vertex shader grows each quad by its corner, see kVertexShader.)
        const auto which = static_cast<std::uint8_t>(out.size() & 3u);
        out.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(z), layer, c[0], c[1], c[2], static_cast<std::uint8_t>(face | (which << kCornerShift))});
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
    };

    // The tops: along each row, runs of the same height, picture and tint.
    for (int j = 0; j < kCells; ++j) {
        for (int i = 0; i < kCells;) {
            const int h = top(i, j);
            const std::uint32_t colour = colourOf(i, j);
            const std::uint16_t layer = topLayer(grid.at(i, j), textures);
            int e = i + 1;
            while (e < kCells && top(e, j) == h && colourOf(e, j) == colour && topLayer(grid.at(e, j), textures) == layer) ++e;
            const bool isWater = grid.at(i, j).water;
            const Colour baseColour{grid.at(i, j).r, grid.at(i, j).g, grid.at(i, j).b};
            const auto c0 = shaded(baseColour, cornerShade(i, j, h, isWater));
            const auto c1 = shaded(baseColour, cornerShade(i, j + 1, h, isWater));
            const auto c2 = shaded(baseColour, cornerShade(e, j + 1, h, isWater));
            const auto c3 = shaded(baseColour, cornerShade(e, j, h, isWater));
            corner(i, h, j, layer, c0, kFaceTop);
            corner(i, h, j + 1, layer, c1, kFaceTop);
            corner(e, h, j + 1, layer, c2, kFaceTop);
            corner(e, h, j, layer, c3, kFaceTop);
            i = e;
        }
    }

    // The walls. `facing` 0..3 is west, east, north, south: a west wall is the face of a cell at x = i that looks to -x, and so on.
    struct Wall {
        bool has;
        int low, high;
    };
    const auto wallOf = [&](int facing, int i, int j) -> Wall {
        static constexpr int kDi[4] = {-1, 1, 0, 0}, kDj[4] = {0, 0, -1, 1};
        const int h = top(i, j);
        const int neighbour = top(i + kDi[facing], j + kDj[facing]);
        const bool onEdge = (facing == 0 && i == 0) || (facing == 1 && i == kCells - 1) || (facing == 2 && j == 0) || (facing == 3 && j == kCells - 1);
        if (onEdge) return {true, std::min(neighbour, h) - skirt, h};
        return {neighbour < h, neighbour, h};
    };
    static constexpr std::uint8_t kFaceOfFacing[4] = {kFaceWest, kFaceEast, kFaceNorth, kFaceSouth};
    for (int facing = 0; facing < 4; ++facing) {
        const bool alongZ = facing < 2;  // west and east walls run along z (their cells are in a column), north and south ones along x
        const float shade = alongZ ? 0.6f : 0.8f;
        const std::uint8_t face = kFaceOfFacing[facing];
        for (int line = 0; line < kCells; ++line) {  // the column (west, east) or row (north, south) of cells the wall runs along
            for (int t = 0; t < kCells;) {
                const int i = alongZ ? line : t, j = alongZ ? t : line;
                const Wall w = wallOf(facing, i, j);
                if (!w.has) {
                    ++t;
                    continue;
                }
                int e = t + 1;
                while (e < kCells) {
                    const int ni = alongZ ? line : e, nj = alongZ ? e : line;
                    const Wall n = wallOf(facing, ni, nj);
                    if (!n.has || n.low != w.low || n.high != w.high || !sameStyle(i, j, ni, nj)) break;
                    ++e;
                }
                std::array<Band, kMaxBands> bands;
                const int bandCount = wallBands(grid.at(i, j), w.low, w.high, textures, bands);
                for (int k = 0; k < bandCount; ++k) {
                    const Band& band = bands[static_cast<std::size_t>(k)];
                    const auto c = shaded(band.tint, shade);
                    if (alongZ) {
                        const int x = facing == 0 ? line : line + 1;
                        corner(x, band.low, t, band.layer, c, face);
                        corner(x, band.high, t, band.layer, c, face);
                        corner(x, band.high, e, band.layer, c, face);
                        corner(x, band.low, e, band.layer, c, face);
                    } else {
                        const int z = facing == 2 ? line : line + 1;
                        corner(t, band.low, z, band.layer, c, face);
                        corner(t, band.high, z, band.layer, c, face);
                        corner(e, band.high, z, band.layer, c, face);
                        corner(e, band.low, z, band.layer, c, face);
                    }
                }
                t = e;
            }
        }
    }
    minY = static_cast<float>(lowest);
    maxY = static_cast<float>(highest);
    return out;
}

void LodRenderer::request(const TileKey& key, double priority) {
    if (tiles_.count(key) != 0) return;
    Tile tile;
    tile.lastUsed = frame_;
    tiles_.emplace(key, tile);
    shared_->building.fetch_add(1);
    const int cell = cellOf(key.level);
    const double size = tileSize(key.level);
    const int originX = static_cast<int>(std::llround(static_cast<double>(key.tx) * size));
    const int originZ = static_cast<int>(std::llround(static_cast<double>(key.tz) * size));
    // Every cell reads the real height (never the preliminary surface, which is only good to eight blocks and loses low land to the sea): whole chunks for the finest cells, a probe
    // for each column of the others.
    const worldgen::LodMode mode = cell <= detail_.chunkMax ? worldgen::LodMode::Chunk : worldgen::LodMode::Direct;
    const int epoch = shared_->epoch.load();
    pool_.submit(static_cast<float>(priority), [surface = surface_, shared = shared_, textures = textures_, key, epoch, cell, mode, originX, originZ] {
        if (shared->epoch.load() == epoch) {
            const worldgen::LodGrid grid = surface->grid(originX, originZ, cell, kCells, mode);
            Result r;
            r.key = key;
            r.epoch = epoch;
            r.vertices = buildColumns(grid, cell, textures, r.minY, r.maxY);
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->done.push_back(std::move(r));
        }
        shared->building.fetch_sub(1);
    });
}

// ---- the GPU's side -----------------------------------------------------------------------------------------------------------------------------------------------

bool LodRenderer::allocate(std::uint32_t count, int& buffer, std::uint32_t& first) {
    if (count == 0 || count > kVerticesPerBuffer) return false;
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        for (auto it = buffers_[i].free.begin(); it != buffers_[i].free.end(); ++it) {
            if (it->second < count) continue;
            buffer = static_cast<int>(i);
            first = it->first;
            const std::uint32_t left = it->second - count;
            const std::uint32_t at = it->first + count;
            buffers_[i].free.erase(it);
            if (left > 0) buffers_[i].free[at] = left;
            return true;
        }
    }
    Buffer b;
    gl::GenBuffers(1, &b.id);
    gl::BindBuffer(gl::ARRAY_BUFFER, b.id);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(static_cast<std::size_t>(kVerticesPerBuffer) * sizeof(Vertex)), nullptr, gl::DYNAMIC_DRAW);
    b.free[count] = kVerticesPerBuffer - count;
    if (kVerticesPerBuffer == count) b.free.clear();
    buffers_.push_back(std::move(b));
    buffer = static_cast<int>(buffers_.size()) - 1;
    first = 0;
    return true;
}

void LodRenderer::release(int buffer, std::uint32_t first, std::uint32_t count) {
    auto& free = buffers_[static_cast<std::size_t>(buffer)].free;
    auto it = free.emplace(first, count).first;
    const auto next = std::next(it);
    if (next != free.end() && it->first + it->second == next->first) {  // joins the range after it
        it->second += next->second;
        free.erase(next);
    }
    if (it != free.begin()) {
        const auto before = std::prev(it);
        if (before->first + before->second == it->first) {  // and the one before
            before->second += it->second;
            free.erase(it);
        }
    }
}

void LodRenderer::upload(Result&& result) {
    const auto it = tiles_.find(result.key);
    if (it == tiles_.end() || it->second.state != State::Requested) return;  // let go of while it was being made
    Tile& tile = it->second;
    const auto count = static_cast<std::uint32_t>(result.vertices.size());
    if (!allocate(count, tile.buffer, tile.first)) {
        tiles_.erase(it);  // nothing to draw (or too big for a buffer, which a tile cannot be)
        return;
    }
    tile.count = count;
    gl::BindBuffer(gl::ARRAY_BUFFER, buffers_[static_cast<std::size_t>(tile.buffer)].id);
    gl::BufferSubData(gl::ARRAY_BUFFER, static_cast<gl::GLintptr>(static_cast<std::size_t>(tile.first) * sizeof(Vertex)), static_cast<gl::GLsizeiptr>(count * sizeof(Vertex)),
                      result.vertices.data());
    tile.minY = result.minY;
    tile.maxY = result.maxY;
    tile.state = State::Uploaded;
    uploadedVertices_ += count;
}

void LodRenderer::evict() {
    // Tiles are kept while they may be wanted again, within a budget of vertices (and of tiles, for the book-keeping); the ones not wanted for the longest go first.
    constexpr std::size_t kKeepTiles = 6000;
    constexpr std::size_t kKeepVertices = 12u * 1000u * 1000u;  // 144 MB
    if (tiles_.size() <= kKeepTiles && uploadedVertices_ <= kKeepVertices) return;
    std::vector<std::pair<std::uint64_t, TileKey>> old;
    for (const auto& [key, tile] : tiles_) {
        if (tile.state == State::Uploaded && tile.lastUsed + 120 < frame_) old.emplace_back(tile.lastUsed, key);
    }
    std::sort(old.begin(), old.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [used, key] : old) {
        if (tiles_.size() <= kKeepTiles && uploadedVertices_ <= kKeepVertices) break;
        const Tile& tile = tiles_.at(key);
        release(tile.buffer, tile.first, tile.count);
        uploadedVertices_ -= tile.count;
        tiles_.erase(key);
    }
}

void LodRenderer::update(double camX, double camZ, int nearChunks, double uploadBudgetMs) {
    ++frame_;
    stats_.tilesBuilding = shared_->building.load();
    if (!settings_.enabled) return;
    const double nearBlocks = static_cast<double>(nearChunks) * 16.0;
    const double maxBlocks = static_cast<double>(settings_.radiusChunks) * 16.0;
    select(camX, camZ, nearBlocks, maxBlocks);

    // Ask for what is missing: first a coarse stand-in a few levels up (cheap, and it fills the view quickly), then the tile itself.
    constexpr int kMaxBuilding = 1500;
    for (const Wanted& w : wanted_) {
        auto it = tiles_.find(w.key);
        if (it != tiles_.end()) {
            it->second.lastUsed = frame_;
            continue;
        }
        if (shared_->building.load() >= kMaxBuilding) break;
        const int standInLevel = std::min(w.key.level + 3, topLevel_);
        if (standInLevel > w.key.level) {
            const int shift = standInLevel - w.key.level;
            const TileKey standIn{standInLevel, w.key.tx >> shift, w.key.tz >> shift};
            auto s = tiles_.find(standIn);
            if (s == tiles_.end()) request(standIn, 1.0e7 + w.distance / 8.0);
            else s->second.lastUsed = frame_;
        }
        request(w.key, 1.0e8 + w.distance);
    }

    // Finished tiles onto the GPU, for no longer than the budget.
    const auto started = std::chrono::steady_clock::now();
    for (;;) {
        Result r;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            if (shared_->done.empty()) break;
            r = std::move(shared_->done.back());
            shared_->done.pop_back();
        }
        if (r.epoch == shared_->epoch.load()) upload(std::move(r));
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > uploadBudgetMs) break;
    }
    if ((frame_ & 31) == 0) evict();
    stats_.tilesBuilding = shared_->building.load();
}

// ---- drawing ------------------------------------------------------------------------------------------------------------------------------------------------------

void LodRenderer::render(const Matrix4f& view, float fovDegrees, float aspect, double camX, double camY, double camZ, int nearChunks, const std::array<float, 3>& fogColor,
                         const std::array<float, 2>& envFog, gl::GLuint lightmapTexture, int fogNearChunks) {
    stats_.tilesDrawn = 0;
    stats_.trianglesDrawn = 0;
    stats_.levels = 0;
    stats_.tilesLoaded = 0;
    for (const auto& [key, tile] : tiles_) stats_.tilesLoaded += tile.state == State::Uploaded ? 1 : 0;
    stats_.megabytes = static_cast<double>(uploadedVertices_) * sizeof(Vertex) / 1048576.0;
    if (!settings_.enabled || wanted_.empty()) return;

    const double nearBlocks = static_cast<double>(nearChunks) * 16.0;
    const double maxBlocks = static_cast<double>(settings_.radiusChunks) * 16.0;
    // Its own depth range: the nearest LOD is about where the chunks end, so the near plane can be far from the eyes, and the ratio stays small.
    const float nearPlane = std::max(2.0f, static_cast<float>(nearBlocks * 0.4));
    const float farPlane = static_cast<float>(maxBlocks * 1.5 + 4096.0);
    const Matrix4f viewProj = perspective(fovDegrees, aspect, nearPlane, farPlane) * view;
    const Frustum frustum = extractFrustum(viewProj);

    struct Draw {
        const Tile* tile;
        TileKey key;
        float bias;
    };
    std::vector<Draw> draws;
    std::vector<TileKey> asked;  // the tiles already chosen (a stand-in may serve several leaves)
    for (const Wanted& w : wanted_) {
        TileKey key = w.key;
        float bias = 0.0f;
        const Tile* tile = nullptr;
        for (;;) {
            const auto it = tiles_.find(key);
            if (it != tiles_.end() && it->second.state == State::Uploaded) {
                tile = &it->second;
                break;
            }
            if (key.level >= topLevel_) break;
            // A coarser tile stands in, slightly biased to avoid z-fighting while maintaining true world elevation:
            ++key.level;
            key.tx >>= 1;
            key.tz >>= 1;
            bias = 0.05f;
        }
        if (tile == nullptr) continue;
        if (std::find(asked.begin(), asked.end(), key) != asked.end()) continue;
        asked.push_back(key);
        draws.push_back({tile, key, bias});
    }
    std::sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.tile->buffer < b.tile->buffer; });

    program_.use();
    models_.bindTextures(0);  // the block pictures on unit 0, the light map on unit 2 (as the chunks' shader has them)
    gl::Uniform1i(uTex_, 0);
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmapTexture);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(uLightmap_, 2);
    gl::UniformMatrix4fv(uViewProj_, 1, 0, viewProj.m.data());
    const auto [fogStart, fogEnd] = fogRange(settings_.radiusChunks, fogNearChunks >= 0 ? fogNearChunks : nearChunks);  // (a zoom's chunks reach further: the haze starts where they end)
    gl::Uniform3f(uFogColor_, fogColor[0], fogColor[1], fogColor[2]);
    gl::Uniform2f(uFogRange_, fogStart, fogEnd);
    gl::Uniform2f(uEnvFog_, envFog[0], envFog[1]);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);
    gl::DepthMask(1);
    gl::Disable(gl::CULL_FACE);  // (the edge walls and the far sides of valleys are seen from either side)
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(vao_);
    int bound = -1;
    int levelsSeen = 0;
    for (const Draw& d : draws) {
        const float size = static_cast<float>(tileSize(d.key.level));
        const double ox = static_cast<double>(d.key.tx) * static_cast<double>(size), oz = static_cast<double>(d.key.tz) * static_cast<double>(size);
        const float rx = static_cast<float>(ox - camX), rz = static_cast<float>(oz - camZ);
        const float ry = static_cast<float>(-camY) - d.bias;
        if (!frustum.mayContain({rx, d.tile->minY + ry, rz}, {rx + size, d.tile->maxY + ry, rz + size})) continue;
        if (d.tile->buffer != bound) {
            bound = d.tile->buffer;
            gl::BindBuffer(gl::ARRAY_BUFFER, buffers_[static_cast<std::size_t>(bound)].id);
            gl::VertexAttribPointer(0, 4, gl::SHORT, 0, sizeof(Vertex), nullptr);
            gl::VertexAttribPointer(1, 4, gl::UNSIGNED_BYTE, 1, sizeof(Vertex), reinterpret_cast<const void*>(8));
        }
        gl::Uniform3f(uOffset_, rx, ry, rz);
        gl::Uniform1f(uCell_, static_cast<float>(cellOf(d.key.level)));
        const std::uint32_t quads = d.tile->count / 4;
        gl::DrawElementsBaseVertex(gl::TRIANGLES, static_cast<gl::GLsizei>(quads * 6), gl::UNSIGNED_INT, nullptr, static_cast<gl::GLint>(d.tile->first));
        ++stats_.tilesDrawn;
        stats_.trianglesDrawn += static_cast<long>(quads) * 2;
        levelsSeen |= 1 << d.key.level;
    }
    for (int l = 0; l < 31; ++l) stats_.levels += (levelsSeen >> l) & 1;
    gl::BindVertexArray(0);
}

}  // namespace falcon
