#pragma once

#include "falcon/world/chunk.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace falcon {

// Light levels 0..15 for every block of a chunk: sky light (from the open sky, straight down without loss, spreading
// sideways and down a level less per block) and block light (from torches, lava, glowstone ...). A section whose
// light is the same everywhere (open sky above the ground: 15, solid rock: 0) stores no array.
struct NibbleArray {
    std::array<std::uint8_t, 2048> data{};
    [[nodiscard]] int get(int index) const { return (data[static_cast<std::size_t>(index >> 1)] >> ((index & 1) * 4)) & 15; }
    void set(int index, int value) {
        std::uint8_t& b = data[static_cast<std::size_t>(index >> 1)];
        b = static_cast<std::uint8_t>((b & ~(15 << ((index & 1) * 4))) | (value << ((index & 1) * 4)));
    }
};

struct ChunkLight {
    std::array<std::shared_ptr<const NibbleArray>, kSectionCount> sky, block;  // null: every block is skyFill / blockFill
    std::array<std::uint8_t, kSectionCount> skyFill{}, blockFill{};

    // Chunk-local x, z (0..15) and a world height; outside the world there is full sky above and none below.
    [[nodiscard]] int skyAt(int x, int y, int z) const {
        if (y >= kMaxY) return 15;
        if (y < kMinY) return 0;
        const int s = sectionOf(y);
        const auto& a = sky[static_cast<std::size_t>(s)];
        return a ? a->get(SectionData::index(x, (y - kMinY) & 15, z)) : skyFill[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] int blockAt(int x, int y, int z) const {
        if (y >= kMaxY || y < kMinY) return 0;
        const int s = sectionOf(y);
        const auto& a = block[static_cast<std::size_t>(s)];
        return a ? a->get(SectionData::index(x, (y - kMinY) & 15, z)) : blockFill[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] std::size_t memoryBytes() const;
};

// What a server says about the light of a chunk when it changes, or with the chunk itself: the sections whose light it sent and the ones it says are dark. A section it
// says nothing about keeps the light it has (a block was placed and only the light around it was sent again).
struct ChunkLightPatch {
    static constexpr std::uint8_t kUnchanged = 0, kSent = 1, kDark = 2;
    std::array<std::shared_ptr<const NibbleArray>, kSectionCount> sky, block;
    std::array<std::uint8_t, kSectionCount> skyState{}, blockState{};

    [[nodiscard]] bool empty() const {
        for (std::size_t s = 0; s < kSectionCount; ++s) {
            if (skyState[s] != kUnchanged || blockState[s] != kUnchanged) return false;
        }
        return true;
    }
    void applyTo(ChunkLight& light) const {
        for (std::size_t s = 0; s < kSectionCount; ++s) {
            if (skyState[s] != kUnchanged) {
                light.sky[s] = skyState[s] == kSent ? sky[s] : nullptr;
                light.skyFill[s] = 0;
            }
            if (blockState[s] != kUnchanged) {
                light.block[s] = blockState[s] == kSent ? block[s] : nullptr;
                light.blockFill[s] = 0;
            }
        }
    }
};

// What the light of a chunk depends on: its own sections and those of the 8 chunks around it (light reaches at most 15
// blocks), index (dx+1) + 3*(dz+1). Sections are immutable and shared, so this is a cheap snapshot for a worker thread.
struct LightInput {
    std::array<std::array<SectionPtr, kSectionCount>, 9> chunks;
    bool skylight = true;  // false in a dimension with no sky (the Nether): only the blocks give light
};

[[nodiscard]] LightInput snapshotLightInput(const Level& level, int chunkX, int chunkZ);

// The light of the middle chunk of `input`. Thread-safe (works in thread-local buffers).
[[nodiscard]] std::shared_ptr<const ChunkLight> computeChunkLight(const LightInput& input);

// The light a player carries (a torch in the hand): `lightLevel` (1..15) shines from block (x, y, z) and spreads like block light - one
// level less per block, or more where the block it enters dims light (water, leaves), and not at all into solid blocks. Fills `volume`
// (size * size * size bytes, x fastest, then y, then z) with the level 0..15 of every block of the cube of `size` blocks whose middle
// block is (x, y, z). Uses the main thread's `level`; chunks that are not loaded count as air.
void computeCarriedLight(const Level& level, int x, int y, int z, int lightLevel, int size, std::vector<std::uint8_t>& volume);

}  // namespace falcon
