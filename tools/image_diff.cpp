// Compares two PNG pictures of the same size: how far apart their pixels are. For checking the Vulkan renderer against the OpenGL one on the same frame.
//
//   falcon_image_diff <a.png> <b.png> [max-rms] [difference.png]
//
// Prints the root-mean-square difference per channel (0..255), the largest difference, and how many pixels differ by more than 16 in some channel. With a third number the exit
// code is 1 when the RMS is above it; with a fourth argument a picture of the differences (amplified) is written.
#include "falcon/assets/image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

std::optional<falcon::Image> load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return falcon::decodePng(bytes);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fputs("usage: falcon_image_diff <a.png> <b.png> [max-rms] [difference.png]\n", stderr);
        return 2;
    }
    const auto a = load(argv[1]);
    const auto b = load(argv[2]);
    if (!a || !b) {
        std::fprintf(stderr, "cannot read %s\n", !a ? argv[1] : argv[2]);
        return 2;
    }
    if (a->width != b->width || a->height != b->height) {
        std::fprintf(stderr, "the pictures differ in size: %dx%d and %dx%d\n", a->width, a->height, b->width, b->height);
        return 2;
    }
    const std::size_t pixels = static_cast<std::size_t>(a->width) * static_cast<std::size_t>(a->height);
    double sum = 0.0;
    int worst = 0;
    std::size_t differing = 0;
    std::vector<unsigned char> picture(pixels * 4, 255);
    for (std::size_t i = 0; i < pixels; ++i) {
        int pixelWorst = 0;
        for (int c = 0; c < 3; ++c) {  // (the alpha of a frame means nothing)
            const int d = std::abs(static_cast<int>(a->rgba[i * 4 + static_cast<std::size_t>(c)]) - static_cast<int>(b->rgba[i * 4 + static_cast<std::size_t>(c)]));
            sum += static_cast<double>(d) * d;
            pixelWorst = std::max(pixelWorst, d);
            picture[i * 4 + static_cast<std::size_t>(c)] = static_cast<unsigned char>(std::min(255, d * 8));
        }
        worst = std::max(worst, pixelWorst);
        if (pixelWorst > 16) ++differing;
    }
    const double rms = std::sqrt(sum / static_cast<double>(pixels * 3));
    std::printf("rms %.3f, largest difference %d, %zu of %zu pixels differ by more than 16 (%.3f%%)\n", rms, worst, differing, pixels, 100.0 * static_cast<double>(differing) / static_cast<double>(pixels));
    if (argc > 4) falcon::writePng(argv[4], a->width, a->height, picture.data());
    if (argc > 3 && rms > std::atof(argv[3])) return 1;
    return 0;
}
