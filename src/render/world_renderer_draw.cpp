#include "falcon/render/world_renderer.hpp"
#include "falcon/jvm/vm.hpp"
#include "falcon/util/log.hpp"
#include "world_renderer_internal.hpp"

#include "falcon/world/block_outlines.hpp"
#include "falcon/world/block_position.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// WorldRenderer: drawing a frame (visibility, the sections, the sky, the selection and the cracks, held things).

namespace falcon {

void WorldRenderer::measureGpu(bool begin) {
    if (!gpuTiming_ && !measuring_ && queriesInFlight_ == 0) return;  // (queries already started are still collected)
    if (begin) {
        if (queriesInFlight_ < 3 && gpuTiming_) {
            gl::BeginQuery(gl::TIME_ELAPSED, queries_[queryWrite_]);
            measuring_ = true;
        }
        return;
    }
    if (measuring_) {
        gl::EndQuery(gl::TIME_ELAPSED);
        measuring_ = false;
        queryWrite_ = (queryWrite_ + 1) % 3;
        ++queriesInFlight_;
    }
    // Collect the oldest measurement if the GPU is done with it.
    while (queriesInFlight_ > 0) {
        const int oldest = ((queryWrite_ - queriesInFlight_) % 3 + 3) % 3;
        gl::GLint available = 0;
        gl::GetQueryObjectiv(queries_[oldest], gl::QUERY_RESULT_AVAILABLE, &available);
        if (available == 0) break;
        gl::GLuint64 nanoseconds = 0;
        gl::GetQueryObjectui64v(queries_[oldest], gl::QUERY_RESULT, &nanoseconds);
        const double ms = static_cast<double>(nanoseconds) / 1.0e6;
        gpuMillis_ = gpuMillis_ == 0.0 ? ms : gpuMillis_ * 0.9 + ms * 0.1;
        --queriesInFlight_;
    }
}

float WorldRenderer::farPlaneFor(int radiusChunks, double cameraY) {
    const float across = static_cast<float>(radiusChunks + 4) * 16.0f * 1.5f;  // (what it was before the height counted)
    const float acrossToTheEdge = static_cast<float>(radiusChunks + 3) * 16.0f;  // (the farthest section column that is drawn, to its far corner)
    const double vertical = std::max(std::fabs(cameraY - static_cast<double>(kMinY)), std::fabs(cameraY - static_cast<double>(kMaxY)));
    const float diagonal = std::hypot(acrossToTheEdge, static_cast<float>(vertical)) + 16.0f;
    return std::max({256.0f, across, diagonal});
}

void WorldRenderer::render(const Camera& camera, int framebufferWidth, int framebufferHeight, int renderDistance) {
    measureGpu(true);
    models_.animate(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() * 20.0);  // water, lava and fire move
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
    const float farPlane = farPlaneFor(drawRadius(renderDistance), camera.y);
    const Matrix4f view = cameraView(camera);
    const Matrix4f viewProj = perspective(camera.fov, aspect, 0.05f, farPlane) * view;
    lastViewProj_ = viewProj;

    // The sky: a gradient from the horizon haze to the blue overhead, drawn behind everything.
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    sky_.use();
    const float tanHalf = std::tan(camera.fov * 0.5f * 0.017453292f);
    gl::Uniform3f(skyRight_, view.m[0], view.m[4], view.m[8]);
    gl::Uniform3f(skyUp_, view.m[1], view.m[5], view.m[9]);
    gl::Uniform3f(skyForward_, -view.m[2], -view.m[6], -view.m[10]);
    gl::Uniform2f(skyTan_, tanHalf * aspect, tanHalf);
    if (underwater_) {  // no sky from under water: the whole view is the water's fog colour, with no sun, moon or stars
        gl::Uniform3f(skyHorizon_, waterFogColor_[0], waterFogColor_[1], waterFogColor_[2]);
        gl::Uniform3f(skyZenith_, waterFogColor_[0], waterFogColor_[1], waterFogColor_[2]);
        gl::Uniform3f(skySunDir_, 0.0f, 0.0f, 0.0f);
        gl::Uniform2f(skyAngle_, 0.0f, 1.0f);
        gl::Uniform1f(skyStars_, 0.0f);
        gl::Uniform4f(skyGlow_, 0.0f, 0.0f, 0.0f, 0.0f);
        gl::Uniform1f(skyMoon_, 0.0f);
    } else {
        gl::Uniform3f(skyHorizon_, skyState_.horizon[0], skyState_.horizon[1], skyState_.horizon[2]);
        gl::Uniform3f(skyZenith_, skyState_.zenith[0], skyState_.zenith[1], skyState_.zenith[2]);
        gl::Uniform3f(skySunDir_, skyState_.sunDir[0], skyState_.sunDir[1], skyState_.sunDir[2]);
        gl::Uniform2f(skyAngle_, skyState_.sinAngle, skyState_.cosAngle);
        gl::Uniform1f(skyStars_, skyState_.stars);
        gl::Uniform4f(skyGlow_, skyState_.glow[0], skyState_.glow[1], skyState_.glow[2], skyState_.glow[3]);
        gl::Uniform1f(skyMoon_, skyState_.moonPhase);
    }
    celestial_.bind(0);  // the sun and the moon
    gl::Uniform1i(skyCelestial_, 0);
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);

    // The levels of detail beyond the loaded chunks, in a pass of their own; then the depth is cleared, so the chunks are drawn over them whatever the LODs predicted.
    const bool lodsOn = lod_ != nullptr && lodSettings_.enabled;
    if (lodsOn) {
        const std::array<float, 3> fogColor = underwater_ ? waterFogColor_ : skyState_.horizon;
        const std::array<float, 2> envFog = underwater_ ? std::array<float, 2>{waterFogStart_, waterFogEnd_} : std::array<float, 2>{envFogStart_, envFogEnd_};
        models_.uploadPending();  // the pictures the LODs use may be new
        if (lightmapBrightness_ != brightness_ || lightmapSkyChanged()) updateLightmap();  // (the chunks below want it the same way)
        // (The chunks that are drawn: the circle of the render distance, and a ring round it while a zoom is up, see drawable(); the LODs are left out of as much of it as is really there.)
        lod_->render(view, camera.fov, aspect, camera.x, camera.y, camera.z, renderDistance, fogColor, envFog, lightmapTex_, drawRadius(renderDistance), playerChunkX_.load(), playerChunkZ_.load(),
                     chunkCircleReady(zoomReach_ > 0 ? renderDistance + 2 : renderDistance));
        gl::DepthMask(1);
        gl::Clear(gl::DEPTH_BUFFER_BIT);
    }

    // The blocks.
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::FrontFace(gl::CCW);
    world_.use();
    gl::UniformMatrix4fv(uViewProj_, 1, 0, viewProj.m.data());
    const float fogEnd = static_cast<float>(drawRadius(renderDistance)) * 16.0f;  // (a zoom's chunks reach further, and the haze with them)
    if (underwater_) {
        gl::Uniform3f(uFogColor_, waterFogColor_[0], waterFogColor_[1], waterFogColor_[2]);
        gl::Uniform2f(uEnvFog_, waterFogStart_, waterFogEnd_);
    } else {
        gl::Uniform3f(uFogColor_, skyState_.horizon[0], skyState_.horizon[1], skyState_.horizon[2]);
        gl::Uniform2f(uEnvFog_, envFogStart_, envFogEnd_);  // (the dimension's own fog, if it has one)
    }
    if (lodsOn) {  // the haze belongs to the far end of the LODs, not to the edge of the chunks
        const auto [lodFogStart, lodFogEnd] = LodRenderer::fogRange(lodSettings_.radiusChunks, drawRadius(renderDistance));
        gl::Uniform2f(uFogRange_, lodFogStart, lodFogEnd);
    } else {
        gl::Uniform2f(uFogRange_, fogEnd * 0.6f, fogEnd);
    }
    models_.uploadPending();  // pictures the workers have loaded since the last frame
    models_.bindTextures(0);
    gl::Uniform1i(uTex_, 0);
    // The carried light (texture unit 1), relative to the camera like every position in the shader.
    gl::ActiveTexture(gl::TEXTURE1);
    gl::BindTexture(gl::TEXTURE_3D, heldLightTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(uHeldLight_, 1);
    gl::Uniform1f(uHeldOn_, heldLightLevel_ > 0 ? 1.0f : 0.0f);
    gl::Uniform3f(uHeldOrigin_, static_cast<float>(heldLightX_ - kHeldLightSize / 2 - camera.x), static_cast<float>(heldLightY_ - kHeldLightSize / 2 - camera.y),
                  static_cast<float>(heldLightZ_ - kHeldLightSize / 2 - camera.z));

    // The light map picture (texture unit 2); made again only when the Brightness option changed.
    if (lightmapBrightness_ != brightness_ || lightmapSkyChanged()) updateLightmap();
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmapTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(uLightmap_, 2);
    gl::Uniform1f(uInstanced_, 0.0f);

    const Frustum frustum = extractFrustum(viewProj);
    lastViewProj_ = viewProj;
    lastFrustum_ = frustum;
    lastCameraPos_ = {camera.x, camera.y, camera.z};
    const float reach = (static_cast<float>(drawRadius(renderDistance)) + 1.0f) * 16.0f + 16.0f;
    visible_.clear();
    // Inside solid rock there is no air to walk out of, and the game then draws everything in view (as the game itself does: it turns its
    // own cave culling off when the camera is inside a solid block).
    bool occlusionNow = useOcclusion_;
    if (occlusionNow) {
        const Block eyeBlock = level_.block(static_cast<int>(std::floor(camera.x)), static_cast<int>(std::floor(camera.y)), static_cast<int>(std::floor(camera.z)));
        if (blockRegistry().state(eyeBlock).has(kStateOpaqueCube)) occlusionNow = false;
    }
    if (occlusionNow) collectVisibleByTraversal(camera, frustum, reach);
    else collectVisibleByScan(camera, frustum, reach);
    visibleChunks_.clear();
    for (const VisibleSection& v : visible_) {
        visibleChunks_.insert(ChunkPos{v.chunkX, v.chunkZ});
    }
    // Water and the like are drawn farthest first (they blend over what is behind them).
    translucentOrder_.clear();
    for (const VisibleSection& v : visible_) {
        if (v.section->translucentVertices > 0) translucentOrder_.push_back(&v);
    }
    if (occlusionNow) {  // (the scan leaves visible_ sorted by distance already, the traversal only roughly)
        std::sort(translucentOrder_.begin(), translucentOrder_.end(), [](const VisibleSection* a, const VisibleSection* b) { return a->distance2 > b->distance2; });
    } else {
        std::reverse(translucentOrder_.begin(), translucentOrder_.end());
    }

    std::size_t quads = 0;
    int drawCalls = 0;
    int drawCommands = 0;
    // A section's quads are kept in order of the way they face, and the directions that turn away from the camera are not drawn: the
    // camera sees the east side of a quad only from the east of its plane, and so on (with a block of margin for plants that are
    // shifted and quads that sit inside their block). Back faces were thrown away after the vertex work before; now they cost nothing.
    // `emit(first, length)` gets each run of vertices (in the section's part) that is to be drawn.
    static const bool everything = std::getenv("FALCON_NO_FACING_CULL") != nullptr;  // (for checking that the picture is the same without it)
    const auto forEachRun = [&](const VisibleSection& v, const std::array<int, kFacingGroups>& groups, const auto& emit) {
        const bool seen[kFacingGroups] = {everything || v.ox < 1.0f, everything || v.ox > -17.0f, everything || v.oy < 1.0f, everything || v.oy > -17.0f,
                                          everything || v.oz < 1.0f, everything || v.oz > -17.0f, true};
        std::uint32_t offset = 0, runStart = 0, runLength = 0;
        const auto flush = [&] {
            if (runLength == 0) return;
            emit(runStart, runLength);
            runLength = 0;
        };
        for (int g = 0; g < kFacingGroups; ++g) {
            const auto count = static_cast<std::uint32_t>(groups[static_cast<std::size_t>(g)]);
            if (count == 0) continue;
            if (seen[g]) {
                if (runLength == 0) runStart = offset;
                runLength += count;
            } else {
                flush();
            }
            offset += count;
        }
        flush();
    };

    // Drawing the opaque part, the cutout part (leaves, plants, glass: texels with less than half alpha are discarded - the game's own
    // threshold for its cutout terrain, RenderPipelines ALPHA_CUTOUT_THRESHOLD_CUTOUT_TERRAIN, which the mip levels' alpha is made to agree
    // with) and then water and other see-through blocks, back to front, blended and without writing depth. Their faces are seen from both
    // sides (the underside of the sea surface): the mesher makes that surface a second, reversed quad, so back faces stay culled.
    gl::Uniform1f(uGamma_, brightness_);
    if (useMultiDraw_) {
        // Every pass's commands are made first and sent to the GPU in one go; then each pass is one call.
        commands_.clear();
        commandOffsets_.clear();
        const auto add = [&](const VisibleSection& v, std::uint32_t indexCount, std::int32_t baseVertex) {
            commands_.push_back({indexCount, 1u, 0u, baseVertex, static_cast<std::uint32_t>(commands_.size())});
            commandOffsets_.push_back(v.ox);
            commandOffsets_.push_back(v.oy);
            commandOffsets_.push_back(v.oz);
        };
        for (const VisibleSection& v : visible_) {
            if (v.section->opaqueVertices == 0) continue;
            forEachRun(v, v.section->opaqueGroups, [&](std::uint32_t first, std::uint32_t length) {
                add(v, length / 4 * 6, static_cast<std::int32_t>(v.section->arenaBase + first));
                quads += length / 4;
            });
        }
        const std::size_t cutoutBegin = commands_.size();
        for (const VisibleSection& v : visible_) {
            if (v.section->cutoutVertices == 0) continue;
            forEachRun(v, v.section->cutoutGroups, [&](std::uint32_t first, std::uint32_t length) {
                add(v, length / 4 * 6, static_cast<std::int32_t>(v.section->arenaBase + static_cast<std::uint32_t>(v.section->opaqueVertices) + first));
                quads += length / 4;
            });
        }
        const std::size_t translucentBegin = commands_.size();
        for (const VisibleSection* v : translucentOrder_) {
            add(*v, static_cast<std::uint32_t>(v->section->translucentVertices / 4 * 6),
                static_cast<std::int32_t>(v->section->arenaBase) + v->section->opaqueVertices + v->section->cutoutVertices);
            quads += static_cast<std::size_t>(v->section->translucentVertices / 4);
        }
        drawCommands = static_cast<int>(commands_.size());
        // Into the buffers: made anew each frame (the old contents are let go of, so the GPU is never waited for).
        const auto upload = [](gl::GLenum target, gl::GLuint buffer, std::size_t& capacity, const void* data, std::size_t bytes) {
            gl::BindBuffer(target, buffer);
            if (bytes > capacity) capacity = std::max<std::size_t>(bytes + bytes / 2, 64u * 1024u);
            gl::BufferData(target, static_cast<gl::GLsizeiptr>(capacity), nullptr, gl::STREAM_DRAW);
            if (bytes > 0) gl::BufferSubData(target, 0, static_cast<gl::GLsizeiptr>(bytes), data);
        };
        upload(gl::DRAW_INDIRECT_BUFFER, commandBuffer_, commandBufferCapacity_, commands_.data(), commands_.size() * sizeof(IndirectCommand));
        upload(gl::ARRAY_BUFFER, offsetBuffer_, offsetBufferCapacity_, commandOffsets_.data(), commandOffsets_.size() * sizeof(float));
        const auto drawRange = [&](std::size_t begin, std::size_t end) {
            if (end <= begin) return;
            gl::MultiDrawElementsIndirect(gl::TRIANGLES, gl::UNSIGNED_INT, reinterpret_cast<const void*>(begin * sizeof(IndirectCommand)), static_cast<gl::GLsizei>(end - begin),
                                          static_cast<gl::GLsizei>(sizeof(IndirectCommand)));
            ++drawCalls;
        };
        gl::BindVertexArray(arenaVaoInstanced_);
        gl::Uniform1f(uInstanced_, 1.0f);
        gl::Uniform1f(uAlphaCut_, 0.0f);
        drawRange(0, cutoutBegin);
        gl::Uniform1f(uAlphaCut_, 0.5f);
        drawRange(cutoutBegin, translucentBegin);
        gl::Enable(gl::BLEND);
        gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
        gl::DepthMask(gl::FALSE_);
        gl::Uniform1f(uAlphaCut_, 0.01f);
        drawRange(translucentBegin, commands_.size());
        gl::Uniform1f(uInstanced_, 0.0f);
    } else {
        // One section at a time: its place as a uniform, then a draw for each run.
        gl::BindVertexArray(arenaVao_);  // (every section's mesh lives in the one arena buffer: the vertex array is bound once)
        gl::Uniform1f(uAlphaCut_, 0.0f);
        for (const VisibleSection& v : visible_) {  // opaque, nearest first: hidden pixels are rejected early
            if (v.section->opaqueVertices == 0) continue;
            gl::Uniform3f(uOffset_, v.ox, v.oy, v.oz);
            forEachRun(v, v.section->opaqueGroups, [&](std::uint32_t first, std::uint32_t length) {
                gl::DrawElementsBaseVertex(gl::TRIANGLES, static_cast<gl::GLsizei>(length / 4 * 6), gl::UNSIGNED_INT, nullptr, static_cast<gl::GLint>(v.section->arenaBase + first));
                quads += length / 4;
                ++drawCalls;
                ++drawCommands;
            });
        }
        gl::Uniform1f(uAlphaCut_, 0.5f);
        for (const VisibleSection& v : visible_) {
            if (v.section->cutoutVertices == 0) continue;
            gl::Uniform3f(uOffset_, v.ox, v.oy, v.oz);
            forEachRun(v, v.section->cutoutGroups, [&](std::uint32_t first, std::uint32_t length) {
                gl::DrawElementsBaseVertex(gl::TRIANGLES, static_cast<gl::GLsizei>(length / 4 * 6), gl::UNSIGNED_INT, nullptr,
                                           static_cast<gl::GLint>(v.section->arenaBase + static_cast<std::uint32_t>(v.section->opaqueVertices) + first));
                quads += length / 4;
                ++drawCalls;
                ++drawCommands;
            });
        }
        gl::Enable(gl::BLEND);
        gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
        gl::DepthMask(gl::FALSE_);
        gl::Uniform1f(uAlphaCut_, 0.01f);
        for (const VisibleSection* v : translucentOrder_) {
            gl::Uniform3f(uOffset_, v->ox, v->oy, v->oz);
            gl::DrawElementsBaseVertex(gl::TRIANGLES, v->section->translucentVertices / 4 * 6, gl::UNSIGNED_INT, nullptr,
                                       static_cast<gl::GLint>(v->section->arenaBase) + v->section->opaqueVertices + v->section->cutoutVertices);
            quads += static_cast<std::size_t>(v->section->translucentVertices / 4);
            ++drawCalls;
            ++drawCommands;
        }
    }
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(0);

    static const bool checkErrors = std::getenv("FALCON_GL_CHECK") != nullptr;  // (a debugging aid: reports the first OpenGL error the world drawing causes)
    if (checkErrors) {
        static bool reported = false;
        if (const gl::GLenum error = gl::GetError(); error != 0 && !reported) {
            reported = true;
            falcon::log::line("OpenGL error 0x%04X after drawing the world (%s)\n", error, useMultiDraw_ ? "multi-draw" : "one draw per section");
        }
    }
    stats_.sectionsDrawn = static_cast<int>(visible_.size());
    stats_.drawCalls = drawCalls;
    stats_.drawCommands = drawCommands;
    stats_.occlusionCulling = occlusionNow;
    stats_.multiDraw = useMultiDraw_;
    stats_.quadsDrawn = quads;
    // The GPU timer covers this pass; endFrame() closes it after the outline too.
}

void WorldRenderer::setGrid(const SectionKey& key, const GpuSection* section, std::uint64_t visibility) {
    if (key.y < 0 || key.y >= kSectionCount) return;
    const std::size_t cell = gridIndex(key.x, key.y, key.z);
    gridSection_[cell] = section;
    gridVisibility_[cell] = visibility;
}

// While a zoom is up, a chunk beyond the render distance is drawn only if the zoom's cone holds it: the ring of chunks round the cone is there so that the light and the faces
// at the cone's edge have their neighbours, and is not complete itself (it would show as dark slivers along the cone's sides).
bool WorldRenderer::drawable(int chunkX, int chunkZ) const {
    const int dx = chunkX - playerChunkX_.load(), dz = chunkZ - playerChunkZ_.load();
    if (zoomReach_ <= 0) {
        // With LODs the chunks end exactly where the LODs begin: a ring of chunks beyond the render distance (there for the light, the mesh of the chunks inside it) was drawn on top of
        // its LOD, and its translucent sea showed the LOD's sea under it, a lighter band round the chunks.
        if (lod_ == nullptr || !lodSettings_.enabled) return true;
        const int r = std::max(lastRadius_, 0);
        return dx * dx + dz * dz <= r * r;
    }
    const int r = std::max(lastRadius_, 0) + 2;
    return dx * dx + dz * dz <= r * r || inZoom(ChunkPos{chunkX, chunkZ}, false);
}

// The sections that may be seen, found by scanning every one with a mesh: those within the render distance and inside the view frustum,
// nearest first.
void WorldRenderer::collectVisibleByScan(const Camera& camera, const Frustum& frustum, float reach) {
    for (const auto& [key, section] : sections_) {
        const float ox = static_cast<float>(key.x * 16 - camera.x);
        const float oy = static_cast<float>(kMinY + key.y * 16 - camera.y);
        const float oz = static_cast<float>(key.z * 16 - camera.z);
        const float cx = ox + 8.0f;
        const float cz = oz + 8.0f;
        const float horizontal2 = cx * cx + cz * cz;
        if (horizontal2 > reach * reach) continue;  // beyond the render distance
        if (!drawable(key.x, key.z)) continue;
        if (!frustum.mayContain({ox, oy, oz}, {ox + 16.0f, oy + 16.0f, oz + 16.0f})) continue;
        const float cy = oy + 8.0f;
        visible_.push_back({&section, ox, oy, oz, horizontal2 + cy * cy, key.x, key.z});
    }
    std::sort(visible_.begin(), visible_.end(), [](const VisibleSection& a, const VisibleSection& b) { return a.distance2 < b.distance2; });
}

// The sections that may be seen, found by walking from the camera's own section through the faces that connect. A section is reached
// through one of its faces and left through another that the mesher found connected to it (the camera's own section lets everything out);
// a walk never turns back on a direction it has already moved in, which is what stops the view from going round corners it cannot. Sections
// are reached in order of their distance (in steps), so the list comes out roughly nearest first.
void WorldRenderer::collectVisibleByTraversal(const Camera& camera, const Frustum& frustum, float reach) {
    static constexpr int kDx[6] = {-1, 1, 0, 0, 0, 0};  // west, east, down, up, north, south: the opposite of face f is f ^ 1
    static constexpr int kDy[6] = {0, 0, -1, 1, 0, 0};
    static constexpr int kDz[6] = {0, 0, 0, 0, -1, 1};
    const int cameraChunkX = chunkOf(static_cast<int>(std::floor(camera.x)));
    const int cameraChunkZ = chunkOf(static_cast<int>(std::floor(camera.z)));
    const int cameraSection = std::clamp(sectionOf(static_cast<int>(std::floor(camera.y))), 0, kSectionCount - 1);
    const float reach2 = reach * reach;

    if (++traversalStamp_ == 0) {  // (the counter wrapped: forget every old mark)
        std::fill(gridStamp_.begin(), gridStamp_.end(), 0u);
        traversalStamp_ = 1;
    }
    if (gridIncoming_.empty()) {
        gridIncoming_.assign(gridStamp_.size(), 0);
        gridTravelled_.assign(gridStamp_.size(), 0);
    }
    traversal_.clear();
    const double cameraY = camera.y;
    if (cameraY >= static_cast<double>(kMaxY) || cameraY < static_cast<double>(kMinY)) {
        // A camera above the world (or below it) is in no section: the walk cannot start in the one it is nearest to, which may be nowhere near the view (from 600 blocks up and looking
        // ahead, the section straight below is far outside it, and the walk died there: every chunk went, the LODs stayed). The game does the same as this: it starts from the top
        // layer (the bottom one) wherever it is in view and in range, each entered from the open side.
        const bool above = cameraY >= static_cast<double>(kMaxY);
        const int layer = above ? kSectionCount - 1 : 0;
        const int rangeChunks = static_cast<int>(std::ceil(reach / 16.0f)) + 1;
        std::vector<std::pair<float, TraversalNode>> seeds;
        for (int dz = -rangeChunks; dz <= rangeChunks; ++dz) {
            for (int dx = -rangeChunks; dx <= rangeChunks; ++dx) {
                const int nx = cameraChunkX + dx, nz = cameraChunkZ + dz;
                const float cx = static_cast<float>(nx * 16 + 8) - static_cast<float>(camera.x);
                const float cz = static_cast<float>(nz * 16 + 8) - static_cast<float>(camera.z);
                if (cx * cx + cz * cz > reach2) continue;
                const float ox = static_cast<float>(nx * 16 - camera.x);
                const float oy = static_cast<float>(kMinY + layer * 16 - camera.y);
                const float oz = static_cast<float>(nz * 16 - camera.z);
                if (!frustum.mayContain({ox, oy, oz}, {ox + 16.0f, oy + 16.0f, oz + 16.0f})) continue;
                const auto cell = static_cast<std::uint32_t>(gridIndex(nx, layer, nz));
                seeds.push_back({cx * cx + cz * cz, {cell, nx, nz, static_cast<std::int8_t>(layer), false}});
            }
        }
        std::sort(seeds.begin(), seeds.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [distance, node] : seeds) {
            gridStamp_[node.cell] = traversalStamp_;
            gridIncoming_[node.cell] = static_cast<std::uint8_t>(above ? 1u << 3 : 1u << 2);  // (it was entered through its top face, or its bottom one)
            gridTravelled_[node.cell] = static_cast<std::uint8_t>(above ? 1u << 2 : 1u << 3);  // (moving down, or up)
            traversal_.push_back(node);
        }
    } else {
        const std::uint32_t startCell = static_cast<std::uint32_t>(gridIndex(cameraChunkX, cameraSection, cameraChunkZ));
        gridStamp_[startCell] = traversalStamp_;
        gridIncoming_[startCell] = 0;
        gridTravelled_[startCell] = 0;
        traversal_.push_back({startCell, cameraChunkX, cameraChunkZ, static_cast<std::int8_t>(cameraSection), true});
    }

    for (std::size_t head = 0; head < traversal_.size(); ++head) {
        const TraversalNode node = traversal_[head];
        // A section that has a mesh, and is in view, is drawn.
        if (const GpuSection* section = gridSection_[node.cell]; section != nullptr && drawable(node.x, node.z)) {
            const float ox = static_cast<float>(node.x * 16 - camera.x);
            const float oy = static_cast<float>(kMinY + node.y * 16 - camera.y);
            const float oz = static_cast<float>(node.z * 16 - camera.z);
            if (!node.entry || frustum.mayContain({ox, oy, oz}, {ox + 16.0f, oy + 16.0f, oz + 16.0f})) {  // (the others passed when they were reached)
                const float cx = ox + 8.0f, cy = oy + 8.0f, cz = oz + 8.0f;
                visible_.push_back({section, ox, oy, oz, cx * cx + cy * cy + cz * cz, node.x, node.z});
            }
        }
        // Which faces it can be left through.
        const unsigned travelled = gridTravelled_[node.cell];
        unsigned outgoing = 0x3F;
        if (!node.entry) {
            outgoing = 0;
            const std::uint64_t connections = gridVisibility_[node.cell];
            const unsigned incoming = gridIncoming_[node.cell];
            for (int a = 0; a < 6; ++a) {
                if ((incoming >> a & 1u) != 0) outgoing |= static_cast<unsigned>(connections >> (a * 6)) & 0x3Fu;
            }
        }
        for (int f = 0; f < 6; ++f) {
            if ((travelled >> (f ^ 1) & 1u) != 0) outgoing &= ~(1u << f);  // (never back the way it came)
        }
        for (int f = 0; f < 6; ++f) {
            if ((outgoing >> f & 1u) == 0) continue;
            const int ny = node.y + kDy[f];
            if (ny < 0 || ny >= kSectionCount) continue;
            const int nx = node.x + kDx[f];
            const int nz = node.z + kDz[f];
            const float cx = static_cast<float>(nx * 16 + 8) - static_cast<float>(camera.x);
            const float cz = static_cast<float>(nz * 16 + 8) - static_cast<float>(camera.z);
            if (cx * cx + cz * cz > reach2) continue;  // beyond the render distance
            const auto cell = static_cast<std::uint32_t>(gridIndex(nx, ny, nz));
            const unsigned enteredThrough = 1u << (f ^ 1);
            const unsigned moved = travelled | (1u << f);
            if (gridStamp_[cell] == traversalStamp_) {  // reached before (and not yet left): it may also be entered this way
                gridIncoming_[cell] = static_cast<std::uint8_t>(gridIncoming_[cell] | enteredThrough);
                gridTravelled_[cell] = static_cast<std::uint8_t>(gridTravelled_[cell] & moved);
            } else {
                gridStamp_[cell] = traversalStamp_;
                // Only the sections in view are walked (and so only they cost anything). That loses nothing that can be seen: a ray through
                // the view crosses a chain of sections that all touch the view, and the walk can follow it, one face at a time.
                const float ox = static_cast<float>(nx * 16 - camera.x);
                const float oy = static_cast<float>(kMinY + ny * 16 - camera.y);
                const float oz = static_cast<float>(nz * 16 - camera.z);
                if (!frustum.mayContain({ox, oy, oz}, {ox + 16.0f, oy + 16.0f, oz + 16.0f})) {
                    gridIncoming_[cell] = 0;
                    continue;
                }
                gridIncoming_[cell] = static_cast<std::uint8_t>(enteredThrough);
                gridTravelled_[cell] = static_cast<std::uint8_t>(moved);
                traversal_.push_back({cell, nx, nz, static_cast<std::int8_t>(ny), false});
            }
        }
    }
}

// The light map as a 16 x 16 picture: for every sky light level (across) and block light level (down), what the shader's lightColor
// works out (the game's own light map, with the overworld's daytime values). The filter blends the levels between, as in the game.
void WorldRenderer::updateLightmap() {
    if (lightmapTex_ == 0) return;
    float texels[16 * 16 * 4];
    for (int b = 0; b < 16; ++b) {
        for (int s = 0; s < 16; ++s) {
            const float sky = static_cast<float>(s) / 15.0f;
            const float blk = static_cast<float>(b) / 15.0f;
            const float blockBrightness = (blk / (4.0f - 3.0f * blk)) * 1.4f;
            const float skyBrightness = sky / (4.0f - 3.0f * sky) * skyState_.skyLightFactor;  // (the night does not take levels off: it dims the sky light)
            const float parabolic = (2.0f * blk - 1.0f) * (2.0f * blk - 1.0f);
            const float tint = 0.9f * parabolic;  // mix(vec3(1, 0.84705883, 0.54901963), vec3(1), tint)
            const float mixed[3] = {1.0f, 0.84705883f + (1.0f - 0.84705883f) * tint, 0.54901963f + (1.0f - 0.54901963f) * tint};
            float color[3];
            for (int c = 0; c < 3; ++c) color[c] = std::clamp(skyState_.ambient[c] + skyState_.skyLightColor[c] * skyBrightness + mixed[c] * blockBrightness, 0.0f, 1.0f);
            const float m = std::max({color[0], color[1], color[2]});
            if (m > 0.0f) {
                const float inverted = 1.0f - m;
                const float lift = (1.0f - inverted * inverted * inverted * inverted) / m;
                for (float& c : color) c = c + (c * lift - c) * brightness_;
            }
            float* texel = &texels[(b * 16 + s) * 4];
            texel[0] = color[0];
            texel[1] = color[1];
            texel[2] = color[2];
            texel[3] = 1.0f;
        }
    }
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmapTex_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA16F), 16, 16, 0, gl::RGBA, gl::FLOAT, texels);
    gl::ActiveTexture(gl::TEXTURE0);
    lightmapBrightness_ = brightness_;
    lightmapSky_ = {skyState_.skyLightFactor, skyState_.skyLightColor[0], skyState_.skyLightColor[1], skyState_.skyLightColor[2],
                    skyState_.ambient[0], skyState_.ambient[1], skyState_.ambient[2]};
}

bool WorldRenderer::lightmapSkyChanged() const {
    const std::array<float, 7> now = {skyState_.skyLightFactor, skyState_.skyLightColor[0], skyState_.skyLightColor[1], skyState_.skyLightColor[2],
                                      skyState_.ambient[0], skyState_.ambient[1], skyState_.ambient[2]};
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (std::abs(now[i] - lightmapSky_[i]) > 0.004f) return true;
    }
    return false;
}

void WorldRenderer::drawSelection(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z) {
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
    const float farPlane = farPlaneFor(std::max(lastRadius_, zoomReach_), camera.y);
    const Matrix4f viewProj = perspective(camera.fov, aspect, 0.05f, farPlane) * cameraView(camera);
    // The lines round the block's OUTLINE shape (a torch's small post, a slab's half block, a fence's posts and rails), shifted the
    // way the block is drawn when it is a plant, and a hair bigger so the block's own faces do not hide them.
    const Block block = level_.block(x, y, z);
    const OutlineShape& outline = blockOutlines().shape(block);
    if (outline.edges.empty()) return;
    double offsetX = 0.0, offsetZ = 0.0;
    if (blockRegistry().state(block).has(kStateHasOffset)) {
        const std::array<double, 2> offset = blockOffset(x, z);
        offsetX = offset[0];
        offsetZ = offset[1];
    }
    float center[3] = {0.5f, 0.5f, 0.5f};  // the middle of the shape: the lines are pushed away from it
    if (!outline.boxes.empty()) {
        float lo[3] = {9.0f, 9.0f, 9.0f}, hi[3] = {-9.0f, -9.0f, -9.0f};
        for (const Aabb& b : outline.boxes) {
            lo[0] = std::min(lo[0], static_cast<float>(b.minX));
            lo[1] = std::min(lo[1], static_cast<float>(b.minY));
            lo[2] = std::min(lo[2], static_cast<float>(b.minZ));
            hi[0] = std::max(hi[0], static_cast<float>(b.maxX));
            hi[1] = std::max(hi[1], static_cast<float>(b.maxY));
            hi[2] = std::max(hi[2], static_cast<float>(b.maxZ));
        }
        for (int a = 0; a < 3; ++a) center[a] = (lo[a] + hi[a]) * 0.5f;
    }
    constexpr float e = 0.003f;
    std::vector<float> lines;
    lines.reserve(outline.edges.size() * 14);
    const double origin[3] = {x - camera.x + offsetX, y - camera.y, z - camera.z + offsetZ};
    for (const std::array<float, 6>& edge : outline.edges) {
        for (int end = 0; end < 2; ++end) {
            for (int a = 0; a < 3; ++a) {
                const float p = edge[static_cast<std::size_t>(end * 3 + a)];
                const float pushed = p > center[a] + 1.0e-4f ? p + e : p < center[a] - 1.0e-4f ? p - e : p;
                lines.push_back(static_cast<float>(origin[a] + static_cast<double>(pushed)));
            }
            for (const float c : {0.0f, 0.0f, 0.0f, 0.6f}) lines.push_back(c);  // (the outline is black, a little see-through)
        }
    }
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    lines_.use();
    gl::UniformMatrix4fv(lineViewProj_, 1, 0, viewProj.m.data());
    gl::BindVertexArray(lineVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, lineVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(lines.size() * sizeof(float)), lines.data(), gl::STREAM_DRAW);
    gl::DrawArrays(gl::LINES, 0, static_cast<gl::GLsizei>(lines.size() / 7));
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
}

void WorldRenderer::drawDebugLines(const Camera& camera, int framebufferWidth, int framebufferHeight, const std::vector<DebugLine>& lines) {
    if (lines.empty()) return;
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
    const float farPlane = farPlaneFor(std::max(lastRadius_, zoomReach_), camera.y);
    const Matrix4f viewProj = perspective(camera.fov, aspect, 0.05f, farPlane) * cameraView(camera);
    // The size of a pixel at one block from the eyes, for the lines that are more than a pixel wide.
    const float pixel = 2.0f * std::tan(camera.fov * 0.5f * 0.017453292f) / static_cast<float>(std::max(1, framebufferHeight));
    // Per kind: plain lines, strips, and each of those for the ones that are not hidden by the world and the ones that are.
    std::vector<float> thin[2], strips[2];
    const auto vertex = [](std::vector<float>& out, const float p[3], const DebugLine& l) {
        out.insert(out.end(), {p[0], p[1], p[2], l.r, l.g, l.b, l.a});
    };
    for (const DebugLine& l : lines) {
        const float a[3] = {static_cast<float>(l.ax - camera.x), static_cast<float>(l.ay - camera.y), static_cast<float>(l.az - camera.z)};
        const float b[3] = {static_cast<float>(l.bx - camera.x), static_cast<float>(l.by - camera.y), static_cast<float>(l.bz - camera.z)};
        const int pass = l.onTop ? 1 : 0;
        if (l.width <= 1.0f) {
            vertex(thin[pass], a, l);
            vertex(thin[pass], b, l);
            continue;
        }
        // A strip across the line, square to the way it is seen: a side vector from the line and the direction to its middle. Its width follows the distance (a pixel wide on the screen
        // at either end), so a line that runs past the eyes is cut in pieces until a piece is short against its distance.
        const std::function<void(const std::array<float, 3>&, const std::array<float, 3>&, int)> strip = [&](const std::array<float, 3>& pa, const std::array<float, 3>& pb, int depth) {
            const float d[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
            const float m[3] = {(pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f};
            const float lengthOf = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float distanceOf = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            if (depth < 9 && lengthOf > 0.5f * distanceOf && lengthOf > 0.25f) {
                const std::array<float, 3> mid = {m[0], m[1], m[2]};
                strip(pa, mid, depth + 1);
                strip(mid, pb, depth + 1);
                return;
            }
            float n[3] = {d[1] * m[2] - d[2] * m[1], d[2] * m[0] - d[0] * m[2], d[0] * m[1] - d[1] * m[0]};
            const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length < 1.0e-6f) return;  // (a line seen end on)
            for (float& c : n) c /= length;
            const auto half = [&](const std::array<float, 3>& q) { return 0.5f * l.width * pixel * std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]); };
            const float ha = half(pa), hb = half(pb);
            const float a0[3] = {pa[0] - n[0] * ha, pa[1] - n[1] * ha, pa[2] - n[2] * ha}, a1[3] = {pa[0] + n[0] * ha, pa[1] + n[1] * ha, pa[2] + n[2] * ha};
            const float b0[3] = {pb[0] - n[0] * hb, pb[1] - n[1] * hb, pb[2] - n[2] * hb}, b1[3] = {pb[0] + n[0] * hb, pb[1] + n[1] * hb, pb[2] + n[2] * hb};
            for (const float* q : {a0, a1, b1, a0, b1, b0}) vertex(strips[pass], q, l);
        };
        strip({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, 0);
    }
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    gl::Disable(gl::CULL_FACE);  // (a strip faces either way)
    gl::DepthMask(gl::FALSE_);
    lines_.use();
    gl::UniformMatrix4fv(lineViewProj_, 1, 0, viewProj.m.data());
    gl::BindVertexArray(lineVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, lineVbo_);
    const auto draw = [&](const std::vector<float>& data, gl::GLenum mode) {
        if (data.empty()) return;
        gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(data.size() * sizeof(float)), data.data(), gl::STREAM_DRAW);
        gl::DrawArrays(mode, 0, static_cast<gl::GLsizei>(data.size() / 7));
    };
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) gl::Enable(gl::DEPTH_TEST);
        else gl::Disable(gl::DEPTH_TEST);
        draw(strips[pass], gl::TRIANGLES);
        draw(thin[pass], gl::LINES);
    }
    gl::BindVertexArray(0);
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::CULL_FACE);
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::BLEND);
}

void WorldRenderer::drawModGeometry(const Camera& camera, int framebufferWidth, int framebufferHeight, const std::vector<jvm::WorldSubmit>& submits) {
    if (submits.empty()) return;
    std::vector<DebugLine> lines;
    std::vector<float> triangles[2];  // x y z r g b a, camera-relative: the ones the world hides (pass 0), and the ones it does not (pass 1)
    for (const jvm::WorldSubmit& s : submits) {
        const std::size_t count = s.vertices.size() / 9;
        const int pass = (s.state & 1) != 0 ? 0 : 1;  // (bit 0: the depth test; without it the geometry is drawn over everything)
        const auto at = [&](std::size_t i) { return s.vertices.data() + i * 9; };
        if (s.topology == 0) {
            const bool strip = (s.state & 16) != 0;
            for (std::size_t i = 0; i + 1 < count; i += strip ? 1 : 2) {
                const float* a = at(i);
                const float* b = at(i + 1);
                DebugLine l{static_cast<double>(a[0]) + camera.x, static_cast<double>(a[1]) + camera.y, static_cast<double>(a[2]) + camera.z,
                            static_cast<double>(b[0]) + camera.x, static_cast<double>(b[1]) + camera.y, static_cast<double>(b[2]) + camera.z, a[3], a[4], a[5], a[6]};
                l.width = s.lineWidth;
                l.onTop = pass == 1;
                lines.push_back(l);
            }
            continue;
        }
        const auto push = [&](std::size_t i) {
            const float* v = at(i);
            triangles[pass].insert(triangles[pass].end(), {v[0], v[1], v[2], v[3], v[4], v[5], v[6]});
        };
        if (s.topology == 2) {  // quads: two triangles each
            for (std::size_t i = 0; i + 3 < count; i += 4) {
                for (const std::size_t k : {0u, 1u, 2u, 0u, 2u, 3u}) push(i + k);
            }
        } else {
            for (std::size_t i = 0; i + 2 < count; i += 3) {
                for (const std::size_t k : {0u, 1u, 2u}) push(i + k);
            }
        }
    }
    if (!triangles[0].empty() || !triangles[1].empty()) {
        const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
        const float farPlane = farPlaneFor(std::max(lastRadius_, zoomReach_), camera.y);
        const Matrix4f viewProj = perspective(camera.fov, aspect, 0.05f, farPlane) * cameraView(camera);
        gl::Enable(gl::BLEND);
        gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
        gl::Disable(gl::CULL_FACE);  // (the mods' faces are drawn from either side)
        gl::DepthMask(gl::FALSE_);
        lines_.use();
        gl::UniformMatrix4fv(lineViewProj_, 1, 0, viewProj.m.data());
        gl::BindVertexArray(lineVao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, lineVbo_);
        for (int pass = 0; pass < 2; ++pass) {
            if (triangles[pass].empty()) continue;
            if (pass == 0) gl::Enable(gl::DEPTH_TEST);
            else gl::Disable(gl::DEPTH_TEST);
            gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(triangles[pass].size() * sizeof(float)), triangles[pass].data(), gl::STREAM_DRAW);
            gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(triangles[pass].size() / 7));
        }
        gl::BindVertexArray(0);
        gl::Enable(gl::DEPTH_TEST);
        gl::Enable(gl::CULL_FACE);
        gl::DepthMask(gl::TRUE_);
        gl::Disable(gl::BLEND);
    }
    drawDebugLines(camera, framebufferWidth, framebufferHeight, lines);
}

void WorldRenderer::setBreakImages(const std::vector<Image>& stages) {
    breakTextures_.clear();
    for (const Image& image : stages) breakTextures_.emplace_back(image, FilterMode::Nearest);
}

// LevelRenderer's crumbling layer: the block's own shape drawn again with destroy_stage_N, blended as (DST_COLOR, SRC_COLOR) so a grey picture leaves the
// block as it is and the dark cracks darken it. It lies a hair off the faces. (The game uses the block's model quads; this uses the outline boxes, which
// are the same for a full block and close for the rest.)
void WorldRenderer::drawBreakOverlay(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z, int stage) {
    if (stage < 0 || static_cast<std::size_t>(stage) >= breakTextures_.size()) return;
    const Block block = level_.block(x, y, z);
    const OutlineShape& outline = blockOutlines().shape(block);
    if (outline.boxes.empty()) return;
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
    const float farPlane = farPlaneFor(std::max(lastRadius_, zoomReach_), camera.y);
    const Matrix4f viewProj = perspective(camera.fov, aspect, 0.05f, farPlane) * cameraView(camera);
    double offsetX = 0.0, offsetZ = 0.0;
    if (blockRegistry().state(block).has(kStateHasOffset)) {
        const std::array<double, 2> offset = blockOffset(x, z);
        offsetX = offset[0];
        offsetZ = offset[1];
    }
    const double ox = x - camera.x + offsetX, oy = y - camera.y, oz = z - camera.z + offsetZ;
    constexpr double e = 0.002;
    std::vector<float> v;
    v.reserve(outline.boxes.size() * 6 * 6 * 5);
    const auto vertex = [&](double px, double py, double pz, double u, double w) {
        for (const double value : {ox + px, oy + py, oz + pz, u, w}) v.push_back(static_cast<float>(value));
    };
    const auto quad = [&](const std::array<std::array<double, 5>, 4>& q) {
        for (const int i : {0, 1, 2, 0, 2, 3}) vertex(q[static_cast<std::size_t>(i)][0], q[static_cast<std::size_t>(i)][1], q[static_cast<std::size_t>(i)][2], q[static_cast<std::size_t>(i)][3], q[static_cast<std::size_t>(i)][4]);
    };
    for (const Aabb& b : outline.boxes) {
        const double x0 = b.minX - e, x1 = b.maxX + e, y0 = b.minY - e, y1 = b.maxY + e, z0 = b.minZ - e, z1 = b.maxZ + e;
        // (u, v) follow the block's own cell, so a half block shows half of the picture.
        quad({{{x1, y0, z0, z0, 1 - y0}, {x1, y1, z0, z0, 1 - y1}, {x1, y1, z1, z1, 1 - y1}, {x1, y0, z1, z1, 1 - y0}}});  // east
        quad({{{x0, y0, z1, z1, 1 - y0}, {x0, y1, z1, z1, 1 - y1}, {x0, y1, z0, z0, 1 - y1}, {x0, y0, z0, z0, 1 - y0}}});  // west
        quad({{{x0, y1, z0, x0, z0}, {x0, y1, z1, x0, z1}, {x1, y1, z1, x1, z1}, {x1, y1, z0, x1, z0}}});                  // up
        quad({{{x0, y0, z1, x0, z1}, {x0, y0, z0, x0, z0}, {x1, y0, z0, x1, z0}, {x1, y0, z1, x1, z1}}});                  // down
        quad({{{x1, y0, z1, x1, 1 - y0}, {x1, y1, z1, x1, 1 - y1}, {x0, y1, z1, x0, 1 - y1}, {x0, y0, z1, x0, 1 - y0}}});  // south
        quad({{{x0, y0, z0, x0, 1 - y0}, {x0, y1, z0, x0, 1 - y1}, {x1, y1, z0, x1, 1 - y1}, {x1, y0, z0, x1, 1 - y0}}});  // north
    }
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthMask(gl::FALSE_);
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::DST_COLOR, gl::SRC_COLOR, gl::ONE, gl::ZERO);
    cracks_.use();
    gl::UniformMatrix4fv(crackViewProj_, 1, 0, viewProj.m.data());
    breakTextures_[static_cast<std::size_t>(stage)].bind(0);
    gl::Uniform1i(crackTex_, 0);
    gl::BindVertexArray(crackVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, crackVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(v.size() * sizeof(float)), v.data(), gl::STREAM_DRAW);
    gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(v.size() / 5));
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
    gl::DepthMask(gl::TRUE_);
}

void WorldRenderer::endFrame() {
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    gl::DepthMask(gl::TRUE_);
    gl::BindVertexArray(0);
    measureGpu(false);
}

void WorldRenderer::drawHeld(const Matrix4f& mvp, const std::vector<MeshVertex>& vertices) {
    const std::size_t quads = std::min(vertices.size() / 4, kMaxQuadsPerSection);
    if (quads == 0) return;
    if (heldVao_ == 0) {
        gl::GenVertexArrays(1, &heldVao_);
        gl::GenBuffers(1, &heldVbo_);
        gl::BindVertexArray(heldVao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, heldVbo_);
        gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, indexBuffer_);
        bindVertexLayout();
    } else {
        gl::BindVertexArray(heldVao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, heldVbo_);
    }
    models_.uploadPending();
    world_.use();
    gl::UniformMatrix4fv(uViewProj_, 1, 0, mvp.m.data());
    gl::Uniform3f(uOffset_, 0.0f, 0.0f, 0.0f);
    gl::Uniform3f(uFogColor_, 0.0f, 0.0f, 0.0f);
    gl::Uniform2f(uFogRange_, 1.0e6f, 2.0e6f);
    gl::Uniform2f(uEnvFog_, 1.0e9f, 2.0e9f);
    gl::Uniform1f(uGamma_, brightness_);
    gl::Uniform1f(uAlphaCut_, 0.1f);
    models_.bindTextures(0);
    gl::Uniform1i(uTex_, 0);
    gl::Uniform1i(uHeldLight_, 1);      // (the sampler must not share unit 0 with the block textures, used or not)
    gl::Uniform1f(uHeldOn_, 0.0f);      // the held item is lit by its own vertices, not by the volume (its positions are model space)
    if (lightmapBrightness_ != brightness_ || lightmapSkyChanged()) updateLightmap();
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmapTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(uLightmap_, 2);
    gl::Uniform1f(uInstanced_, 0.0f);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(quads * 4 * sizeof(MeshVertex)), vertices.data(), gl::STREAM_DRAW);
    gl::DrawElements(gl::TRIANGLES, static_cast<gl::GLsizei>(quads * 6), gl::UNSIGNED_INT, nullptr);
    gl::BindVertexArray(0);
}

}  // namespace falcon
