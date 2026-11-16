#pragma once

#include "falcon/assets/nbt.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/item_entity.hpp"
#include "falcon/world/item_stack.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace falcon {

// Every block of the world that is not what the generator makes: the player's changes, and where water has flowed. The terrain itself is not stored, only
// this difference: a world is its seed plus its edits, and a chunk that is made again (after it was out of range for long, or after the game was
// closed) gets its edits put back at once, from any thread.
class WorldEdits {
public:
    void set(int x, int y, int z, Block block);
    // Puts the edits of the chunk into it (the chunk is not shared with anyone yet, or its owner is the caller).
    void applyTo(ChunkPos pos, Chunk& chunk) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t chunkCount() const;
    // Changed since the last toNbt() or fromNbt().
    [[nodiscard]] bool changed() const;

    // The edits as one compound: a palette of block names ("oak_log[axis=y]") and, per chunk, an int array of (position, palette index) pairs.
    [[nodiscard]] NbtTag toNbt(const BlockRegistry& registry);
    // Replaces the edits with what `tag` holds; false if it is not what toNbt() writes (a block name this game does not know is skipped).
    bool fromNbt(const NbtTag& tag, const BlockRegistry& registry);

private:
    mutable std::mutex mutex_;
    // chunk -> (local position: x | z << 4 | (y - kMinY) << 8) -> the block's state
    std::unordered_map<ChunkPos, std::unordered_map<std::uint32_t, std::uint16_t>, ChunkPosHash> chunks_;
    bool changed_ = false;
};

// What of the player is kept.
struct SavedPlayer {
    double x = 0.0, y = 0.0, z = 0.0;
    float yaw = 0.0f, pitch = 0.0f;
    bool flying = false;
    float health = 20.0f;
    int food = 20;
    float saturation = 5.0f;
    int level = 0;
    float progress = 0.0f;
    int selectedSlot = 0;
    int gameType = -1;  // -1: not stored
    std::vector<std::pair<int, ItemStack>> inventory;  // (slot, stack) of the non-empty slots
};

// One world's own save, <world folder>/falcon_save.dat: the player and the items lying about (as gzip NBT), and the block edits.
struct WorldSave {
    static constexpr int kVersion = 1;
    bool hasPlayer = false;
    SavedPlayer player;
    std::vector<ItemEntity> items;
    std::map<std::string, std::string> gameRules;  // the game rules that differ from their defaults (/gamerule)
    int difficulty = -1;                           // -1: not stored
    NbtTag mobs;                                   // the mobs and the chunks that had their animals (Mobs::toNbt); type End: none stored
    int dragonState = 0;                           // 0 the Ender Dragon has not been met, 1 it is in the End, 2 it was killed
    int dimension = 0;                             // the dimension the player is in: 0 Overworld, 1 Nether, 2 End
    long long dayTime = -1;                        // the time of day in ticks (world/day_cycle.hpp); -1: not stored (a world from before there was a clock)
    long long gameTime = 0;                        // the ticks the world has run
    NbtTag containers;                             // what the chests, furnaces ... hold (BlockStorages::toNbt); type End: none stored
    std::vector<std::pair<int, ItemStack>> enderChest;  // (slot, stack) of the player's ender chest
};

// An item stack as NBT ({id, count, damage, name, components}) and back (an item this game does not know gives an empty stack).
[[nodiscard]] NbtTag stackToNbt(const ItemStack& stack);
[[nodiscard]] ItemStack stackFromNbt(const NbtTag& tag);

// Writes the save: the file is written whole beside the old one and then swapped in, the previous copy is kept as falcon_save.dat_old. Returns "" or
// what went wrong.
[[nodiscard]] std::string writeWorldSave(const std::filesystem::path& file, const WorldSave& save, WorldEdits& edits, const BlockRegistry& registry);
// Reads it (falling back to the _old copy if the file is damaged). False, with `error` empty, when there is no save yet; false with a message when it
// is unreadable.
[[nodiscard]] bool readWorldSave(const std::filesystem::path& file, WorldSave& save, WorldEdits& edits, const BlockRegistry& registry, std::string& error);

}  // namespace falcon
