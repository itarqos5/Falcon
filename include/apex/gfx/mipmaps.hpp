#pragma once

#include "apex/assets/image.hpp"

#include <string_view>
#include <vector>

namespace apex {

// How a texture's smaller mip levels are made from its picture: the game's MipmapStrategy, set per texture in its .png.mcmeta
// ("texture": {"mipmap_strategy": "dark_cutout"}). Leaves are dark_cutout, flowers and many plants strict_cutout, glass and
// redstone dust mean; a texture that says nothing is Auto.
enum class MipmapStrategy : unsigned char {
    Auto,          // Cutout when the picture has fully transparent texels, else Mean
    Mean,          // each texel is the average of the four above it (in linear light)
    Cutout,        // like Mean, but the transparent texels first take the colour of the opaque ones next to them, and every level is
                   // given the alpha that keeps the share of the picture that passes the alpha test (0.5) the same as in the picture
    StrictCutout,  // like Cutout, with the alpha test at 0.3
    DarkCutout,    // like Cutout, but a transparent texel counts as black in the average and starts out dark: the far leaves of a tree
                   // are darker than their near ones, and keep their holes
};

// "mean", "cutout", "strict_cutout", "dark_cutout" (anything else: Auto).
[[nodiscard]] MipmapStrategy parseMipmapStrategy(std::string_view name);

// The mip levels of `base`: [0] is the picture itself (a cutout strategy first fills its transparent texels' colours, their alpha
// stays 0), then up to `maxLevel` smaller ones, each half the size, down to 1 x 1. The picture must be square. A port of the game's
// MipmapGenerator.generateMipLevels, TextureUtil.solidify / fillEmptyAreasWithDarkColor and ARGB.meanLinear, so far terrain looks
// the way it does there. `alphaCutoffBias` is the texture's "alpha_cutoff_bias" (0 when it has none).
[[nodiscard]] std::vector<Image> generateMipLevels(Image base, int maxLevel, MipmapStrategy strategy, float alphaCutoffBias = 0.0f);

// The share (0..1) of a picture that passes an alpha test at `cutoff`, with its alpha scaled by `scale` and bilinearly filtered, as the
// game measures it (exposed for tests).
[[nodiscard]] float alphaTestCoverage(const Image& image, float cutoff, float scale);

}  // namespace apex
