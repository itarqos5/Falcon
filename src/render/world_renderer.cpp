#include "falcon/render/world_renderer.hpp"
#include "world_renderer_internal.hpp"

#include "falcon/world/block_outlines.hpp"
#include "falcon/world/block_position.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace falcon {
namespace {

// The world's shaders. The #version line and a few #define lines in front of them are added when they are built (worldShaderSource), so
// the same text serves the variants that are compared with each other: LIGHT_LUT (the light map as a 16 x 16 picture instead of
// arithmetic on every pixel) and FLAT_NORMAL (a face's direction from the vertex instead of from screen derivatives).
constexpr char kWorldVertexBody[] = R"(// vk: world.vert
layout(location = 0) in vec4 aPos;    // x, y, z inside the section (1/1024 block), and the texture layer
layout(location = 1) in vec2 aUv;     // 1/1024 of a texture repeat
layout(location = 2) in vec4 aColor;  // tint colour, and the shade of the face's direction
layout(location = 3) in vec4 aLight;  // sky light, block light (0..1: the level / 15), the face's direction (1 + the Face number; 0: not known)
layout(location = 4) in vec3 aSectionOffset;  // when drawing many sections in one call: where the section's corner is (one per command)
uniform mat4 uViewProj;
uniform vec3 uOffset;                 // where the section's corner is, relative to the camera (one section per call)
uniform float uInstanced;             // 1: the corner comes from aSectionOffset
out vec2 vUv;
flat out float vLayer;
flat out float vFace;
out vec3 vTint;    // the tint colour times the shade of the face's direction (and of its ambient occlusion)
out vec2 vLight;   // sky light, block light: interpolated as levels, turned into a colour per pixel (as the game's light map is)
out vec3 vPos;     // where this is, relative to the camera
out float vDist;
void main() {
    vFace = floor(aLight.z * 255.0 + 0.5);
    vec3 p = aPos.xyz * (1.0 / 1024.0) + (uInstanced > 0.5 ? aSectionOffset : uOffset);
    gl_Position = uViewProj * vec4(p, 1.0);
    vUv = aUv * (1.0 / 1024.0);
    vLayer = aPos.w;
    vTint = aColor.rgb * aColor.a;
    vLight = aLight.xy;
    vPos = p;
    vDist = length(p);
}
)";

constexpr char kWorldFragmentBody[] = R"(// vk: world.frag
in vec2 vUv;
flat in float vLayer;
flat in float vFace;
in vec3 vTint;
in vec2 vLight;
in vec3 vPos;
in float vDist;
uniform sampler2DArray uTex;
uniform vec3 uFogColor;
uniform vec2 uFogRange;               // the render distance's fog: start, end (distance from the camera)
uniform vec2 uEnvFog;                 // the fog of the medium the eyes are in (water): start, end; far away when there is none
uniform float uAlphaCut;
uniform float uGamma;                 // the Brightness option, 0..1
// The light the player carries (a torch in the hand): the light level of every block of a 32 x 32 x 32 volume around them, flooded
// round walls like the block light is. uHeldOrigin is the volume's corner relative to the camera; uHeldOn is 0 with nothing held.
uniform sampler3D uHeldLight;
uniform vec3 uHeldOrigin;
uniform float uHeldOn;
uniform sampler2D uLightmap;          // LIGHT_LUT: the light map, sky light across and block light down, one texel per level
// The game's own light map (assets/shaders/core/lightmap.fsh), with the values the overworld has by day:
// ambient black, sky light white at factor 1, block light tinted #FFD88C at factor 1.4 (its flicker averages 0), and
// "notGamma" (which lifts the dark end without changing the hue) mixed in by the Brightness option.
#ifdef LIGHT_LUT
vec3 lightColor(float sky, float blk) {
    // (The texture holds exactly that, per level, and the filter blends the levels between: as the game's own 16 x 16 light map does.)
    return texture(uLightmap, vec2(sky, blk) * (15.0 / 16.0) + vec2(0.5 / 16.0)).rgb;
}
#else
vec3 lightColor(float sky, float blk) {
    float block_brightness = (blk / (4.0 - 3.0 * blk)) * 1.4;
    float sky_brightness = sky / (4.0 - 3.0 * sky);
    float parabolic = (2.0 * blk - 1.0) * (2.0 * blk - 1.0);
    vec3 color = vec3(sky_brightness) + mix(vec3(1.0, 0.84705883, 0.54901963), vec3(1.0), 0.9 * parabolic) * block_brightness;
    color = clamp(color, 0.0, 1.0);
    float m = max(max(color.r, color.g), color.b);
    if (m > 0.0) {
        float inverted = 1.0 - m;
        vec3 notGamma = color * ((1.0 - inverted * inverted * inverted * inverted) / m);
        color = mix(color, notGamma, uGamma);
    }
    return color;
}
#endif
#ifdef FLAT_NORMAL
// East, west, up, down, south, north (the mesher's Face order): the even ones point the positive way.
vec3 faceNormal(int f) {
    float s = (f & 1) == 0 ? 1.0 : -1.0;
    return f < 2 ? vec3(s, 0.0, 0.0) : (f < 4 ? vec3(0.0, s, 0.0) : vec3(0.0, 0.0, s));
}
#endif
out vec4 frag;
void main() {
    vec4 c = texture(uTex, vec3(vUv, vLayer));
    if (c.a < uAlphaCut) discard;
    float blockLight = vLight.y;
    if (uHeldOn > 0.5) {
        // The light a face receives is the light of the cell in FRONT of it: step half a block out along the face's normal (known from the
        // mesher, or else from the screen derivatives; turned towards the viewer) and read the volume there.
#ifdef FLAT_NORMAL
        vec3 n = vFace > 0.5 ? faceNormal(int(vFace + 0.5) - 1) : normalize(cross(dFdx(vPos), dFdy(vPos)));
#else
        vec3 n = normalize(cross(dFdx(vPos), dFdy(vPos)));
#endif
        if (dot(n, vPos) > 0.0) n = -n;
        blockLight = max(blockLight, texture(uHeldLight, (vPos + n * 0.5 - uHeldOrigin) * (1.0 / 32.0)).r);
    }
    // The game's fog is the larger of the render distance's and the medium's (water's: it starts at the eyes and ends 24 to 96 blocks on).
    float fog = max(clamp((vDist - uFogRange.x) / (uFogRange.y - uFogRange.x), 0.0, 1.0), clamp((vDist - uEnvFog.x) / (uEnvFog.y - uEnvFog.x), 0.0, 1.0));
    frag = vec4(mix(c.rgb * (vTint * lightColor(vLight.x, blockLight)), uFogColor, fog), c.a);
}
)";

std::string worldShaderSource(const char* body) {
    std::string source = "#version 330 core\n";
    if (!optionOff("FALCON_NO_LIGHTLUT")) source += "#define LIGHT_LUT\n";
    if (!optionOff("FALCON_NO_FLATNORMAL")) source += "#define FLAT_NORMAL\n";
    source += body;
    return source;
}

constexpr char kSkyVertex[] = R"(// vk: sky.vert
#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
)";

constexpr char kSkyFragment[] = R"(// vk: sky.frag
#version 330 core
in vec2 vNdc;
uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uTan;
uniform vec3 uHorizon;
uniform vec3 uZenith;
out vec4 frag;
void main() {
    vec3 d = normalize(uForward + uRight * (vNdc.x * uTan.x) + uUp * (vNdc.y * uTan.y));
    float t = clamp(d.y, 0.0, 1.0);
    frag = vec4(mix(uHorizon, uZenith, pow(t, 0.55)), 1.0);
}
)";

constexpr char kLineVertex[] = R"(// vk: lines.vert
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

constexpr char kLineFragment[] = R"(// vk: lines.frag
#version 330 core
out vec4 frag;
void main() { frag = vec4(0.0, 0.0, 0.0, 0.6); }
)";

// The cracks of a block being dug: the picture alone, multiplied into what is already there by the blend (see drawBreakOverlay).
constexpr char kCrackVertex[] = R"(// vk: cracks.vert
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uViewProj;
out vec2 vUv;
void main() { vUv = aUv; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

constexpr char kCrackFragment[] = R"(// vk: cracks.frag
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 frag;
void main() {
    vec4 c = texture(uTex, vUv);
    if (c.a < 0.01) discard;
    frag = c;
}
)";

struct Offset {
    int dx, dz, d2;
};
// Every (dx, dz) within 48 chunks, nearest first.
const std::vector<Offset>& offsetsByDistance() {
    static const std::vector<Offset> offsets = [] {
        std::vector<Offset> all;
        constexpr int kMax = 48;
        for (int dz = -kMax; dz <= kMax; ++dz) {
            for (int dx = -kMax; dx <= kMax; ++dx) {
                if (dx * dx + dz * dz <= kMax * kMax) all.push_back({dx, dz, dx * dx + dz * dz});
            }
        }
        std::sort(all.begin(), all.end(), [](const Offset& a, const Offset& b) { return a.d2 < b.d2; });
        return all;
    }();
    return offsets;
}

}  // namespace

WorldRenderer::WorldRenderer(BlockModels& models, std::int64_t seed, TerrainStyle style, unsigned workerThreads,
                             std::shared_ptr<const worldgen::OverworldGenerator> overworld)
    : models_(models),
      generator_(seed, style),
      overworld_(std::move(overworld)),
      decorator_(overworld_ ? std::make_shared<Decorator>(*overworld_, models.registry(), seed) : nullptr),
      world_("world", worldShaderSource(kWorldVertexBody).c_str(), worldShaderSource(kWorldFragmentBody).c_str()),
      sky_("sky", kSkyVertex, kSkyFragment),
      lines_("lines", kLineVertex, kLineFragment),
      cracks_("cracks", kCrackVertex, kCrackFragment),
      pool_(workerThreads) {
    uViewProj_ = world_.uniform("uViewProj");
    uOffset_ = world_.uniform("uOffset");
    uFogColor_ = world_.uniform("uFogColor");
    uFogRange_ = world_.uniform("uFogRange");
    uAlphaCut_ = world_.uniform("uAlphaCut");
    uGamma_ = world_.uniform("uGamma");
    uTex_ = world_.uniform("uTex");
    uEnvFog_ = world_.uniform("uEnvFog");
    uHeldLight_ = world_.uniform("uHeldLight");
    uHeldOrigin_ = world_.uniform("uHeldOrigin");
    uHeldOn_ = world_.uniform("uHeldOn");
    uInstanced_ = world_.uniform("uInstanced");
    uLightmap_ = world_.uniform("uLightmap");
    skyForward_ = sky_.uniform("uForward");
    skyRight_ = sky_.uniform("uRight");
    skyUp_ = sky_.uniform("uUp");
    skyTan_ = sky_.uniform("uTan");
    skyHorizon_ = sky_.uniform("uHorizon");
    skyZenith_ = sky_.uniform("uZenith");
    lineViewProj_ = lines_.uniform("uViewProj");
    crackViewProj_ = cracks_.uniform("uViewProj");
    crackTex_ = cracks_.uniform("uTex");

    // The one index list every section shares: quad n is vertices 4n..4n+3, drawn as two triangles.
    std::vector<std::uint32_t> indices;
    indices.reserve(kMaxQuadsPerSection * 6);
    for (std::uint32_t q = 0; q < kMaxQuadsPerSection; ++q) {
        const std::uint32_t v = q * 4;
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(v + i);
    }
    gl::GenBuffers(1, &indexBuffer_);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, indexBuffer_);
    // (A section's own VAO remembers this buffer, so bind it with a throwaway VAO active.)
    gl::GenVertexArrays(1, &emptyVao_);
    gl::BindVertexArray(emptyVao_);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, indexBuffer_);
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), gl::STATIC_DRAW);
    gl::BindVertexArray(0);

    // The shared vertex arena: one buffer, one vertex array. Start at 8M vertices (160 MB) so low and medium render distances
    // never have to grow it (growing copies the whole buffer, a brief stall); high ones grow a few times during the first load.
    arenaCapacity_ = 8u * 1024u * 1024u;
    gl::GenBuffers(1, &arenaBuffer_);
    gl::BindBuffer(gl::ARRAY_BUFFER, arenaBuffer_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(arenaCapacity_ * sizeof(MeshVertex)), nullptr, gl::DYNAMIC_DRAW);
    gl::GenVertexArrays(1, &arenaVao_);
    setupArenaVao();

    gl::GenVertexArrays(1, &lineVao_);
    gl::GenBuffers(1, &lineVbo_);
    gl::BindVertexArray(lineVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, lineVbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 3 * sizeof(float), nullptr);
    gl::BindVertexArray(0);

    gl::GenVertexArrays(1, &crackVao_);
    gl::GenBuffers(1, &crackVbo_);
    gl::BindVertexArray(crackVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, crackVbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 5 * sizeof(float), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, 5 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl::BindVertexArray(0);

    // The light the player carries: a 32 x 32 x 32 volume of light levels, one byte (level * 17) per block.
    heldLightVolume_.assign(static_cast<std::size_t>(kHeldLightSize) * kHeldLightSize * kHeldLightSize, 0);
    gl::GenTextures(1, &heldLightTex_);
    gl::ActiveTexture(gl::TEXTURE1);
    gl::BindTexture(gl::TEXTURE_3D, heldLightTex_);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_R, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexImage3D(gl::TEXTURE_3D, 0, static_cast<gl::GLint>(gl::R8), kHeldLightSize, kHeldLightSize, kHeldLightSize, 0, gl::RED, gl::UNSIGNED_BYTE,
                   heldLightVolume_.data());
    gl::ActiveTexture(gl::TEXTURE0);

    gl::GenQueries(3, queries_);

    // The light map as a picture (texture unit 2): sky light across, block light down.
    gl::GenTextures(1, &lightmapTex_);
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmapTex_);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::ActiveTexture(gl::TEXTURE0);
    updateLightmap();

    // The grid of sections for occlusion culling (everything unknown, so open), and the buffers for drawing many sections in one call.
    const std::size_t cells = static_cast<std::size_t>(kGridSize) * kGridSize * kSectionCount;
    gridSection_.assign(cells, nullptr);
    gridVisibility_.assign(cells, kVisibilityOpen);
    gridStamp_.assign(cells, 0);
    useOcclusion_ = !optionOff("FALCON_NO_OCCLUSION");
    useMultiDraw_ = gl::multiDrawIndirectAvailable();
    if (useMultiDraw_) {
        gl::GenBuffers(1, &commandBuffer_);
        gl::GenBuffers(1, &offsetBuffer_);
        gl::GenVertexArrays(1, &arenaVaoInstanced_);
        setupArenaVao();  // (both vertex arrays)
    }
    std::printf("[falcon] world drawing: %s, %s, light map %s, face directions %s\n", useMultiDraw_ ? "multi-draw (one call per pass)" : "one draw per section",
                useOcclusion_ ? "occlusion culling" : "frustum culling only", optionOff("FALCON_NO_LIGHTLUT") ? "per pixel" : "from a 16x16 picture",
                optionOff("FALCON_NO_FLATNORMAL") ? "from screen derivatives" : "from the mesh");
}

WorldRenderer::~WorldRenderer() {
    if (lightmapTex_ != 0) gl::DeleteTextures(1, &lightmapTex_);
    if (commandBuffer_ != 0) gl::DeleteBuffers(1, &commandBuffer_);
    if (offsetBuffer_ != 0) gl::DeleteBuffers(1, &offsetBuffer_);
    if (arenaVaoInstanced_ != 0) gl::DeleteVertexArrays(1, &arenaVaoInstanced_);
    if (heldLightTex_ != 0) gl::DeleteTextures(1, &heldLightTex_);
    // The workers must be gone before anything they use is destroyed: pool_ is the last member, so it is destroyed
    // first, but be explicit that queued work is dropped.
    pool_.clear();
    for (auto& [key, section] : sections_) destroySection(section);
    gl::DeleteBuffers(1, &arenaBuffer_);
    if (stagingBuffer_ != 0) gl::DeleteBuffers(1, &stagingBuffer_);
    gl::DeleteVertexArrays(1, &arenaVao_);
    gl::DeleteBuffers(1, &indexBuffer_);
    gl::DeleteVertexArrays(1, &emptyVao_);
    gl::DeleteVertexArrays(1, &lineVao_);
    gl::DeleteBuffers(1, &lineVbo_);
    gl::DeleteBuffers(1, &crackVbo_);
    gl::DeleteVertexArrays(1, &crackVao_);
    if (heldVao_ != 0) gl::DeleteVertexArrays(1, &heldVao_);
    if (heldVbo_ != 0) gl::DeleteBuffers(1, &heldVbo_);
    gl::DeleteQueries(3, queries_);
}

// The attribute layout of a MeshVertex, pointing at whatever arena buffer is bound. Re-run after the arena grows.
void WorldRenderer::setupArenaVao() {
    const auto layout = [&](gl::GLuint vao, bool instanced) {
        gl::BindVertexArray(vao);
        gl::BindBuffer(gl::ARRAY_BUFFER, arenaBuffer_);
        gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, indexBuffer_);
        bindVertexLayout();
        if (instanced) {
            // The section's place, one per command (its base instance picks it): a three-float attribute that advances per instance.
            gl::BindBuffer(gl::ARRAY_BUFFER, offsetBuffer_);
            gl::EnableVertexAttribArray(4);
            gl::VertexAttribPointer(4, 3, gl::FLOAT, 0, 3 * sizeof(float), nullptr);
            gl::VertexAttribDivisor(4, 1);
        }
        gl::BindVertexArray(0);
    };
    layout(arenaVao_, false);
    if (arenaVaoInstanced_ != 0) layout(arenaVaoInstanced_, true);
}

// Packs the live sections to the front of the arena (into a fresh buffer of the same size), reclaiming the dead space left by
// sections that were removed or re-meshed smaller.
void WorldRenderer::arenaCompact(std::size_t newCapacity) {
    if (newCapacity == 0) newCapacity = arenaCapacity_;
    std::vector<GpuSection*> live;
    live.reserve(sections_.size());
    for (auto& [key, section] : sections_) {
        if (section.arenaVerts > 0) live.push_back(&section);
    }
    std::sort(live.begin(), live.end(), [](const GpuSection* a, const GpuSection* b) { return a->arenaBase < b->arenaBase; });
    gl::GLuint packed = 0;
    gl::GenBuffers(1, &packed);
    gl::BindBuffer(gl::COPY_WRITE_BUFFER, packed);
    gl::BufferData(gl::COPY_WRITE_BUFFER, static_cast<gl::GLsizeiptr>(newCapacity * sizeof(MeshVertex)), nullptr, gl::DYNAMIC_DRAW);
    arenaCapacity_ = newCapacity;
    gl::BindBuffer(gl::COPY_READ_BUFFER, arenaBuffer_);
    std::uint32_t at = 0;
    for (GpuSection* s : live) {
        gl::CopyBufferSubData(gl::COPY_READ_BUFFER, gl::COPY_WRITE_BUFFER, static_cast<gl::GLintptr>(s->arenaBase) * sizeof(MeshVertex),
                              static_cast<gl::GLintptr>(at) * sizeof(MeshVertex), static_cast<gl::GLsizeiptr>(s->arenaVerts) * sizeof(MeshVertex));
        s->arenaBase = at;
        at += s->arenaVerts;
    }
    gl::DeleteBuffers(1, &arenaBuffer_);
    arenaBuffer_ = packed;
    arenaUsed_ = at;
    arenaDead_ = 0;
    setupArenaVao();  // the VAO must point at the new buffer
}

// Gives the memory of the buffer back when the world got much smaller (a lower render distance, or the player moved on): the driver
// keeps the whole buffer in system memory as well, so a buffer that stays at its largest size keeps all that RAM for nothing.
void WorldRenderer::arenaMaybeShrink() {
    constexpr std::size_t kStep = 2u * 1024u * 1024u;  // vertices: 40 MB
    constexpr std::size_t kMinimum = 8u * 1024u * 1024u;
    if (arenaCapacity_ <= kMinimum || arenaUsed_ < arenaDead_) return;
    const std::size_t live = arenaUsed_ - arenaDead_;
    if (live * 2 > arenaCapacity_) return;  // still more than half full: leave it
    std::size_t target = (live + live / 4 + kStep - 1) / kStep * kStep;  // a quarter of room to grow into
    target = std::max(target, kMinimum);
    if (target >= arenaCapacity_) return;
    std::printf("[falcon] mesh buffer shrunk from %zu MB to %zu MB (%zu MB of meshes in use)\n", arenaCapacity_ * sizeof(MeshVertex) >> 20, target * sizeof(MeshVertex) >> 20,
                live * sizeof(MeshVertex) >> 20);
    arenaCompact(target);
}

void WorldRenderer::arenaGrow(std::size_t needed) {
    // A quarter more each time (and at least what is needed, in steps of 40 MB), not twice as much: the buffer is a big part of the
    // game's memory and a doubled one is half empty.
    constexpr std::size_t kStep = 2u * 1024u * 1024u;
    std::size_t newCapacity = std::max<std::size_t>(arenaCapacity_ + arenaCapacity_ / 4, arenaUsed_ + needed);
    newCapacity = (newCapacity + kStep - 1) / kStep * kStep;
    gl::GLuint bigger = 0;
    gl::GenBuffers(1, &bigger);
    gl::BindBuffer(gl::COPY_WRITE_BUFFER, bigger);
    gl::BufferData(gl::COPY_WRITE_BUFFER, static_cast<gl::GLsizeiptr>(newCapacity * sizeof(MeshVertex)), nullptr, gl::DYNAMIC_DRAW);
    gl::BindBuffer(gl::COPY_READ_BUFFER, arenaBuffer_);
    gl::CopyBufferSubData(gl::COPY_READ_BUFFER, gl::COPY_WRITE_BUFFER, 0, 0, static_cast<gl::GLsizeiptr>(arenaUsed_ * sizeof(MeshVertex)));
    gl::DeleteBuffers(1, &arenaBuffer_);
    arenaBuffer_ = bigger;
    arenaCapacity_ = newCapacity;
    setupArenaVao();
}

// A bump allocator with compaction: sections are placed at the end; freed space is reclaimed in bulk when enough has piled up.
std::uint32_t WorldRenderer::arenaAllocate(std::uint32_t verts) {
    if (arenaUsed_ + verts > arenaCapacity_) {
        // Reclaim dead space first if there is a worthwhile amount; only grow if that is still not enough.
        if (arenaDead_ * 3 > arenaCapacity_) arenaCompact();
        if (arenaUsed_ + verts > arenaCapacity_) arenaGrow(verts);
    }
    const std::uint32_t base = static_cast<std::uint32_t>(arenaUsed_);
    arenaUsed_ += verts;
    return base;
}

void WorldRenderer::arenaFree(std::uint32_t base, std::uint32_t verts) {
    (void)base;
    arenaDead_ += verts;
}

void WorldRenderer::destroySection(GpuSection& section) {
    if (section.arenaVerts > 0) arenaFree(section.arenaBase, section.arenaVerts);
    meshBytes_ -= static_cast<std::size_t>(section.opaqueVertices + section.cutoutVertices + section.translucentVertices) * sizeof(MeshVertex);
    quadsTotal_ -= static_cast<std::size_t>(section.opaqueVertices + section.cutoutVertices + section.translucentVertices) / 4;
    section = {};
}

std::uint32_t WorldRenderer::stampOf(ChunkPos pos) const {
    const auto it = editStamps_.find(pos);
    return it == editStamps_.end() ? 0 : it->second;
}

bool WorldRenderer::neighborsLoaded(ChunkPos pos) const {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (level_.chunk({pos.x + dx, pos.z + dz}) == nullptr) return false;
        }
    }
    return true;
}

void WorldRenderer::requestChunk(ChunkPos pos, float priority) {
    chunkRequested_.insert(pos);
    ++generating_;
    pool_.submit(priority, [this, pos] {
        std::shared_ptr<Chunk> chunk;
        const int dx = pos.x - playerChunkX_.load();
        const int dz = pos.z - playerChunkZ_.load();
        const int far = wantedRadius_.load() + 3;
        if (dx * dx + dz * dz <= far * far) {  // else: the player moved on
            if (std::shared_ptr<Chunk> cached = chunkCache_.take(pos)) {
                chunk = std::move(cached);  // it was here before: its blocks (and anything the player changed) come back from the small copy
            } else if (overworld_) {
                worldgen::ChunkExtras extras;
                auto started = std::chrono::steady_clock::now();
                chunk = overworld_->generate(pos.x, pos.z, extras);
                addStageTime(kStageTerrain, started);
                started = std::chrono::steady_clock::now();
                if (decorator_) decorator_->decorate(*chunk, pos.x, pos.z, extras);
                addStageTime(kStageDecorate, started);
                auto grid = std::make_shared<BiomeGrid>();
                std::copy(extras.biomes.ids.begin(), extras.biomes.ids.end(), grid->ids.begin());
                chunk->biomes = std::move(grid);
            } else {
                chunk = generator_.generate(pos.x, pos.z);
            }
            if (edits_ && chunk) edits_->applyTo(pos, *chunk);  // what the player changed here comes back (a chunk from the cache has it already)
        }
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            chunkResults_.push_back({pos, std::move(chunk)});
        }
        --generating_;
    });
}

bool WorldRenderer::neighborsLit(ChunkPos pos) const {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* c = level_.chunk({pos.x + dx, pos.z + dz});
            if (c == nullptr || !c->light) return false;
        }
    }
    return true;
}

// Light is worked out for a chunk once the 8 chunks around it exist (it reaches 15 blocks); a chunk is meshed once it and
// its neighbours have their light (the faces at its edges read their neighbours' light).
void WorldRenderer::requestLight(ChunkPos pos, bool force) {
    Chunk* chunk = level_.chunk(pos);
    if (chunk == nullptr || !neighborsLoaded(pos)) return;
    if (!force && (chunk->light || lightRequested_.contains(pos))) return;
    lightRequested_.insert(pos);
    const std::uint32_t stamp = ++lightStamps_[pos];
    const int dx = pos.x - playerChunkX_.load();
    const int dz = pos.z - playerChunkZ_.load();
    const float priority = static_cast<float>(dx * dx + dz * dz) - (force ? 1.0e6f : 0.5f);  // edits first; light before the meshes at its distance
    ++lighting_;
    pool_.submit(priority, [this, pos, stamp, input = snapshotLightInput(level_, pos.x, pos.z)] {
        const auto started = std::chrono::steady_clock::now();
        LightResult result{pos, stamp, computeChunkLight(input)};
        addStageTime(kStageLight, started);
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            lightResults_.push_back(std::move(result));
        }
        --lighting_;
    });
}

bool WorldRenderer::leafLodFor(const SectionKey& key) const {
    const int dx = key.x - playerChunkX_.load();
    const int dz = key.z - playerChunkZ_.load();
    return dx * dx + dz * dz > kLeafDetailChunks * kLeafDetailChunks;
}

void WorldRenderer::refreshLeafDetail() {
    if (!cutoutLeaves_) return;  // Fast leaves never have inner faces
    const int pcx = playerChunkX_.load(), pcz = playerChunkZ_.load();
    constexpr int kNear = kLeafDetailChunks * kLeafDetailChunks;
    constexpr int kFar = (kLeafDetailChunks + kLeafDetailMargin) * (kLeafDetailChunks + kLeafDetailMargin);
    for (const auto& [key, section] : sections_) {
        if (!section.lodMatters || meshPending_.contains(key)) continue;
        const int dx = key.x - pcx, dz = key.z - pcz;
        const int d2 = dx * dx + dz * dz;
        // Near sections need their inner leaf faces, far ones do not; in between they stay as they are.
        if ((section.lodFar && d2 <= kNear) || (!section.lodFar && d2 > kFar)) requestSectionMesh(key, static_cast<float>(d2));
    }
}

void WorldRenderer::remeshAll() {
    // The light and leaf shape are baked into the vertices, so every section already on the GPU has to be built again.
    const std::vector<SectionKey> keys(meshRequested_.begin(), meshRequested_.end());
    const int pcx = playerChunkX_.load(), pcz = playerChunkZ_.load();
    for (const SectionKey& key : keys) {
        const int dx = key.x - pcx, dz = key.z - pcz;
        requestSectionMesh(key, static_cast<float>(dx * dx + dz * dz));
    }
}

void WorldRenderer::setSmoothLighting(bool on) {
    if (on == smoothLighting_) return;
    smoothLighting_ = on;
    remeshAll();
}

void WorldRenderer::setCutoutLeaves(bool on) {
    if (on == cutoutLeaves_) return;
    cutoutLeaves_ = on;
    remeshAll();
}

void WorldRenderer::setCarriedLight(int x, int y, int z, int level) {
    level = std::clamp(level, 0, 15);
    if (level == 0) {
        heldLightLevel_ = 0;
        return;
    }
    const bool same = level == heldLightLevel_ && x == heldLightX_ && y == heldLightY_ && z == heldLightZ_ && heldLightEdits_ == blockEdits_;
    if (same && ++heldLightAge_ < 30) return;  // (also redone twice a second or so: chunks load and change around the player)
    heldLightAge_ = 0;
    heldLightLevel_ = level;
    heldLightX_ = x;
    heldLightY_ = y;
    heldLightZ_ = z;
    heldLightEdits_ = blockEdits_;

    // Flood the level out from the player's block the way block light spreads (see computeCarriedLight).
    constexpr int n = kHeldLightSize;
    computeCarriedLight(level_, x, y, z, level, n, heldLightVolume_);
    for (std::uint8_t& v : heldLightVolume_) v = static_cast<std::uint8_t>(v * 17);  // 0..15 -> 0..255: the scale of a vertex's block light
    gl::ActiveTexture(gl::TEXTURE1);
    gl::BindTexture(gl::TEXTURE_3D, heldLightTex_);
    gl::TexSubImage3D(gl::TEXTURE_3D, 0, 0, 0, 0, n, n, n, gl::RED, gl::UNSIGNED_BYTE, heldLightVolume_.data());
    gl::ActiveTexture(gl::TEXTURE0);
}

void WorldRenderer::requestSectionMesh(const SectionKey& key, float priority) {
    const std::uint32_t gen = ++meshGen_[key];
    ++meshing_;
    meshPending_.insert(key);  // until its result is applied (uploadMesh): what sectionReady() looks at
    pool_.submit(priority, [this, key, gen, stamp = stampOf({key.x, key.z}), around = snapshotNeighborhood(level_, key.x, key.y, key.z)] {
        MeshResult result{key, stamp, {}, 0, 0, 0, gen};
        const int ddx = key.x - playerChunkX_.load();
        const int ddz = key.z - playerChunkZ_.load();
        const int far = wantedRadius_.load() + 3;
        if (ddx * ddx + ddz * ddz <= far * far) {
            const bool lod = leafLodFor(key);
            auto started = std::chrono::steady_clock::now();
            SectionMesh mesh = meshSection(around, models_, key.x * 16, kMinY + key.y * 16, key.z * 16, smoothLighting_, cutoutLeaves_, lod);
            addStageTime(kStageMesh, started);
            result.leafInterior = mesh.leafInterior;
            result.visibility = mesh.visibility;
            result.lodFar = lod;
            started = std::chrono::steady_clock::now();
            packMesh(result, std::move(mesh));
            addStageTime(kStagePack, started);
        }
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            meshResults_.push_back(std::move(result));
        }
        --meshing_;
    });
}

void WorldRenderer::requestMeshes(ChunkPos pos) {
    const Chunk* chunk = level_.chunk(pos);
    if (chunk == nullptr) return;
    const int dx = pos.x - playerChunkX_.load();
    const int dz = pos.z - playerChunkZ_.load();
    const float priority = static_cast<float>(dx * dx + dz * dz);
    for (int sy = 0; sy < kSectionCount; ++sy) {
        if (!chunk->sections[static_cast<std::size_t>(sy)]) continue;  // air
        const SectionKey key{pos.x, pos.z, sy};
        if (!meshRequested_.insert(key).second) continue;
        requestSectionMesh(key, priority);
    }
}

void WorldRenderer::acceptChunk(ChunkPos pos, std::shared_ptr<Chunk> chunk) {
    level_.putChunk(pos, std::move(chunk));
    // This chunk may complete the surroundings of itself and of its eight neighbours: they can be lit now.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) requestLight({pos.x + dx, pos.z + dz}, false);
    }
}

void WorldRenderer::applyLight(LightResult&& r) {
    lightRequested_.erase(r.pos);
    const auto stamp = lightStamps_.find(r.pos);
    Chunk* chunk = level_.chunk(r.pos);
    if (chunk == nullptr || stamp == lightStamps_.end() || stamp->second != r.stamp) return;  // gone, or a newer job is on its way
    const std::shared_ptr<const ChunkLight> old = chunk->light;
    chunk->light = r.light;
    if (!old) {  // the first light: this chunk and its neighbours may be meshable now
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkPos p{r.pos.x + dx, r.pos.z + dz};
                if (level_.chunk(p) != nullptr && neighborsLit(p)) requestMeshes(p);
            }
        }
        return;
    }
    // A new light for a chunk that was already drawn (after an edit): redo the sections whose light changed, and the ones
    // beside them (their faces read this section's light).
    const auto same = [&](int sy) {
        const auto s = static_cast<std::size_t>(sy);
        const auto equal = [](const std::shared_ptr<const NibbleArray>& a, std::uint8_t fa, const std::shared_ptr<const NibbleArray>& b, std::uint8_t fb) {
            if (!a && !b) return fa == fb;
            if (a && b) return a->data == b->data;
            return false;
        };
        return equal(old->sky[s], old->skyFill[s], r.light->sky[s], r.light->skyFill[s]) && equal(old->block[s], old->blockFill[s], r.light->block[s], r.light->blockFill[s]);
    };
    for (int sy = 0; sy < kSectionCount; ++sy) {
        if (same(sy)) continue;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const SectionKey key{r.pos.x + dx, r.pos.z + dz, sy + dy};
                    if (key.y < 0 || key.y >= kSectionCount || !meshRequested_.contains(key)) continue;
                    meshRequested_.insert(key);
                    requestSectionMesh(key, -1000.0f);
                }
            }
        }
    }
}

void WorldRenderer::dropSections(ChunkPos pos) {
    for (int sy = 0; sy < kSectionCount; ++sy) {
        const SectionKey key{pos.x, pos.z, sy};
        meshRequested_.erase(key);
        meshPending_.erase(key);
        meshGen_.erase(key);
        setGrid(key, nullptr, kVisibilityOpen);  // (unknown again: whatever comes to this place next starts open)
        if (const auto it = sections_.find(key); it != sections_.end()) {
            destroySection(it->second);
            sections_.erase(it);
        }
    }
    editStamps_.erase(pos);
    lightStamps_.erase(pos);
    lightRequested_.erase(pos);
}

void WorldRenderer::uploadMesh(MeshResult&& result) {
    const ChunkPos pos{result.key.x, result.key.z};
    if (level_.chunk(pos) == nullptr || result.stamp != stampOf(pos) || !meshRequested_.contains(result.key)) return;  // stale
    if (const auto g = meshGen_.find(result.key); g != meshGen_.end() && g->second != result.gen) return;           // a newer request is on its way
    meshPending_.erase(result.key);  // the latest request for this section is back (even if it has nothing to draw)

    auto existing = sections_.find(result.key);
    if (result.vertices.empty()) {  // nothing to see here any more
        if (existing != sections_.end()) {
            destroySection(existing->second);
            sections_.erase(existing);
        }
        setGrid(result.key, nullptr, result.visibility);  // (but how its faces connect still matters: air lets the view through, rock stops it)
        return;
    }
    GpuSection& s = existing != sections_.end() ? existing->second : sections_[result.key];
    setGrid(result.key, &s, result.visibility);
    meshBytes_ -= static_cast<std::size_t>(s.opaqueVertices + s.cutoutVertices + s.translucentVertices) * sizeof(MeshVertex);
    quadsTotal_ -= static_cast<std::size_t>(s.opaqueVertices + s.cutoutVertices + s.translucentVertices) / 4;
    const auto verts = static_cast<std::uint32_t>(result.vertices.size());
    // Reuse the section's arena space if it still fits; otherwise free it and take a fresh (bigger) region. A mesh that became much
    // smaller (a section that went far away lost its inner leaf faces) gives the extra room back too.
    if (s.arenaVerts < verts || (s.arenaVerts > 256 && verts + verts / 2 < s.arenaVerts)) {
        if (s.arenaVerts > 0) arenaFree(s.arenaBase, s.arenaVerts);
        s.arenaBase = arenaAllocate(verts);
        s.arenaVerts = verts;
    }
    // The mesh is written into a staging buffer (new storage each time, so nothing has to wait) and copied into the arena by the GPU, in order after
    // the draws already queued. Writing straight into the arena made the driver wait for every frame in flight that was still drawing from it: with
    // vsync that is one frame, without it (hundreds of fps) a queue of them, and each upload became a stall.
    const auto bytes = static_cast<gl::GLsizeiptr>(verts) * static_cast<gl::GLsizeiptr>(sizeof(MeshVertex));
    static const bool directUpload = std::getenv("FALCON_DIRECT_UPLOAD") != nullptr;  // (the old way, to compare)
    gl::BindBuffer(gl::ARRAY_BUFFER, arenaBuffer_);
    if (directUpload) {
        gl::BufferSubData(gl::ARRAY_BUFFER, static_cast<gl::GLintptr>(s.arenaBase) * static_cast<gl::GLintptr>(sizeof(MeshVertex)), bytes, result.vertices.data());
    } else {
        if (stagingBuffer_ == 0) gl::GenBuffers(1, &stagingBuffer_);
        gl::BindBuffer(gl::COPY_READ_BUFFER, stagingBuffer_);
        gl::BufferData(gl::COPY_READ_BUFFER, bytes, result.vertices.data(), gl::STREAM_DRAW);
        gl::CopyBufferSubData(gl::COPY_READ_BUFFER, gl::ARRAY_BUFFER, 0, static_cast<gl::GLintptr>(s.arenaBase) * static_cast<gl::GLintptr>(sizeof(MeshVertex)), bytes);
    }
    s.opaqueVertices = result.opaqueVertices;
    s.cutoutVertices = result.cutoutVertices;
    s.translucentVertices = result.translucentVertices;
    s.lodFar = result.lodFar;
    s.lodMatters = result.leafInterior;
    s.opaqueGroups = result.opaqueGroups;
    s.cutoutGroups = result.cutoutGroups;
    meshBytes_ += result.vertices.size() * sizeof(MeshVertex);
    quadsTotal_ += result.vertices.size() / 4;
}

void WorldRenderer::setLods(const LodSettings& settings) {
    lodSettings_ = settings;
    lodSettings_.enabled = settings.enabled && overworld_ != nullptr && !remote_;
    if (lodSettings_.enabled && !lod_) {
        std::vector<worldgen::LodTrees> trees;  // what each biome grows, so that the distant forests can be drawn
        if (decorator_) {
            trees.resize(overworld_->biomes().size());
            for (std::size_t i = 0; i < trees.size(); ++i) trees[i] = decorator_->treeFacts(static_cast<worldgen::BiomeId>(i));
        }
        lod_ = std::make_unique<LodRenderer>(overworld_, std::move(trees), pool_);
    }
    if (lod_) lod_->setSettings(lodSettings_);
}

void WorldRenderer::update(double playerX, double playerZ, int renderDistance, double uploadBudgetMs) {
    if (lod_ && lodSettings_.enabled) lod_->update(playerX, playerZ, renderDistance, uploadBudgetMs * 0.5);
    const int pcx = chunkOf(static_cast<int>(std::floor(playerX)));
    const int pcz = chunkOf(static_cast<int>(std::floor(playerZ)));
    playerChunkX_ = pcx;
    playerChunkZ_ = pcz;
    wantedRadius_ = renderDistance;
    const bool moved = pcx != lastChunkX_ || pcz != lastChunkZ_ || renderDistance != lastRadius_;
    lastChunkX_ = pcx;
    lastChunkZ_ = pcz;
    lastRadius_ = renderDistance;

    // Chunks the workers finished.
    std::vector<ChunkResult> done;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        done.swap(chunkResults_);
    }
    for (ChunkResult& r : done) {
        chunkRequested_.erase(r.pos);
        if (r.chunk == nullptr) continue;  // dropped: the player went elsewhere
        const int dx = r.pos.x - pcx;
        const int dz = r.pos.z - pcz;
        const int stream = streamRadius(renderDistance);
        if (dx * dx + dz * dz <= stream * stream) acceptChunk(r.pos, std::move(r.chunk));
    }

    // A server's chunks that came (and the ones it says to forget).
    if (remote_) {
        std::vector<std::pair<ChunkPos, std::shared_ptr<Chunk>>> arrived;
        std::vector<ChunkPos> forgotten;
        {
            std::lock_guard<std::mutex> lock(remoteMutex_);
            arrived.swap(remoteChunks_);
            forgotten.swap(remoteForgets_);
        }
        for (auto& [pos, chunk] : arrived) {
            if (chunk == nullptr) {  // forgetAllChunks: the old world is gone
                std::vector<ChunkPos> all;
                for (const auto& [loaded, held] : level_.chunks()) all.push_back(loaded);
                for (const ChunkPos& old : all) {
                    dropSections(old);
                    level_.removeChunk(old);
                }
                continue;
            }
            acceptChunk(pos, std::move(chunk));
        }
        for (const ChunkPos& pos : forgotten) {
            dropSections(pos);
            level_.removeChunk(pos);
        }
    }

    // Light the workers finished.
    std::vector<LightResult> lit;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        lit.swap(lightResults_);
    }
    for (LightResult& r : lit) applyLight(std::move(r));

    if (moved && !remote_) {
        // Ask for every missing chunk in range (a ring wider than the drawn one, so the drawn chunks have their
        // neighbours), nearest first; the pool keeps them in that order.
        // Light needs the ring around the drawn chunks, and meshing the ring after that: see streamRadius() for why this is
        // R + 3 (R + 2 left the loading area unfinishable at small render distances).
        const int generateRadius = streamRadius(renderDistance);
        for (const Offset& o : offsetsByDistance()) {
            if (o.d2 > generateRadius * generateRadius) break;
            const ChunkPos pos{pcx + o.dx, pcz + o.dz};
            if (level_.chunk(pos) == nullptr && !chunkRequested_.contains(pos)) requestChunk(pos, static_cast<float>(o.d2));
        }
        // Let go of what is now too far away.
        std::vector<ChunkPos> unload;
        const int keep = streamRadius(renderDistance) * streamRadius(renderDistance);
        for (const auto& [pos, chunk] : level_.chunks()) {
            const int dx = pos.x - pcx;
            const int dz = pos.z - pcz;
            if (dx * dx + dz * dz > keep) unload.push_back(pos);
        }
        for (const ChunkPos& pos : unload) {
            dropSections(pos);
            const auto found = level_.chunks().find(pos);
            std::shared_ptr<Chunk> gone = found != level_.chunks().end() ? found->second : nullptr;
            level_.removeChunk(pos);
            if (gone) {
                // Not thrown away: kept as a small compressed copy (the worker makes it), for when the player comes back.
                chunkCache_.hold(pos, std::move(gone));
                pool_.submit(1.0e9f, [this, pos] { chunkCache_.pack(pos); });
            }
        }
        chunkCache_.trim({pcx, pcz});
        refreshLeafDetail();
    }
    if (statTick_ % 90 == 0) arenaMaybeShrink();  // (every second or two: after a lower render distance the buffer gets smaller)

    // Finished meshes go to the GPU, a few per frame.
    const auto start = std::chrono::steady_clock::now();
    int uploads = 0;
    for (;;) {
        MeshResult result;
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            if (meshResults_.empty()) break;
            result = std::move(meshResults_.front());
            meshResults_.pop_front();
        }
        uploadMesh(std::move(result));
        ++uploads;
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= uploadBudgetMs) break;
    }

    stats_.chunksLoaded = static_cast<int>(level_.chunks().size());
    stats_.sectionsLoaded = static_cast<int>(sections_.size());
    stats_.quadsTotal = quadsTotal_;
    stats_.meshBytes = meshBytes_;
    if (statTick_++ % 20 == 0) {
        stats_.levelBytes = level_.memoryBytes();  // walks every chunk: not every frame
        stats_.cachedChunks = chunkCache_.count();
        stats_.cacheBytes = chunkCache_.packedBytes();
    }
    stats_.textureBytes = models_.gpuBytes();
    stats_.jobsWaiting = static_cast<int>(pool_.waiting());
    stats_.jobsInFlight = generating_.load() + meshing_.load() + lighting_.load();
    const auto stage = [this](Stage s) { return RenderStats::StageTime{static_cast<double>(stageNanos_[s].load()) / 1.0e6, stageJobs_[s].load()}; };
    stats_.terrainTime = stage(kStageTerrain);
    stats_.decorateTime = stage(kStageDecorate);
    stats_.lightTime = stage(kStageLight);
    stats_.meshTime = stage(kStageMesh);
    stats_.packTime = stage(kStagePack);
    stats_.uploadsThisFrame = uploads;
}

void WorldRenderer::receiveChunk(ChunkPos pos, std::shared_ptr<Chunk> chunk) {
    std::lock_guard<std::mutex> lock(remoteMutex_);
    remoteChunks_.emplace_back(pos, std::move(chunk));
}

void WorldRenderer::forgetAllChunks() {
    std::lock_guard<std::mutex> lock(remoteMutex_);
    remoteChunks_.emplace_back(ChunkPos{0, 0}, nullptr);  // (a chunk that is not there: everything before it goes)
}

void WorldRenderer::forgetChunk(ChunkPos pos) {
    std::lock_guard<std::mutex> lock(remoteMutex_);
    remoteForgets_.push_back(pos);
}

float WorldRenderer::loadProgress(int radius) const {
    const int pcx = playerChunkX_.load();
    const int pcz = playerChunkZ_.load();
    int total = 0;
    int ready = 0;
    for (const Offset& o : offsetsByDistance()) {
        if (o.d2 > radius * radius) break;
        ++total;
        const ChunkPos pos{pcx + o.dx, pcz + o.dz};
        if (level_.chunk(pos) != nullptr && neighborsLit(pos)) ++ready;
    }
    if (total == 0) return 1.0f;
    // Readiness is only about the spawn area (`radius`), not the whole render distance: the rest streams in after the player
    // spawns, as it does in the game. (Before, waiting for every chunk out to the render distance to finish stuck high render
    // distances near 90% for a long time.)
    return static_cast<float>(ready) / static_cast<float>(total);
}

ChunkLoadState WorldRenderer::chunkLoadState(ChunkPos pos) const {
    const Chunk* chunk = level_.chunk(pos);
    if (chunk == nullptr) return chunkRequested_.contains(pos) ? ChunkLoadState::Queued : ChunkLoadState::None;
    if (!chunk->light) return ChunkLoadState::Generated;
    return neighborsLit(pos) ? ChunkLoadState::Ready : ChunkLoadState::Lit;
}

bool WorldRenderer::sectionReady(int x, int y, int z) const {
    const ChunkPos pos{chunkOf(x), chunkOf(z)};
    const Chunk* chunk = level_.chunk(pos);
    if (chunk == nullptr) return false;
    const int sy = std::clamp((y - kMinY) >> 4, 0, kSectionCount - 1);
    if (!chunk->sections[static_cast<std::size_t>(sy)]) return true;  // air: nothing to draw
    const SectionKey key{pos.x, pos.z, sy};
    return meshRequested_.contains(key) && !meshPending_.contains(key);
}

}  // namespace falcon

