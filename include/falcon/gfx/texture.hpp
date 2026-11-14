#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/gfx/gl.hpp"

#include <functional>
#include <utility>

namespace falcon {

enum class FilterMode { Nearest, Linear };

class Texture2D {
public:
    Texture2D() = default;
    Texture2D(const Image& image, FilterMode filter);
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    // A transparent texture of the given size, to be filled in pieces with upload().
    [[nodiscard]] static Texture2D empty(int width, int height, FilterMode filter);
    // Replaces the pixels of the rectangle at (x, y) with `image` (which must fit inside).
    void upload(int x, int y, const Image& image);

    void bind(unsigned unit = 0) const;
    [[nodiscard]] gl::GLuint id() const { return id_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    // Exchanges the pixels (GL textures) of two Texture2D objects while each object keeps its
    // address, so anything holding a reference sees the new picture at once.
    void swap(Texture2D& other) noexcept;

private:
    void destroy();
    gl::GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

// Six square faces in OpenGL order: +X, -X, +Y, -Y, +Z, -Z.
class CubeMapTexture {
public:
    CubeMapTexture() = default;
    // `loadFace(i)` is called once per face, in order; each face is uploaded and its pixels freed
    // before the next is requested, so only one face is ever in memory at a time.
    CubeMapTexture(const std::function<Image(unsigned face)>& loadFace, FilterMode filter);
    ~CubeMapTexture();

    CubeMapTexture(const CubeMapTexture&) = delete;
    CubeMapTexture& operator=(const CubeMapTexture&) = delete;
    CubeMapTexture(CubeMapTexture&& other) noexcept;
    CubeMapTexture& operator=(CubeMapTexture&& other) noexcept;

    // Staged building, for filling a cube map a face per frame: allocate(), then setFace() for
    // each of the six faces (+X, -X, +Y, -Y, +Z, -Z), then swap() it into the one in use.
    void allocate(FilterMode filter);
    void setFace(unsigned face, const Image& image);
    void swap(CubeMapTexture& other) noexcept { std::swap(id_, other.id_); }

    void bind(unsigned unit = 0) const;

private:
    void destroy();
    gl::GLuint id_ = 0;
};

}  // namespace falcon
