#pragma once

#include "apex/world/chunk.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace apex {

// Where chunks go when the player moves away from them (or lowers the Render Distance): not away for good, but into a compressed
// copy of their blocks and biomes, a few KB each. When they are wanted again the blocks come back from there, without generating
// the terrain again (so a block the player broke or placed is still broken or placed), and only light and meshes are made again.
// The light and the meshes are not kept: they are most of a chunk's memory and cheap to make again.
//
// The cache has a memory budget; past it the chunks furthest from the player are forgotten (those are made afresh from the seed).
// hold() and pack() are meant for the thread that owns the level and a worker (pack() is the slow part: the compression).
class ChunkCache {
public:
    explicit ChunkCache(std::size_t budgetBytes) : budget_(budgetBytes) {}

    // Takes over a chunk that has left the loaded area. Until pack() has made its small version it is kept as it is, so take() finds it.
    void hold(ChunkPos pos, std::shared_ptr<Chunk> chunk);
    // Compresses a held chunk and lets go of the full one. Safe to call from a worker thread.
    void pack(ChunkPos pos);
    // The chunk back without its light (null if the cache does not have it), and forgotten by the cache.
    [[nodiscard]] std::shared_ptr<Chunk> take(ChunkPos pos);
    [[nodiscard]] bool contains(ChunkPos pos) const;
    // Forgets the chunks furthest from `center` until the packed ones fit the budget.
    void trim(ChunkPos center);

    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] std::size_t packedBytes() const;  // what the compressed copies take
    void setBudget(std::size_t bytes);

    // The two directions on their own, for tests: a chunk's blocks and biomes as bytes, and back.
    [[nodiscard]] static std::vector<unsigned char> compress(const Chunk& chunk);
    [[nodiscard]] static std::shared_ptr<Chunk> decompress(const std::vector<unsigned char>& bytes);

private:
    struct Entry {
        std::shared_ptr<Chunk> held;       // until it is packed
        std::vector<unsigned char> packed; // after
    };
    mutable std::mutex mutex_;
    std::unordered_map<ChunkPos, Entry, ChunkPosHash> entries_;
    std::size_t packedBytes_ = 0;
    std::size_t budget_;
};

}  // namespace apex
