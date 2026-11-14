#include "falcon/gfx/texture.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace falcon {
namespace {

void setSampling(gl::GLenum target, FilterMode filter, bool withR) {
    const gl::GLenum f = filter == FilterMode::Nearest ? gl::NEAREST : gl::LINEAR;
    gl::TexParameteri(target, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(f));
    gl::TexParameteri(target, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(f));
    gl::TexParameteri(target, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(target, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    if (withR) {
        gl::TexParameteri(target, gl::TEXTURE_WRAP_R, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    }
}

}  // namespace

// ---- Texture2D -------------------------------------------------------------------------

Texture2D::Texture2D(const Image& image, FilterMode filter)
    : width_(image.width), height_(image.height) {
    if (image.empty()) throw std::runtime_error("cannot create a texture from an empty image");
    gl::GenTextures(1, &id_);
    gl::BindTexture(gl::TEXTURE_2D, id_);
    setSampling(gl::TEXTURE_2D, filter, false);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA8), image.width, image.height,
                   0, gl::RGBA, gl::UNSIGNED_BYTE, image.rgba.data());
}

Texture2D Texture2D::empty(int width, int height, FilterMode filter) {
    if (width <= 0 || height <= 0) throw std::runtime_error("cannot create an empty texture of size 0");
    Texture2D texture;
    texture.width_ = width;
    texture.height_ = height;
    gl::GenTextures(1, &texture.id_);
    gl::BindTexture(gl::TEXTURE_2D, texture.id_);
    setSampling(gl::TEXTURE_2D, filter, false);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    const std::vector<unsigned char> zeros(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA8), width, height, 0, gl::RGBA,
                   gl::UNSIGNED_BYTE, zeros.data());
    return texture;
}

void Texture2D::upload(int x, int y, const Image& image) {
    if (image.empty() || x < 0 || y < 0 || x + image.width > width_ || y + image.height > height_) {
        throw std::logic_error("texture upload outside the texture");
    }
    gl::BindTexture(gl::TEXTURE_2D, id_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, x, y, image.width, image.height, gl::RGBA, gl::UNSIGNED_BYTE, image.rgba.data());
}

Texture2D::~Texture2D() { destroy(); }

Texture2D::Texture2D(Texture2D&& other) noexcept
    : id_(std::exchange(other.id_, 0)),
      width_(std::exchange(other.width_, 0)),
      height_(std::exchange(other.height_, 0)) {}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = std::exchange(other.id_, 0);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

void Texture2D::destroy() {
    if (id_ != 0) {
        gl::DeleteTextures(1, &id_);
        id_ = 0;
    }
}

void Texture2D::swap(Texture2D& other) noexcept {
    std::swap(id_, other.id_);
    std::swap(width_, other.width_);
    std::swap(height_, other.height_);
}

void Texture2D::bind(unsigned unit) const {
    gl::ActiveTexture(gl::TEXTURE0 + unit);
    gl::BindTexture(gl::TEXTURE_2D, id_);
}

// ---- CubeMapTexture --------------------------------------------------------------------

CubeMapTexture::CubeMapTexture(const std::function<Image(unsigned face)>& loadFace, FilterMode filter) {
    allocate(filter);
    for (unsigned i = 0; i < 6; ++i) {
        const Image face = loadFace(i);  // freed at the end of this iteration
        try {
            setFace(i, face);
        } catch (...) {
            destroy();
            throw;
        }
    }
}

void CubeMapTexture::allocate(FilterMode filter) {
    destroy();
    gl::GenTextures(1, &id_);
    gl::BindTexture(gl::TEXTURE_CUBE_MAP, id_);
    setSampling(gl::TEXTURE_CUBE_MAP, filter, true);
}

void CubeMapTexture::setFace(unsigned face, const Image& image) {
    if (id_ == 0 || face >= 6) throw std::logic_error("cube map face set before allocate()");
    if (image.empty() || image.width != image.height) {
        throw std::runtime_error("cube map faces must be non-empty squares");
    }
    gl::BindTexture(gl::TEXTURE_CUBE_MAP, id_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, static_cast<gl::GLint>(gl::RGBA8),
                   image.width, image.height, 0, gl::RGBA, gl::UNSIGNED_BYTE, image.rgba.data());
}

CubeMapTexture::~CubeMapTexture() { destroy(); }

CubeMapTexture::CubeMapTexture(CubeMapTexture&& other) noexcept
    : id_(std::exchange(other.id_, 0)) {}

CubeMapTexture& CubeMapTexture::operator=(CubeMapTexture&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void CubeMapTexture::destroy() {
    if (id_ != 0) {
        gl::DeleteTextures(1, &id_);
        id_ = 0;
    }
}

void CubeMapTexture::bind(unsigned unit) const {
    gl::ActiveTexture(gl::TEXTURE0 + unit);
    gl::BindTexture(gl::TEXTURE_CUBE_MAP, id_);
}

}  // namespace falcon
