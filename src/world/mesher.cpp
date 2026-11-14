#include "falcon/world/mesher.hpp"

#include "falcon/world/block_position.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace falcon {
namespace {

constexpr int kPad = 18;  // the section plus a one block border on every side
constexpr int kPadVolume = kPad * kPad * kPad;

// A section and its borders as one flat array, so the mesher never has to ask which section a block is in.
struct Padded {
    std::array<Block, kPadVolume> blocks;
    std::array<std::uint16_t, kPadVolume> flags;  // StateInfo::flags of every block
    std::array<std::uint8_t, kPadVolume> light;   // sky light << 4 | block light of every cell
    [[nodiscard]] int skyAt(int x, int y, int z) const { return light[static_cast<std::size_t>(idx(x, y, z))] >> 4; }
    [[nodiscard]] int blockLightAt(int x, int y, int z) const { return light[static_cast<std::size_t>(idx(x, y, z))] & 15; }
    [[nodiscard]] static constexpr int idx(int x, int y, int z) { return (y + 1) * kPad * kPad + (z + 1) * kPad + (x + 1); }  // each -1..16
    [[nodiscard]] Block at(int x, int y, int z) const { return blocks[static_cast<std::size_t>(idx(x, y, z))]; }
    [[nodiscard]] bool opaque(int x, int y, int z) const { return (flags[static_cast<std::size_t>(idx(x, y, z))] & kStateOpaqueCube) != 0; }
    [[nodiscard]] std::uint16_t flagsAt(int x, int y, int z) const { return flags[static_cast<std::size_t>(idx(x, y, z))]; }
};

// For one axis, how neighbour -1, 0 and +1 map into the padded array: where it starts there, how many blocks it
// covers, and where in the neighbour those blocks start.
struct AxisSpan {
    int padStart;
    int count;
    int srcStart;
};
constexpr AxisSpan kSpans[3] = {{0, 1, 15}, {1, 16, 0}, {17, 1, 0}};

void fillPadded(const SectionNeighborhood& around, const BlockRegistry& registry, Padded& out) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const SectionPtr& s = around.sections[static_cast<std::size_t>(SectionNeighborhood::slot(dx, dy, dz))];
                const AxisSpan sx = kSpans[dx + 1];
                const AxisSpan sy = kSpans[dy + 1];
                const AxisSpan sz = kSpans[dz + 1];
                for (int y = 0; y < sy.count; ++y) {
                    for (int z = 0; z < sz.count; ++z) {
                        const std::size_t base = static_cast<std::size_t>((sy.padStart + y) * kPad * kPad + (sz.padStart + z) * kPad + sx.padStart);
                        Block* row = out.blocks.data() + base;
                        if (!s) {
                            std::fill_n(row, sx.count, Block::Air);
                        } else {
                            s->copyRow(sx.srcStart, sy.srcStart + y, sz.srcStart + z, sx.count, row);
                        }
                        std::uint16_t* flagRow = out.flags.data() + base;
                        if (!s || s->isUniform()) {
                            const std::uint16_t f = registry.state(s ? s->uniform : Block::Air).flags;
                            std::fill_n(flagRow, sx.count, f);
                        } else {
                            for (int i = 0; i < sx.count; ++i) flagRow[i] = registry.state(row[i]).flags;
                        }
                    }
                }
            }
        }
    }
}

// The light of the padded cells, from the light of the chunks around the section (full sky light where there is none yet).
void fillLight(const SectionNeighborhood& around, int originY, Padded& out) {
    for (int z = -1; z <= 16; ++z) {
        const int dcz = z < 0 ? -1 : (z > 15 ? 1 : 0);
        for (int x = -1; x <= 16; ++x) {
            const int dcx = x < 0 ? -1 : (x > 15 ? 1 : 0);
            const ChunkLight* chunk = around.lights[static_cast<std::size_t>((dcx + 1) + 3 * (dcz + 1))].get();
            for (int y = -1; y <= 16; ++y) {
                int sky = 15, block = 0;
                if (chunk != nullptr) {
                    sky = chunk->skyAt(x & 15, originY + y, z & 15);
                    block = chunk->blockAt(x & 15, originY + y, z & 15);
                }
                out.light[static_cast<std::size_t>(Padded::idx(x, y, z))] = static_cast<std::uint8_t>((sky << 4) | block);
            }
        }
    }
}

// How each cube face's mask is laid out. The mask's fast axis is `a`, its slow axis is `b`, the face is perpendicular
// to `n`.
struct FaceSetup {
    int n, a, b;   // axes: 0 = x, 1 = y, 2 = z
    int normal;    // +1 or -1 along n
};
constexpr FaceSetup kFaces[kFaceCount] = {
    /* east  +X */ {0, 2, 1, +1},
    /* west  -X */ {0, 2, 1, -1},
    /* up    +Y */ {1, 0, 2, +1},
    /* down  -Y */ {1, 0, 2, -1},
    /* south +Z */ {2, 0, 1, +1},
    /* north -Z */ {2, 0, 1, -1},
};

// The four corners of a face, counter-clockwise seen from outside, as min (0) or max (1) of the box on each axis.
constexpr int kFaceCorners[kFaceCount][4][3] = {
    /* east  */ {{1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}},
    /* west  */ {{0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}},
    /* up    */ {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
    /* down  */ {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}},
    /* south */ {{0, 1, 1}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}},
    /* north */ {{1, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}},
};
constexpr int kStep[kFaceCount][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// The picture coordinates (in blocks) that a point of the section has on a face, so that textures line up across blocks.
void naturalUv(int face, double x, double y, double z, double& u, double& v) {
    switch (face) {
        case kDown: u = x; v = 16 - z; break;
        case kUp: u = x; v = z; break;
        case kNorth: u = 16 - x; v = 16 - y; break;
        case kSouth: u = x; v = 16 - y; break;
        case kWest: u = z; v = 16 - y; break;
        default: u = 16 - z; v = 16 - y; break;  // east
    }
}

std::int16_t q(double v) { return static_cast<std::int16_t>(std::lround(v * kMeshScale)); }

MeshVertex makeVertex(double x, double y, double z, int layer, double u, double v, std::uint32_t rgb, float shade, int sky = 255, int blockLight = 0) {  // sky and blockLight 0..255
    MeshVertex m{};
    m.x = q(x);
    m.y = q(y);
    m.z = q(z);
    m.layer = static_cast<std::int16_t>(layer);
    m.u = q(u);
    m.v = q(v);
    m.r = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
    m.g = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
    m.b = static_cast<std::uint8_t>(rgb & 0xFF);
    m.shade = static_cast<std::uint8_t>(std::lround(std::clamp(shade, 0.0f, 1.0f) * 255.0f));
    m.sky = static_cast<std::uint8_t>(std::clamp(sky, 0, 255));
    m.block = static_cast<std::uint8_t>(std::clamp(blockLight, 0, 255));
    return m;
}

std::vector<MeshVertex>& listFor(SectionMesh& mesh, RenderType type) {
    switch (type) {
        case RenderType::Cutout: return mesh.cutout;
        case RenderType::Translucent: return mesh.translucent;
        default: return mesh.opaque;
    }
}

// Notes which way the quad just added to the list of `type` faces (a Face, or kAnyFacing for a quad that is not square to an axis).
void tagQuad(SectionMesh& mesh, RenderType type, int facing) {
    std::vector<std::uint8_t>& faces = type == RenderType::Cutout ? mesh.cutoutFace : type == RenderType::Translucent ? mesh.translucentFace : mesh.opaqueFace;
    faces.push_back(static_cast<std::uint8_t>(facing));
}

// ---------------------------------------------------------------------------------------------------------------
// The game picks between alternative models with a Java LCG seeded from the block position; the same here so that
// the pattern of rotated grass tops is the game's.
struct LegacyRandom {
    std::int64_t seed = 0;
    void setSeed(std::int64_t s) { seed = (s ^ 0x5DEECE66DLL) & ((1LL << 48) - 1); }
    int next(int bits) {
        seed = (seed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
        return static_cast<int>(seed >> (48 - bits));
    }
    int nextInt(int bound) {
        if ((bound & -bound) == bound) return static_cast<int>((static_cast<std::int64_t>(bound) * static_cast<std::int64_t>(next(31))) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (static_cast<std::int32_t>(static_cast<std::uint32_t>(bits - val) + static_cast<std::uint32_t>(bound - 1)) < 0);
        return val;
    }
};

// ---------------------------------------------------------------------------------------------------------------

struct Mesher {
    const BlockRenderProvider& models;
    const BlockRegistry& registry;
    const Padded& padded;
    SectionMesh& mesh;
    int ox, oy, oz;
    std::array<const StateRender*, kSectionVolume> state;
    const std::shared_ptr<const BiomeGrid>* biomes = nullptr;  // the 3x3 chunks around the section
    TintGrid tints;
    bool tintsReady = false;
    bool smooth = true;  // the Smooth Lighting option: per-corner light and ambient occlusion, or flat per-face light
    bool cutoutLeaves = true;  // the Leaves option: see-through (Cutout) leaves, or solid (Opaque) ones
    bool leafLod = false;      // far from the player: leaf faces between two leaf blocks are not made (they cannot be told apart at that distance)
    bool leafInterior = false; // set when a leaf face next to another leaf block came up (made or skipped): what the level of detail changes

    // The tint colour a face of this state has in column (x, z) of the section: from the biomes, made on first use.
    [[nodiscard]] std::uint32_t tint(const StateRender& s, int x, int z) {
        if (static_cast<TintSource>(s.tintSource) == TintSource::Constant) return s.tintConstant;
        if (static_cast<TintSource>(s.tintSource) == TintSource::None) return 0xFFFFFF;
        if (!tintsReady) {
            tintsReady = true;
            if (biomes == nullptr || !models.fillTints(biomes, ox, oy, oz, tints)) {  // no biomes: the same colour everywhere
                StateRender probe;
                const auto flat = [&](TintSource source, std::array<std::uint32_t, 256>& out) {
                    probe.tintSource = static_cast<std::uint8_t>(source);
                    out.fill(models.tintColor(probe, ox, oy, oz));
                };
                flat(TintSource::Grass, tints.grass);
                flat(TintSource::Foliage, tints.foliage);
                flat(TintSource::DryFoliage, tints.dryFoliage);
                flat(TintSource::Water, tints.water);
            }
        }
        const std::size_t i = static_cast<std::size_t>(z * 16 + x);
        switch (static_cast<TintSource>(s.tintSource)) {
            case TintSource::Grass: return tints.grass[i];
            case TintSource::Foliage: return tints.foliage[i];
            case TintSource::DryFoliage: return tints.dryFoliage[i];
            case TintSource::Water: return tints.water[i];
            default: return 0xFFFFFF;
        }
    }

    [[nodiscard]] bool sameBlock(Block a, Block b) const { return registry.state(a).block == registry.state(b).block; }

    // ---- light ----
    // One corner of a face, as the game's BlockModelLighter.prepareQuadAmbientOcclusion works it out. Around the corner are four
    // cells: `center` (where the face's light is measured), the two beside it (sides a and b, `ax..` and `bx..` away from the centre)
    // and the one diagonal between them. Their light is added up (0..60 for each kind) and so is how many of them are full cubes
    // (0..4: the ambient occlusion, each darkening the corner by a fifth).
    //  - A cell with no light at all (solid, or a dark hole) takes the centre's light instead: it must not darken the corner.
    //  - The diagonal cell only counts when the corner is open to it: when the cell one step further out along the face's normal
    //    `n` beside either side is light-permeable. Otherwise the first side stands in for it.
    //  - What shades (0.2 instead of 1) is a cell whose collision shape is a full block - leaves, glass and ice shade their
    //    neighbours too - while what blocks light is a solid, opaque cube.
    struct Corner {
        int sky = 15 * 4, block = 0, ao = 0;
        bool operator==(const Corner&) const = default;
    };
    [[nodiscard]] bool shadesCell(int x, int y, int z) const { return (padded.flagsAt(x, y, z) & kStateFullCollision) != 0; }
    [[nodiscard]] Corner corner(int fx, int fy, int fz, int ax, int ay, int az, int bx, int by, int bz, const int* n) const {
        const int sky0 = padded.skyAt(fx, fy, fz), blk0 = padded.blockLightAt(fx, fy, fz);
        if (!smooth) {  // flat lighting: every corner is the light of the cell in front of the face, no occlusion
            return {sky0 * 4, blk0 * 4, 0};
        }
        const int s1[3] = {fx + ax, fy + ay, fz + az};
        const int s2[3] = {fx + bx, fy + by, fz + bz};
        // The diagonal cell, or the first side when the corner is closed to it. (The cells one step further out can lie beyond the one
        // cell of padding round the section, for a face on its edge: not known, so taken as open.)
        const auto permeable = [&](const int* cell) {
            const int x = cell[0] + n[0], y = cell[1] + n[1], z = cell[2] + n[2];
            return x < -1 || x > 16 || y < -1 || y > 16 || z < -1 || z > 16 || !padded.opaque(x, y, z);
        };
        const bool open = permeable(s1) || permeable(s2);
        const int d[3] = {open ? fx + ax + bx : s1[0], open ? fy + ay + by : s1[1], open ? fz + az + bz : s1[2]};
        const int* cells[3] = {s1, s2, d};
        Corner c;
        c.sky = sky0;
        c.block = blk0;
        c.ao = shadesCell(fx, fy, fz) ? 1 : 0;
        for (const int* cell : cells) {
            int sky = padded.skyAt(cell[0], cell[1], cell[2]), blk = padded.blockLightAt(cell[0], cell[1], cell[2]);
            if ((sky == 0 && blk == 0) || padded.opaque(cell[0], cell[1], cell[2])) {
                sky = sky0;
                blk = blk0;
            }
            c.sky += sky;
            c.block += blk;
            if (shadesCell(cell[0], cell[1], cell[2])) ++c.ao;
        }
        return c;
    }
    // Flat light for shapes: the cell in front of the quad (or the block's own) and the block's own, whichever is brighter.
    void flatLight(int x, int y, int z, int frontFace, int& sky255, int& block255) const {
        int sky = padded.skyAt(x, y, z), blk = padded.blockLightAt(x, y, z);
        if (frontFace >= 0) {
            const int* step = kStep[frontFace];
            sky = std::max(sky, padded.skyAt(x + step[0], y + step[1], z + step[2]));
            blk = std::max(blk, padded.blockLightAt(x + step[0], y + step[1], z + step[2]));
        }
        sky255 = sky * 17;
        block255 = blk * 17;
    }

    // ---- greedy cube faces ----
    // A mask value: 1 | ao << 1 (3 bits) | sky sum << 4 (6 bits) | block sum << 10 (6 bits) | layer << 16 | colour << 32 | render type << 56
    void cubeFaces() {
        thread_local std::array<std::uint64_t, 256> mask;
        for (int face = 0; face < kFaceCount; ++face) {
            const FaceSetup& f = kFaces[face];
            const int* step = kStep[face];
            for (int slice = 0; slice < 16; ++slice) {
                bool any = false;
                mask.fill(0);
                for (int b = 0; b < 16; ++b) {
                    for (int a = 0; a < 16; ++a) {
                        int p[3] = {0, 0, 0};
                        p[f.n] = slice;
                        p[f.a] = a;
                        p[f.b] = b;
                        const StateRender* s = state[static_cast<std::size_t>(SectionData::index(p[0], p[1], p[2]))];
                        if (s == nullptr || s->kind != RenderKind::Cube) continue;
                        const int nx = p[0] + step[0], ny = p[1] + step[1], nz = p[2] + step[2];
                        if (padded.opaque(nx, ny, nz)) continue;  // hidden by its neighbour
                        // (Leaves hide their faces against each other only when drawn solid - Fast graphics. Fancy leaves keep every
                        // face, as the game's do: through the holes of the nearest leaf you see the faces behind it, which is what
                        // gives a tree its dense look.)
                        if (s->cullSame && sameBlock(padded.at(p[0], p[1], p[2]), padded.at(nx, ny, nz))) {
                            if (s->leaves) leafInterior = true;
                            if (!s->leaves || !cutoutLeaves || leafLod) continue;
                        }
                        std::uint32_t rgb = 0xFFFFFF;
                        if (s->faceTint[static_cast<std::size_t>(face)] >= 0) rgb = tint(*s, p[0], p[2]);
                        // The light at the four corners of the face. Flat ones (all four alike) may merge with their
                        // neighbours; the rest are drawn alone, with their own corners.
                        Corner c[4];
                        bool flat = true;
                        for (int i = 0; i < 4; ++i) {
                            const int* sel = kFaceCorners[face][i];
                            int da[3] = {0, 0, 0}, db[3] = {0, 0, 0};
                            da[f.a] = sel[f.a] ? 1 : -1;
                            db[f.b] = sel[f.b] ? 1 : -1;
                            c[i] = corner(nx, ny, nz, da[0], da[1], da[2], db[0], db[1], db[2], step);
                            flat = flat && c[i] == c[0];
                        }
                        // Fast leaves: draw the same leaf texture solid (Opaque) instead of see-through (Cutout).
                        const RenderType rt = (!cutoutLeaves && s->leaves) ? RenderType::Opaque : s->type;
                        const std::uint64_t texture = (static_cast<std::uint64_t>(s->faceLayer[static_cast<std::size_t>(face)]) << 16) |
                                                      (static_cast<std::uint64_t>(rgb) << 32) | (static_cast<std::uint64_t>(rt) << 56);
                        if (flat) {
                            mask[static_cast<std::size_t>(b * 16 + a)] = 1ULL | (static_cast<std::uint64_t>(c[0].ao) << 1) | (static_cast<std::uint64_t>(c[0].sky) << 4) |
                                                                         (static_cast<std::uint64_t>(c[0].block) << 10) | texture;
                            any = true;
                        } else {
                            emitCube(face, f, slice, a, b, 1, 1, 1ULL | texture, c);
                        }
                    }
                }
                if (any) greedy(mask, face, f, slice);
            }
        }
    }

    void greedy(std::array<std::uint64_t, 256>& mask, int face, const FaceSetup& f, int slice) {
        for (int b = 0; b < 16; ++b) {
            for (int a = 0; a < 16;) {
                const std::uint64_t value = mask[static_cast<std::size_t>(b * 16 + a)];
                if (value == 0) {
                    ++a;
                    continue;
                }
                int w = 1;
                while (a + w < 16 && mask[static_cast<std::size_t>(b * 16 + a + w)] == value) ++w;
                int h = 1;
                for (; b + h < 16; ++h) {
                    bool rowMatches = true;
                    for (int k = 0; k < w; ++k) {
                        if (mask[static_cast<std::size_t>((b + h) * 16 + a + k)] != value) {
                            rowMatches = false;
                            break;
                        }
                    }
                    if (!rowMatches) break;
                }
                for (int dy = 0; dy < h; ++dy) {
                    for (int dx = 0; dx < w; ++dx) mask[static_cast<std::size_t>((b + dy) * 16 + a + dx)] = 0;
                }
                Corner merged;
                merged.ao = static_cast<int>((value >> 1) & 7);
                merged.sky = static_cast<int>((value >> 4) & 63);
                merged.block = static_cast<int>((value >> 10) & 63);
                const Corner corners[4] = {merged, merged, merged, merged};
                emitCube(face, f, slice, a, b, w, h, value, corners);
                a += w;
            }
        }
    }

    void emitCube(int face, const FaceSetup& f, int slice, int a0, int b0, int w, int h, std::uint64_t value, const Corner (&corners)[4]) {
        const int layer = static_cast<int>((value >> 16) & 0xFFFF);
        const std::uint32_t rgb = static_cast<std::uint32_t>((value >> 32) & 0xFFFFFF);
        const auto type = static_cast<RenderType>((value >> 56) & 3);
        int lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        lo[f.n] = hi[f.n] = f.normal > 0 ? slice + 1 : slice;
        lo[f.a] = a0;
        hi[f.a] = a0 + w;
        lo[f.b] = b0;
        hi[f.b] = b0 + h;
        std::vector<MeshVertex>& out = listFor(mesh, type);
        // Where the light is uneven the quad's diagonal follows the brighter pair of corners, as in the game, so the
        // dark corner does not smear across the face.
        const int first = (corners[0].ao + corners[2].ao > corners[1].ao + corners[3].ao) ? 1 : 0;
        for (int k = 0; k < 4; ++k) {
            const int i = (first + k) & 3;
            const int* sel = kFaceCorners[face][i];
            const double x = sel[0] ? hi[0] : lo[0];
            const double y = sel[1] ? hi[1] : lo[1];
            const double z = sel[2] ? hi[2] : lo[2];
            double u = 0, v = 0;
            naturalUv(face, x, y, z, u, v);
            const Corner& c = corners[i];
            out.push_back(makeVertex(x, y, z, layer, u, v, rgb, faceShade(face) * (1.0f - 0.2f * static_cast<float>(c.ao)), c.sky * 17 / 4, c.block * 17 / 4));
        }
        tagQuad(mesh, type, face);
    }

    // ---- everything that is not a plain cube ----
    void shapes() {
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const StateRender* s = state[static_cast<std::size_t>(SectionData::index(x, y, z))];
                    const Block block = padded.at(x, y, z);
                    const std::uint16_t flags = padded.flagsAt(x, y, z);
                    if (s != nullptr && s->kind == RenderKind::Model) model(*s, block, x, y, z);
                    if (s != nullptr && s->kind == RenderKind::Fluid) fluid(*s, block, x, y, z);
                    // Water held by another block (a waterlogged stair, kelp).
                    if (s != nullptr && s->kind != RenderKind::Fluid && (flags & kStateWater) != 0) {
                        fluid(models.render(commonBlocks().water), commonBlocks().water, x, y, z);
                    }
                }
            }
        }
    }

    void model(const StateRender& s, Block block, int x, int y, int z) {
        const int wx = ox + x, wy = oy + y, wz = oz + z;
        LegacyRandom random;
        bool seeded = false;
        double dx = 0, dz = 0;
        if (registry.state(block).has(kStateHasOffset)) {
            // Plants are shifted by up to a quarter block, from the position (the outline you aim at is shifted the same way).
            const std::array<double, 2> offset = blockOffset(wx, wz);
            dx = offset[0];
            dz = offset[1];
        }
        for (const ModelPart& part : s.parts) {
            const ModelVariant* chosen = &part.variants[0];
            if (part.variants.size() > 1) {
                if (!seeded) {
                    random.setSeed(positionSeed(wx, wy, wz));
                    seeded = true;
                }
                int roll = random.nextInt(part.totalWeight);
                for (const ModelVariant& v : part.variants) {
                    roll -= v.weight;
                    if (roll < 0) {
                        chosen = &v;
                        break;
                    }
                }
            }
            for (const ModelQuad& quad : *chosen->quads) {
                if (quad.cullFace >= 0) {
                    const int* step = kStep[quad.cullFace];
                    if (padded.opaque(x + step[0], y + step[1], z + step[2])) continue;
                    if (s.cullSame && sameBlock(block, padded.at(x + step[0], y + step[1], z + step[2]))) {
                        if (s.leaves) leafInterior = true;
                        if (!s.leaves || !cutoutLeaves || leafLod) continue;
                    }
                }
                const std::uint32_t rgb = quad.tint >= 0 ? tint(s, x, z) : 0xFFFFFF;
                // BlockModelLighter.getDirectionalBrightness: the override's face if the element has one, else the way the quad faces.
                const float faceSh = quad.shade ? faceShade(quad.shadeFace >= 0 ? quad.shadeFace : quad.face) : 1.0f;
                const RenderType qrt = (!cutoutLeaves && s.leaves) ? RenderType::Opaque : quad.type;
                std::vector<MeshVertex>& out = listFor(mesh, qrt);
                if (smooth && quad.ao) {
                    // Smooth Lighting for a model quad, as the game does it (BlockModelLighter.prepareQuadAmbientOcclusion): light
                    // the four corners of the quad's facing (just like a cube face), then blend them per vertex by where the
                    // vertex sits on that face. This is what makes grass blocks, stairs, slabs, fences and the like - all drawn as
                    // models, not plain cubes - react to the option. Models that say "ambientocclusion": false (plants, crops,
                    // doors, ladders...) are lit flat instead (the branch below), as the game does.
                    const int face = quad.face;
                    const FaceSetup& f = kFaces[face];
                    const int* step = kStep[face];
                    // "faceCubic" in the game's prepareQuadShape: the quad is flat on its facing axis and either lies on the
                    // block's boundary or the block is a full collision cube. Then the light is read in FRONT of the face (the
                    // neighbouring cell); a quad inside the block (a slab's top, a post, a plant's cross) is lit from the
                    // block's own cell - reading the cell in front of it would often be solid terrain, which is black.
                    float lo = 2.0f, hi = -2.0f;
                    for (int i = 0; i < 4; ++i) {
                        const float c = quad.pos[static_cast<std::size_t>(i)][static_cast<std::size_t>(f.n)];
                        lo = std::min(lo, c);
                        hi = std::max(hi, c);
                    }
                    const bool flatOnAxis = hi - lo < 1.0e-5f;
                    const bool onBoundary = step[f.n] > 0 ? hi > 0.9999f : lo < 1.0e-4f;
                    const bool cubic = flatOnAxis && (onBoundary || registry.state(block).has(kStateFullCollision));
                    int cx = cubic ? x + step[0] : x, cy = cubic ? y + step[1] : y, cz = cubic ? z + step[2] : z;
                    if (!cubic && padded.opaque(cx, cy, cz)) {  // a block that hides its own light: use the cell in front after all
                        cx = x + step[0];
                        cy = y + step[1];
                        cz = z + step[2];
                    }
                    Corner cc[4];
                    for (int k = 0; k < 4; ++k) {
                        const int* sel = kFaceCorners[face][k];
                        int da[3] = {0, 0, 0}, db[3] = {0, 0, 0};
                        da[f.a] = sel[f.a] ? 1 : -1;
                        db[f.b] = sel[f.b] ? 1 : -1;
                        cc[k] = corner(cx, cy, cz, da[0], da[1], da[2], db[0], db[1], db[2], step);
                    }
                    for (int i = 0; i < 4; ++i) {
                        const double ua = std::clamp(static_cast<double>(quad.pos[static_cast<std::size_t>(i)][static_cast<std::size_t>(f.a)]), 0.0, 1.0);
                        const double ub = std::clamp(static_cast<double>(quad.pos[static_cast<std::size_t>(i)][static_cast<std::size_t>(f.b)]), 0.0, 1.0);
                        double sky = 0, blk = 0, ao = 0;
                        for (int k = 0; k < 4; ++k) {
                            const int* sel = kFaceCorners[face][k];
                            const double w = (sel[f.a] ? ua : 1.0 - ua) * (sel[f.b] ? ub : 1.0 - ub);
                            sky += w * cc[k].sky;
                            blk += w * cc[k].block;
                            ao += w * static_cast<double>(cc[k].ao);
                        }
                        const float shade = faceSh * (1.0f - 0.2f * static_cast<float>(ao));
                        out.push_back(makeVertex(x + quad.pos[static_cast<std::size_t>(i)][0] + dx, y + quad.pos[static_cast<std::size_t>(i)][1],
                                                 z + quad.pos[static_cast<std::size_t>(i)][2] + dz, quad.layer, quad.uv[static_cast<std::size_t>(i)][0],
                                                 quad.uv[static_cast<std::size_t>(i)][1], rgb, shade, static_cast<int>(std::lround(sky * 17.0 / 4.0)),
                                                 static_cast<int>(std::lround(blk * 17.0 / 4.0))));
                    }
                } else {
                    int sky255 = 255, block255 = 0;
                    flatLight(x, y, z, quad.cullFace, sky255, block255);
                    for (int i = 0; i < 4; ++i) {
                        out.push_back(makeVertex(x + quad.pos[static_cast<std::size_t>(i)][0] + dx, y + quad.pos[static_cast<std::size_t>(i)][1],
                                                 z + quad.pos[static_cast<std::size_t>(i)][2] + dz, quad.layer, quad.uv[static_cast<std::size_t>(i)][0],
                                                 quad.uv[static_cast<std::size_t>(i)][1], rgb, faceSh, sky255, block255));
                    }
                }
                tagQuad(mesh, qrt, quad.axial ? quad.face : kAnyFacing);
            }
        }
    }

    // ---- fluids ----
    [[nodiscard]] static bool isFluid(std::uint16_t flags, bool lava) { return (flags & (lava ? kStateLava : kStateWater)) != 0; }

    // The height of the fluid in this block (0 if none), as a fraction of a block.
    [[nodiscard]] double ownHeight(int x, int y, int z, bool lava) const {
        const std::uint16_t flags = padded.flagsAt(x, y, z);
        if (!isFluid(flags, lava)) return 0.0;
        const int level = registry.state(padded.at(x, y, z)).fluidLevel;
        // Level 0 is a source, 1..7 flowing (lower the bigger), 8 and up falling.
        const int amount = (level == 0 || level >= 8) ? 8 : 8 - level;
        return amount / 9.0;
    }

    // The height of one corner: the weighted mean of the four blocks around it (the game's own rule).
    [[nodiscard]] double cornerHeight(int x, int y, int z, int cx, int cz, bool lava) const {
        double sum = 0;
        int weight = 0;
        for (int j = 0; j < 4; ++j) {
            const int bx = x + cx - (j & 1);
            const int bz = z + cz - ((j >> 1) & 1);
            if (isFluid(padded.flagsAt(bx, y + 1, bz), lava)) return 1.0;
            const std::uint16_t flags = padded.flagsAt(bx, y, bz);
            if (isFluid(flags, lava)) {
                const double h = ownHeight(bx, y, bz, lava);
                if (h >= 0.8) {
                    sum += h * 10.0;
                    weight += 10;
                } else {
                    sum += h;
                    ++weight;
                }
            } else if ((flags & kStateOpaqueCube) == 0) {
                ++weight;
            }
        }
        return weight > 0 ? sum / weight : 0.0;
    }

    void fluid(const StateRender& fluidState, Block /*block*/, int x, int y, int z) {
        const bool lava = fluidState.isLava;
        const std::uint32_t rgb = fluidState.fluidTint >= 0 ? tint(fluidState, x, z) : 0xFFFFFF;
        int sky255 = 255, block255 = 0;
        flatLight(x, y, z, kUp, sky255, block255);
        if (fluidState.isLava) block255 = 255;  // lava glows
        std::vector<MeshVertex>& out = listFor(mesh, fluidState.type);
        const bool aboveSame = isFluid(padded.flagsAt(x, y + 1, z), lava);
        double h[2][2];  // [cx][cz]
        for (int cx = 0; cx < 2; ++cx) {
            for (int cz = 0; cz < 2; ++cz) h[cx][cz] = aboveSame ? 1.0 : cornerHeight(x, y, z, cx, cz, lava);
        }
        const auto quad = [&](int face, const double (&heights)[4], int layer, bool reversed = false) {
            const std::size_t first = out.size();
            for (int i = 0; i < 4; ++i) {
                const int* sel = kFaceCorners[face][i];
                const double px = x + sel[0];
                const double pz = z + sel[2];
                const double py = sel[1] ? y + heights[i] : y;
                double u = 0, v = 0;
                if (face == kUp || face == kDown) {
                    naturalUv(face, px, py, pz, u, v);
                } else {
                    // Sides: across the width, and down from the surface.
                    u = (face == kEast) ? 16 - pz : (face == kWest) ? pz : (face == kNorth) ? 16 - px : px;
                    v = sel[1] ? 1.0 - heights[i] : 1.0;
                }
                out.push_back(makeVertex(px, py, pz, layer, u, v, rgb, faceShade(face), sky255, block255));
            }
            if (reversed) std::swap(out[first + 1], out[first + 3]);  // the same quad seen from the other side
            tagQuad(mesh, fluidState.type, kAnyFacing);  // (the tops are not flat: the corners have their own heights)
        };
        if (!aboveSame) {
            const double top[4] = {h[0][0], h[0][1], h[1][1], h[1][0]};  // up: (0,0) (0,1) (1,1) (1,0) in (x, z)
            quad(kUp, top, fluidState.fluidStill);
            quad(kUp, top, fluidState.fluidStill, true);  // and from below, where the sea surface is seen from under the water
        }
        // Underside: only where there is something to look through.
        if (!isFluid(padded.flagsAt(x, y - 1, z), lava) && !padded.opaque(x, y - 1, z)) {
            const double flat[4] = {1, 1, 1, 1};
            quad(kDown, flat, fluidState.fluidStill);
        }
        for (const int face : {kEast, kWest, kSouth, kNorth}) {
            const int* step = kStep[face];
            const int nx = x + step[0], nz = z + step[2];
            if (isFluid(padded.flagsAt(nx, y, nz), lava) || padded.opaque(nx, y, nz)) continue;
            // Heights at the face's two top corners, in the face's corner order (the table's 0 and 3 are the top ones).
            double sideH[4] = {0, 0, 0, 0};
            for (int i = 0; i < 4; ++i) {
                const int* sel = kFaceCorners[face][i];
                if (sel[1]) sideH[i] = h[sel[0]][sel[2]];
            }
            quad(face, sideH, fluidState.fluidFlow);
        }
    }
};

}  // namespace

SectionMesh meshSection(const SectionNeighborhood& around, const BlockRenderProvider& models, int originX, int originY, int originZ,
                        bool smoothLighting, bool cutoutLeaves, bool leafLod) {
    SectionMesh mesh;
    const BlockRegistry& registry = blockRegistry();
    const SectionPtr& middle = around.sections[static_cast<std::size_t>(SectionNeighborhood::slot(0, 0, 0))];
    if (!middle) return mesh;  // an empty section has nothing to draw
    if (middle->isUniform() && registry.isAir(middle->uniform)) return mesh;
    const auto solidOpaque = [&](const SectionPtr& s) { return s && s->isUniform() && registry.state(s->uniform).has(kStateOpaqueCube); };
    if (solidOpaque(middle) && std::all_of(around.sections.begin(), around.sections.end(), solidOpaque)) {
        return mesh;  // solid rock with solid sections all around it shows nothing: most of the ground below the surface
    }

    thread_local Padded padded;
    fillPadded(around, registry, padded);
    fillLight(around, originY, padded);

    Mesher m{models, registry, padded, mesh, originX, originY, originZ, {}, nullptr, {}, false};
    m.biomes = around.biomes.data();
    m.smooth = smoothLighting;
    m.cutoutLeaves = cutoutLeaves;
    m.leafLod = leafLod;
    bool anyShape = false;
    for (int i = 0; i < kSectionVolume; ++i) {
        const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
        const Block b = padded.at(x, y, z);
        const std::uint16_t flags = padded.flagsAt(x, y, z);
        if ((flags & kStateAir) != 0 && (flags & kStateWater) == 0) {
            m.state[static_cast<std::size_t>(i)] = nullptr;
            continue;
        }
        const StateRender* s = &models.render(b);
        if (s->kind == RenderKind::Nothing && (flags & kStateWater) == 0) s = nullptr;
        m.state[static_cast<std::size_t>(i)] = s;
        if (s != nullptr && s->kind != RenderKind::Cube) anyShape = true;
        if (s == nullptr && (flags & kStateWater) != 0) anyShape = true;
    }
    // Fluids first: the translucent list is drawn in this order, so ice and glass over water must come after the water.
    if (anyShape) m.shapes();
    m.cubeFaces();
    mesh.leafInterior = m.leafInterior;
    return mesh;
}

SectionNeighborhood snapshotNeighborhood(const Level& level, int chunkX, int sectionIndex, int chunkZ) {
    SectionNeighborhood around;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* chunk = level.chunk({chunkX + dx, chunkZ + dz});
            if (chunk != nullptr) {
                around.biomes[static_cast<std::size_t>((dx + 1) + 3 * (dz + 1))] = chunk->biomes;
                around.lights[static_cast<std::size_t>((dx + 1) + 3 * (dz + 1))] = chunk->light;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                const int sy = sectionIndex + dy;
                SectionPtr s;
                if (sy < 0) {
                    s = uniformSection(commonBlocks().bedrock);  // the bottom of the world: its faces are never seen
                } else if (sy < kSectionCount && chunk != nullptr) {
                    s = chunk->sections[static_cast<std::size_t>(sy)];
                }
                around.sections[static_cast<std::size_t>(SectionNeighborhood::slot(dx, dy, dz))] = std::move(s);
            }
        }
    }
    return around;
}

}  // namespace falcon
