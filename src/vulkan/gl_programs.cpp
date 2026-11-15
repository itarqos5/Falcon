#include "gl_state.hpp"

#include "shader_registry.hpp"
#include "spirv_data.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace falcon::vulkan {

namespace {

constexpr GLenum kByte = 0x1400;

std::map<GLuint, std::unordered_map<std::string, VkPipeline>>& pipelineCache() {
    static std::map<GLuint, std::unordered_map<std::string, VkPipeline>> cache;
    return cache;
}

template <typename T>
void put(std::string& key, const T& value) {
    key.append(reinterpret_cast<const char*>(&value), sizeof value);
}

std::uint32_t typeSize(GLenum type) {
    switch (type) {
        case gl::FLOAT: return 4;
        case gl::SHORT:
        case gl::UNSIGNED_SHORT: return 2;
        default: return 1;
    }
}

// What a vertex attribute reads as: the numbers of the game's meshes are bytes (as colours, 0..1), 16-bit integers (as plain numbers) and floats.
VkFormat attribFormat(GLenum type, int size, bool normalized) {
    const int i = std::clamp(size, 1, 4) - 1;
    static const VkFormat floats[4] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
    static const VkFormat ubyteNorm[4] = {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    static const VkFormat ubyteScaled[4] = {VK_FORMAT_R8_USCALED, VK_FORMAT_R8G8_USCALED, VK_FORMAT_R8G8B8_USCALED, VK_FORMAT_R8G8B8A8_USCALED};
    static const VkFormat byteNorm[4] = {VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM};
    static const VkFormat byteScaled[4] = {VK_FORMAT_R8_SSCALED, VK_FORMAT_R8G8_SSCALED, VK_FORMAT_R8G8B8_SSCALED, VK_FORMAT_R8G8B8A8_SSCALED};
    static const VkFormat shortNorm[4] = {VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM};
    static const VkFormat shortScaled[4] = {VK_FORMAT_R16_SSCALED, VK_FORMAT_R16G16_SSCALED, VK_FORMAT_R16G16B16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED};
    static const VkFormat ushortNorm[4] = {VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM};
    static const VkFormat ushortScaled[4] = {VK_FORMAT_R16_USCALED, VK_FORMAT_R16G16_USCALED, VK_FORMAT_R16G16B16_USCALED, VK_FORMAT_R16G16B16A16_USCALED};
    switch (type) {
        case gl::UNSIGNED_BYTE: return normalized ? ubyteNorm[i] : ubyteScaled[i];
        case kByte: return normalized ? byteNorm[i] : byteScaled[i];
        case gl::SHORT: return normalized ? shortNorm[i] : shortScaled[i];
        case gl::UNSIGNED_SHORT: return normalized ? ushortNorm[i] : ushortScaled[i];
        default: return floats[i];
    }
}

VkBlendFactor blendFactorOf(GLenum f) {
    switch (f) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x0300: return VK_BLEND_FACTOR_SRC_COLOR;
        case 0x0301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x0302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x0303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x0304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x0305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x0306: return VK_BLEND_FACTOR_DST_COLOR;
        case 0x0307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

struct VertexLayout {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attribs;
    VertexBindings buffers;
    std::string signature;
};

// The vertex input a draw has: for each input the vertex shader reads, the attribute of the vertex array (or the constant (0, 0, 0, 1) when it has none), with attributes that
// share a buffer, a stride and a divisor in one binding.
bool buildVertexLayout(const GlProgram& program, const GlVao& vao, VertexLayout& out) {
    GlState& s = gls();
    struct BindingKey {
        GLuint buffer;
        GLsizei stride;
        GLuint divisor;
    };
    std::vector<BindingKey> keys;
    for (const SpirvInfo::Input& in : program.inputs) {
        const VertexAttrib* a = in.location < kMaxAttribs ? &vao.attribs[in.location] : nullptr;
        const bool usable = a != nullptr && a->enabled && bufferPtr(a->buffer) != nullptr;
        BindingKey key{};
        VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
        std::uint32_t offset = 0;
        if (usable) {
            key = {a->buffer, a->stride != 0 ? a->stride : static_cast<GLsizei>(static_cast<std::uint32_t>(a->size) * typeSize(a->type)), a->divisor};
            format = attribFormat(a->type, a->size, a->normalized);
            offset = static_cast<std::uint32_t>(a->offset);
        } else {
            key = {0, 0, 0};  // (nothing is read from the array: the constant attribute, the same for every vertex)
        }
        std::uint32_t binding = 0;
        for (; binding < keys.size(); ++binding) {
            if (keys[binding].buffer == key.buffer && keys[binding].stride == key.stride && keys[binding].divisor == key.divisor) break;
        }
        if (binding == keys.size()) {
            keys.push_back(key);
            VkVertexInputBindingDescription b{};
            b.binding = binding;
            b.stride = static_cast<std::uint32_t>(key.stride);
            b.inputRate = key.divisor > 0 ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            out.bindings.push_back(b);
            if (key.buffer != 0) {
                const BufferBacking backing = backingOf(key.buffer);
                out.buffers.buffers[binding] = backing.buffer;
                out.buffers.offsets[binding] = backing.offset;
            } else {
                out.buffers.buffers[binding] = s.zeroBuffer.buffer;
                out.buffers.offsets[binding] = 0;
            }
            if (binding + 1 >= static_cast<std::uint32_t>(kMaxAttribs)) return false;
        }
        out.attribs.push_back({in.location, binding, format, offset});
    }
    out.buffers.count = static_cast<std::uint32_t>(keys.size());
    for (const auto& b : out.bindings) {
        put(out.signature, b.binding);
        put(out.signature, b.stride);
        put(out.signature, b.inputRate);
    }
    for (const auto& a : out.attribs) {
        put(out.signature, a.location);
        put(out.signature, a.binding);
        put(out.signature, a.format);
        put(out.signature, a.offset);
    }
    return true;
}

VkPipeline createPipeline(const PipelineRequest& r, const VertexLayout& layout) {
    GlState& s = gls();
    Core& c = core();
    const GlProgram& p = *r.program;
    const Target& target = *r.target;

    VkSpecializationMapEntry entries[2] = {{0, 0, sizeof(VkBool32)}, {1, sizeof(VkBool32), sizeof(VkBool32)}};
    const VkBool32 spec[2] = {(p.specMask & kSpecLightLut) != 0 ? VK_TRUE : VK_FALSE, (p.specMask & kSpecFlatNormal) != 0 ? VK_TRUE : VK_FALSE};
    VkSpecializationInfo specInfo{2, entries, sizeof spec, spec};

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = p.vertex;
    stages[0].pName = "main";
    stages[0].pSpecializationInfo = &specInfo;
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p.fragment;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = &specInfo;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(layout.bindings.size());
    vertexInput.pVertexBindingDescriptions = layout.bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(layout.attribs.size());
    vertexInput.pVertexAttributeDescriptions = layout.attribs.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = r.topology;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // The targets are drawn with an ordinary viewport, which turns the picture over relative to what OpenGL's winding means: counter-clockwise there is clockwise here.
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = s.cullFace ? (s.cullMode == 0x0404 ? VK_CULL_MODE_FRONT_BIT : (s.cullMode == 0x0408 ? VK_CULL_MODE_FRONT_AND_BACK : VK_CULL_MODE_BACK_BIT)) : VK_CULL_MODE_NONE;
    raster.frontFace = s.frontFace == 0x0901 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    const bool hasDepth = target.depthImage != VK_NULL_HANDLE;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = hasDepth && s.depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = hasDepth && s.depthTest && s.depthMask ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = static_cast<VkCompareOp>(std::clamp<int>(static_cast<int>(s.depthFunc) - 0x0200, 0, 7));

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = s.blend ? VK_TRUE : VK_FALSE;
    blend.srcColorBlendFactor = blendFactorOf(s.blendSrcRgb);
    blend.dstColorBlendFactor = blendFactorOf(s.blendDstRgb);
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = blendFactorOf(s.blendSrcAlpha);
    blend.dstAlphaBlendFactor = blendFactorOf(s.blendDstAlpha);
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;

    const VkDynamicState dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamic;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &target.colorFormat;
    rendering.depthAttachmentFormat = hasDepth ? target.depthFormat : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blendState;
    info.pDynamicState = &dynamicState;
    info.layout = c.pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = api::vkCreateGraphicsPipelines(c.device, c.pipelineCache, 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan: vkCreateGraphicsPipelines failed (") + api::resultName(result) + ")");
    return pipeline;
}

VkShaderModule makeModule(const SpirvBlob& blob) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = blob.wordCount * 4;
    info.pCode = blob.words;
    VkShaderModule module = VK_NULL_HANDLE;
    if (api::vkCreateShaderModule(core().device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

GlShader* shaderPtr(GLuint id) {
    auto& all = gls().shaders;
    return id != 0 && id < all.size() && all[id].exists ? &all[id] : nullptr;
}

void releaseProgram(GlProgram& p, GLuint id) {
    if (p.vertex != VK_NULL_HANDLE || p.fragment != VK_NULL_HANDLE) {
        const VkShaderModule v = p.vertex, f = p.fragment;
        std::vector<VkPipeline> pipelines;
        auto& cache = pipelineCache();
        if (const auto it = cache.find(id); it != cache.end()) {
            for (const auto& [key, pipeline] : it->second) pipelines.push_back(pipeline);
            cache.erase(it);
        }
        defer([v, f, pipelines] {
            Core& c = core();
            for (const VkPipeline pipeline : pipelines) api::vkDestroyPipeline(c.device, pipeline, nullptr);
            if (v != VK_NULL_HANDLE) api::vkDestroyShaderModule(c.device, v, nullptr);
            if (f != VK_NULL_HANDLE) api::vkDestroyShaderModule(c.device, f, nullptr);
        });
    }
    p = GlProgram{};
}

// Writes uniform values into the program's block (and notes that they changed).
void writeUniform(GLint location, const void* data, std::uint32_t bytes) {
    GlState& s = gls();
    GlProgram* p = programPtr(s.program);
    if (p == nullptr || location < 0 || static_cast<std::size_t>(location) >= p->uniforms.size()) return;
    const SpirvInfo::Member& m = p->uniforms[static_cast<std::size_t>(location)];
    if (m.offset + bytes > p->block.size()) return;
    if (std::memcmp(p->block.data() + m.offset, data, bytes) == 0) return;
    std::memcpy(p->block.data() + m.offset, data, bytes);
    p->blockDirty = true;
}

}  // namespace

GlProgram* programPtr(GLuint id) {
    auto& all = gls().programs;
    return id != 0 && id < all.size() && all[id].exists ? &all[id] : nullptr;
}

VkPipeline pipelineFor(const PipelineRequest& request, VertexBindings& bindings) {
    VertexLayout layout;
    if (!buildVertexLayout(*request.program, *request.vao, layout)) return VK_NULL_HANDLE;
    bindings = layout.buffers;
    GlState& s = gls();
    const GLuint programId = s.program;
    std::string key = layout.signature;
    put(key, static_cast<std::uint32_t>(request.topology));
    put(key, static_cast<std::uint8_t>(s.blend));
    put(key, static_cast<std::uint32_t>(s.blendSrcRgb));
    put(key, static_cast<std::uint32_t>(s.blendDstRgb));
    put(key, static_cast<std::uint32_t>(s.blendSrcAlpha));
    put(key, static_cast<std::uint32_t>(s.blendDstAlpha));
    const bool hasDepth = request.target->depthImage != VK_NULL_HANDLE;
    put(key, static_cast<std::uint8_t>(hasDepth && s.depthTest));
    put(key, static_cast<std::uint8_t>(hasDepth && s.depthTest && s.depthMask));
    put(key, static_cast<std::uint32_t>(s.depthFunc));
    put(key, static_cast<std::uint8_t>(s.cullFace));
    put(key, static_cast<std::uint32_t>(s.cullMode));
    put(key, static_cast<std::uint32_t>(s.frontFace));
    put(key, static_cast<std::uint32_t>(request.target->colorFormat));
    put(key, static_cast<std::uint32_t>(hasDepth ? request.target->depthFormat : VK_FORMAT_UNDEFINED));
    auto& perProgram = pipelineCache()[programId];
    const auto it = perProgram.find(key);
    if (it != perProgram.end()) return it->second;
    static const bool trace = std::getenv("FALCON_VK_TRACE") != nullptr;  // FALCON_VK_TRACE=1: every pipeline made, with how long it took
    static int made = 0;
    const auto started = std::chrono::steady_clock::now();
    const VkPipeline pipeline = createPipeline(request, layout);
    if (trace) {
        std::fprintf(stderr, "[vulkan] pipeline %d (program %u, %zu vertex bindings) made in %.2f ms\n", ++made, static_cast<unsigned>(programId), layout.bindings.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    perProgram.emplace(std::move(key), pipeline);
    return pipeline;
}

void destroyProgramObjects() {
    Core& c = core();
    for (auto& [id, pipelines] : pipelineCache()) {
        for (auto& [key, pipeline] : pipelines) api::vkDestroyPipeline(c.device, pipeline, nullptr);
    }
    pipelineCache().clear();
    for (GlProgram& p : gls().programs) {
        if (p.vertex != VK_NULL_HANDLE) api::vkDestroyShaderModule(c.device, p.vertex, nullptr);
        if (p.fragment != VK_NULL_HANDLE) api::vkDestroyShaderModule(c.device, p.fragment, nullptr);
        p.vertex = VK_NULL_HANDLE;
        p.fragment = VK_NULL_HANDLE;
    }
}

namespace glemu {

GLuint CreateShader(GLenum type) {
    auto& all = gls().shaders;
    all.emplace_back();
    all.back().exists = true;
    all.back().type = type;
    return static_cast<GLuint>(all.size() - 1);
}

void ShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    GlShader* sh = shaderPtr(shader);
    if (sh == nullptr) return;
    sh->source.clear();
    for (GLsizei i = 0; i < count; ++i) {
        if (lengths != nullptr && lengths[i] >= 0) sh->source.append(strings[i], static_cast<std::size_t>(lengths[i]));
        else sh->source.append(strings[i]);
    }
}

void CompileShader(GLuint shader) {
    GlShader* sh = shaderPtr(shader);
    if (sh == nullptr) return;
    std::string why;
    sh->twin = findTwin(sh->type == gl::VERTEX_SHADER, sh->source, sh->specMask, why);
    sh->compiled = sh->twin != nullptr;
    sh->log = sh->compiled ? std::string() : why;
}

void GetShaderiv(GLuint shader, GLenum name, GLint* out) {
    GlShader* sh = shaderPtr(shader);
    if (sh == nullptr || out == nullptr) return;
    if (name == gl::COMPILE_STATUS) *out = sh->compiled ? 1 : 0;
    else if (name == gl::INFO_LOG_LENGTH) *out = static_cast<GLint>(sh->log.size() + 1);
}

void GetShaderInfoLog(GLuint shader, GLsizei size, GLsizei* length, GLchar* log) {
    GlShader* sh = shaderPtr(shader);
    if (sh == nullptr || log == nullptr || size <= 0) return;
    const std::size_t n = std::min(sh->log.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(log, sh->log.data(), n);
    log[n] = '\0';
    if (length != nullptr) *length = static_cast<GLsizei>(n);
}

void DeleteShader(GLuint shader) {
    if (GlShader* sh = shaderPtr(shader)) *sh = GlShader{};
}

GLuint CreateProgram() {
    auto& all = gls().programs;
    all.emplace_back();
    all.back().exists = true;
    return static_cast<GLuint>(all.size() - 1);
}

void AttachShader(GLuint program, GLuint shader) {
    if (GlProgram* p = programPtr(program)) p->attached.push_back(shader);
}

void LinkProgram(GLuint program) {
    GlProgram* p = programPtr(program);
    if (p == nullptr) return;
    p->linked = false;
    p->log.clear();
    const SpirvBlob* vs = nullptr;
    const SpirvBlob* fs = nullptr;
    std::uint32_t spec = 0;
    for (const GLuint id : p->attached) {
        GlShader* sh = shaderPtr(id);
        if (sh == nullptr || !sh->compiled) continue;
        (sh->type == gl::VERTEX_SHADER ? vs : fs) = sh->twin;
        spec |= sh->specMask;
    }
    if (vs == nullptr || fs == nullptr) {
        p->log = "a program needs a compiled vertex shader and a compiled fragment shader";
        return;
    }
    std::string error;
    if (!reflectSpirv(vs->words, vs->wordCount, p->vertexInfo, error) || !reflectSpirv(fs->words, fs->wordCount, p->fragmentInfo, error)) {
        p->log = error;
        return;
    }
    // The block both stages declare: the vertex shader's members, then the fragment shader's that the vertex shader does not have.
    p->uniforms = p->vertexInfo.block;
    for (const SpirvInfo::Member& m : p->fragmentInfo.block) {
        bool found = false;
        for (const SpirvInfo::Member& v : p->vertexInfo.block) {
            if (v.name != m.name) continue;
            found = true;
            if (v.offset != m.offset) {
                p->log = "the uniform '" + m.name + "' is at different places in the two stages' blocks";
                return;
            }
        }
        if (!found) p->uniforms.push_back(m);
    }
    p->blockSize = std::max(p->vertexInfo.blockSize, p->fragmentInfo.blockSize);
    if (p->blockSize > kUniformRange) {
        p->log = "the uniform block is larger than " + std::to_string(kUniformRange) + " bytes";
        return;
    }
    p->samplers = p->fragmentInfo.samplers;
    for (const SpirvInfo::Sampler& s : p->vertexInfo.samplers) p->samplers.push_back(s);
    for (const SpirvInfo::Sampler& s : p->samplers) {
        if (s.binding >= static_cast<std::uint32_t>(kMaxSamplers)) {
            p->log = "the sampler '" + s.name + "' is bound beyond the " + std::to_string(kMaxSamplers) + " a program may have";
            return;
        }
    }
    p->inputs = p->vertexInfo.inputs;
    p->specMask = spec;
    p->block.assign(p->blockSize, 0);
    p->blockDirty = true;
    for (int& unit : p->samplerUnit) unit = 0;
    p->vertex = makeModule(*vs);
    p->fragment = makeModule(*fs);
    if (p->vertex == VK_NULL_HANDLE || p->fragment == VK_NULL_HANDLE) {
        p->log = "the graphics card did not take the shader module";
        return;
    }
    p->linked = true;
}

void GetProgramiv(GLuint program, GLenum name, GLint* out) {
    GlProgram* p = programPtr(program);
    if (p == nullptr || out == nullptr) return;
    if (name == gl::LINK_STATUS) *out = p->linked ? 1 : 0;
    else if (name == gl::INFO_LOG_LENGTH) *out = static_cast<GLint>(p->log.size() + 1);
}

void GetProgramInfoLog(GLuint program, GLsizei size, GLsizei* length, GLchar* log) {
    GlProgram* p = programPtr(program);
    if (p == nullptr || log == nullptr || size <= 0) return;
    const std::size_t n = std::min(p->log.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(log, p->log.data(), n);
    log[n] = '\0';
    if (length != nullptr) *length = static_cast<GLsizei>(n);
}

void DeleteProgram(GLuint program) {
    GlState& s = gls();
    if (GlProgram* p = programPtr(program)) releaseProgram(*p, program);
    if (s.program == program) s.program = 0;
}

void UseProgram(GLuint program) { gls().program = program; }

GLint GetUniformLocation(GLuint program, const GLchar* name) {
    GlProgram* p = programPtr(program);
    if (p == nullptr || !p->linked) return -1;
    for (std::size_t i = 0; i < p->uniforms.size(); ++i) {
        if (p->uniforms[i].name == name) return static_cast<GLint>(i);
    }
    for (std::size_t i = 0; i < p->samplers.size(); ++i) {
        if (p->samplers[i].name == name) return static_cast<GLint>(p->uniforms.size() + i);
    }
    return -1;
}

void Uniform1i(GLint location, GLint value) {
    GlProgram* p = programPtr(gls().program);
    if (p == nullptr || location < 0) return;
    if (static_cast<std::size_t>(location) >= p->uniforms.size()) {
        const std::size_t sampler = static_cast<std::size_t>(location) - p->uniforms.size();
        if (sampler < p->samplers.size()) p->samplerUnit[p->samplers[sampler].binding] = value;
        return;
    }
    writeUniform(location, &value, sizeof value);
}

void Uniform1f(GLint location, GLfloat x) { writeUniform(location, &x, sizeof x); }

void Uniform2f(GLint location, GLfloat x, GLfloat y) {
    const GLfloat v[2] = {x, y};
    writeUniform(location, v, sizeof v);
}

void Uniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    const GLfloat v[3] = {x, y, z};
    writeUniform(location, v, sizeof v);
}

void Uniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const GLfloat v[4] = {x, y, z, w};
    writeUniform(location, v, sizeof v);
}

void UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (count < 1 || value == nullptr) return;
    GLfloat m[16];
    std::memcpy(m, value, sizeof m);
    if (transpose != 0) {
        for (int r = 0; r < 4; ++r) {
            for (int col = 0; col < 4; ++col) m[col * 4 + r] = value[r * 4 + col];
        }
    }
    writeUniform(location, m, sizeof m);
}

}  // namespace glemu

}  // namespace falcon::vulkan
