#include "falcon/gfx/render_target.hpp"

#include <stdexcept>

namespace falcon {

RenderTarget::~RenderTarget() { destroy(); }

void RenderTarget::destroy() {
    if (fbo_ != 0) gl::DeleteFramebuffers(1, &fbo_);
    if (texture_ != 0) gl::DeleteTextures(1, &texture_);
    fbo_ = 0;
    texture_ = 0;
}

void RenderTarget::resize(int width, int height) {
    if (width == width_ && height == height_ && fbo_ != 0) return;
    destroy();
    width_ = width;
    height_ = height;

    gl::GenTextures(1, &texture_);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA8), width, height, 0, gl::RGBA,
                   gl::UNSIGNED_BYTE, nullptr);

    gl::GenFramebuffers(1, &fbo_);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture_, 0);
    const gl::GLenum status = gl::CheckFramebufferStatus(gl::FRAMEBUFFER);
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    if (status != gl::FRAMEBUFFER_COMPLETE) {
        destroy();
        throw std::runtime_error("could not create an off-screen framebuffer");
    }
}

void RenderTarget::bindForDrawing() const {
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, width_, height_);
}

void RenderTarget::bindTexture(unsigned unit) const {
    gl::ActiveTexture(gl::TEXTURE0 + unit);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
}

}  // namespace falcon
