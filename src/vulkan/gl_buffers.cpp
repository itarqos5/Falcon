#include "gl_state.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace falcon::vulkan {

namespace {

constexpr VkBufferUsageFlags kDeviceBufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

// STREAM_DRAW: the data is made again before every use, so it is put in the frame's host memory instead of a buffer of its own.
bool isStreamUsage(GLenum usage) { return usage == gl::STREAM_DRAW || usage == 0x88E1 /* STREAM_READ */ || usage == 0x88E2 /* STREAM_COPY */; }

// The binding a target names. ELEMENT_ARRAY_BUFFER is part of the current vertex array.
GLuint* bindingOf(GLenum target) {
    GlState& s = gls();
    switch (target) {
        case gl::ARRAY_BUFFER: return &s.arrayBuffer;
        case gl::COPY_READ_BUFFER: return &s.copyReadBuffer;
        case gl::COPY_WRITE_BUFFER: return &s.copyWriteBuffer;
        case gl::DRAW_INDIRECT_BUFFER: return &s.drawIndirectBuffer;
        case gl::ELEMENT_ARRAY_BUFFER: return s.vao != 0 ? &s.vaos[s.vao].elementBuffer : nullptr;
        default: return nullptr;
    }
}

GlBuffer* bound(GLenum target) {
    GLuint* binding = bindingOf(target);
    return binding != nullptr ? bufferPtr(*binding) : nullptr;
}

// Lets go of what a buffer holds. A device buffer may still be read by commands that are being run: it is destroyed when they are done.
void releaseStorage(GlBuffer& b) {
    if (b.onDevice && b.device.buffer != VK_NULL_HANDLE) {
        DeviceBuffer old = b.device;
        defer([old]() mutable { destroyBuffer(old); });
    }
    b.device = DeviceBuffer{};
    b.onDevice = false;
    b.slice = HostSlice{};
    b.sliceFrame = 0;
    b.sliceUsed = false;
    b.shadow.clear();
    b.size = 0;
}

}  // namespace

GlBuffer* bufferPtr(GLuint id) {
    auto& all = gls().buffers;
    return id != 0 && id < all.size() && all[id].exists ? &all[id] : nullptr;
}

BufferBacking backingOf(GLuint id) {
    GlBuffer* b = bufferPtr(id);
    if (b == nullptr) return {};
    if (b->onDevice) return {b->device.buffer, 0};
    Frame& f = frame();
    (void)f;
    if (b->slice.buffer == VK_NULL_HANDLE || b->sliceFrame != core().frameNumber) {
        b->slice = frame().arena.allocate(static_cast<VkDeviceSize>(std::max<GLsizeiptr>(b->size, 4)), 16);
        if (!b->shadow.empty()) std::memcpy(b->slice.ptr, b->shadow.data(), b->shadow.size());
        b->sliceFrame = core().frameNumber;
    }
    b->sliceUsed = true;
    return {b->slice.buffer, b->slice.offset};
}

void beforeTransfer() {
    endRendering();
    GlState& s = gls();
    if (s.drawnSinceBarrier) {  // (what the draws read must not be overwritten before they have read it)
        fullBarrier(cmd());
        s.drawnSinceBarrier = false;
    }
}

void uploadToBuffer(GlBuffer& buffer, VkDeviceSize offset, const void* data, VkDeviceSize bytes) {
    if (bytes == 0 || data == nullptr) return;
    beforeTransfer();
    const HostSlice staging = frame().arena.allocate(bytes, 16);
    std::memcpy(staging.ptr, data, static_cast<std::size_t>(bytes));
    VkBufferCopy region{staging.offset, offset, bytes};
    api::vkCmdCopyBuffer(cmd(), staging.buffer, buffer.device.buffer, 1, &region);
    gls().bufferBarrierPending = true;
}

void destroyBufferObjects() {
    GlState& s = gls();
    for (GlBuffer& b : s.buffers) {
        if (b.onDevice) destroyBuffer(b.device);
    }
    destroyBuffer(s.zeroBuffer);
}

namespace glemu {

void GenBuffers(GLsizei n, GLuint* ids) {
    auto& all = gls().buffers;
    for (GLsizei i = 0; i < n; ++i) {
        all.emplace_back();
        all.back().exists = true;
        ids[i] = static_cast<GLuint>(all.size() - 1);
    }
}

void DeleteBuffers(GLsizei n, const GLuint* ids) {
    GlState& s = gls();
    for (GLsizei i = 0; i < n; ++i) {
        GlBuffer* b = bufferPtr(ids[i]);
        if (b == nullptr) continue;
        releaseStorage(*b);
        b->exists = false;
        if (s.arrayBuffer == ids[i]) s.arrayBuffer = 0;
        if (s.copyReadBuffer == ids[i]) s.copyReadBuffer = 0;
        if (s.copyWriteBuffer == ids[i]) s.copyWriteBuffer = 0;
        if (s.drawIndirectBuffer == ids[i]) s.drawIndirectBuffer = 0;
    }
}

void BindBuffer(GLenum target, GLuint id) {
    GLuint* binding = bindingOf(target);
    if (binding == nullptr) return;
    auto& all = gls().buffers;
    if (id != 0 && id < all.size() && !all[id].exists) return;  // a deleted one
    *binding = id;
}

void BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    GlBuffer* b = bound(target);
    if (b == nullptr) return;
    releaseStorage(*b);
    b->size = size;
    if (isStreamUsage(usage)) {
        b->shadow.assign(static_cast<std::size_t>(size), 0);
        if (data != nullptr && size > 0) std::memcpy(b->shadow.data(), data, static_cast<std::size_t>(size));
        return;
    }
    b->onDevice = true;
    if (!createBuffer(static_cast<VkDeviceSize>(std::max<GLsizeiptr>(size, 4)), kDeviceBufferUsage, false, b->device)) {
        b->onDevice = false;
        throw std::runtime_error("Vulkan: no memory on the graphics card for a buffer of " + std::to_string(size) + " bytes");
    }
    if (data != nullptr) uploadToBuffer(*b, 0, data, static_cast<VkDeviceSize>(size));
}

void BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    GlBuffer* b = bound(target);
    if (b == nullptr || data == nullptr || size <= 0) return;
    if (b->onDevice) {
        uploadToBuffer(*b, static_cast<VkDeviceSize>(offset), data, static_cast<VkDeviceSize>(size));
        return;
    }
    if (static_cast<std::size_t>(offset + size) > b->shadow.size()) b->shadow.resize(static_cast<std::size_t>(offset + size), 0);
    std::memcpy(b->shadow.data() + offset, data, static_cast<std::size_t>(size));
    b->sliceFrame = 0;  // (the next use puts the new contents in a piece of memory of its own: draws that are already recorded keep the old ones)
}

void CopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    GlBuffer* src = bound(readTarget);
    GlBuffer* dst = bound(writeTarget);
    if (src == nullptr || dst == nullptr || size <= 0) return;
    if (!dst->onDevice) {  // (into a host buffer: both are host buffers in practice)
        if (src->onDevice) throw std::runtime_error("Vulkan: a copy from a device buffer into a streamed one");
        if (static_cast<std::size_t>(writeOffset + size) > dst->shadow.size()) dst->shadow.resize(static_cast<std::size_t>(writeOffset + size), 0);
        std::memcpy(dst->shadow.data() + writeOffset, src->shadow.data() + readOffset, static_cast<std::size_t>(size));
        dst->sliceFrame = 0;
        return;
    }
    const GLuint srcId = *bindingOf(readTarget);
    const BufferBacking from = backingOf(srcId);
    beforeTransfer();
    VkBufferCopy region{from.offset + static_cast<VkDeviceSize>(readOffset), static_cast<VkDeviceSize>(writeOffset), static_cast<VkDeviceSize>(size)};
    api::vkCmdCopyBuffer(cmd(), from.buffer, dst->device.buffer, 1, &region);
    gls().bufferBarrierPending = true;
}

void GenVertexArrays(GLsizei n, GLuint* ids) {
    auto& all = gls().vaos;
    for (GLsizei i = 0; i < n; ++i) {
        all.emplace_back();
        all.back().exists = true;
        ids[i] = static_cast<GLuint>(all.size() - 1);
    }
}

void DeleteVertexArrays(GLsizei n, const GLuint* ids) {
    GlState& s = gls();
    for (GLsizei i = 0; i < n; ++i) {
        if (ids[i] != 0 && ids[i] < s.vaos.size()) s.vaos[ids[i]] = GlVao{};
        if (s.vao == ids[i]) s.vao = 0;
    }
}

void BindVertexArray(GLuint id) {
    GlState& s = gls();
    if (id != 0 && (id >= s.vaos.size() || !s.vaos[id].exists)) return;
    s.vao = id;
}

void EnableVertexAttribArray(GLuint index) {
    GlState& s = gls();
    if (s.vao == 0 || index >= kMaxAttribs) return;
    s.vaos[s.vao].attribs[index].enabled = true;
}

void VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    GlState& s = gls();
    if (s.vao == 0 || index >= kMaxAttribs) return;
    VertexAttrib& a = s.vaos[s.vao].attribs[index];
    a.size = size;
    a.type = type;
    a.normalized = normalized != 0;
    a.stride = stride;
    a.offset = reinterpret_cast<std::uintptr_t>(pointer);
    a.buffer = s.arrayBuffer;  // (the buffer that is bound now is the one the attribute reads)
}

void VertexAttribDivisor(GLuint index, GLuint divisor) {
    GlState& s = gls();
    if (s.vao == 0 || index >= kMaxAttribs) return;
    s.vaos[s.vao].attribs[index].divisor = divisor;
}

}  // namespace glemu

}  // namespace falcon::vulkan
