#pragma once

#include "falcon/gfx/gl.hpp"

namespace falcon {

// An off-screen RGBA8 colour buffer (framebuffer object + texture) that can be drawn into and
// then sampled with bilinear filtering.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // (Re)creates the buffers when the size changes; a no-op when it is unchanged.
    void resize(int width, int height);

    // Makes this target the draw destination and sets the viewport to cover it.
    void bindForDrawing() const;
    void bindTexture(unsigned unit) const;

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    void destroy();
    gl::GLuint fbo_ = 0;
    gl::GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace falcon
