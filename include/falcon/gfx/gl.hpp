#pragma once

// Minimal OpenGL 3.3 core loader. Only the entry points the client uses are declared;
// call falcon::gl::load() once, after a context has been made current.

#include <cstddef>

#ifndef APIENTRY
#ifdef _WIN32
#define APIENTRY __stdcall
#else
#define APIENTRY
#endif
#endif

namespace falcon::gl {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLuint64 = unsigned long long;
using GLsync = void*;  // (the driver's own opaque pointer)

inline constexpr GLenum FALSE_ = 0;
inline constexpr GLenum TRUE_ = 1;
inline constexpr GLenum COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr GLenum DEPTH_TEST = 0x0B71;
inline constexpr GLenum CULL_FACE = 0x0B44;
inline constexpr GLenum BLEND = 0x0BE2;
inline constexpr GLenum SCISSOR_TEST = 0x0C11;
inline constexpr GLenum ZERO = 0;
inline constexpr GLenum ONE = 1;
inline constexpr GLenum SRC_ALPHA = 0x0302;
inline constexpr GLenum ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum TEXTURE_2D = 0x0DE1;
inline constexpr GLenum TEXTURE_2D_ARRAY = 0x8C1A;
inline constexpr GLenum TEXTURE_3D = 0x806F;
inline constexpr GLenum TEXTURE1 = 0x84C1;
inline constexpr GLenum R8 = 0x8229;
inline constexpr GLenum RED = 0x1903;
inline constexpr GLenum TEXTURE_MAX_LEVEL = 0x813D;
inline constexpr GLenum NEAREST_MIPMAP_NEAREST = 0x2700;
inline constexpr GLenum NEAREST_MIPMAP_LINEAR = 0x2702;
inline constexpr GLenum REPEAT = 0x2901;
inline constexpr GLenum LESS = 0x0201;
inline constexpr GLenum LEQUAL = 0x0203;
inline constexpr GLenum CW = 0x0900;
inline constexpr GLenum CCW = 0x0901;
inline constexpr GLenum BACK = 0x0405;
inline constexpr GLenum UNSIGNED_INT = 0x1405;
inline constexpr GLenum LINES = 0x0001;
inline constexpr GLenum TIME_ELAPSED = 0x88BF;
inline constexpr GLenum QUERY_RESULT = 0x8866;
inline constexpr GLenum QUERY_RESULT_AVAILABLE = 0x8867;
inline constexpr GLenum DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum TEXTURE_CUBE_MAP = 0x8513;
inline constexpr GLenum TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
inline constexpr GLenum TEXTURE_CUBE_MAP_SEAMLESS = 0x884F;
inline constexpr GLenum TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum TEXTURE_WRAP_R = 0x8072;
inline constexpr GLenum NEAREST = 0x2600;
inline constexpr GLenum LINEAR = 0x2601;
inline constexpr GLenum CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum RGBA = 0x1908;
inline constexpr GLenum RGBA8 = 0x8058;
inline constexpr GLenum UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum SHORT = 0x1402;
inline constexpr GLenum UNSIGNED_SHORT = 0x1403;
inline constexpr GLenum FLOAT = 0x1406;
inline constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum PACK_ALIGNMENT = 0x0D05;
inline constexpr GLenum ARRAY_BUFFER = 0x8892;
inline constexpr GLenum ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum COPY_READ_BUFFER = 0x8F36;
inline constexpr GLenum COPY_WRITE_BUFFER = 0x8F37;
inline constexpr GLenum DRAW_INDIRECT_BUFFER = 0x8F3F;  // (GL 4.3)
inline constexpr GLenum RGBA16F = 0x881A;
inline constexpr GLenum STATIC_DRAW = 0x88E4;
inline constexpr GLenum STREAM_DRAW = 0x88E0;
inline constexpr GLenum TRIANGLES = 0x0004;
inline constexpr GLenum VERTEX_SHADER = 0x8B31;
inline constexpr GLenum FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum COMPILE_STATUS = 0x8B81;
inline constexpr GLenum LINK_STATUS = 0x8B82;
inline constexpr GLenum INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum TEXTURE0 = 0x84C0;
inline constexpr GLenum FRAMEBUFFER = 0x8D40;
inline constexpr GLenum COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
inline constexpr GLenum VENDOR = 0x1F00;
inline constexpr GLenum RENDERER = 0x1F01;
inline constexpr GLenum SYNC_GPU_COMMANDS_COMPLETE = 0x9117;
inline constexpr GLbitfield SYNC_FLUSH_COMMANDS_BIT = 0x1;
inline constexpr GLenum VERSION = 0x1F02;

// X(return type, name without the "gl" prefix, parameter list)
#define FALCON_GL_FUNCTIONS(X)                                                                  \
    X(void, Clear, (GLbitfield))                                                              \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                 \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                       \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                        \
    X(void, Enable, (GLenum))                                                                 \
    X(void, Disable, (GLenum))                                                                \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                              \
    X(void, PixelStorei, (GLenum, GLint))                                                     \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))              \
    X(const GLubyte*, GetString, (GLenum))                                                    \
    X(GLenum, GetError, ())                                                                   \
    X(void, GenTextures, (GLsizei, GLuint*))                                                  \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                         \
    X(void, BindTexture, (GLenum, GLuint))                                                    \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                           \
    X(void, ActiveTexture, (GLenum))                                                          \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                   \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                          \
    X(void, BindBuffer, (GLenum, GLuint))                                                     \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                            \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                       \
    X(void, CopyBufferSubData, (GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr))              \
    X(void, MultiDrawElementsBaseVertex, (GLenum, const GLsizei*, GLenum, const void* const*, GLsizei, const GLint*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                              \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                     \
    X(void, BindVertexArray, (GLuint))                                                        \
    X(void, EnableVertexAttribArray, (GLuint))                                                \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))    \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                             \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                             \
    X(GLuint, CreateShader, (GLenum))                                                         \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))              \
    X(void, CompileShader, (GLuint))                                                          \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                            \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                           \
    X(void, DeleteShader, (GLuint))                                                           \
    X(GLuint, CreateProgram, ())                                                              \
    X(void, AttachShader, (GLuint, GLuint))                                                   \
    X(void, LinkProgram, (GLuint))                                                            \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                           \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                          \
    X(void, DeleteProgram, (GLuint))                                                          \
    X(void, UseProgram, (GLuint))                                                             \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                     \
    X(void, Uniform1i, (GLint, GLint))                                                        \
    X(void, Uniform1f, (GLint, GLfloat))                                                      \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                             \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                              \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                     \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                    \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                               \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                    \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                    \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                           \
    X(void, DepthFunc, (GLenum))                                                              \
    X(void, DepthMask, (GLboolean))                                                           \
    X(void, CullFace, (GLenum))                                                               \
    X(void, FrontFace, (GLenum))                                                              \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, GenerateMipmap, (GLenum))                                                         \
    X(void, GenQueries, (GLsizei, GLuint*))                                                   \
    X(void, DeleteQueries, (GLsizei, const GLuint*))                                          \
    X(void, BeginQuery, (GLenum, GLuint))                                                     \
    X(void, EndQuery, (GLenum))                                                               \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, GLuint64*))                                 \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint*))                                       \
    X(void, DrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void*, GLint))            \
    X(void, VertexAttribDivisor, (GLuint, GLuint))                                            \
    X(GLsync, FenceSync, (GLenum, GLbitfield))                                                \
    X(GLenum, ClientWaitSync, (GLsync, GLbitfield, GLuint64))                                 \
    X(void, DeleteSync, (GLsync))

#define FALCON_GL_DECLARE(ret, name, args)       \
    using PFN_##name = ret(APIENTRY*) args;    \
    extern PFN_##name name;
FALCON_GL_FUNCTIONS(FALCON_GL_DECLARE)
#undef FALCON_GL_DECLARE

// Entry points of newer OpenGL versions, loaded when the context has them and used only after checking multiDrawIndirectAvailable().
#define FALCON_GL_OPTIONAL_FUNCTIONS(X) \
    X(void, MultiDrawElementsIndirect, (GLenum, GLenum, const void*, GLsizei, GLsizei))
#define FALCON_GL_DECLARE_OPTIONAL(ret, name, args) \
    using PFN_##name = ret(APIENTRY*) args;         \
    extern PFN_##name name;
FALCON_GL_OPTIONAL_FUNCTIONS(FALCON_GL_DECLARE_OPTIONAL)
#undef FALCON_GL_DECLARE_OPTIONAL

// The version of the current context (set by load()).
extern int contextMajor;
extern int contextMinor;
// glMultiDrawElementsIndirect with a base instance per command (OpenGL 4.3): one call draws a whole list of sections.
[[nodiscard]] bool multiDrawIndirectAvailable();

// Resolves every entry point. Throws std::runtime_error listing any that are missing.
void load();

}  // namespace falcon::gl
