// Build-time helper: makes a Windows .ico from one square master PNG. The master is scaled down to the
// sizes Windows asks for (16 to 256) and each size is stored as a PNG-compressed entry, which Windows
// Vista and later understand. Used to give Falcon.exe and its window the Falcon icon.
//
//   make_ico <out.ico> <master.png>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

// Scales an RGBA image down to size x size by averaging the source pixels each new pixel covers. The colours
// are weighted by alpha, so the transparent pixels around the logo do not tint its edge.
Bytes scaleDown(const std::uint8_t* src, int srcSize, int size) {
    Bytes out(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    const double ratio = static_cast<double>(srcSize) / size;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double x0 = x * ratio;
            const double x1 = (x + 1) * ratio;
            const double y0 = y * ratio;
            const double y1 = (y + 1) * ratio;
            double r = 0.0, g = 0.0, b = 0.0, a = 0.0, weight = 0.0;
            for (int sy = static_cast<int>(y0); sy < std::min(srcSize, static_cast<int>(std::ceil(y1))); ++sy) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, static_cast<double>(sy));
                for (int sx = static_cast<int>(x0); sx < std::min(srcSize, static_cast<int>(std::ceil(x1))); ++sx) {
                    const double w = wy * (std::min(x1, sx + 1.0) - std::max(x0, static_cast<double>(sx)));
                    const std::uint8_t* p = src + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(srcSize) + static_cast<std::size_t>(sx)) * 4;
                    const double alpha = p[3] / 255.0;
                    r += p[0] * alpha * w;
                    g += p[1] * alpha * w;
                    b += p[2] * alpha * w;
                    a += alpha * w;
                    weight += w;
                }
            }
            std::uint8_t* d = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
            if (a > 0.0) {
                d[0] = static_cast<std::uint8_t>(std::lround(std::clamp(r / a, 0.0, 255.0)));
                d[1] = static_cast<std::uint8_t>(std::lround(std::clamp(g / a, 0.0, 255.0)));
                d[2] = static_cast<std::uint8_t>(std::lround(std::clamp(b / a, 0.0, 255.0)));
            }
            d[3] = static_cast<std::uint8_t>(std::lround(std::clamp(a / weight, 0.0, 1.0) * 255.0));
        }
    }
    return out;
}

void appendBytes(void* context, void* data, int size) {
    auto* bytes = static_cast<Bytes*>(context);
    const auto* p = static_cast<const std::uint8_t*>(data);
    bytes->insert(bytes->end(), p, p + size);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: make_ico <out.ico> <master.png>\n");
        return 2;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    std::uint8_t* master = stbi_load(argv[2], &width, &height, &channels, 4);
    if (master == nullptr) {
        std::fprintf(stderr, "make_ico: cannot read %s: %s\n", argv[2], stbi_failure_reason());
        return 1;
    }
    if (width != height || width < 256) {
        std::fprintf(stderr, "make_ico: %s must be square and at least 256px (it is %dx%d)\n", argv[2], width, height);
        stbi_image_free(master);
        return 1;
    }

    static constexpr int kSizes[] = {16, 24, 32, 48, 64, 128, 256};
    std::vector<Bytes> pngs;
    for (const int size : kSizes) {
        const Bytes pixels = scaleDown(master, width, size);
        Bytes png;
        if (stbi_write_png_to_func(appendBytes, &png, size, size, 4, pixels.data(), size * 4) == 0) {
            std::fprintf(stderr, "make_ico: cannot encode the %dpx icon\n", size);
            stbi_image_free(master);
            return 1;
        }
        pngs.push_back(std::move(png));
    }
    stbi_image_free(master);

    Bytes out;
    put16(out, 0);  // reserved
    put16(out, 1);  // type: icon
    put16(out, static_cast<std::uint16_t>(pngs.size()));

    std::uint32_t offset = 6 + 16 * static_cast<std::uint32_t>(pngs.size());
    for (std::size_t i = 0; i < pngs.size(); ++i) {
        const auto dim = static_cast<std::uint8_t>(kSizes[i] == 256 ? 0 : kSizes[i]);
        out.push_back(dim);  // width  (0 means 256)
        out.push_back(dim);  // height
        out.push_back(0);    // palette size
        out.push_back(0);    // reserved
        put16(out, 1);       // colour planes
        put16(out, 32);      // bits per pixel
        put32(out, static_cast<std::uint32_t>(pngs[i].size()));
        put32(out, offset);
        offset += static_cast<std::uint32_t>(pngs[i].size());
    }
    for (const Bytes& png : pngs) out.insert(out.end(), png.begin(), png.end());

    std::ofstream file(argv[1], std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file) {
        std::fprintf(stderr, "make_ico: cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
