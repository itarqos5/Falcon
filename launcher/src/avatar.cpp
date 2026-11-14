#include "avatar.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <vector>

namespace launcher {

namespace {

void appendBytes(void* context, void* data, int size) { static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size)); }

// Source-over of one RGBA pixel on another (both straight alpha).
void blend(unsigned char* dst, const unsigned char* src) {
    const int a = src[3];
    if (a == 0) return;
    if (a == 255) {
        std::copy(src, src + 4, dst);
        return;
    }
    const int da = dst[3];
    const int outA = a + da * (255 - a) / 255;
    for (int c = 0; c < 3; ++c) dst[c] = static_cast<unsigned char>(outA == 0 ? 0 : (src[c] * a + dst[c] * da * (255 - a) / 255) / outA);
    dst[3] = static_cast<unsigned char>(outA);
}

}  // namespace

std::string base64(const std::string& bytes) {
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = static_cast<unsigned char>(bytes[i]);
        const unsigned b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        out.push_back(digits[b0 >> 2]);
        out.push_back(digits[((b0 & 3) << 4) | (b1 >> 4)]);
        out.push_back(i + 1 < bytes.size() ? digits[((b1 & 15) << 2) | (b2 >> 6)] : '=');
        out.push_back(i + 2 < bytes.size() ? digits[b2 & 63] : '=');
    }
    return out;
}

bool isSkinUrl(const std::string& url) {
    for (const char* prefix : {"https://textures.minecraft.net/", "http://textures.minecraft.net/"}) {
        const std::string p = prefix;
        if (url.size() > p.size() && url.compare(0, p.size(), p) == 0) return url.find_first_of(" \"<>\r\n") == std::string::npos;
    }
    return false;
}

std::string avatarFromFace(const unsigned char* rgba8x8, int scale) {
    scale = std::clamp(scale, 1, 32);
    const int size = 8 * scale;
    std::vector<unsigned char> big(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const unsigned char* px = rgba8x8 + (static_cast<std::size_t>(y / scale) * 8 + static_cast<std::size_t>(x / scale)) * 4;
            std::copy(px, px + 4, big.begin() + (static_cast<std::ptrdiff_t>(y) * size + x) * 4);
        }
    }
    std::string png;
    if (stbi_write_png_to_func(appendBytes, &png, size, size, 4, big.data(), size * 4) == 0) return {};
    return "data:image/png;base64," + base64(png);
}

std::string avatarFromSkin(const std::string& skinPng, int scale) {
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(skinPng.data()), static_cast<int>(skinPng.size()), &w, &h, &channels, 4);
    if (pixels == nullptr) return {};
    std::string result;
    // Skins are 64 wide (64x64 now, 64x32 for the old ones); a higher-resolution one is the same picture, larger.
    if (w >= 64 && w % 64 == 0 && h >= w / 2) {
        const int unit = w / 64;  // pixels per skin pixel
        unsigned char face[8 * 8 * 4];
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const auto at = [&](int sx, int sy) { return pixels + (static_cast<std::size_t>(sy * unit + unit / 2) * static_cast<std::size_t>(w) + static_cast<std::size_t>(sx * unit + unit / 2)) * 4; };
                unsigned char* out = face + (y * 8 + x) * 4;
                std::copy(at(8 + x, 8 + y), at(8 + x, 8 + y) + 4, out);
                out[3] = 255;                      // the face itself is opaque
                blend(out, at(40 + x, 8 + y));     // the hat layer over it
            }
        }
        result = avatarFromFace(face, scale);
    }
    stbi_image_free(pixels);
    return result;
}

}  // namespace launcher
