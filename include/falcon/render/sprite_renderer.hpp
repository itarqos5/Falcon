#pragma once

#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/gfx/texture_array.hpp"
#include "falcon/render/camera.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace falcon {

// Quads with a picture on them, faded into the world's fog: the clouds, the drops of rain and snow, the bits a broken block throws and the soft shadow under a mob are all
// drawn with it. The corners are given in the world, relative to the camera, and the caller decides which way a quad faces (a billboard is built from the camera's right and
// up). Everything added in one frame goes into one buffer and is drawn in one call.
class SpriteRenderer {
public:
    SpriteRenderer() = default;
    ~SpriteRenderer();
    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    // The program, the buffer and the vertex array (needs the GL context: call it once the window exists).
    void init();

    // One frame: `viewProj` for the camera, the world's fog, the plain picture (the clouds, the rain: whatever is not a block picture), the block pictures a particle's bits come
    // from, and the light map. Nothing is drawn while `quads` is empty.
    void begin(const Matrix4f& viewProj, const std::array<float, 3>& fogColor, const std::array<float, 2>& fogRange, const Texture2D& picture, const TextureArray2D* blockPictures,
               gl::GLuint lightmap);
    // A quad: its four corners in order round it (in the world, relative to the camera), the picture's corners, the tint, how opaque it is, and either the plain picture or
    // `layer` of the block pictures (a block's own bits).
    void quad(const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2, const std::array<float, 3>& p3, float u0, float v0, float u1, float v1,
              const std::array<float, 3>& tint, float alpha, float layer = 0.0f, bool fromBlocks = false);
    // Draws what was added (blended, tested against the depth buffer, not writing it: the world is already drawn) and puts the GL state back.
    void end();

    [[nodiscard]] bool empty() const { return vertices_.empty(); }

private:
    struct Vertex {
        float x, y, z;
        float u, v;
        float r, g, b, a;
        float layer;
        float fromBlocks;
    };
    void pushQuad(const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2, const std::array<float, 3>& p3, float u0, float v0, float u1, float v1,
                  float r, float g, float b, float a, float layer, float fromBlocks);

    ShaderProgram shader_;
    gl::GLuint vao_ = 0, vbo_ = 0;
    gl::GLsizeiptr capacity_ = 0;  // bytes the buffer holds
    gl::GLint uViewProj_ = -1, uFogColor_ = -1, uFogRange_ = -1, uPicture_ = -1, uPictures_ = -1, uLightmap_ = -1, uFullbright_ = -1;
    std::vector<Vertex> vertices_;
};

}  // namespace falcon
