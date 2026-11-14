#include "falcon/gfx/mipmaps.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>

namespace falcon {

namespace {

// ARGB.SRGB_TO_LINEAR and LINEAR_TO_SRGB of the game: an sRGB channel (0..255) as linear light in 0..1023, and back.
struct Tables {
    std::array<std::int16_t, 256> toLinear{};
    std::array<std::uint8_t, 1024> toSrgb{};
    Tables() {
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            const float linear = c <= 0.04045f ? c / 12.92f : static_cast<float>(std::pow((static_cast<double>(c) + 0.055) / 1.055, 2.4));
            toLinear[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::floor(linear * 1023.0f + 0.5f));
        }
        for (int i = 0; i < 1024; ++i) {
            const float c = static_cast<float>(i) / 1023.0f;
            const float srgb = c <= 0.0031308f ? c * 12.92f : static_cast<float>(1.055 * std::pow(static_cast<double>(c), 0.4166666666666667) - 0.055);
            toSrgb[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::floor(srgb * 255.0f + 0.5f)));
        }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}

float srgbToLinearChannel(int c) { return static_cast<float>(tables().toLinear[static_cast<std::size_t>(c)]) / 1023.0f; }
int linearToSrgbChannel(float f) {
    const int index = std::clamp(static_cast<int>(std::floor(f * 1023.0f)), 0, 1023);
    return tables().toSrgb[static_cast<std::size_t>(index)];
}
// ARGB.linearChannelMean: the average of four sRGB channels, taken in linear light.
int linearChannelMean(int a, int b, int c, int d) {
    const Tables& t = tables();
    const int sum = t.toLinear[static_cast<std::size_t>(a)] + t.toLinear[static_cast<std::size_t>(b)] + t.toLinear[static_cast<std::size_t>(c)] +
                    t.toLinear[static_cast<std::size_t>(d)];
    return t.toSrgb[static_cast<std::size_t>(std::clamp(sum / 4, 0, 1023))];
}

struct Pixel {
    int r, g, b, a;
};
Pixel pixelAt(const Image& image, int x, int y) {
    const unsigned char* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4];
    return {p[0], p[1], p[2], p[3]};
}
void setPixel(Image& image, int x, int y, const Pixel& p) {
    unsigned char* d = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4];
    d[0] = static_cast<unsigned char>(p.r);
    d[1] = static_cast<unsigned char>(p.g);
    d[2] = static_cast<unsigned char>(p.b);
    d[3] = static_cast<unsigned char>(p.a);
}

// ARGB.meanLinear: the alpha is the plain average, the colours are averaged in linear light (transparent texels count with the
// colour they carry, which is why the cutout strategies fill them first).
Pixel meanLinear(const Pixel& p0, const Pixel& p1, const Pixel& p2, const Pixel& p3) {
    return {linearChannelMean(p0.r, p1.r, p2.r, p3.r), linearChannelMean(p0.g, p1.g, p2.g, p3.g), linearChannelMean(p0.b, p1.b, p2.b, p3.b),
            (p0.a + p1.a + p2.a + p3.a) / 4};
}

// MipmapGenerator.darkenedAlphaBlend: only the texels that are not fully transparent add their light (and their alpha, through the
// same curve), but the sum is still divided by four: a transparent texel is as good as black.
Pixel darkenedAlphaBlend(const Pixel& p0, const Pixel& p1, const Pixel& p2, const Pixel& p3) {
    float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
    for (const Pixel* p : {&p0, &p1, &p2, &p3}) {
        if (p->a == 0) continue;
        a += srgbToLinearChannel(p->a);
        r += srgbToLinearChannel(p->r);
        g += srgbToLinearChannel(p->g);
        b += srgbToLinearChannel(p->b);
    }
    a /= 4.0f;
    r /= 4.0f;
    g /= 4.0f;
    b /= 4.0f;
    return {linearToSrgbChannel(r), linearToSrgbChannel(g), linearToSrgbChannel(b), linearToSrgbChannel(a)};
}

// TextureUtil.solidify: every fully transparent texel gets the colour of the nearest texel that is not (found by a breadth-first sweep
// outwards over the four neighbours); its alpha stays 0.
void solidify(Image& image) {
    const int w = image.width, h = image.height;
    const auto index = [h](int x, int y) { return x * h + y; };
    std::vector<int> distance(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), std::numeric_limits<int>::max());
    std::vector<Pixel> source(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), Pixel{0, 0, 0, 0});
    std::deque<int> queue;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const Pixel p = pixelAt(image, x, y);
            if (p.a == 0) continue;
            const auto i = static_cast<std::size_t>(index(x, y));
            distance[i] = 0;
            source[i] = p;
            queue.push_back(index(x, y));
        }
    }
    constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!queue.empty()) {
        const int cell = queue.front();
        queue.pop_front();
        const int x = cell / h, y = cell % h;
        for (const auto& d : kDirections) {
            const int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const auto from = static_cast<std::size_t>(cell), to = static_cast<std::size_t>(index(nx, ny));
            if (distance[to] > distance[from] + 1) {
                distance[to] = distance[from] + 1;
                source[to] = source[from];
                queue.push_back(index(nx, ny));
            }
        }
    }
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (pixelAt(image, x, y).a != 0) continue;
            Pixel p = source[static_cast<std::size_t>(index(x, y))];
            p.a = 0;
            setPixel(image, x, y, p);
        }
    }
}

// TextureUtil.fillEmptyAreasWithDarkColor: every fully transparent texel becomes three quarters of the darkest texel that is not
// transparent (alpha 0): the holes of a leaf texture start out dark green.
void fillEmptyAreasWithDarkColor(Image& image) {
    Pixel darkest{255, 255, 255, 255};
    int darkestSum = std::numeric_limits<int>::max();
    for (int x = 0; x < image.width; ++x) {
        for (int y = 0; y < image.height; ++y) {
            const Pixel p = pixelAt(image, x, y);
            if (p.a == 0) continue;
            const int sum = p.r + p.g + p.b;
            if (sum < darkestSum) {
                darkestSum = sum;
                darkest = p;
            }
        }
    }
    const Pixel fill{3 * darkest.r / 4, 3 * darkest.g / 4, 3 * darkest.b / 4, 0};
    for (int x = 0; x < image.width; ++x) {
        for (int y = 0; y < image.height; ++y) {
            if (pixelAt(image, x, y).a == 0) setPixel(image, x, y, fill);
        }
    }
}

// The game's scaleAlphaToCoverage: finds (five bisection steps over 0..4) the factor that gives the picture the wanted share of
// texels passing the alpha test, then applies it, plus the texture's bias and a little (0.025).
void scaleAlphaToCoverage(Image& image, float desiredCoverage, float cutoff, float alphaCutoffBias) {
    float low = 0.0f, high = 4.0f, scale = 1.0f, best = 1.0f, bestError = std::numeric_limits<float>::max();
    for (int i = 0; i < 5; ++i) {
        const float coverage = alphaTestCoverage(image, cutoff, scale);
        const float error = std::fabs(coverage - desiredCoverage);
        if (error < bestError) {
            bestError = error;
            best = scale;
        }
        if (coverage < desiredCoverage) {
            low = scale;
        } else if (coverage > desiredCoverage) {
            high = scale;
        } else {
            break;
        }
        scale = (low + high) * 0.5f;
    }
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            Pixel p = pixelAt(image, x, y);
            float alpha = static_cast<float>(p.a) / 255.0f * best + alphaCutoffBias + 0.025f;
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            p.a = static_cast<int>(std::floor(alpha * 255.0f));
            setPixel(image, x, y, p);
        }
    }
}

}  // namespace

MipmapStrategy parseMipmapStrategy(std::string_view name) {
    if (name == "mean") return MipmapStrategy::Mean;
    if (name == "cutout") return MipmapStrategy::Cutout;
    if (name == "strict_cutout") return MipmapStrategy::StrictCutout;
    if (name == "dark_cutout") return MipmapStrategy::DarkCutout;
    return MipmapStrategy::Auto;
}

float alphaTestCoverage(const Image& image, float cutoff, float scale) {
    const int w = image.width, h = image.height;
    float coverage = 0.0f;
    for (int y = 0; y < h - 1; ++y) {
        for (int x = 0; x < w - 1; ++x) {
            const auto alpha = [&](int px, int py) { return std::clamp(static_cast<float>(pixelAt(image, px, py).a) / 255.0f * scale, 0.0f, 1.0f); };
            const float a00 = alpha(x, y), a10 = alpha(x + 1, y), a01 = alpha(x, y + 1), a11 = alpha(x + 1, y + 1);
            int passing = 0;
            for (int j = 0; j < 4; ++j) {
                const float v = (static_cast<float>(j) + 0.5f) / 4.0f;
                for (int i = 0; i < 4; ++i) {
                    const float u = (static_cast<float>(i) + 0.5f) / 4.0f;
                    const float value = (a00 * (1.0f - u) + a10 * u) * (1.0f - v) + (a01 * (1.0f - u) + a11 * u) * v;
                    if (value > cutoff) ++passing;
                }
            }
            coverage += static_cast<float>(passing) / 16.0f;
        }
    }
    return coverage / static_cast<float>((w - 1) * (h - 1));
}

std::vector<Image> generateMipLevels(Image base, int maxLevel, MipmapStrategy strategy, float alphaCutoffBias) {
    if (strategy == MipmapStrategy::Auto) {
        bool transparent = false;
        for (std::size_t i = 3; i < base.rgba.size() && !transparent; i += 4) transparent = base.rgba[i] == 0;
        strategy = transparent ? MipmapStrategy::Cutout : MipmapStrategy::Mean;
    }
    if (strategy == MipmapStrategy::Cutout || strategy == MipmapStrategy::StrictCutout) {
        solidify(base);
    } else if (strategy == MipmapStrategy::DarkCutout) {
        fillEmptyAreasWithDarkColor(base);
    }
    std::vector<Image> levels;
    levels.push_back(std::move(base));
    const bool cutout = strategy == MipmapStrategy::Cutout || strategy == MipmapStrategy::StrictCutout || strategy == MipmapStrategy::DarkCutout;
    const float cutoff = strategy == MipmapStrategy::StrictCutout ? 0.3f : 0.5f;
    const float coverage = cutout ? alphaTestCoverage(levels[0], cutoff, 1.0f) : 0.0f;
    for (int level = 1; level <= maxLevel; ++level) {
        const Image& previous = levels[static_cast<std::size_t>(level) - 1];
        Image next;
        next.width = previous.width >> 1;
        next.height = previous.height >> 1;
        if (next.width <= 0 || next.height <= 0) break;
        next.rgba.assign(static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height) * 4, 0);
        for (int x = 0; x < next.width; ++x) {
            for (int y = 0; y < next.height; ++y) {
                const Pixel p0 = pixelAt(previous, x * 2, y * 2), p1 = pixelAt(previous, x * 2 + 1, y * 2);
                const Pixel p2 = pixelAt(previous, x * 2, y * 2 + 1), p3 = pixelAt(previous, x * 2 + 1, y * 2 + 1);
                setPixel(next, x, y, strategy == MipmapStrategy::DarkCutout ? darkenedAlphaBlend(p0, p1, p2, p3) : meanLinear(p0, p1, p2, p3));
            }
        }
        if (cutout) scaleAlphaToCoverage(next, coverage, cutoff, alphaCutoffBias);
        levels.push_back(std::move(next));
    }
    return levels;
}

}  // namespace falcon
