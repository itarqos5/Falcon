#include "falcon/render/sprite_renderer.hpp"

#include "falcon/gfx/gl.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

// A quad with a picture on it, drawn over the world: the clouds (a flat layer of the game's own cloud picture), the drops of rain and snow, the bits a broken block throws
// (which come out of the block picture array, so a particle looks like the block it came from) and the soft shadow under a mob. Unlit (the light map's sky light is used) and
// faded into the world's fog, so a cloud at the edge of the distance melts into the horizon. The caller gives the corners already turned to face the camera.
// (The Vulkan twins are src/vulkan/shaders/sprite.vert and sprite.frag.)
constexpr char kVertexShader[] = R"(// vk: sprite.vert
#version 330 core
layout(location = 0) in vec3 aPos;     // the corner, in the world, relative to the camera
layout(location = 1) in vec2 aUv;      // where it is on the picture
layout(location = 2) in vec4 aColor;   // rgb: the tint; a: how opaque the quad is
layout(location = 3) in vec2 aInfo;    // x: the layer of the block pictures; y: 1 when the block pictures are used, 0 for the plain picture
uniform mat4 uViewProj;
out vec2 vUv;
out vec4 vColor;
out float vDist;
flat out float vLayer;
flat out float vFromBlocks;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUv = aUv;
    vColor = aColor;
    vDist = length(aPos);
    vLayer = aInfo.x;
    vFromBlocks = aInfo.y;
}
)";

constexpr char kFragmentShader[] = R"(// vk: sprite.frag
#version 330 core
in vec2 vUv;
in vec4 vColor;
in float vDist;
flat in float vLayer;
flat in float vFromBlocks;
uniform sampler2D uPicture;           // the clouds, the rain, a shadow
uniform sampler2DArray uPictures;     // the block pictures (a particle's bits)
uniform sampler2D uLightmap;          // the world's light map: sky light across, block light down
uniform vec3 uFogColor;
uniform vec2 uFogRange;               // the world's fog: start, end (distance from the camera)
uniform float uFullbright;            // 1: drawn at its own brightness (the sun through no light map at all), 0: lit by the sky
out vec4 frag;
void main() {
    vec4 t;
    float alpha;
    vec3 col;
    if (vLayer < -1.5) {  // the soft shadow under a mob: a round blob of its own, with no picture (nothing here is that plain shade of a circle with an edge that fades)
        vec2 d = vUv * 2.0 - 1.0;
        alpha = vColor.a * clamp(1.0 - dot(d, d), 0.0, 1.0);
        col = vColor.rgb;
    } else {
        t = vFromBlocks > 0.5 ? texture(uPictures, vec3(vUv, vLayer)) : texture(uPicture, vUv);
        alpha = t.a * vColor.a;
        col = t.rgb * vColor.rgb;
    }
    if (alpha < 0.02) discard;  // (the holes in the cloud picture, the clear parts of a picture, the corners of a shadow)
    if (uFullbright < 0.5) col *= texture(uLightmap, vec2(15.5 / 16.0, 0.5 / 16.0)).rgb;  // sky light 15, no block light
    float fog = clamp((vDist - uFogRange.x) / max(1.0, uFogRange.y - uFogRange.x), 0.0, 1.0);
    frag = vec4(mix(col, uFogColor, fog), alpha);
}
)";

}  // namespace

SpriteRenderer::~SpriteRenderer() {
    if (vbo_ != 0) gl::DeleteBuffers(1, &vbo_);
    if (vao_ != 0) gl::DeleteVertexArrays(1, &vao_);
}

void SpriteRenderer::init() {
    shader_ = ShaderProgram("sprite", kVertexShader, kFragmentShader);
    uViewProj_ = shader_.uniform("uViewProj");
    uPicture_ = shader_.uniform("uPicture");
    uPictures_ = shader_.uniform("uPictures");
    uLightmap_ = shader_.uniform("uLightmap");
    uFogColor_ = shader_.uniform("uFogColor");
    uFogRange_ = shader_.uniform("uFogRange");
    uFullbright_ = shader_.uniform("uFullbright");
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto stride = static_cast<gl::GLsizei>(sizeof(Vertex));
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, stride, nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(3 * sizeof(float)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(5 * sizeof(float)));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 2, gl::FLOAT, 0, stride, reinterpret_cast<const void*>(9 * sizeof(float)));
    gl::BindVertexArray(0);
}

void SpriteRenderer::begin(const Matrix4f& viewProj, const std::array<float, 3>& fogColor, const std::array<float, 2>& fogRange, const Texture2D& picture,
                           const TextureArray2D* blockPictures, gl::GLuint lightmap) {
    vertices_.clear();
    shader_.use();
    gl::UniformMatrix4fv(uViewProj_, 1, 0, viewProj.m.data());
    gl::Uniform3f(uFogColor_, fogColor[0], fogColor[1], fogColor[2]);
    gl::Uniform2f(uFogRange_, fogRange[0], fogRange[1]);
    gl::Uniform1f(uFullbright_, 0.0f);
    picture.bind(0);
    gl::Uniform1i(uPicture_, 0);
    if (blockPictures != nullptr) blockPictures->bind(1);
    gl::Uniform1i(uPictures_, 1);
    gl::ActiveTexture(gl::TEXTURE0 + 2);
    gl::BindTexture(gl::TEXTURE_2D, lightmap);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(uLightmap_, 2);
}

void SpriteRenderer::quad(const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2, const std::array<float, 3>& p3, float u0, float v0, float u1,
                          float v1, const std::array<float, 3>& tint, float alpha, float layer, bool fromBlocks) {
    pushQuad(p0, p1, p2, p3, u0, v0, u1, v1, tint[0], tint[1], tint[2], alpha, layer, fromBlocks ? 1.0f : 0.0f);
}

void SpriteRenderer::pushQuad(const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2, const std::array<float, 3>& p3, float u0, float v0,
                              float u1, float v1, float r, float g, float b, float a, float layer, float fromBlocks) {
    const auto add = [&](const std::array<float, 3>& p, float u, float v) {
        vertices_.push_back(Vertex{p[0], p[1], p[2], u, v, r, g, b, a, layer, fromBlocks});
    };
    add(p0, u0, v0);
    add(p1, u1, v0);
    add(p2, u1, v1);
    add(p0, u0, v0);
    add(p2, u1, v1);
    add(p3, u0, v1);
}

void SpriteRenderer::end() {
    if (vertices_.empty()) return;
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    gl::DepthMask(0);
    gl::Disable(gl::CULL_FACE);
    shader_.use();
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto bytes = static_cast<gl::GLsizeiptr>(vertices_.size() * sizeof(Vertex));
    if (bytes > capacity_) {  // (the buffer keeps the size of the largest frame it has drawn: a frame of clouds does not grow it again)
        gl::BufferData(gl::ARRAY_BUFFER, bytes, vertices_.data(), gl::DYNAMIC_DRAW);
        capacity_ = bytes;
    } else {
        gl::BufferSubData(gl::ARRAY_BUFFER, 0, bytes, vertices_.data());
    }
    gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(vertices_.size()));
    gl::DepthMask(1);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::CULL_FACE);
}

}  // namespace falcon
