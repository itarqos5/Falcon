#include "falcon/render/world_renderer.hpp"

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
constexpr char kWorldVertexBody[] = R"(
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

constexpr char kWorldFragmentBody[] = R"(
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

// Which of the new ways of doing things are on. Each can be turned off with an environment variable, to compare the picture and the speed.
bool optionOff(const char* name) { return std::getenv(name) != nullptr; }

// The attribute layout of a MeshVertex for the vertex array and array buffer that are bound.
void bindVertexLayout() {
    // x, y, z, layer (4 shorts), u, v (2 shorts), tint + shade (4 normalised bytes), sky + block light + face (4 bytes)
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 4, gl::SHORT, 0, sizeof(MeshVertex), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::SHORT, 0, sizeof(MeshVertex), reinterpret_cast<const void*>(8));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, sizeof(MeshVertex), reinterpret_cast<const void*>(12));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 4, gl::UNSIGNED_BYTE, 1, sizeof(MeshVertex), reinterpret_cast<const void*>(16));
}

std::string worldShaderSource(const char* body) {
    std::string source = "#version 330 core\n";
    if (!optionOff("FALCON_NO_LIGHTLUT")) source += "#define LIGHT_LUT\n";
    if (!optionOff("FALCON_NO_FLATNORMAL")) source += "#define FLAT_NORMAL\n";
    source += body;
    return source;
}


// The most quads one section's mesh may hold (the shared index list covers this many).
constexpr std::size_t kMaxQuadsPerSection = 4096 * 12;

constexpr char kSkyVertex[] = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
)";

constexpr char kSkyFragment[] = R"(#version 330 core
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

constexpr char kLineVertex[] = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

constexpr char kLineFragment[] = R"(#version 330 core
out vec4 frag;
void main() { frag = vec4(0.0, 0.0, 0.0, 0.6); }
)";

// The plains sky: pale haze at the horizon, blue overhead (the game's daytime colours).
constexpr float kHorizon[3] = {0.753f, 0.847f, 1.0f};
constexpr float kZenith[3] = {0.471f, 0.655f, 1.0f};


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

// Puts the three lists of a mesh into one vertex array (opaque, cutout, translucent) and counts each. A mesh is never
// longer than the shared index list can address. The opaque and cutout quads are put in order of the way they face (east, west, up, down,
// south, north, then the ones at other angles) and each direction counted, so that the draw can leave out the directions that turn away
// from the camera: a face seen from behind is thrown away by the back-face culling anyway, after the vertex work has been done.
template <typename Result>
void packMesh(Result& result, SectionMesh&& mesh) {
    std::size_t room = kMaxQuadsPerSection * 4;
    const auto take = [&](std::vector<MeshVertex>& list) {
        const std::size_t n = std::min(list.size(), room) / 4 * 4;
        result.vertices.insert(result.vertices.end(), list.begin(), list.begin() + static_cast<std::ptrdiff_t>(n));
        room -= n;
        return static_cast<int>(n);
    };
    const auto takeSorted = [&](std::vector<MeshVertex>& list, const std::vector<std::uint8_t>& faces, std::array<int, kFacingGroups>& groups) {
        const std::size_t quads = list.size() / 4;
        const bool tagged = faces.size() == quads;
        std::array<std::size_t, kFacingGroups> count{};
        for (std::size_t q = 0; q < quads; ++q) ++count[tagged ? std::min<std::size_t>(faces[q], kFacingGroups - 1) : kFacingGroups - 1];
        std::array<std::size_t, kFacingGroups> next{};
        std::size_t at = 0;
        for (std::size_t g = 0; g < kFacingGroups; ++g) {
            next[g] = at;
            at += count[g];
        }
        std::vector<MeshVertex> sorted(list.size());
        for (std::size_t q = 0; q < quads; ++q) {
            const std::size_t g = tagged ? std::min<std::size_t>(faces[q], kFacingGroups - 1) : kFacingGroups - 1;
            const std::size_t to = next[g]++ * 4;
            const auto face = static_cast<std::uint8_t>(g < static_cast<std::size_t>(kFaceCount) ? g + 1 : 0);  // (the shader's carried-light lookup)
            for (std::size_t i = 0; i < 4; ++i) {
                sorted[to + i] = list[q * 4 + i];
                sorted[to + i].face = face;
            }
        }
        const int taken = take(sorted);
        // (If the list was cut short at the end, the last directions lose their share.)
        std::size_t left = static_cast<std::size_t>(taken);
        for (std::size_t g = 0; g < kFacingGroups; ++g) {
            const std::size_t verts = std::min(count[g] * 4, left);
            groups[g] = static_cast<int>(verts);
            left -= verts;
        }
        return taken;
    };
    result.vertices.reserve(mesh.vertexCount());
    result.opaqueVertices = takeSorted(mesh.opaque, mesh.opaqueFace, result.opaqueGroups);
    result.cutoutVertices = takeSorted(mesh.cutout, mesh.cutoutFace, result.cutoutGroups);
    result.translucentVertices = take(mesh.translucent);
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
                chunk = overworld_->generate(pos.x, pos.z, extras);
                if (decorator_) decorator_->decorate(*chunk, pos.x, pos.z, extras);
                auto grid = std::make_shared<BiomeGrid>();
                std::copy(extras.biomes.ids.begin(), extras.biomes.ids.end(), grid->ids.begin());
                chunk->biomes = std::move(grid);
            } else {
                chunk = generator_.generate(pos.x, pos.z);
            }
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
        LightResult result{pos, stamp, computeChunkLight(input)};
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
            SectionMesh mesh = meshSection(around, models_, key.x * 16, kMinY + key.y * 16, key.z * 16, smoothLighting_, cutoutLeaves_, lod);
            result.leafInterior = mesh.leafInterior;
            result.visibility = mesh.visibility;
            result.lodFar = lod;
            packMesh(result, std::move(mesh));
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

void WorldRenderer::update(double playerX, double playerZ, int renderDistance, double uploadBudgetMs) {
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

    // Light the workers finished.
    std::vector<LightResult> lit;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        lit.swap(lightResults_);
    }
    for (LightResult& r : lit) applyLight(std::move(r));

    if (moved) {
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
    stats_.uploadsThisFrame = uploads;
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

void WorldRenderer::render(const Camera& camera, int framebufferWidth, int framebufferHeight, int renderDistance) {
    measureGpu(true);
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
    const float farPlane = std::max(256.0f, static_cast<float>(renderDistance + 4) * 16.0f * 1.5f);
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
    if (underwater_) {  // no sky from under water: the whole view is the water's fog colour
        gl::Uniform3f(skyHorizon_, waterFogColor_[0], waterFogColor_[1], waterFogColor_[2]);
        gl::Uniform3f(skyZenith_, waterFogColor_[0], waterFogColor_[1], waterFogColor_[2]);
    } else {
        gl::Uniform3f(skyHorizon_, kHorizon[0], kHorizon[1], kHorizon[2]);
        gl::Uniform3f(skyZenith_, kZenith[0], kZenith[1], kZenith[2]);
    }
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);

    // The blocks.
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::FrontFace(gl::CCW);
    world_.use();
    gl::UniformMatrix4fv(uViewProj_, 1, 0, viewProj.m.data());
    const float fogEnd = static_cast<float>(renderDistance) * 16.0f;
    if (underwater_) {
        gl::Uniform3f(uFogColor_, waterFogColor_[0], waterFogColor_[1], waterFogColor_[2]);
        gl::Uniform2f(uEnvFog_, waterFogStart_, waterFogEnd_);
    } else {
        gl::Uniform3f(uFogColor_, kHorizon[0], kHorizon[1], kHorizon[2]);
        gl::Uniform2f(uEnvFog_, 1.0e9f, 2.0e9f);  // no medium fog
    }
    gl::Uniform2f(uFogRange_, fogEnd * 0.6f, fogEnd);
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
    if (lightmapBrightness_ != brightness_) updateLightmap();
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmapTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(uLightmap_, 2);
    gl::Uniform1f(uInstanced_, 0.0f);

    const Frustum frustum = extractFrustum(viewProj);
    const float reach = (static_cast<float>(renderDistance) + 1.0f) * 16.0f + 16.0f;
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
            std::printf("[falcon] OpenGL error 0x%04X after drawing the world (%s)\n", error, useMultiDraw_ ? "multi-draw" : "one draw per section");
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
        if (!frustum.mayContain({ox, oy, oz}, {ox + 16.0f, oy + 16.0f, oz + 16.0f})) continue;
        const float cy = oy + 8.0f;
        visible_.push_back({&section, ox, oy, oz, horizontal2 + cy * cy});
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
    const std::uint32_t startCell = static_cast<std::uint32_t>(gridIndex(cameraChunkX, cameraSection, cameraChunkZ));
    gridStamp_[startCell] = traversalStamp_;
    gridIncoming_[startCell] = 0;
    gridTravelled_[startCell] = 0;
    traversal_.push_back({startCell, cameraChunkX, cameraChunkZ, static_cast<std::int8_t>(cameraSection)});

    for (std::size_t head = 0; head < traversal_.size(); ++head) {
        const TraversalNode node = traversal_[head];
        // A section that has a mesh, and is in view, is drawn.
        if (const GpuSection* section = gridSection_[node.cell]; section != nullptr) {
            const float ox = static_cast<float>(node.x * 16 - camera.x);
            const float oy = static_cast<float>(kMinY + node.y * 16 - camera.y);
            const float oz = static_cast<float>(node.z * 16 - camera.z);
            if (head != 0 || frustum.mayContain({ox, oy, oz}, {ox + 16.0f, oy + 16.0f, oz + 16.0f})) {  // (the others passed when they were reached)
                const float cx = ox + 8.0f, cy = oy + 8.0f, cz = oz + 8.0f;
                visible_.push_back({section, ox, oy, oz, cx * cx + cy * cy + cz * cz});
            }
        }
        // Which faces it can be left through.
        const unsigned travelled = gridTravelled_[node.cell];
        unsigned outgoing = 0x3F;
        if (head != 0) {
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
                traversal_.push_back({cell, nx, nz, static_cast<std::int8_t>(ny)});
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
            const float skyBrightness = sky / (4.0f - 3.0f * sky);
            const float parabolic = (2.0f * blk - 1.0f) * (2.0f * blk - 1.0f);
            const float tint = 0.9f * parabolic;  // mix(vec3(1, 0.84705883, 0.54901963), vec3(1), tint)
            const float mixed[3] = {1.0f, 0.84705883f + (1.0f - 0.84705883f) * tint, 0.54901963f + (1.0f - 0.54901963f) * tint};
            float color[3];
            for (int c = 0; c < 3; ++c) color[c] = std::clamp(skyBrightness + mixed[c] * blockBrightness, 0.0f, 1.0f);
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
}

void WorldRenderer::drawSelection(const Camera& camera, int framebufferWidth, int framebufferHeight, int x, int y, int z) {
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(std::max(1, framebufferHeight));
    const float farPlane = std::max(256.0f, static_cast<float>(lastRadius_ + 4) * 16.0f * 1.5f);
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
    lines.reserve(outline.edges.size() * 6);
    const double origin[3] = {x - camera.x + offsetX, y - camera.y, z - camera.z + offsetZ};
    for (const std::array<float, 6>& edge : outline.edges) {
        for (int end = 0; end < 2; ++end) {
            for (int a = 0; a < 3; ++a) {
                const float p = edge[static_cast<std::size_t>(end * 3 + a)];
                const float pushed = p > center[a] + 1.0e-4f ? p + e : p < center[a] - 1.0e-4f ? p - e : p;
                lines.push_back(static_cast<float>(origin[a] + static_cast<double>(pushed)));
            }
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
    gl::DrawArrays(gl::LINES, 0, static_cast<gl::GLsizei>(lines.size() / 3));
    gl::BindVertexArray(0);
    gl::Disable(gl::BLEND);
}

void WorldRenderer::endFrame() {
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    gl::DepthMask(gl::TRUE_);
    gl::BindVertexArray(0);
    measureGpu(false);
}

void WorldRenderer::meshNow(const SectionKey& key) {
    const ChunkPos pos{key.x, key.z};
    if (key.y < 0 || key.y >= kSectionCount || level_.chunk(pos) == nullptr || !neighborsLit(pos)) return;
    MeshResult result{key, stampOf(pos), {}, 0, 0, 0, ++meshGen_[key]};
    if (level_.chunk(pos)->sections[static_cast<std::size_t>(key.y)]) {
        const bool lod = leafLodFor(key);
        SectionMesh mesh = meshSection(snapshotNeighborhood(level_, key.x, key.y, key.z), models_, key.x * 16, kMinY + key.y * 16, key.z * 16, smoothLighting_, cutoutLeaves_, lod);
        result.leafInterior = mesh.leafInterior;
        result.visibility = mesh.visibility;
        result.lodFar = lod;
        packMesh(result, std::move(mesh));
    }
    meshRequested_.insert(key);
    uploadMesh(std::move(result));
}

bool WorldRenderer::setBlock(int x, int y, int z, Block block) {
    if (!level_.setBlock(x, y, z, block)) return false;
    ++blockEdits_;  // the carried light may now reach round a corner, or stop at a wall
    const int lx = x & 15;
    const int lz = z & 15;
    const int ly = (y - kMinY) & 15;
    const int cx = chunkOf(x);
    const int cz = chunkOf(z);
    const int sy = sectionOf(y);
    // The edited section and, when the block is on a border, the section next to it: their faces change too.
    std::vector<SectionKey> touched = {{cx, cz, sy}};
    if (lx == 0) touched.push_back({cx - 1, cz, sy});
    if (lx == 15) touched.push_back({cx + 1, cz, sy});
    if (lz == 0) touched.push_back({cx, cz - 1, sy});
    if (lz == 15) touched.push_back({cx, cz + 1, sy});
    if (ly == 0) touched.push_back({cx, cz, sy - 1});
    if (ly == 15) touched.push_back({cx, cz, sy + 1});
    for (const SectionKey& k : touched) ++editStamps_[{k.x, k.z}];  // any mesh still being made from older data is stale
    for (const SectionKey& k : touched) meshNow(k);
    // The light changes too (a torch, a roof, a hole in the ground): redo it for the chunk and the ones around it.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) requestLight({cx + dx, cz + dz}, true);
    }
    return true;
}

}  // namespace falcon

namespace falcon {

TerrainGenerator::Spawn WorldRenderer::findSpawn() const {
    if (overworld_) {
        const worldgen::OverworldGenerator::Spawn s = overworld_->findSpawn();
        return {s.x, s.y, s.z};
    }
    return generator_.findSpawn();
}

int WorldRenderer::surfaceHeight(int x, int z) const { return overworld_ ? overworld_->surfaceHeightAt(x, z) : generator_.surfaceHeight(x, z); }

}  // namespace falcon

namespace falcon {

void WorldRenderer::lightAt(int x, int y, int z, int& sky, int& block) const {
    sky = 15;
    block = 0;
    const Chunk* chunk = level_.chunk({chunkOf(x), chunkOf(z)});
    if (chunk == nullptr || !chunk->light) return;
    sky = chunk->light->skyAt(x & 15, y, z & 15);
    block = chunk->light->blockAt(x & 15, y, z & 15);
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
    if (lightmapBrightness_ != brightness_) updateLightmap();
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
