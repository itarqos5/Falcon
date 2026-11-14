#pragma once

#include "apex/world/block_render.hpp"
#include "apex/world/chunk.hpp"
#include "apex/world/light.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace apex {

// One corner of a quad, 20 bytes:
//   x, y, z  where it is inside its section, in 1/1024 of a block (a section is 0..16384)
//   layer    which layer of the block texture array to draw
//   u, v     where in that picture, in 1/1024 of a repeat (a quad covering several blocks runs past 1)
//   r, g, b  the tint colour (255 = none)
//   shade    how much light the face's direction gets, 0..255
//   sky, block  the light reaching it from the sky and from blocks, 0..255
struct MeshVertex {
    std::int16_t x, y, z, layer;
    std::int16_t u, v;
    std::uint8_t r, g, b, shade;
    std::uint8_t sky, block, unused0, unused1;
};
static_assert(sizeof(MeshVertex) == 20);
inline constexpr int kMeshScale = 1024;

// The triangles of one section, one list per way of drawing; four vertices per quad (the index list is the same for
// every quad and is shared).
struct SectionMesh {
    std::vector<MeshVertex> opaque;
    std::vector<MeshVertex> cutout;
    std::vector<MeshVertex> translucent;
    // True if the section has leaf faces between two leaf blocks (hidden by nothing, kept by Fancy leaves): the faces the leaf level of
    // detail drops far away. Only sections with some are worth building again when the player comes near or goes far.
    bool leafInterior = false;
    // For each quad of the three lists, the way it faces: a Face (0..5) when it lies square to an axis, kAnyFacing (6) otherwise. The
    // renderer puts the quads of one direction together so that a section can skip the directions that turn away from the camera.
    std::vector<std::uint8_t> opaqueFace, cutoutFace, translucentFace;
    [[nodiscard]] bool empty() const { return opaque.empty() && cutout.empty() && translucent.empty(); }
    [[nodiscard]] std::size_t vertexCount() const { return opaque.size() + cutout.size() + translucent.size(); }
};

// The section and the 26 around it (index (dx+1) + (dy+1)*3 + (dz+1)*9), as the mesher needs to know what
// sits next to its border blocks. Null entries are air.
struct SectionNeighborhood {
    std::array<SectionPtr, 27> sections;
    std::array<std::shared_ptr<const BiomeGrid>, 9> biomes;  // the chunk of the section and the 8 around it, index (dx+1) + 3*(dz+1)
    std::array<std::shared_ptr<const ChunkLight>, 9> lights;  // their light (null: treated as full sky light)
    [[nodiscard]] static constexpr int slot(int dx, int dy, int dz) { return (dx + 1) + (dy + 1) * 3 + (dz + 1) * 9; }
};

// Builds the quads of the middle section. `originX/Y/Z` is the block position of its corner (for tints and the choice
// between alternative models). Only faces that can be seen are made, and neighbouring cube faces with the same
// picture are merged into one quad.
[[nodiscard]] SectionMesh meshSection(const SectionNeighborhood& around, const BlockRenderProvider& models, int originX, int originY,
                                      int originZ, bool smoothLighting = true, bool cutoutLeaves = true, bool leafLod = false);

// Collects the neighbourhood of a section from a level. Below the world is solid bedrock, above it air.
[[nodiscard]] SectionNeighborhood snapshotNeighborhood(const Level& level, int chunkX, int sectionIndex, int chunkZ);

// How many quads a mesh with this many vertices has.
[[nodiscard]] constexpr std::size_t quadCount(std::size_t vertexCount) { return vertexCount / 4; }

}  // namespace apex
