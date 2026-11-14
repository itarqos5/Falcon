#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace falcon {

// Decoded 8-bit RGBA image, rows stored top to bottom.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
    [[nodiscard]] unsigned char alphaAt(int x, int y) const {
        return rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) * 4 + 3];
    }
    void flipVertical();
};

// Decodes PNG data (palette / grey / RGB / RGBA are all expanded to RGBA).
[[nodiscard]] std::optional<Image> decodePng(const std::vector<unsigned char>& bytes);

bool writePng(const std::filesystem::path& file, int width, int height, const unsigned char* rgba);

}  // namespace falcon
