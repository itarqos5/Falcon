#include "falcon/render/lod_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace falcon {
namespace {

// The shaders: a tile's vertices carry their place in the tile (x and z in cells, y in blocks) and the colour of the column, with the shade of the face already in it. Fog is the
// world's, so LODs and chunks melt into the same haze. (The Vulkan twins are src/vulkan/shaders/lod.vert and lod.frag.)
constexpr char kVertexShader[] = R"(// vk: lod.vert
#version 330 core
layout(location = 0) in vec4 aPos;     // x and z in cells, y in blocks, inside the tile
layout(location = 1) in vec4 aColor;   // the colour of the face
uniform mat4 uViewProj;
uniform vec3 uOffset;                  // where the tile's corner is, relative to the camera
uniform float uCell;                   // the width of a cell, in blocks
out vec3 vColor;
out float vDist;
void main() {
    vec3 p = vec3(aPos.x * uCell, aPos.y, aPos.z * uCell) + uOffset;
    gl_Position = uViewProj * vec4(p, 1.0);
    vColor = aColor.rgb;
    vDist = length(p);
}
)";

constexpr char kFragmentShader[] = R"(// vk: lod.frag
#version 330 core
in vec3 vColor;
in float vDist;
uniform vec3 uFogColor;
uniform vec2 uFogRange;                // the world's fog: start, end (distance from the camera)
uniform vec2 uEnvFog;                  // the fog of the medium the eyes are in (water); far away when there is none
out vec4 frag;
void main() {
    float fog = max(clamp((vDist - uFogRange.x) / (uFogRange.y - uFogRange.x), 0.0, 1.0), clamp((vDist - uEnvFog.x) / (uEnvFog.y - uEnvFog.x), 0.0, 1.0));
    frag = vec4(mix(vColor, uFogColor, fog), 1.0);
}
)";

constexpr int kCells = LodRenderer::kCells;
constexpr int kSea = worldgen::LodSurface::kSeaLevel;
// The most quads a tile can hold: a top and four walls for every cell.
constexpr std::uint32_t kMaxQuads = kCells * kCells * 5;

// The one index list every tile shares: quad n is vertices 4n..4n+3, drawn as two triangles.
std::vector<std::uint32_t> makeIndices() {
    std::vector<std::uint32_t> out;
    out.reserve(static_cast<std::size_t>(kMaxQuads) * 6);
    for (std::uint32_t q = 0; q < kMaxQuads; ++q) {
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) out.push_back(q * 4 + i);
    }
    return out;
}

}  // namespace

LodRenderer::Detail LodRenderer::detailOf(int detail) {
    switch (detail) {
        // Cells as wide as a block next to the loaded chunks and twice as wide for every doubling of the distance (a cell at most k times as far as it is wide), as the
        // gallery of Distant Horizons shows it; Low starts at 8 blocks and never reads a real chunk.
        case kLodDetailLow: return {8, 1.0f / 40.0f, 0, false};
        case kLodDetailFull: return {1, 1.0f / 200.0f, 2, true};
        default: return {2, 1.0f / 100.0f, 2, true};
    }
}

std::pair<float, float> LodRenderer::fogRange(int radiusChunks, int nearChunks) {
    const float end = static_cast<float>(radiusChunks) * 16.0f;
    float start = std::max(static_cast<float>(nearChunks) * 16.0f, 0.6f * end);
    if (start > end * 0.9f) start = end * 0.8f;
    return {start, end};
}

LodRenderer::LodRenderer(std::shared_ptr<const worldgen::OverworldGenerator> generator, std::vector<worldgen::LodTrees> trees, PriorityPool& pool)
    : surface_(std::make_shared<const worldgen::LodSurface>(std::move(generator), std::move(trees))),
      pool_(pool),
      shared_(std::make_shared<Shared>()),
      program_("lod", kVertexShader, kFragmentShader) {
    uViewProj_ = program_.uniform("uViewProj");
    uOffset_ = program_.uniform("uOffset");
    uCell_ = program_.uniform("uCell");
    uFogColor_ = program_.uniform("uFogColor");
    uFogRange_ = program_.uniform("uFogRange");
    uEnvFog_ = program_.uniform("uEnvFog");

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
// twice); a run of cells with the same top and colour is one quad, so flat country (the sea above all) is a few quads. The edge of the tile also gets a wall that hangs a
// good way under it whatever its neighbour is, which hides the gaps where it meets a tile of another level (their cells do not line up).
std::vector<LodRenderer::Vertex> LodRenderer::buildColumns(const worldgen::LodGrid& grid, int cell, float& minY, float& maxY) {
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
    const int skirt = std::min(2 * cell + 4, 320);
    // The shade of each way a face looks, as the game gives its blocks: up 1, north and south 0.8, east and west 0.6.
    const auto shaded = [](std::uint32_t rgb, float shade) {
        const auto channel = [&](std::uint32_t c) { return static_cast<std::uint8_t>(std::min(255.0f, std::floor(static_cast<float>(c) * shade + 0.5f))); };
        return std::array<std::uint8_t, 3>{channel((rgb >> 16) & 255u), channel((rgb >> 8) & 255u), channel(rgb & 255u)};
    };
    const auto corner = [&](int x, int y, int z, const std::array<std::uint8_t, 3>& c) {
        out.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(z), 0, c[0], c[1], c[2], 255});
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
    };

    // The tops: along each row, runs of the same height and colour.
    for (int j = 0; j < kCells; ++j) {
        for (int i = 0; i < kCells;) {
            const int h = top(i, j);
            const std::uint32_t colour = colourOf(i, j);
            int e = i + 1;
            while (e < kCells && top(e, j) == h && colourOf(e, j) == colour) ++e;
            const auto c = shaded(colour, 1.0f);
            corner(i, h, j, c);
            corner(i, h, j + 1, c);
            corner(e, h, j + 1, c);
            corner(e, h, j, c);
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
    for (int facing = 0; facing < 4; ++facing) {
        const bool alongZ = facing < 2;  // west and east walls run along z (their cells are in a column), north and south ones along x
        const float shade = alongZ ? 0.6f : 0.8f;
        for (int line = 0; line < kCells; ++line) {  // the column (west, east) or row (north, south) of cells the wall runs along
            for (int t = 0; t < kCells;) {
                const int i = alongZ ? line : t, j = alongZ ? t : line;
                const Wall w = wallOf(facing, i, j);
                if (!w.has) {
                    ++t;
                    continue;
                }
                const std::uint32_t colour = colourOf(i, j);
                int e = t + 1;
                while (e < kCells) {
                    const int ni = alongZ ? line : e, nj = alongZ ? e : line;
                    const Wall n = wallOf(facing, ni, nj);
                    if (!n.has || n.low != w.low || n.high != w.high || colourOf(ni, nj) != colour) break;
                    ++e;
                }
                const auto c = shaded(colour, shade);
                if (alongZ) {
                    const int x = facing == 0 ? line : line + 1;
                    corner(x, w.low, t, c);
                    corner(x, w.high, t, c);
                    corner(x, w.high, e, c);
                    corner(x, w.low, e, c);
                } else {
                    const int z = facing == 2 ? line : line + 1;
                    corner(t, w.low, z, c);
                    corner(t, w.high, z, c);
                    corner(e, w.high, z, c);
                    corner(e, w.low, z, c);
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
    const worldgen::LodMode mode = cell <= detail_.chunkMax ? worldgen::LodMode::Chunk : (detail_.residual ? worldgen::LodMode::Residual : worldgen::LodMode::Preliminary);
    const int epoch = shared_->epoch.load();
    pool_.submit(static_cast<float>(priority), [surface = surface_, shared = shared_, key, epoch, cell, mode, originX, originZ] {
        if (shared->epoch.load() == epoch) {
            const worldgen::LodGrid grid = surface->grid(originX, originZ, cell, kCells, mode);
            Result r;
            r.key = key;
            r.epoch = epoch;
            r.vertices = buildColumns(grid, cell, r.minY, r.maxY);
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
                         const std::array<float, 2>& envFog) {
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
            // A coarser tile stands in, a little lower than the finer ones that are there beside it, so they win where both are drawn.
            ++key.level;
            key.tx >>= 1;
            key.tz >>= 1;
            bias = 0.75f * static_cast<float>(cellOf(key.level)) + 1.0f;
        }
        if (tile == nullptr) continue;
        if (std::find(asked.begin(), asked.end(), key) != asked.end()) continue;
        asked.push_back(key);
        draws.push_back({tile, key, bias});
    }
    std::sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.tile->buffer < b.tile->buffer; });

    program_.use();
    gl::UniformMatrix4fv(uViewProj_, 1, 0, viewProj.m.data());
    const auto [fogStart, fogEnd] = fogRange(settings_.radiusChunks, nearChunks);
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
