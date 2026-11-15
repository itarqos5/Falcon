#pragma once

// The part of OpenGL the renderers use, implemented on Vulkan: the objects (buffers, textures, programs, vertex arrays, framebuffers, queries), the state machine (what is bound,
// what is enabled), and the commands that turn into Vulkan commands. The files gl_*.cpp each hold one kind of call; this header is what they share.

#include "falcon/gfx/gl.hpp"
#include "spirv_reflect.hpp"
#include "vk_core.hpp"
#include "vk_swapchain.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace falcon::vulkan {

using gl::GLbitfield;
using gl::GLboolean;
using gl::GLchar;
using gl::GLenum;
using gl::GLfloat;
using gl::GLint;
using gl::GLintptr;
using gl::GLsizei;
using gl::GLsizeiptr;
using gl::GLsync;
using gl::GLubyte;
using gl::GLuint;
using gl::GLuint64;

// ---- buffers ------------------------------------------------------------------------------------------------------------------------------------------------

// Where a buffer's data is for the commands of this frame.
struct BufferBacking {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
};

// A buffer object. What the game fills again every frame (STREAM_DRAW) lives in the frame's host-visible memory and is made again by every glBufferData; the rest (STATIC_DRAW,
// DYNAMIC_DRAW) is a device-local buffer, written through the frame's staging memory.
struct GlBuffer {
    bool exists = false;
    GLsizeiptr size = 0;
    bool onDevice = false;
    DeviceBuffer device;                 // onDevice
    HostSlice slice;                     // !onDevice: this frame's piece of host memory...
    std::uint64_t sliceFrame = 0;        // ...made in this frame (a frame later the data is put again from the shadow)
    bool sliceUsed = false;              // a draw of this frame has read the slice (a later change gets a new one)
    std::vector<unsigned char> shadow;   // !onDevice: the contents
};

struct VertexAttrib {
    bool enabled = false;
    GLint size = 4;
    GLenum type = gl::FLOAT;
    bool normalized = false;
    GLsizei stride = 0;
    std::uintptr_t offset = 0;
    GLuint buffer = 0;
    GLuint divisor = 0;
};
constexpr int kMaxAttribs = 16;

struct GlVao {
    bool exists = false;
    VertexAttrib attribs[kMaxAttribs];
    GLuint elementBuffer = 0;
};

// ---- textures -----------------------------------------------------------------------------------------------------------------------------------------------

enum TexKind { Tex2D = 0, Tex2DArray = 1, Tex3D = 2, TexCube = 3 };

struct GlTexture {
    bool exists = false;
    bool kindKnown = false;
    TexKind kind = Tex2D;
    GLenum internalFormat = gl::RGBA8;
    int width = 0;
    int height = 0;
    int depth = 0;              // layers of an array, depth of a volume (6 for a cube)
    int levelsDefined = 0;      // as many as glTexImage was called for
    int maxLevel = 1000;
    GLenum minFilter = 0x2702;  // NEAREST_MIPMAP_LINEAR, the initial value
    GLenum magFilter = gl::LINEAR;
    GLenum wrapS = 0x2901;      // REPEAT
    GLenum wrapT = 0x2901;
    GLenum wrapR = 0x2901;
    bool attachable = false;    // created empty: it may be drawn into

    DeviceImage image;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t levels = 1;
    std::uint32_t layers = 1;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool created = false;
    bool dirty = false;         // written by a copy: needs to go to the shader-read layout before it is sampled
    bool written = false;       // has had contents put in it
    ClearRequest clear;         // as a render target
    bool targetValid = false;   // as a render target: drawn on since it was made
};

struct GlFramebuffer {
    bool exists = false;
    GLuint colorTexture = 0;
};

// ---- programs -----------------------------------------------------------------------------------------------------------------------------------------------

struct SpirvBlob;

struct GlShader {
    bool exists = false;
    GLenum type = 0;
    std::string source;
    const SpirvBlob* twin = nullptr;
    std::uint32_t specMask = 0;
    bool compiled = false;
    std::string log;
};

struct GlProgram {
    bool exists = false;
    std::vector<GLuint> attached;
    bool linked = false;
    std::string log;
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    std::uint32_t specMask = 0;
    SpirvInfo vertexInfo;
    SpirvInfo fragmentInfo;
    std::vector<SpirvInfo::Member> uniforms;     // the block's members, by uniform location
    std::vector<SpirvInfo::Sampler> samplers;    // after the members in the location numbers
    std::vector<SpirvInfo::Input> inputs;        // the vertex shader's
    std::uint32_t blockSize = 0;
    std::vector<unsigned char> block;            // the uniform values, as the block is laid out
    bool blockDirty = true;
    int samplerUnit[kMaxSamplers] = {};          // the texture unit each sampler (by binding) reads
    HostSlice blockSlice;                        // the block as it was put in this frame's memory...
    std::uint64_t blockFrame = 0;                // ...in this frame
};

struct GlQuery {
    bool exists = false;
    bool started = false;
    bool ready = false;
    std::uint32_t firstQuery = 0;  // the index of its first timestamp in the frame's pool
    std::uint64_t resultNs = 0;
};

// ---- the state ----------------------------------------------------------------------------------------------------------------------------------------------

struct Unit {
    GLuint texture[4] = {};  // bound per kind of texture
};

// The target the game is drawing on: the window's picture or a texture.
struct Target {
    bool isBackbuffer = true;
    GlTexture* texture = nullptr;
    VkImage colorImage = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImageLayout* colorLayout = nullptr;
    VkImageLayout* depthLayout = nullptr;
    bool* colorValid = nullptr;
    bool* depthValid = nullptr;
    ClearRequest* clear = nullptr;
    [[nodiscard]] bool same(const Target& other) const { return colorImage == other.colorImage; }
};

struct GlState {
    // objects (by id; an id is never given out twice)
    std::vector<GlBuffer> buffers = std::vector<GlBuffer>(1);
    std::vector<GlVao> vaos = std::vector<GlVao>(1);
    std::vector<GlTexture> textures = std::vector<GlTexture>(1);
    std::vector<GlFramebuffer> framebuffers = std::vector<GlFramebuffer>(1);
    std::vector<GlShader> shaders = std::vector<GlShader>(1);
    std::vector<GlProgram> programs = std::vector<GlProgram>(1);
    std::vector<GlQuery> queries = std::vector<GlQuery>(1);

    // bindings
    GLuint arrayBuffer = 0;
    GLuint copyReadBuffer = 0;
    GLuint copyWriteBuffer = 0;
    GLuint drawIndirectBuffer = 0;
    GLuint vao = 0;
    GLuint framebuffer = 0;
    GLuint program = 0;
    int activeUnit = 0;
    Unit units[16];

    // fixed-function state
    bool depthTest = false;
    bool cullFace = false;
    bool blend = false;
    bool scissorTest = false;
    GLenum depthFunc = 0x0201;  // LESS
    bool depthMask = true;
    GLenum cullMode = 0x0405;   // BACK
    GLenum frontFace = 0x0901;  // CCW
    GLenum blendSrcRgb = gl::ONE, blendDstRgb = gl::ZERO, blendSrcAlpha = gl::ONE, blendDstAlpha = gl::ZERO;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint scissor[4] = {0, 0, 0, 0};
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLint unpackAlignment = 4;
    GLint packAlignment = 4;
    bool viewportSet = false;

    // recording
    bool rendering = false;
    Target renderTarget;
    bool bufferBarrierPending = false;
    bool drawnSinceBarrier = false;            // draws have read buffers since the last barrier: a copy into one of them waits for them
    std::vector<GLuint> dirtyTextures;       // written by copies, to be moved to the shader-read layout before the next pass
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundUniformSet = VK_NULL_HANDLE;
    VkDescriptorSet boundSamplerSet = VK_NULL_HANDLE;
    std::uint32_t boundUniformOffset = 0xFFFFFFFFu;
    bool viewportKnown = false;
    VkViewport boundViewport{};
    VkRect2D boundScissor{};
    bool scissorKnown = false;
    std::uint64_t preparedFrame = 0;  // the frame the per-frame set-up has run for
    int queriesOpen = 0;
    GLuint openQuery = 0;
    GLuint dummyTextures[4] = {};     // what an unbound sampler reads (black)
    DeviceBuffer zeroBuffer;          // what a vertex input with no array reads
};

[[nodiscard]] GlState& gls();

// The implementations of the GL functions, one for every entry of gl.hpp's lists (they are put into gl:: by installGlFunctions).
namespace glemu {
#define FALCON_GLEMU_DECLARE(ret, name, args) ret name args;
FALCON_GL_FUNCTIONS(FALCON_GLEMU_DECLARE)
FALCON_GL_OPTIONAL_FUNCTIONS(FALCON_GLEMU_DECLARE)
#undef FALCON_GLEMU_DECLARE
}  // namespace glemu

// ---- shared between the gl_*.cpp files ----------------------------------------------------------------------------------------------------------------------

// Installs the Vulkan implementation into gl:: (gl_misc.cpp).
void installGlFunctions();
void resetGlState();

// buffers (gl_buffers.cpp)
[[nodiscard]] GlBuffer* bufferPtr(GLuint id);
// Where the buffer's data is for this frame's commands (a host-visible buffer from an earlier frame is put in this frame's memory again).
[[nodiscard]] BufferBacking backingOf(GLuint id);
// Copies `bytes` into a device buffer at `offset` (through the frame's staging memory).
void uploadToBuffer(GlBuffer& buffer, VkDeviceSize offset, const void* data, VkDeviceSize bytes);
// Before a copy: the pass is ended and draws that read buffers are finished with.
void beforeTransfer();
void destroyBufferObjects();

// textures (gl_textures.cpp)
[[nodiscard]] GlTexture* texturePtr(GLuint id);
[[nodiscard]] bool targetOf(GLuint framebuffer, Target& out);
// The Vulkan image behind a texture (made when first needed). False when it cannot be made (no size yet).
[[nodiscard]] bool ensureTextureImage(GlTexture& texture);
// The view and sampler a draw reads a texture of this kind at this unit with (the black stand-in when nothing usable is bound).
void samplerBinding(int unit, int kind, VkImageView& view, VkSampler& sampler);
// Moves the textures written by copies to the shader-read layout and makes buffer writes visible: before a pass starts or the frame ends.
void flushTransfers();
void createDummyTextures();
void destroyTextureObjects();

// programs (gl_programs.cpp)
[[nodiscard]] GlProgram* programPtr(GLuint id);
struct PipelineRequest {
    GlProgram* program = nullptr;
    GlVao* vao = nullptr;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const Target* target = nullptr;
};
// The pipeline for the current state (made when first needed), and the vertex buffers the vertex array's attributes read for it.
struct VertexBindings {
    VkBuffer buffers[kMaxAttribs] = {};
    VkDeviceSize offsets[kMaxAttribs] = {};
    std::uint32_t count = 0;
};
[[nodiscard]] VkPipeline pipelineFor(const PipelineRequest& request, VertexBindings& bindings);
void destroyProgramObjects();

// rendering (gl_draw.cpp)
// Starts a pass on the current target (or goes on with the one that is open). False when there is no target to draw on.
[[nodiscard]] bool beginRendering();
void endRendering();
[[nodiscard]] bool currentTarget(Target& out);
// Makes ready for the next draw and sets everything it needs; false when the draw cannot be made (no program, no target).
[[nodiscard]] bool prepareDraw(VkPrimitiveTopology topology, bool indexed, GLenum indexType, std::uintptr_t indexOffset, std::uint32_t& firstIndexAdjust);
// Makes the frame's per-frame set-up (the backbuffer's size, the first viewport) if the frame has none.
void prepareFrame();

}  // namespace falcon::vulkan
