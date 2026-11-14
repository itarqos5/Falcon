#pragma once

#include "apex/assets/image.hpp"
#include "apex/gfx/gl.hpp"

namespace apex {

// A stack of equal-sized square pictures in one GL texture (GL_TEXTURE_2D_ARRAY), repeating and mip-mapped:
// near pixels are sharp (nearest), far ones blend smoothly (the mip chain). Every layer is its own picture, so
// the mips of one never bleed into another, and a quad that covers several blocks simply repeats its layer.
class TextureArray2D {
public:
    TextureArray2D() = default;
    // `size` is the width and height of every layer; `layers` how many there are.
    TextureArray2D(int size, int layers);
    ~TextureArray2D();

    TextureArray2D(const TextureArray2D&) = delete;
    TextureArray2D& operator=(const TextureArray2D&) = delete;
    TextureArray2D(TextureArray2D&& other) noexcept;
    TextureArray2D& operator=(TextureArray2D&& other) noexcept;

    // Fills one layer; `image` must be size x size.
    void setLayer(int layer, const Image& image);
    // Fills the mip levels 1.. of one layer with pictures made by generateMipLevels (levels[0] is the layer's own picture, which
    // setLayer uploads; it is skipped here).
    void setLayerMips(int layer, const std::vector<Image>& levels);
    // Builds the whole mip chain with the GPU's plain box filter. Call once after every layer is set, unless setLayerMips gave them.
    void finish();

    void bind(unsigned unit = 0) const;
    [[nodiscard]] int size() const { return size_; }
    [[nodiscard]] int layers() const { return layers_; }
    [[nodiscard]] std::size_t gpuBytes() const;  // with the mips

private:
    void destroy();
    gl::GLuint id_ = 0;
    int size_ = 0;
    int layers_ = 0;
};

}  // namespace apex
