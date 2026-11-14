#pragma once

#include "falcon/world/block_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace falcon {

// The shape of the world: columns of 16x16 blocks, 384 blocks tall (y from -64 to 319), cut into 24
// sections of 16x16x16.
inline constexpr int kSectionSize = 16;
inline constexpr int kSectionVolume = 16 * 16 * 16;
inline constexpr int kMinY = -64;
inline constexpr int kWorldHeight = 384;
inline constexpr int kMaxY = kMinY + kWorldHeight;  // exclusive
inline constexpr int kSectionCount = kWorldHeight / kSectionSize;

// How a section's blocks are stored. Most of the world is solid rock or air (Uniform: one value, no array), and
// a section rarely holds more than a few kinds of block, so the rest keep a palette and 4 or 8 bits per block;
// only a section with more than 256 kinds stores the 16-bit ids themselves.
enum class Packing : std::uint8_t { Uniform, Nibble, Byte, Wide };

// One 16x16x16 piece of a chunk. Sections are never changed once they are shared: an edit builds a new one
// (sectionWithBlock), so another thread meshing the old one never sees a change.
struct SectionData {
    Block uniform = Block::Air;
    Packing packing = Packing::Uniform;
    std::vector<Block> palette;             // Nibble and Byte: index -> block
    std::unique_ptr<std::uint8_t[]> data;   // Nibble 2048 bytes, Byte 4096, Wide 8192 (little-endian 16-bit ids)

    [[nodiscard]] static constexpr int index(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
    [[nodiscard]] Block at(int x, int y, int z) const { return atIndex(index(x, y, z)); }
    [[nodiscard]] Block atIndex(int i) const {
        switch (packing) {
            case Packing::Uniform: return uniform;
            case Packing::Nibble: return palette[(data[static_cast<std::size_t>(i >> 1)] >> ((i & 1) * 4)) & 15];
            case Packing::Byte: return palette[data[static_cast<std::size_t>(i)]];
            case Packing::Wide: {
                const std::size_t o = static_cast<std::size_t>(i) * 2;
                return static_cast<Block>(data[o] | (data[o + 1] << 8));
            }
        }
        return uniform;
    }
    [[nodiscard]] bool isUniform() const { return packing == Packing::Uniform; }
    // `count` blocks of one row (fixed y and z), starting at x0, into dst.
    void copyRow(int x0, int y, int z, int count, Block* dst) const;
    // All 4096 blocks, in index() order.
    void decode(Block* dst) const;
    [[nodiscard]] std::size_t memoryBytes() const;
};
using SectionPtr = std::shared_ptr<const SectionData>;  // null means all air

// Builds a section block by block; build() stores it compactly.
class SectionBuilder {
public:
    SectionBuilder() { blocks_.fill(Block::Air); }
    void set(int x, int y, int z, Block b) { blocks_[static_cast<std::size_t>(SectionData::index(x, y, z))] = b; }
    [[nodiscard]] Block get(int x, int y, int z) const { return blocks_[static_cast<std::size_t>(SectionData::index(x, y, z))]; }
    void fill(Block b) { blocks_.fill(b); }
    // y0..y1 (exclusive) of every column set to `b`.
    void fillLayers(int y0, int y1, Block b);
    // Replaces the contents with those of `section` (null: all air).
    void load(const SectionPtr& section);
    [[nodiscard]] SectionPtr build() const;  // null for an all-air section
    [[nodiscard]] std::array<Block, kSectionVolume>& raw() { return blocks_; }

private:
    std::array<Block, kSectionVolume> blocks_;
};

[[nodiscard]] SectionPtr uniformSection(Block b);  // shared, so a solid-stone section costs nothing
// A copy of `section` with one block replaced (compacted again if it became uniform).
[[nodiscard]] SectionPtr sectionWithBlock(const SectionPtr& section, int x, int y, int z, Block b);

struct ChunkLight;  // world/light.hpp

// The biome of every 4x4x4 cell of a chunk (4 x 96 x 4, one byte each; ids belong to the world generator's BiomeRegistry),
// kept so tints can follow the biomes. Index ((qz * 4 + qx) * 96 + qyIndex), the generator's own layout.
struct BiomeGrid {
    static constexpr int kQuartsY = 96;
    static constexpr int kMinQuartY = -16;
    std::array<std::uint8_t, 4 * 4 * kQuartsY> ids{};
    // Chunk-local block x, z (0..15) and world block y (clamped to the world).
    [[nodiscard]] std::uint8_t at(int x, int y, int z) const {
        int qy = (y >> 2) - kMinQuartY;
        qy = qy < 0 ? 0 : (qy >= kQuartsY ? kQuartsY - 1 : qy);
        return ids[static_cast<std::size_t>(((z >> 2) * 4 + (x >> 2)) * kQuartsY + qy)];
    }
};

// A column of 24 sections.
struct Chunk {
    std::array<SectionPtr, kSectionCount> sections;
    std::shared_ptr<const BiomeGrid> biomes;  // null for chunks that have no biome data (Superflat)
    std::shared_ptr<const ChunkLight> light;  // null until the light engine has done this chunk
    [[nodiscard]] Block at(int x, int y, int z) const;  // x, z in 0..15, y a world height
    [[nodiscard]] std::size_t memoryBytes() const;
};

struct ChunkPos {
    int x = 0;
    int z = 0;
    bool operator==(const ChunkPos&) const = default;
};
struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& p) const {
        return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.x)) << 32) | static_cast<std::uint32_t>(p.z));
    }
};

// The chunks that are loaded. Only the main thread touches it; other threads work on SectionPtr snapshots.
class Level {
public:
    void putChunk(ChunkPos pos, std::shared_ptr<Chunk> chunk) { chunks_[pos] = std::move(chunk); }
    void removeChunk(ChunkPos pos) { chunks_.erase(pos); }
    [[nodiscard]] Chunk* chunk(ChunkPos pos);
    [[nodiscard]] const Chunk* chunk(ChunkPos pos) const;
    [[nodiscard]] bool isLoaded(int blockX, int blockZ) const;
    // Air above and below the world and where nothing is loaded.
    [[nodiscard]] Block block(int x, int y, int z) const;
    // False if the block is outside the world or its chunk is not loaded.
    bool setBlock(int x, int y, int z, Block b);
    [[nodiscard]] const std::unordered_map<ChunkPos, std::shared_ptr<Chunk>, ChunkPosHash>& chunks() const { return chunks_; }
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    std::unordered_map<ChunkPos, std::shared_ptr<Chunk>, ChunkPosHash> chunks_;
};

// Floor division by 16 (so -1 is chunk -1, not 0).
[[nodiscard]] constexpr int chunkOf(int blockCoordinate) { return blockCoordinate >> 4; }
[[nodiscard]] constexpr int sectionOf(int y) { return (y - kMinY) >> 4; }

}  // namespace falcon
