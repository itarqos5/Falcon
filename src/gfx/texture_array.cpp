#include "apex/gfx/texture_array.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace apex {
namespace {

// Mip levels for a picture of this size, down to 1x1, but no more than 5 (16 px blocks).
int mipLevelCount(int size) {
    int levels = 1;
    while ((size >> levels) > 0 && levels < 5) ++levels;
    return levels;
}

}  // namespace

TextureArray2D::TextureArray2D(int size, int layers) : size_(size), layers_(layers) {
    if (size <= 0 || layers <= 0) throw std::runtime_error("empty texture array");
    gl::GenTextures(1, &id_);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, id_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    // Room for every mip level (setLayerMips fills them, or finish() makes them).
    for (int level = 0; level < mipLevelCount(size); ++level) {
        const int s = std::max(1, size >> level);
        gl::TexImage3D(gl::TEXTURE_2D_ARRAY, level, static_cast<gl::GLint>(gl::RGBA8), s, s, layers, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    }
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::REPEAT));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::REPEAT));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::NEAREST));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::NEAREST_MIPMAP_LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAX_LEVEL, mipLevelCount(size) - 1);
}

TextureArray2D::~TextureArray2D() { destroy(); }

TextureArray2D::TextureArray2D(TextureArray2D&& other) noexcept
    : id_(std::exchange(other.id_, 0)), size_(std::exchange(other.size_, 0)), layers_(std::exchange(other.layers_, 0)) {}

TextureArray2D& TextureArray2D::operator=(TextureArray2D&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = std::exchange(other.id_, 0);
        size_ = std::exchange(other.size_, 0);
        layers_ = std::exchange(other.layers_, 0);
    }
    return *this;
}

void TextureArray2D::destroy() {
    if (id_ != 0) gl::DeleteTextures(1, &id_);
    id_ = 0;
}

void TextureArray2D::setLayer(int layer, const Image& image) {
    if (image.width != size_ || image.height != size_ || layer < 0 || layer >= layers_) {
        throw std::runtime_error("texture array layer has the wrong size");
    }
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, id_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexSubImage3D(gl::TEXTURE_2D_ARRAY, 0, 0, 0, layer, size_, size_, 1, gl::RGBA, gl::UNSIGNED_BYTE, image.rgba.data());
}

void TextureArray2D::setLayerMips(int layer, const std::vector<Image>& levels) {
    if (layer < 0 || layer >= layers_) throw std::runtime_error("texture array layer out of range");
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, id_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    for (std::size_t level = 1; level < levels.size() && static_cast<int>(level) < mipLevelCount(size_); ++level) {
        const Image& m = levels[level];
        if (m.width != std::max(1, size_ >> level) || m.height != m.width) throw std::runtime_error("texture array mip level has the wrong size");
        gl::TexSubImage3D(gl::TEXTURE_2D_ARRAY, static_cast<gl::GLint>(level), 0, 0, layer, m.width, m.height, 1, gl::RGBA, gl::UNSIGNED_BYTE, m.rgba.data());
    }
}

void TextureArray2D::finish() {
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, id_);
    gl::GenerateMipmap(gl::TEXTURE_2D_ARRAY);
}

void TextureArray2D::bind(unsigned unit) const {
    gl::ActiveTexture(gl::TEXTURE0 + unit);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, id_);
}

std::size_t TextureArray2D::gpuBytes() const {
    std::size_t total = 0;
    for (int level = 0; level < mipLevelCount(size_); ++level) {
        const auto s = static_cast<std::size_t>(std::max(1, size_ >> level));
        total += s * s * 4 * static_cast<std::size_t>(layers_);
    }
    return total;
}

}  // namespace apex
