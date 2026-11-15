#include "gl_state.hpp"

#include <algorithm>
#include <cstring>

namespace falcon::vulkan {

namespace {

constexpr GLenum kCullFace = 0x0B44;
constexpr GLenum kDepthTest = 0x0B71;
constexpr GLenum kBlend = 0x0BE2;
constexpr GLenum kScissorTest = 0x0C11;
constexpr GLenum kColorBufferBit = 0x4000;
constexpr GLenum kDepthBufferBit = 0x0100;
constexpr GLenum kLinesMode = 0x0001;
constexpr GLenum kTriangleStrip = 0x0005;

VkPrimitiveTopology topologyOf(GLenum mode) {
    switch (mode) {
        case kLinesMode: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case kTriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkIndexType indexTypeOf(GLenum type) { return type == gl::UNSIGNED_INT ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16; }
std::uint32_t indexSize(GLenum type) { return type == gl::UNSIGNED_INT ? 4u : 2u; }

VkDeviceSize alignUp(VkDeviceSize v, VkDeviceSize a) { return a == 0 ? v : (v + a - 1) / a * a; }

// The scissor for a draw or a clear: the game's rectangle when the scissor test is on, else the whole target; inside the target either way.
VkRect2D scissorRect(const GlState& s, const Target& t) {
    VkRect2D r{{0, 0}, {t.width, t.height}};
    if (!s.scissorTest) return r;
    const std::int32_t x0 = std::clamp<std::int32_t>(s.scissor[0], 0, static_cast<std::int32_t>(t.width));
    const std::int32_t y0 = std::clamp<std::int32_t>(s.scissor[1], 0, static_cast<std::int32_t>(t.height));
    const std::int32_t x1 = std::clamp<std::int32_t>(s.scissor[0] + std::max(s.scissor[2], 0), x0, static_cast<std::int32_t>(t.width));
    const std::int32_t y1 = std::clamp<std::int32_t>(s.scissor[1] + std::max(s.scissor[3], 0), y0, static_cast<std::int32_t>(t.height));
    r.offset = {x0, y0};
    r.extent = {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
    return r;
}

// Forget what the command buffer was told: a pass or a frame starts with nothing bound.
void forgetBoundState(GlState& s) {
    s.boundPipeline = VK_NULL_HANDLE;
    s.boundUniformSet = VK_NULL_HANDLE;
    s.boundSamplerSet = VK_NULL_HANDLE;
    s.boundUniformOffset = 0xFFFFFFFFu;
    s.viewportKnown = false;
    s.scissorKnown = false;
}

void clearInPass(GlState& s, const Target& t, bool color, bool depth) {
    VkClearAttachment attachments[2]{};
    std::uint32_t n = 0;
    if (color) {
        attachments[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[n].colorAttachment = 0;
        std::memcpy(attachments[n].clearValue.color.float32, s.clearColor, sizeof(float) * 4);
        ++n;
    }
    if (depth) {
        attachments[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachments[n].clearValue.depthStencil = {1.0f, 0};
        ++n;
    }
    VkClearRect rect{scissorRect(s, t), 0, 1};
    api::vkCmdClearAttachments(cmd(), n, attachments, 1, &rect);
}

}  // namespace

bool currentTarget(Target& out) { return targetOf(gls().framebuffer, out); }

void prepareFrame() {
    GlState& s = gls();
    Core& c = core();
    (void)frame();  // (begins the frame)
    if (s.preparedFrame == c.frameNumber) return;
    s.preparedFrame = c.frameNumber;
    ensureBackbuffer();
    forgetBoundState(s);
    s.drawnSinceBarrier = false;
    if (!s.viewportSet) {  // (the first viewport of a context is the whole window)
        s.viewport[2] = static_cast<GLint>(backbuffer().width);
        s.viewport[3] = static_cast<GLint>(backbuffer().height);
        s.scissor[2] = s.viewport[2];
        s.scissor[3] = s.viewport[3];
    }
}

bool beginRendering() {
    GlState& s = gls();
    prepareFrame();
    Target t;
    if (!currentTarget(t)) return false;
    if (s.rendering) {
        if (s.renderTarget.same(t)) return true;
        endRendering();
    }
    flushTransfers();
    VkCommandBuffer cb = cmd();
    const bool hasDepth = t.depthImage != VK_NULL_HANDLE;
    if (*t.colorLayout != VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
        transitionImage(cb, t.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, *t.colorValid ? *t.colorLayout : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        *t.colorLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    }
    if (hasDepth && *t.depthLayout != VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL) {
        transitionImage(cb, t.depthImage, core().depthAspect, *t.depthValid ? *t.depthLayout : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        *t.depthLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = t.colorView;
    color.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (t.clear->color || !*t.colorValid) {
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        std::memcpy(color.clearValue.color.float32, t.clear->rgba, sizeof(float) * 4);
    } else {
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (hasDepth) {
        depth.imageView = t.depthView;
        depth.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (t.clear->depth || !*t.depthValid) {
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.clearValue.depthStencil = {1.0f, 0};
        } else {
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
    }
    t.clear->color = false;
    t.clear->depth = false;
    *t.colorValid = true;
    if (hasDepth) *t.depthValid = true;

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {t.width, t.height}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = hasDepth ? &depth : nullptr;
    api::vkCmdBeginRendering(cb, &info);
    s.rendering = true;
    s.renderTarget = t;
    forgetBoundState(s);
    return true;
}

void endRendering() {
    GlState& s = gls();
    if (!s.rendering) return;
    api::vkCmdEndRendering(cmd());
    s.rendering = false;
    const Target& t = s.renderTarget;
    if (!t.isBackbuffer && t.colorLayout != nullptr) {  // (a texture that was drawn on is read next)
        transitionImage(cmd(), t.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        *t.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    s.renderTarget = Target{};
}

bool prepareDraw(VkPrimitiveTopology topology, bool indexed, GLenum indexType, std::uintptr_t indexOffset, std::uint32_t& firstIndexAdjust) {
    GlState& s = gls();
    Core& c = core();
    firstIndexAdjust = 0;
    GlProgram* p = programPtr(s.program);
    if (p == nullptr || !p->linked) return false;
    GlVao* v = &s.vaos[s.vao];
    prepareFrame();

    // The textures the program reads. (First, since one that nothing has written yet is made readable, which cannot happen inside a pass.)
    VkImageView views[kMaxSamplers] = {};
    VkSampler samplers[kMaxSamplers] = {};
    std::uint32_t usedMask = 0;
    for (const SpirvInfo::Sampler& smp : p->samplers) {
        samplerBinding(p->samplerUnit[smp.binding], smp.kind, views[smp.binding], samplers[smp.binding]);
        if (views[smp.binding] != VK_NULL_HANDLE) usedMask |= 1u << smp.binding;
    }
    if (!beginRendering()) return false;
    const Target& t = s.renderTarget;

    PipelineRequest request;
    request.program = p;
    request.vao = v;
    request.topology = topology;
    request.target = &t;
    VertexBindings vertexBuffers;
    const VkPipeline pipeline = pipelineFor(request, vertexBuffers);
    if (pipeline == VK_NULL_HANDLE) return false;

    VkCommandBuffer cb = cmd();
    if (pipeline != s.boundPipeline) {
        api::vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        s.boundPipeline = pipeline;
    }

    VkViewport viewport{static_cast<float>(s.viewport[0]), static_cast<float>(s.viewport[1]), static_cast<float>(s.viewport[2]), static_cast<float>(s.viewport[3]), 0.0f, 1.0f};
    if (s.viewport[2] <= 0 || s.viewport[3] <= 0) {
        viewport.width = static_cast<float>(t.width);
        viewport.height = static_cast<float>(t.height);
    }
    if (!s.viewportKnown || std::memcmp(&viewport, &s.boundViewport, sizeof viewport) != 0) {
        api::vkCmdSetViewport(cb, 0, 1, &viewport);
        s.boundViewport = viewport;
        s.viewportKnown = true;
    }
    const VkRect2D scissor = scissorRect(s, t);
    if (!s.scissorKnown || std::memcmp(&scissor, &s.boundScissor, sizeof scissor) != 0) {
        api::vkCmdSetScissor(cb, 0, 1, &scissor);
        s.boundScissor = scissor;
        s.scissorKnown = true;
    }

    // The uniforms: the block is put in the frame's memory when it changed (or for the first draw of a frame) and the draw reads it at that offset.
    VkDescriptorSet uniformSet = VK_NULL_HANDLE;
    std::uint32_t dynamicOffset = 0;
    if (p->blockSize > 0) {
        if (p->blockDirty || p->blockSlice.buffer == VK_NULL_HANDLE || p->blockFrame != c.frameNumber) {
            const VkDeviceSize alignment = std::max<VkDeviceSize>(c.properties.limits.minUniformBufferOffsetAlignment, 16);
            p->blockSlice = frame().arena.allocate(alignUp(p->blockSize, 16), alignment);
            std::memcpy(p->blockSlice.ptr, p->block.data(), p->blockSize);
            p->blockFrame = c.frameNumber;
            p->blockDirty = false;
        }
        uniformSet = uniformSetFor(p->blockSlice.buffer);
        dynamicOffset = static_cast<std::uint32_t>(p->blockSlice.offset);
    }
    VkDescriptorSet samplerSet = VK_NULL_HANDLE;
    if (usedMask != 0) {
        SamplerSetKey key;
        for (int i = 0; i < kMaxSamplers; ++i) {
            key.views[i] = (usedMask & (1u << i)) != 0 ? views[i] : VK_NULL_HANDLE;
            key.samplers[i] = (usedMask & (1u << i)) != 0 ? samplers[i] : VK_NULL_HANDLE;
        }
        samplerSet = samplerSetFor(key, usedMask);
    }
    if (uniformSet != s.boundUniformSet || samplerSet != s.boundSamplerSet || dynamicOffset != s.boundUniformOffset) {
        VkDescriptorSet sets[2] = {uniformSet, samplerSet};
        if (uniformSet != VK_NULL_HANDLE && samplerSet != VK_NULL_HANDLE) {
            api::vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipelineLayout, 0, 2, sets, 1, &dynamicOffset);
        } else if (uniformSet != VK_NULL_HANDLE) {
            api::vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipelineLayout, 0, 1, sets, 1, &dynamicOffset);
        } else if (samplerSet != VK_NULL_HANDLE) {
            api::vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipelineLayout, 1, 1, &sets[1], 0, nullptr);
        }
        s.boundUniformSet = uniformSet;
        s.boundSamplerSet = samplerSet;
        s.boundUniformOffset = dynamicOffset;
    }

    if (vertexBuffers.count > 0) api::vkCmdBindVertexBuffers(cb, 0, vertexBuffers.count, vertexBuffers.buffers, vertexBuffers.offsets);
    if (indexed) {
        const BufferBacking index = backingOf(v->elementBuffer);
        if (index.buffer == VK_NULL_HANDLE) return false;
        api::vkCmdBindIndexBuffer(cb, index.buffer, index.offset, indexTypeOf(indexType));
        firstIndexAdjust = static_cast<std::uint32_t>(indexOffset / indexSize(indexType));
    }
    s.drawnSinceBarrier = true;
    return true;
}

namespace glemu {

void Viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    GlState& s = gls();
    s.viewport[0] = x;
    s.viewport[1] = y;
    s.viewport[2] = w;
    s.viewport[3] = h;
    s.viewportSet = true;
}

void Scissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    GlState& s = gls();
    s.scissor[0] = x;
    s.scissor[1] = y;
    s.scissor[2] = w;
    s.scissor[3] = h;
}

void Enable(GLenum cap) {
    GlState& s = gls();
    switch (cap) {
        case kDepthTest: s.depthTest = true; break;
        case kCullFace: s.cullFace = true; break;
        case kBlend: s.blend = true; break;
        case kScissorTest: s.scissorTest = true; break;
        default: break;  // (cube map seams: Vulkan always blends them)
    }
}

void Disable(GLenum cap) {
    GlState& s = gls();
    switch (cap) {
        case kDepthTest: s.depthTest = false; break;
        case kCullFace: s.cullFace = false; break;
        case kBlend: s.blend = false; break;
        case kScissorTest: s.scissorTest = false; break;
        default: break;
    }
}

void BlendFuncSeparate(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha) {
    GlState& s = gls();
    s.blendSrcRgb = srcRgb;
    s.blendDstRgb = dstRgb;
    s.blendSrcAlpha = srcAlpha;
    s.blendDstAlpha = dstAlpha;
}

void DepthFunc(GLenum func) { gls().depthFunc = func; }
void DepthMask(GLboolean flag) { gls().depthMask = flag != 0; }
void CullFace(GLenum mode) { gls().cullMode = mode; }
void FrontFace(GLenum mode) { gls().frontFace = mode; }

void ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    GlState& s = gls();
    s.clearColor[0] = r;
    s.clearColor[1] = g;
    s.clearColor[2] = b;
    s.clearColor[3] = a;
}

void Clear(GLbitfield mask) {
    GlState& s = gls();
    prepareFrame();
    Target t;
    if (!currentTarget(t)) return;
    const bool color = (mask & kColorBufferBit) != 0;
    const bool depth = (mask & kDepthBufferBit) != 0 && t.depthImage != VK_NULL_HANDLE;
    if (!color && !depth) return;
    if (s.rendering && s.renderTarget.same(t)) {
        clearInPass(s, t, color, depth);
    } else if (s.scissorTest) {  // (a clear of a part: needs a pass to clear in)
        if (!beginRendering()) return;
        clearInPass(s, s.renderTarget, color, depth);
    } else {  // (the whole target: it becomes the first thing its next pass does)
        if (color) {
            t.clear->color = true;
            std::memcpy(t.clear->rgba, s.clearColor, sizeof(float) * 4);
        }
        if (depth) t.clear->depth = true;
    }
}

void DrawArrays(GLenum mode, GLint first, GLsizei count) {
    std::uint32_t adjust = 0;
    if (count <= 0 || !prepareDraw(topologyOf(mode), false, 0, 0, adjust)) return;
    api::vkCmdDraw(cmd(), static_cast<std::uint32_t>(count), 1, static_cast<std::uint32_t>(first), 0);
}

void DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    std::uint32_t adjust = 0;
    if (count <= 0 || !prepareDraw(topologyOf(mode), true, type, reinterpret_cast<std::uintptr_t>(indices), adjust)) return;
    api::vkCmdDrawIndexed(cmd(), static_cast<std::uint32_t>(count), 1, adjust, 0, 0);
}

void DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint baseVertex) {
    std::uint32_t adjust = 0;
    if (count <= 0 || !prepareDraw(topologyOf(mode), true, type, reinterpret_cast<std::uintptr_t>(indices), adjust)) return;
    api::vkCmdDrawIndexed(cmd(), static_cast<std::uint32_t>(count), 1, adjust, baseVertex, 0);
}

void MultiDrawElementsBaseVertex(GLenum mode, const GLsizei* counts, GLenum type, const void* const* indices, GLsizei drawCount, const GLint* baseVertices) {
    for (GLsizei i = 0; i < drawCount; ++i) DrawElementsBaseVertex(mode, counts[i], type, indices[i], baseVertices[i]);
}

// The commands come from the buffer bound to DRAW_INDIRECT_BUFFER, in the layout Vulkan reads them in (the same five numbers as OpenGL's).
void MultiDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect, GLsizei drawCount, GLsizei stride) {
    GlState& s = gls();
    std::uint32_t adjust = 0;
    if (drawCount <= 0 || !prepareDraw(topologyOf(mode), true, type, 0, adjust)) return;
    const BufferBacking commands = backingOf(s.drawIndirectBuffer);
    if (commands.buffer == VK_NULL_HANDLE) return;
    const std::uint32_t step = stride != 0 ? static_cast<std::uint32_t>(stride) : static_cast<std::uint32_t>(sizeof(VkDrawIndexedIndirectCommand));
    api::vkCmdDrawIndexedIndirect(cmd(), commands.buffer, commands.offset + reinterpret_cast<std::uintptr_t>(indirect), static_cast<std::uint32_t>(drawCount), step);
}

}  // namespace glemu

}  // namespace falcon::vulkan
