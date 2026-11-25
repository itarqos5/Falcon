// The Vulkan shader twins (src/vulkan/shaders/, compiled into spirv_data.cpp): every program the game has must be there, with the uniforms, samplers and vertex inputs the GLSL 330
// shader of the game declares, and with the two stages' uniform blocks laid out alike. No graphics card is needed: the SPIR-V is only read.
#include "../src/vulkan/shader_registry.hpp"
#include "../src/vulkan/spirv_data.hpp"
#include "../src/vulkan/spirv_reflect.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
#define CHECK(expr)                                                                      \
    do {                                                                                 \
        if (!(expr)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                                \
        }                                                                                \
    } while (false)

using falcon::vulkan::SpirvBlob;
using falcon::vulkan::SpirvInfo;

struct Sampler {
    const char* name;
    int kind;
    std::uint32_t binding;
};
struct Input {
    std::uint32_t location;
    std::uint32_t components;
};
struct Expected {
    const char* program;
    std::vector<const char*> uniforms;
    std::vector<Sampler> samplers;
    std::vector<Input> inputs;
};

const SpirvBlob* find(const std::string& name) {
    for (std::size_t i = 0; i < falcon::vulkan::kSpirvBlobCount; ++i) {
        if (name == falcon::vulkan::kSpirvBlobs[i].name) return &falcon::vulkan::kSpirvBlobs[i];
    }
    return nullptr;
}

// The programs of the game, as their GLSL 330 shaders declare them (kinds: 0 2D, 1 2D array, 2 3D, 3 cube).
const std::vector<Expected>& programs() {
    static const std::vector<Expected> list = {
        {"gui", {"uProjection", "uCutoff"}, {{"uTexture", 0, 0}}, {{0, 2}, {1, 2}, {2, 4}}},
        {"sky", {"uForward", "uRight", "uUp", "uTan", "uHorizon", "uZenith", "uSunDir", "uAngle", "uStars", "uGlow", "uMoon"}, {{"uCelestial", 0, 0}}, {}},
        {"lines", {"uViewProj"}, {}, {{0, 3}, {1, 4}}},
        {"lod", {"uViewProj", "uOffset", "uCell", "uFogColor", "uFogRange", "uEnvFog", "uHoleOrigin", "uHoleRadius2"}, {{"uTex", 1, 0}, {"uLightmap", 0, 2}}, {{0, 4}, {1, 4}}},
        {"cracks", {"uViewProj"}, {{"uTex", 0, 0}}, {{0, 3}, {1, 2}}},
        {"world",
         {"uViewProj", "uOffset", "uInstanced", "uFogColor", "uFogRange", "uEnvFog", "uAlphaCut", "uGamma", "uHeldOrigin", "uHeldOn"},
         {{"uTex", 1, 0}, {"uHeldLight", 2, 1}, {"uLightmap", 0, 2}},
         {{0, 4}, {1, 2}, {2, 4}, {3, 4}, {4, 3}}},
        {"entity", {"uProj", "uLight", "uTint", "uHurt"}, {{"uSkin", 0, 0}}, {{0, 3}, {1, 2}, {2, 1}}},
        {"arm", {"uProj", "uLight"}, {{"uSkin", 0, 0}}, {{0, 3}, {1, 2}, {2, 1}}},
        {"panorama", {"uProjection", "uView"}, {{"uSky", 3, 0}}, {{0, 3}}},
        {"blur", {"uInSize", "uBlurDir", "uRadius"}, {{"uInput", 0, 0}}, {}},
        {"sprite",
         {"uViewProj", "uFogColor", "uFogRange", "uFullbright"},
         {{"uPicture", 0, 0}, {"uPictures", 1, 1}, {"uLightmap", 0, 2}},
         {{0, 3}, {1, 2}, {2, 4}, {3, 2}}},
    };
    return list;
}

void testProgram(const Expected& e) {
    const SpirvBlob* vs = find(std::string(e.program) + ".vert");
    const SpirvBlob* fs = find(std::string(e.program) + ".frag");
    CHECK(vs != nullptr && fs != nullptr);
    if (vs == nullptr || fs == nullptr) return;
    SpirvInfo v, f;
    std::string error;
    CHECK(falcon::vulkan::reflectSpirv(vs->words, vs->wordCount, v, error));
    CHECK(falcon::vulkan::reflectSpirv(fs->words, fs->wordCount, f, error));

    // The block: the members of the two stages together (a stage that does not use the block may leave it out), at the same places where both have a member.
    std::map<std::string, std::uint32_t> offsets;
    for (const auto* info : {&v, &f}) {
        for (const SpirvInfo::Member& m : info->block) {
            const auto it = offsets.find(m.name);
            if (it != offsets.end()) CHECK(it->second == m.offset);
            offsets[m.name] = m.offset;
        }
    }
    std::set<std::string> want(e.uniforms.begin(), e.uniforms.end()), have;
    for (const auto& [name, offset] : offsets) have.insert(name);
    CHECK(want == have);
    CHECK(v.blockSize <= 1024u && f.blockSize <= 1024u);

    // The samplers are the fragment shader's, at the bindings the program's texture units are mapped through.
    CHECK(f.samplers.size() == e.samplers.size());
    for (const Sampler& s : e.samplers) {
        bool found = false;
        for (const SpirvInfo::Sampler& g : f.samplers) {
            if (s.name == g.name) {
                found = true;
                CHECK(g.kind == s.kind && g.binding == s.binding);
            }
        }
        CHECK(found);
    }
    CHECK(v.samplers.empty());

    // The vertex inputs: the locations and sizes the vertex arrays of the renderers set up.
    CHECK(v.inputs.size() == e.inputs.size());
    for (const Input& in : e.inputs) {
        bool found = false;
        for (const SpirvInfo::Input& g : v.inputs) {
            if (g.location == in.location) {
                found = true;
                CHECK(g.components == in.components);
            }
        }
        CHECK(found);
    }
}

// std140 puts a float in the room a vec3 leaves, which is what lets the world's uniforms stay small, and the world shader's block is the one the CPU side fills in by name.
void testWorldBlockLayout() {
    SpirvInfo v;
    std::string error;
    const SpirvBlob* vs = find("world.vert");
    CHECK(vs != nullptr && falcon::vulkan::reflectSpirv(vs->words, vs->wordCount, v, error));
    std::map<std::string, std::uint32_t> at;
    for (const SpirvInfo::Member& m : v.block) at[m.name] = m.offset;
    CHECK(at["uViewProj"] == 0 && at["uOffset"] == 64 && at["uInstanced"] == 76);
    CHECK(at["uHeldOn"] == at["uHeldOrigin"] + 12);
}

void testTwinsAreFound() {
    std::uint32_t spec = 0;
    std::string why;
    CHECK(falcon::vulkan::findTwin(true, "// vk: gui.vert\n#version 330 core\n", spec, why) == find("gui.vert") && spec == 0);
    CHECK(falcon::vulkan::findTwin(false, "// vk: gui.frag\n#version 330 core\n", spec, why) == find("gui.frag"));
    CHECK(falcon::vulkan::findTwin(true, "#version 330 core\n#define LIGHT_LUT\n#define FLAT_NORMAL\n// vk: world.vert\n", spec, why) == find("world.vert") &&
          spec == (falcon::vulkan::kSpecLightLut | falcon::vulkan::kSpecFlatNormal));
    CHECK(falcon::vulkan::findTwin(true, "#version 330 core\n// vk: world.vert\n", spec, why) == find("world.vert") && spec == 0);
    CHECK(falcon::vulkan::findTwin(true, "#version 330 core\nvoid main() {}\n", spec, why) == nullptr && !why.empty());  // no tag
    CHECK(falcon::vulkan::findTwin(false, "// vk: gui.vert\n", spec, why) == nullptr);                                  // a vertex twin asked for as a fragment shader
    CHECK(falcon::vulkan::findTwin(true, "// vk: nonsense.vert\n", spec, why) == nullptr);
}

}  // namespace

int main() {
    std::printf("vulkan shader tests\n");
    CHECK(falcon::vulkan::kSpirvBlobCount == 2 * programs().size());
    for (const Expected& e : programs()) testProgram(e);
    testWorldBlockLayout();
    testTwinsAreFound();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all vulkan shader tests passed\n");
    return 0;
}
