#include "gl_state.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace falcon::vulkan {

namespace {
constexpr GLenum kAlreadySignaled = 0x911A;
constexpr GLenum kTimeoutExpired = 0x911B;
constexpr GLenum kConditionSatisfied = 0x911C;
}  // namespace

GlState& gls() {
    static GlState state;
    return state;
}

void resetGlState() { gls() = GlState{}; }

void installGlFunctions() {
#define FALCON_GLEMU_INSTALL(ret, name, args) gl::name = &glemu::name;
    FALCON_GL_FUNCTIONS(FALCON_GLEMU_INSTALL)
#undef FALCON_GLEMU_INSTALL
    const Core& c = core();
    // Drawing many sections with one call needs the device to do indirect draws with a first instance per command.
    gl::MultiDrawElementsIndirect = c.multiDrawIndirect && c.drawIndirectFirstInstance ? &glemu::MultiDrawElementsIndirect : nullptr;
    gl::contextMajor = 4;  // (what the renderers ask about: a context that does the OpenGL 4.3 drawing)
    gl::contextMinor = 5;
}

namespace glemu {

const GLubyte* GetString(GLenum name) {
    static std::string vendor, renderer, version;
    const Core& c = core();
    vendor = c.vendorName;
    renderer = c.deviceName;
    version = "Vulkan " + c.versionText;
    switch (name) {
        case gl::VENDOR: return reinterpret_cast<const GLubyte*>(vendor.c_str());
        case gl::RENDERER: return reinterpret_cast<const GLubyte*>(renderer.c_str());
        case gl::VERSION: return reinterpret_cast<const GLubyte*>(version.c_str());
        default: return reinterpret_cast<const GLubyte*>("");
    }
}

GLenum GetError() { return 0; }

// The picture of the target in the order OpenGL gives it (the first row is the bottom), which is the order the targets are stored in.
void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels) {
    if (pixels == nullptr || width <= 0 || height <= 0 || format != gl::RGBA || type != gl::UNSIGNED_BYTE) return;
    GlState& s = gls();
    prepareFrame();
    Target t;
    if (!currentTarget(t)) return;
    // What was asked to be cleared, and a target nothing was drawn on, are made real first.
    if (!s.rendering || !s.renderTarget.same(t)) {
        if (s.rendering) endRendering();
        if (t.clear->color || t.clear->depth || !*t.colorValid) {
            if (!beginRendering()) return;
        }
    }
    endRendering();
    flushTransfers();
    VkCommandBuffer cb = cmd();
    transitionImage(cb, t.colorImage, VK_IMAGE_ASPECT_COLOR_BIT, *t.colorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    *t.colorLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    const std::uint32_t w = static_cast<std::uint32_t>(std::min<GLsizei>(width, static_cast<GLsizei>(t.width) - x));
    const std::uint32_t h = static_cast<std::uint32_t>(std::min<GLsizei>(height, static_cast<GLsizei>(t.height) - y));
    DeviceBuffer readback;
    if (!createBuffer(static_cast<VkDeviceSize>(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, readback)) throw std::runtime_error("Vulkan: no memory to read the picture back");
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {x, y, 0};
    region.imageExtent = {w, h, 1};
    api::vkCmdCopyImageToBuffer(cb, t.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
    flushAndWait();

    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
    const std::size_t stride = (static_cast<std::size_t>(width) * 4 + static_cast<std::size_t>(s.packAlignment) - 1) / static_cast<std::size_t>(s.packAlignment) * static_cast<std::size_t>(s.packAlignment);
    auto* out = static_cast<unsigned char*>(pixels);
    for (std::uint32_t row = 0; row < h; ++row) std::memcpy(out + row * stride, static_cast<const unsigned char*>(readback.memory.mapped) + row * rowBytes, rowBytes);
    destroyBuffer(readback);
}

void GenQueries(GLsizei n, GLuint* ids) {
    auto& all = gls().queries;
    for (GLsizei i = 0; i < n; ++i) {
        all.emplace_back();
        all.back().exists = true;
        ids[i] = static_cast<GLuint>(all.size() - 1);
    }
}

void DeleteQueries(GLsizei n, const GLuint* ids) {
    auto& all = gls().queries;
    for (GLsizei i = 0; i < n; ++i) {
        if (ids[i] != 0 && ids[i] < all.size()) all[ids[i]].exists = false;
    }
}

// A timer is two timestamps in the frame's query pool; the result is read when the frame's commands are known to be done.
void BeginQuery(GLenum target, GLuint id) {
    GlState& s = gls();
    if (target != gl::TIME_ELAPSED || id == 0 || id >= s.queries.size() || s.openQuery != 0) return;
    prepareFrame();
    Frame& f = frame();
    GlQuery& q = s.queries[id];
    q.ready = false;
    if (f.queriesUsed + 2 > kQueriesPerFrame) {
        q.started = false;
        return;
    }
    q.started = true;
    api::vkCmdWriteTimestamp2(cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queryPool, f.queriesUsed);
    s.openQuery = id;
    q.firstQuery = f.queriesUsed;
    f.queriesUsed += 2;
}

void EndQuery(GLenum target) {
    GlState& s = gls();
    if (target != gl::TIME_ELAPSED || s.openQuery == 0) return;
    Frame& f = frame();
    const GLuint id = s.openQuery;
    s.openQuery = 0;
    const std::uint32_t first = s.queries[id].firstQuery;
    api::vkCmdWriteTimestamp2(cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queryPool, first + 1);
    const VkQueryPool pool = f.queryPool;
    f.onRetire.push_back([id, pool, first] {
        std::uint64_t data[2] = {0, 0};
        Core& c = core();
        if (api::vkGetQueryPoolResults(c.device, pool, first, 2, sizeof data, data, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
        GlQuery& q = gls().queries[id];
        q.resultNs = static_cast<std::uint64_t>(static_cast<double>(data[1] - data[0]) * static_cast<double>(c.properties.limits.timestampPeriod));
        q.ready = true;
    });
}

void GetQueryObjectiv(GLuint id, GLenum name, GLint* out) {
    GlState& s = gls();
    if (out == nullptr || id == 0 || id >= s.queries.size()) return;
    if (name == gl::QUERY_RESULT_AVAILABLE) *out = s.queries[id].ready ? 1 : 0;
    else if (name == gl::QUERY_RESULT) *out = static_cast<GLint>(s.queries[id].resultNs);
}

void GetQueryObjectui64v(GLuint id, GLenum name, GLuint64* out) {
    GlState& s = gls();
    if (out == nullptr || id == 0 || id >= s.queries.size()) return;
    if (name == gl::QUERY_RESULT_AVAILABLE) *out = s.queries[id].ready ? 1 : 0;
    else if (name == gl::QUERY_RESULT) *out = s.queries[id].ready ? s.queries[id].resultNs : 0;
}

// A fence is a submission's number: it is signalled when that submission is done. (After a frame was shown it names that frame.)
GLsync FenceSync(GLenum, GLbitfield) {
    const Core& c = core();
    const std::uint64_t serial = c.frameBegun ? c.submitCount + 1 : c.submitCount;
    return reinterpret_cast<GLsync>(static_cast<std::uintptr_t>(serial));
}

GLenum ClientWaitSync(GLsync sync, GLbitfield, GLuint64 timeout) {
    const auto serial = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(sync));
    const Core& c = core();
    if (serial == 0 || serial > c.submitCount) return kAlreadySignaled;  // (not submitted yet: nothing to wait for from here)
    if (waitForSerial(serial, 0)) return kAlreadySignaled;
    return waitForSerial(serial, timeout) ? kConditionSatisfied : kTimeoutExpired;
}

void DeleteSync(GLsync) {}

}  // namespace glemu

}  // namespace falcon::vulkan
