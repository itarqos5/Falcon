#include "falcon/game/edition_badge.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace falcon {
namespace {

using Rgba = std::array<std::uint8_t, 4>;

constexpr int kWidth = 512;
constexpr int kHeight = 64;

constexpr Rgba kBlack = {0, 0, 0, 255};
constexpr Rgba kRing = {0, 0, 0, 238};  // translucent outer edge of the outline

// Geometry of the original "JAVA EDITION" badge (x in pixels). All glyphs share rows 4..43 for
// the face; the letter E occupies x 209..249 and the whole EDITION word runs to x 507.
constexpr int kEFaceLeft = 209;
constexpr int kEFaceRight = 249;
constexpr int kEditionFirstColumn = 203;  // transparent margin just left of the E
constexpr int kEditionFaceLeft = 208;     // where the E's face starts in the original
constexpr int kEditionFaceRight = 507;    // right end of the N
constexpr int kEMid = 230;                // a column through the middle of the E's arms

// Layout of the new text (face to face, in pixels).
constexpr int kLetterGap = 7;
constexpr int kWordGap = 18;
constexpr int kCWidth = kEFaceRight - kEFaceLeft + 1;  // 41
constexpr int kPlusWidth = 33;

// The plus: an 11px wide vertical bar (rows 8..39) crossed by a 10px thick bar (rows 19..28).
constexpr int kPlusBarWidth = 11;
constexpr int kPlusTop = 8;
constexpr int kPlusBottom = 39;
constexpr int kPlusArmTop = 19;
constexpr int kPlusArmBottom = 28;

Rgba get(const Image& img, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2], img.rgba[i + 3]};
}

void put(Image& img, int x, int y, const Rgba& c) {
    const std::size_t i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
    img.rgba[i] = c[0];
    img.rgba[i + 1] = c[1];
    img.rgba[i + 2] = c[2];
    img.rgba[i + 3] = c[3];
}

Image blank() {
    Image img;
    img.width = kWidth;
    img.height = kHeight;
    img.rgba.assign(static_cast<std::size_t>(kWidth) * kHeight * 4, 0);
    return img;
}

bool inBounds(int x, int y) { return x >= 0 && x < kWidth && y >= 0 && y < kHeight; }

// Composites `src` over `dst` (straight alpha).
void alphaOver(Image& dst, const Image& src) {
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const Rgba s = get(src, x, y);
            if (s[3] == 0) continue;
            const Rgba d = get(dst, x, y);
            const float sa = static_cast<float>(s[3]) / 255.0f;
            const float da = static_cast<float>(d[3]) / 255.0f;
            const float oa = sa + da * (1.0f - sa);
            Rgba out{};
            for (int c = 0; c < 3; ++c) {
                const float v = (static_cast<float>(s[static_cast<std::size_t>(c)]) * sa +
                                 static_cast<float>(d[static_cast<std::size_t>(c)]) * da * (1.0f - sa)) /
                                oa;
                out[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::lround(v));
            }
            out[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
            put(dst, x, y, out);
        }
    }
}

// The font's outline: 3px solid black around everything opaque (the diagonal corner pixel is
// left out, which chamfers corners by one pixel) plus a 1px translucent ring outside it.
void addOutline(Image& layer) {
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(kWidth) * kHeight, 0);
    std::vector<std::uint8_t> ring(solid.size(), 0);
    const auto at = [](int x, int y) {
        return static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
    };

    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (get(layer, x, y)[3] == 0) continue;
            for (int dy = -4; dy <= 4; ++dy) {
                for (int dx = -4; dx <= 4; ++dx) {
                    const int cheb = std::max(std::abs(dx), std::abs(dy));
                    const int corner = std::min(std::abs(dx), std::abs(dy));
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (!inBounds(nx, ny)) continue;
                    if (cheb <= 3 && !(cheb == 3 && corner == 3)) {
                        solid[at(nx, ny)] = 1;
                    } else if (cheb <= 4 && !(cheb == 4 && corner >= 3)) {
                        ring[at(nx, ny)] = 1;  // outer edge (and the chamfered corner pixel)
                    }
                }
            }
        }
    }
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (get(layer, x, y)[3] != 0) continue;
            if (solid[at(x, y)] != 0) {
                put(layer, x, y, kBlack);
            } else if (ring[at(x, y)] != 0) {
                put(layer, x, y, kRing);
            }
        }
    }
}

bool isLight(const Rgba& c) { return c[3] == 255 && c[0] >= 190; }
bool isBlack(const Rgba& c) { return c[3] == 255 && c[0] < 30 && c[1] < 30 && c[2] < 30; }

// Spot checks that this really is the JAVA EDITION lettering where we expect it.
bool looksLikeJavaEdition(const Image& img) {
    if (img.width != kWidth || img.height != kHeight ||
        img.rgba.size() != static_cast<std::size_t>(kWidth) * kHeight * 4) {
        return false;
    }
    const bool shape = isLight(get(img, 215, 8)) &&   // E: top arm
                       isLight(get(img, 240, 8)) &&
                       isLight(get(img, 212, 24)) &&  // E: stem
                       isLight(get(img, 240, 38)) &&  // E: bottom arm
                       isBlack(get(img, 242, 26)) &&  // E: counter between the arms
                       get(img, 195, 30)[3] < 40;     // empty margin left of the E
    if (!shape) return false;

    // The plus is coloured by sampling the E, so those samples must be solid pixels.
    if (get(img, kEMid, 4)[3] != 255 || get(img, kEMid, 13)[3] != 255 ||
        get(img, kEFaceLeft, 8)[3] != 255 || get(img, kEFaceRight, 8)[3] != 255) {
        return false;
    }
    for (int row = 43; row <= 50; ++row) {
        if (get(img, kEMid, row)[3] != 255) return false;
    }
    return true;
}

// A plus sign centred on column `xc`, with extrusion band and outline, using colours sampled
// from the E so it matches the lettering.
Image renderPlus(const Image& orig, int xc) {
    const Rgba fill = get(orig, kEMid, 8);
    const Rgba topEdge = get(orig, kEMid, 4);
    const Rgba highlight = get(orig, kEMid, 13);
    const Rgba leftEdge = get(orig, kEFaceLeft, 8);
    const Rgba rightEdge = get(orig, kEFaceRight, 8);
    std::array<Rgba, 8> band{};
    for (int k = 1; k <= 8; ++k) band[static_cast<std::size_t>(k - 1)] = get(orig, kEMid, 43 + k - 1);

    std::vector<std::uint8_t> face(static_cast<std::size_t>(kWidth) * kHeight, 0);
    const auto idx = [](int x, int y) {
        return static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
    };
    const auto isFace = [&](int x, int y) { return inBounds(x, y) && face[idx(x, y)] != 0; };

    const int barLeft = xc - kPlusBarWidth / 2;
    const int barRight = xc + kPlusBarWidth / 2;
    const int armLeft = xc - kPlusWidth / 2;
    const int armRight = xc + kPlusWidth / 2;
    for (int y = kPlusTop; y <= kPlusBottom; ++y) {
        for (int x = barLeft; x <= barRight; ++x) face[idx(x, y)] = 1;
    }
    for (int y = kPlusArmTop; y <= kPlusArmBottom; ++y) {
        for (int x = armLeft; x <= armRight; ++x) face[idx(x, y)] = 1;
    }

    Image layer = blank();
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (isFace(x, y)) {
                if (!isFace(x, y + 1)) {
                    put(layer, x, y, highlight);
                } else if (!isFace(x, y - 1)) {
                    put(layer, x, y, topEdge);
                } else if (!isFace(x - 1, y)) {
                    put(layer, x, y, leftEdge);
                } else if (!isFace(x + 1, y)) {
                    put(layer, x, y, rightEdge);
                } else {
                    put(layer, x, y, fill);
                }
            } else {
                // Extrusion: up to 8px of dark band below any face pixel.
                for (int k = 1; k <= 8; ++k) {
                    if (isFace(x, y - k)) {
                        put(layer, x, y, band[static_cast<std::size_t>(k - 1)]);
                        break;
                    }
                }
            }
        }
    }
    addOutline(layer);
    return layer;
}

}  // namespace

std::optional<Image> makeCppEditionBadge(const Image& javaEdition) {
    if (!looksLikeJavaEdition(javaEdition)) return std::nullopt;
    const Image& orig = javaEdition;

    // Face-to-face layout of "C++ EDITION", centred like the original text (faces at 4..507).
    const int editionWidth = kEditionFaceRight - kEditionFaceLeft + 1;
    const int total = kCWidth + kLetterGap + kPlusWidth + kLetterGap + kPlusWidth + kWordGap + editionWidth;
    const int cFace = (kWidth - total) / 2;
    const int plus1Face = cFace + kCWidth + kLetterGap;
    const int plus2Face = plus1Face + kPlusWidth + kLetterGap;
    const int editionFace = plus2Face + kPlusWidth + kWordGap;

    Image out = blank();

    // EDITION: copied unchanged, shifted so its E starts at `editionFace`.
    const int shift = editionFace - kEditionFaceLeft;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = kEditionFirstColumn; x < kWidth; ++x) {
            const int nx = x + shift;
            if (inBounds(nx, y)) put(out, nx, y, get(orig, x, y));
        }
    }

    // C: the E (columns 205..252) with the middle arm removed.
    constexpr int kCellLeft = 205;
    constexpr int kCellRight = 252;
    const int cellShift = cFace - kEFaceLeft;
    Image cLayer = blank();
    for (int y = 0; y < kHeight; ++y) {
        for (int x = kCellLeft; x <= kCellRight; ++x) {
            Rgba c = get(orig, x, y);
            if (y >= 18 && y <= 21) {
                c = get(orig, x, 17);  // the band under the top arm carries on
            } else if (y >= 22 && y <= 32) {
                // Counter: keep only the stem, everything right of it is black.
                if (x >= 220) {
                    c = kBlack;
                } else if (x == 219) {
                    c = get(orig, 219, 16);  // the stem's right edge pixel
                }
            }
            put(cLayer, x + cellShift, y, c);
        }
    }
    alphaOver(out, cLayer);

    // ++
    alphaOver(out, renderPlus(orig, plus1Face + kPlusWidth / 2));
    alphaOver(out, renderPlus(orig, plus2Face + kPlusWidth / 2));
    return out;
}

std::optional<Image> buildCppEditionBadge(const ResourceManager& resources) {
    const auto original = TextureManager::decode(resources, kEditionTexturePath);
    if (!original) return std::nullopt;
    return makeCppEditionBadge(*original);
}

bool installCppEditionBadge(const ResourceManager& resources, TextureManager& textures) {
    const auto badge = buildCppEditionBadge(resources);
    if (!badge) return false;
    textures.add(kEditionTexturePath, *badge);
    return true;
}

}  // namespace falcon
