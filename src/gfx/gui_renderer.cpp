#include "falcon/gfx/gui_renderer.hpp"

#include <algorithm>
#include <cstddef>

namespace falcon {
namespace {

const char* const kVertexSource = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* const kFragmentSource = R"(#version 330 core
uniform sampler2D uTexture;
uniform float uCutoff;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec4 color = texture(uTexture, vUV) * vColor;
    if (color.a == 0.0 || color.a < uCutoff) {
        discard;
    }
    fragColor = color;
}
)";

Image makeWhitePixel() {
    Image img;
    img.width = 1;
    img.height = 1;
    img.rgba = {255, 255, 255, 255};
    return img;
}

}  // namespace

GuiRenderer::GuiRenderer()
    : shader_("gui", kVertexSource, kFragmentSource), white_(makeWhitePixel(), FilterMode::Nearest) {
    projLoc_ = shader_.uniform("uProjection");
    texLoc_ = shader_.uniform("uTexture");
    cutoffLoc_ = shader_.uniform("uCutoff");

    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto stride = static_cast<gl::GLsizei>(sizeof(Vertex));
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, u)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, stride,
                            reinterpret_cast<const void*>(offsetof(Vertex, r)));
    gl::BindVertexArray(0);
    vertices_.reserve(4096);
}

GuiRenderer::~GuiRenderer() {
    if (vbo_ != 0) gl::DeleteBuffers(1, &vbo_);
    if (vao_ != 0) gl::DeleteVertexArrays(1, &vao_);
}

void GuiRenderer::begin(int framebufferWidth, int framebufferHeight, int guiScale) {
    framebufferWidth_ = framebufferWidth;
    framebufferHeight_ = framebufferHeight;
    guiScale_ = guiScale;
    // 1 GUI unit is exactly `guiScale` framebuffer pixels.
    projection_ = Mat4::ortho(0.0f, static_cast<float>(framebufferWidth) / static_cast<float>(guiScale),
                              static_cast<float>(framebufferHeight) / static_cast<float>(guiScale),
                              0.0f, -1.0f, 1.0f);
    pose_ = Pose2D{};
    poseStack_.clear();
    textMode_ = false;
    restoreState();
}

void GuiRenderer::restoreState() {
    gl::Viewport(0, 0, framebufferWidth_, framebufferHeight_);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Enable(gl::BLEND);
    gl::BlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    currentTexture_ = 0;
}

void GuiRenderer::end() { flush(); }

void GuiRenderer::pushPose() { poseStack_.push_back(pose_); }

void GuiRenderer::popPose() {
    if (poseStack_.empty()) return;
    pose_ = poseStack_.back();
    poseStack_.pop_back();
}

void GuiRenderer::setTextMode(bool enabled) {
    if (enabled == textMode_) return;
    flush();
    textMode_ = enabled;
}

void GuiRenderer::enableScissor(int x0, int y0, int x1, int y1) {
    flush();
    const int s = guiScale_;
    const int left = std::clamp(x0 * s, 0, framebufferWidth_);
    const int right = std::clamp(x1 * s, left, framebufferWidth_);
    const int top = std::clamp(y0 * s, 0, framebufferHeight_);
    const int bottom = std::clamp(y1 * s, top, framebufferHeight_);
    gl::Enable(gl::SCISSOR_TEST);
    // glScissor's origin is the bottom-left corner.
    gl::Scissor(left, framebufferHeight_ - bottom, right - left, bottom - top);
}

void GuiRenderer::disableScissor() {
    flush();
    gl::Disable(gl::SCISSOR_TEST);
}

void GuiRenderer::flush() {
    if (vertices_.empty()) return;
    shader_.use();
    gl::Uniform1i(texLoc_, 0);
    gl::Uniform1f(cutoffLoc_, textMode_ ? 0.1f : 0.0f);
    gl::UniformMatrix4fv(projLoc_, 1, 0, projection_.m.data());
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, currentTexture_);

    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                   vertices_.data(), gl::STREAM_DRAW);
    gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(vertices_.size()));
    gl::BindVertexArray(0);
    vertices_.clear();
}

void GuiRenderer::pushVertex(float x, float y, float u, float v, std::uint32_t argb) {
    float tx = 0.0f;
    float ty = 0.0f;
    pose_.apply(x, y, tx, ty);
    vertices_.push_back(Vertex{tx, ty, u, v, static_cast<std::uint8_t>((argb >> 16) & 0xFF),
                               static_cast<std::uint8_t>((argb >> 8) & 0xFF),
                               static_cast<std::uint8_t>(argb & 0xFF),
                               static_cast<std::uint8_t>((argb >> 24) & 0xFF)});
}

void GuiRenderer::quad(const Texture2D& texture, float x0, float y0, float x1, float y1, float u0,
                       float v0, float u1, float v1, std::uint32_t argb) {
    if (currentTexture_ != texture.id()) {
        flush();
        currentTexture_ = texture.id();
    }
    pushVertex(x0, y0, u0, v0, argb);
    pushVertex(x1, y0, u1, v0, argb);
    pushVertex(x1, y1, u1, v1, argb);
    pushVertex(x0, y0, u0, v0, argb);
    pushVertex(x1, y1, u1, v1, argb);
    pushVertex(x0, y1, u0, v1, argb);
}

void GuiRenderer::fill(float x0, float y0, float x1, float y1, std::uint32_t argb) {
    quad(white_, x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, argb);
}

}  // namespace falcon
