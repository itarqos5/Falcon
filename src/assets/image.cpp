#include "falcon/assets/image.hpp"

#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <fstream>

namespace falcon {

void Image::flipVertical() {
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        auto* top = rgba.data() + static_cast<std::size_t>(y) * stride;
        auto* bottom = rgba.data() + static_cast<std::size_t>(height - 1 - y) * stride;
        std::swap_ranges(top, top + stride, bottom);
    }
}

std::optional<Image> decodePng(const std::vector<unsigned char>& bytes) {
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w,
                                                &h, &channels, 4);
    if (data == nullptr) return std::nullopt;
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);
    return img;
}

bool writePng(const std::filesystem::path& file, int width, int height,
              const unsigned char* rgba) {
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    const auto sink = [](void* context, void* data, int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
    };
    return stbi_write_png_to_func(sink, &out, width, height, 4, rgba, width * 4) != 0 && out.good();
}

}  // namespace falcon
