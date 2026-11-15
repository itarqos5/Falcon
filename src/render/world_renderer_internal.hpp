#pragma once

// Small things the parts of WorldRenderer share (world_renderer*.cpp).
#include "falcon/gfx/gl.hpp"
#include "falcon/world/mesher.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace falcon {

// Which of the new ways of doing things are on. Each can be turned off with an environment variable, to compare the picture and the speed.
inline bool optionOff(const char* name) { return std::getenv(name) != nullptr; }

// The attribute layout of a MeshVertex for the vertex array and array buffer that are bound.
inline void bindVertexLayout() {
    // x, y, z, layer (4 shorts), u, v (2 shorts), tint + shade (4 normalised bytes), sky + block light + face (4 bytes)
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 4, gl::SHORT, 0, sizeof(MeshVertex), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::SHORT, 0, sizeof(MeshVertex), reinterpret_cast<const void*>(8));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, 1, sizeof(MeshVertex), reinterpret_cast<const void*>(12));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 4, gl::UNSIGNED_BYTE, 1, sizeof(MeshVertex), reinterpret_cast<const void*>(16));
}


// The most quads one section's mesh may hold (the shared index list covers this many).
inline constexpr std::size_t kMaxQuadsPerSection = 4096 * 12;

// The plains sky: pale haze at the horizon, blue overhead (the game's daytime colours).
inline constexpr float kHorizon[3] = {0.753f, 0.847f, 1.0f};

inline constexpr float kZenith[3] = {0.471f, 0.655f, 1.0f};

// Puts the three lists of a mesh into one vertex array (opaque, cutout, translucent) and counts each. A mesh is never
// longer than the shared index list can address. The opaque and cutout quads are put in order of the way they face (east, west, up, down,
// south, north, then the ones at other angles) and each direction counted, so that the draw can leave out the directions that turn away
// from the camera: a face seen from behind is thrown away by the back-face culling anyway, after the vertex work has been done.
template <typename Result>
void packMesh(Result& result, SectionMesh&& mesh) {
    std::size_t room = kMaxQuadsPerSection * 4;
    const auto take = [&](std::vector<MeshVertex>& list) {
        const std::size_t n = std::min(list.size(), room) / 4 * 4;
        result.vertices.insert(result.vertices.end(), list.begin(), list.begin() + static_cast<std::ptrdiff_t>(n));
        room -= n;
        return static_cast<int>(n);
    };
    const auto takeSorted = [&](std::vector<MeshVertex>& list, const std::vector<std::uint8_t>& faces, std::array<int, kFacingGroups>& groups) {
        const std::size_t quads = list.size() / 4;
        const bool tagged = faces.size() == quads;
        std::array<std::size_t, kFacingGroups> count{};
        for (std::size_t q = 0; q < quads; ++q) ++count[tagged ? std::min<std::size_t>(faces[q], kFacingGroups - 1) : kFacingGroups - 1];
        std::array<std::size_t, kFacingGroups> next{};
        std::size_t at = 0;
        for (std::size_t g = 0; g < kFacingGroups; ++g) {
            next[g] = at;
            at += count[g];
        }
        std::vector<MeshVertex> sorted(list.size());
        for (std::size_t q = 0; q < quads; ++q) {
            const std::size_t g = tagged ? std::min<std::size_t>(faces[q], kFacingGroups - 1) : kFacingGroups - 1;
            const std::size_t to = next[g]++ * 4;
            const auto face = static_cast<std::uint8_t>(g < static_cast<std::size_t>(kFaceCount) ? g + 1 : 0);  // (the shader's carried-light lookup)
            for (std::size_t i = 0; i < 4; ++i) {
                sorted[to + i] = list[q * 4 + i];
                sorted[to + i].face = face;
            }
        }
        const int taken = take(sorted);
        // (If the list was cut short at the end, the last directions lose their share.)
        std::size_t left = static_cast<std::size_t>(taken);
        for (std::size_t g = 0; g < kFacingGroups; ++g) {
            const std::size_t verts = std::min(count[g] * 4, left);
            groups[g] = static_cast<int>(verts);
            left -= verts;
        }
        return taken;
    };
    result.vertices.reserve(mesh.vertexCount());
    result.opaqueVertices = takeSorted(mesh.opaque, mesh.opaqueFace, result.opaqueGroups);
    result.cutoutVertices = takeSorted(mesh.cutout, mesh.cutoutFace, result.cutoutGroups);
    result.translucentVertices = take(mesh.translucent);
}

}  // namespace falcon
