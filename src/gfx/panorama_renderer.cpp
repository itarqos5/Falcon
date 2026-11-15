#include "falcon/gfx/panorama_renderer.hpp"

#include "falcon/util/math.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace falcon {
namespace {

const char* const kVertexSource = R"(// vk: panorama.vert
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uProjection;
uniform mat4 uView;
out vec3 vDirection;
void main() {
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
    vDirection = aPos;
}
)";

const char* const kFragmentSource = R"(// vk: panorama.frag
#version 330 core
uniform samplerCube uSky;
in vec3 vDirection;
out vec4 fragColor;
void main() {
    fragColor = texture(uSky, vDirection);
}
)";

// Full-screen triangle generated from gl_VertexID, like the game's screenquad.vsh.
const char* const kBlurVertexSource = R"(// vk: blur.vert
#version 330 core
out vec2 texCoord;
void main() {
    vec2 uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    texCoord = uv;
}
)";

// The game's post/box_blur.fsh: bilinear taps two pixels apart, so each sample covers two texels.
// With uRadius == 0 it degenerates to a plain copy, which is used to present the result.
const char* const kBlurFragmentSource = R"(// vk: blur.frag
#version 330 core
uniform sampler2D uInput;
uniform vec2 uInSize;
uniform vec2 uBlurDir;
uniform float uRadius;
in vec2 texCoord;
out vec4 fragColor;
void main() {
    vec2 oneTexel = 1.0 / uInSize;
    vec2 sampleStep = oneTexel * uBlurDir;
    vec4 blurred = vec4(0.0);
    for (float a = -uRadius + 0.5; a <= uRadius; a += 2.0) {
        blurred += texture(uInput, texCoord + sampleStep * a);
    }
    blurred += texture(uInput, texCoord + sampleStep * uRadius) / 2.0;
    fragColor = blurred / (uRadius + 0.5);
}
)";

// Face files in OpenGL cube map order (+X, -X, +Y, -Y, +Z, -Z), exactly as the game maps them.
constexpr std::array<int, 6> kFaceFiles = {1, 3, 5, 4, 0, 2};

Image loadFace(const ResourceManager& resources, unsigned face) {
    auto image = PanoramaRenderer::decodeFace(resources, face);
    if (!image) {
        throw std::runtime_error("missing or broken panorama face " +
                                 PanoramaRenderer::faceLocation(face).toString());
    }
    return std::move(*image);
}

// Unit cube; the vertex position doubles as the cube map sampling direction.
constexpr std::array<float, 24> kCubeVertices = {
    -1, -1, -1,  // 0
    1,  -1, -1,  // 1
    1,  1,  -1,  // 2
    -1, 1,  -1,  // 3
    -1, -1, 1,   // 4
    1,  -1, 1,   // 5
    1,  1,  1,   // 6
    -1, 1,  1,   // 7
};

constexpr std::array<unsigned short, 36> kCubeIndices = {
    0, 1, 2, 0, 2, 3,  // -Z
    4, 6, 5, 4, 7, 6,  // +Z
    0, 3, 7, 0, 7, 4,  // -X
    1, 5, 6, 1, 6, 2,  // +X
    3, 2, 6, 3, 6, 7,  // +Y
    0, 4, 5, 0, 5, 1,  // -Y
};

}  // namespace

ResourceLocation PanoramaRenderer::faceLocation(unsigned face) {
    return ResourceLocation(
        "minecraft",
        "textures/gui/title/background/panorama_" + std::to_string(kFaceFiles[face]) + ".png");
}

std::optional<Image> PanoramaRenderer::decodeFace(const ResourceManager& resources, unsigned face) {
    const auto bytes = resources.readBytes(faceLocation(face));
    if (!bytes) return std::nullopt;
    auto image = decodePng(*bytes);
    if (!image) return std::nullopt;
    // The game stores each face vertically flipped; the view matrix compensates.
    image->flipVertical();
    return image;
}

PanoramaRenderer::PanoramaRenderer(const ResourceManager& resources)
    : PanoramaRenderer(std::function<Image(unsigned)>([&resources](unsigned face) { return loadFace(resources, face); })) {}

PanoramaRenderer::PanoramaRenderer(const std::function<Image(unsigned face)>& faceProvider)
    : shader_("panorama", kVertexSource, kFragmentSource),
      blurShader_("box_blur", kBlurVertexSource, kBlurFragmentSource),
      cubeMap_(faceProvider, FilterMode::Linear) {
    projLoc_ = shader_.uniform("uProjection");
    viewLoc_ = shader_.uniform("uView");
    skyLoc_ = shader_.uniform("uSky");
    blurSamplerLoc_ = blurShader_.uniform("uInput");
    blurSizeLoc_ = blurShader_.uniform("uInSize");
    blurDirLoc_ = blurShader_.uniform("uBlurDir");
    blurRadiusLoc_ = blurShader_.uniform("uRadius");
    gl::GenVertexArrays(1, &emptyVao_);

    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::GenBuffers(1, &ibo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(sizeof(kCubeVertices)),
                   kCubeVertices.data(), gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 3 * static_cast<gl::GLsizei>(sizeof(float)), nullptr);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, ibo_);
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(sizeof(kCubeIndices)),
                   kCubeIndices.data(), gl::STATIC_DRAW);
    gl::BindVertexArray(0);
}

PanoramaRenderer::~PanoramaRenderer() {
    if (emptyVao_ != 0) gl::DeleteVertexArrays(1, &emptyVao_);
    if (ibo_ != 0) gl::DeleteBuffers(1, &ibo_);
    if (vbo_ != 0) gl::DeleteBuffers(1, &vbo_);
    if (vao_ != 0) gl::DeleteVertexArrays(1, &vao_);
}

void PanoramaRenderer::render(float pitchDegrees, float yawDegrees, int framebufferWidth,
                              int framebufferHeight, int blurRadius) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;

    if (blurRadius < 1) {
        drawCube(pitchDegrees, yawDegrees, framebufferWidth, framebufferHeight);
        return;
    }

    // Blurred: draw the panorama off-screen, run 3 x (horizontal + vertical) box blur passes
    // (the game's blur.json), then present the result.
    targetA_.resize(framebufferWidth, framebufferHeight);
    targetB_.resize(framebufferWidth, framebufferHeight);

    targetA_.bindForDrawing();
    drawCube(pitchDegrees, yawDegrees, framebufferWidth, framebufferHeight);
    const auto radius = static_cast<float>(blurRadius);
    for (int i = 0; i < 3; ++i) {
        targetB_.bindForDrawing();
        blurPass(targetA_, 1.0f, 0.0f, radius);
        targetA_.bindForDrawing();
        blurPass(targetB_, 0.0f, 1.0f, radius);
    }

    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::Viewport(0, 0, framebufferWidth, framebufferHeight);
    blurPass(targetA_, 1.0f, 0.0f, 0.0f);  // radius 0: copy to the screen
}

void PanoramaRenderer::blurPass(const RenderTarget& source, float dirX, float dirY, float radius) {
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    blurShader_.use();
    gl::Uniform1i(blurSamplerLoc_, 0);
    gl::Uniform2f(blurSizeLoc_, static_cast<float>(source.width()),
                  static_cast<float>(source.height()));
    gl::Uniform2f(blurDirLoc_, dirX, dirY);
    gl::Uniform1f(blurRadiusLoc_, radius);
    source.bindTexture(0);
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);
    gl::BindVertexArray(0);
}

void PanoramaRenderer::drawCube(float pitchDegrees, float yawDegrees, int framebufferWidth,
                                int framebufferHeight) {
    // Same camera as the game: 85 degree vertical FOV, near 0.05, far 10; the view is
    // rotationX(pi) * rotateX(pitch) * rotateY(yaw).
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    const Mat4 projection = Mat4::perspective(85.0f * kDegToRad, aspect, 0.05f, 10.0f);
    const Mat4 view = Mat4::rotationX(kPi) * Mat4::rotationX(pitchDegrees * kDegToRad) *
                      Mat4::rotationY(yawDegrees * kDegToRad);

    gl::Viewport(0, 0, framebufferWidth, framebufferHeight);
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::TEXTURE_CUBE_MAP_SEAMLESS);

    shader_.use();
    gl::UniformMatrix4fv(projLoc_, 1, 0, projection.m.data());
    gl::UniformMatrix4fv(viewLoc_, 1, 0, view.m.data());
    gl::Uniform1i(skyLoc_, 0);
    cubeMap_.bind(0);

    gl::BindVertexArray(vao_);
    gl::DrawElements(gl::TRIANGLES, static_cast<gl::GLsizei>(kCubeIndices.size()),
                     gl::UNSIGNED_SHORT, nullptr);
    gl::BindVertexArray(0);
}

}  // namespace falcon
