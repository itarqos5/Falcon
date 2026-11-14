#pragma once

#include "falcon/assets/resource_manager.hpp"
#include "falcon/gfx/render_target.hpp"
#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"

#include <functional>
#include <optional>

namespace falcon {

// Draws the six-face title screen panorama as a cube map seen from the inside, optionally
// blurred the way menu screens blur it (the game's "Menu Background Blur" option).
class PanoramaRenderer {
public:
    explicit PanoramaRenderer(const ResourceManager& resources);
    // Takes each face (0..5, in cube map order) from `faceProvider`, e.g. pictures decoded earlier on
    // other threads; it is called once per face, in order.
    explicit PanoramaRenderer(const std::function<Image(unsigned face)>& faceProvider);
    ~PanoramaRenderer();
    PanoramaRenderer(const PanoramaRenderer&) = delete;
    PanoramaRenderer& operator=(const PanoramaRenderer&) = delete;

    // Draws over the whole framebuffer. Angles are in degrees. `blurRadius` is in framebuffer
    // pixels (the option's 0..10 value); 0 draws the panorama sharp.
    void render(float pitchDegrees, float yawDegrees, int framebufferWidth, int framebufferHeight,
                int blurRadius = 0);

    // ---- reloading resources (see ResourceReloader) ----
    // Faces in GL cube map order (+X, -X, +Y, -Y, +Z, -Z), as the game maps its panorama_N.png.
    [[nodiscard]] static ResourceLocation faceLocation(unsigned face);
    // Reads one face without touching GL (safe on any thread); null if it is missing or broken.
    [[nodiscard]] static std::optional<Image> decodeFace(const ResourceManager& resources, unsigned face);
    // Swaps in a finished cube map (built with CubeMapTexture::allocate/setFace); `fresh` ends up
    // holding the old one.
    void replaceCubeMap(CubeMapTexture& fresh) { cubeMap_.swap(fresh); }

private:
    void drawCube(float pitchDegrees, float yawDegrees, int width, int height);
    // One full-screen pass of the box blur shader from `source` into the bound framebuffer.
    void blurPass(const RenderTarget& source, float dirX, float dirY, float radius);

    ShaderProgram shader_;
    ShaderProgram blurShader_;
    CubeMapTexture cubeMap_;
    RenderTarget targetA_;
    RenderTarget targetB_;
    gl::GLuint vao_ = 0;
    gl::GLuint vbo_ = 0;
    gl::GLuint ibo_ = 0;
    gl::GLuint emptyVao_ = 0;  // attribute-less VAO for the full-screen triangle
    gl::GLint projLoc_ = -1;
    gl::GLint viewLoc_ = -1;
    gl::GLint skyLoc_ = -1;
    gl::GLint blurSamplerLoc_ = -1;
    gl::GLint blurSizeLoc_ = -1;
    gl::GLint blurDirLoc_ = -1;
    gl::GLint blurRadiusLoc_ = -1;
};

}  // namespace falcon
