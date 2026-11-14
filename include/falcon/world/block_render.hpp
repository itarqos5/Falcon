#pragma once

#include "falcon/world/block_registry.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace falcon {

// The six faces of a block, in the order meshes and shaders number them.
enum Face : int { kEast, kWest, kUp, kDown, kSouth, kNorth, kFaceCount };  // +X, -X, +Y, -Y, +Z, -Z
// What a quad is filed under for the draw's back-face culling: its Face when it lies square to an axis, or this for any other angle.
inline constexpr int kAnyFacing = kFaceCount;
inline constexpr int kFacingGroups = kFaceCount + 1;  // the six directions, then the quads at other angles

// How a face of the world is drawn.
enum class RenderType : std::uint8_t {
    Opaque,       // no transparency at all: the cheapest pass
    Cutout,       // fully transparent or fully opaque texels (leaves, glass, plants): alpha-tested
    Translucent,  // blended, drawn last (water, stained glass, ice)
};

// One flat face of a model, ready to be put into a mesh.
struct ModelQuad {
    std::array<std::array<float, 3>, 4> pos{};  // corners in block units 0..1, counter-clockwise seen from outside
    std::array<std::array<float, 2>, 4> uv{};   // 0..1 across the texture picture
    std::uint16_t layer = 0;                    // layer of the block texture array
    std::int8_t cullFace = -1;                  // -1: always drawn; else hidden when the neighbour on that Face is opaque
    std::int8_t tint = -1;                      // -1: none, else the tint index of the model (0 = grass, foliage, water...)
    std::uint8_t face = kUp;                    // the Face the quad mostly looks at (for shading)
    bool shade = true;                          // false: no direction shading
    // The element's "shade_direction_override" (26.3 replaced "shade": false with it): the Face whose brightness the quad gets instead
    // of its own facing, -1 for none. Crossed plants (grass, ferns, flowers, saplings, crops ...) say "up", so they are lit at full
    // brightness from every side, not by the way their diagonal planes happen to face.
    std::int8_t shadeFace = -1;
    // True when the quad lies square to an axis (its normal is a Face's): from the other side it is never seen, so a section drawn from
    // the other side of it can skip the whole direction.
    bool axial = false;
    // false: the model says "ambientocclusion": false (plants, crops, doors, ladders, levers, ...): it is lit flat, with no smooth
    // lighting, as the game does (ModelBlockRenderer picks the flat lighter for such models).
    bool ao = true;
    RenderType type = RenderType::Opaque;
};

enum class RenderKind : std::uint8_t {
    Nothing,  // invisible (air, barriers, ...)
    Cube,     // one plain full cube: merged greedily
    Model,    // any other shape: its quads one by one
    Fluid,    // water and lava
};

// Where a tinted face gets its colour (set per block by name; the model only says "tint index 0").
enum class TintSource : std::uint8_t { None, Grass, Foliage, Water, DryFoliage, Constant };

// The tint colours (0xRRGGBB) of the 16x16 columns of one section, index x + 16 * z, blended over the surrounding biomes.
struct TintGrid {
    std::array<std::uint32_t, 256> grass{}, foliage{}, dryFoliage{}, water{};
};
struct BiomeGrid;  // the biomes of one chunk (world/chunk.hpp)

using QuadList = std::shared_ptr<const std::vector<ModelQuad>>;

// One alternative of a part of a model (a block can pick between weighted alternatives by position).
struct ModelVariant {
    QuadList quads;
    int weight = 1;
};
// A part is a group of alternatives; a state with several parts (a fence: post + sides) draws one of each.
struct ModelPart {
    std::vector<ModelVariant> variants;
    int totalWeight = 0;
};

// How the world draws one block state.
struct StateRender {
    RenderKind kind = RenderKind::Nothing;
    // Cube: the picture and tint of each face, indexed by Face.
    std::array<std::uint16_t, kFaceCount> faceLayer{};
    std::array<std::int8_t, kFaceCount> faceTint{};
    RenderType type = RenderType::Opaque;
    // Model: what to draw.
    std::vector<ModelPart> parts;
    // Fluid: the pictures (still for the top, flowing for the sides) and tint index.
    std::uint16_t fluidStill = 0;
    std::uint16_t fluidFlow = 0;
    std::int8_t fluidTint = -1;
    bool isLava = false;
    // Where tinted faces get their colour: a TintSource, and the colour itself when it is a constant.
    std::uint8_t tintSource = 0;
    std::uint32_t tintConstant = 0xFFFFFF;
    // Glass and ice: a face next to the same block is not drawn.
    bool cullSame = false;
    // Leaves: drawn see-through (Cutout) under Fancy graphics, or solid (Opaque) under Fast, per the "Leaves" video option.
    bool leaves = false;
};

// Answers "how do I draw this state" for the mesher. Called from several threads at once.
class BlockRenderProvider {
public:
    virtual ~BlockRenderProvider() = default;
    [[nodiscard]] virtual const StateRender& render(Block block) const = 0;
    // The colour (0xRRGGBB) a tinted face of this state has at a world position.
    [[nodiscard]] virtual std::uint32_t tintColor(const StateRender&, int /*x*/, int /*y*/, int /*z*/) const { return 0xFFFFFF; }
    // The tint colours of the columns of the section whose corner is at world y `originY` and whose chunk is the middle one of
    // `biomes` (the 3x3 chunks around it, index (dx+1) + 3*(dz+1); entries may be null). False: no biome data, use tintColor.
    virtual bool fillTints(const std::shared_ptr<const BiomeGrid>* /*biomes*/, int /*originX*/, int /*originY*/, int /*originZ*/, TintGrid& /*out*/) const { return false; }
};

// How much of the light a face gets, by direction (the game's own constants: top full, bottom half,
// north/south 80%, east/west 60%).
[[nodiscard]] constexpr float faceShade(int face) {
    constexpr float kShade[kFaceCount] = {0.6f, 0.6f, 1.0f, 0.5f, 0.8f, 0.8f};
    return kShade[face];
}

}  // namespace falcon
