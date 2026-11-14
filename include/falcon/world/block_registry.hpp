#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace falcon {

// A block state: one id per combination of a block's properties (26.3 has 35,723 of them). Id 0 is air.
// The ids are the game's own numbering: a block's states are consecutive, in the order the game lists them
// (the last property changes fastest).
enum class Block : std::uint16_t { Air = 0 };

[[nodiscard]] constexpr std::uint16_t stateId(Block b) { return static_cast<std::uint16_t>(b); }

enum StateFlag : std::uint16_t {
    kStateOpaqueCube = 1 << 0,   // "solid_render": a full opaque cube, hides its neighbours' faces
    kStateCanOcclude = 1 << 1,
    kStateLiquid = 1 << 2,
    kStateReplaceable = 1 << 3,  // placing a block here replaces it (air, water, grass, ...)
    kStateFullCollision = 1 << 4,  // a plain 1x1x1 collision box
    kStateHasModel = 1 << 5,     // render shape MODEL (not invisible)
    kStateBlockEntity = 1 << 6,
    kStateRandomTicks = 1 << 7,
    kStateSkylightDown = 1 << 8,  // skylight passes straight down through it undimmed
    kStateRequiresTool = 1 << 9,
    kStateIgnitedByLava = 1 << 10,
    kStateHasOffset = 1 << 11,    // drawn shifted randomly (flowers, grass)
    kStateShapeOcclusion = 1 << 12,
    kStateAir = 1 << 13,          // air, cave air or void air
    kStateWater = 1 << 14,        // holds water: water itself, waterlogged blocks, kelp and seagrass
    kStateLava = 1 << 15,
};

// Facts about one state, taken from the game's exported data.
struct StateInfo {
    std::uint16_t flags = 0;
    std::uint16_t block = 0;      // index into BlockRegistry::blocks()
    std::uint8_t emission = 0;    // block light it gives off, 0..15
    std::uint8_t dampening = 0;   // light it absorbs, 0..15 (15: opaque)
    std::uint8_t push = 0;        // 0 normal, 1 destroyed by pistons, 2 blocked, 3 ignored, 4 push-only
    std::uint8_t fluidLevel = 0;   // water and lava: the "level" property (0 source, 1-7 flowing, 8+ falling)
    float hardness = 0.0f;        // < 0 means unbreakable
    float explosionResistance = 0.0f;
    float friction = 0.6f;
    float speedFactor = 1.0f;
    float jumpFactor = 1.0f;

    [[nodiscard]] bool has(StateFlag f) const { return (flags & f) != 0; }
};

struct BlockProperty {
    std::string name;
    std::vector<std::string> values;  // in the order the game numbers them
};

// One block: a name and the property combinations that make up its states.
struct BlockType {
    std::string name;  // "oak_stairs": no "minecraft:" prefix
    std::uint16_t firstState = 0;
    std::uint16_t stateCount = 1;
    std::uint16_t defaultState = 0;
    std::vector<BlockProperty> properties;  // sorted like the game's (by name)
    std::string item;   // the item that stands for it ("oak_stairs"), empty if none
    std::string sound;  // sound set: "stone", "grass", "wood", ...
};

class BlockRegistry {
public:
    // Reads the game's exported data: <gameDataDir>/registries.json, model-states.json and gameplay-facts.json
    // (a 55 MB file, streamed once and then kept as a small binary cache in cacheDir). Null and `error` on failure.
    [[nodiscard]] static std::shared_ptr<const BlockRegistry> loadFromGameData(const std::filesystem::path& gameDataDir,
                                                                               const std::filesystem::path& cacheDir,
                                                                               std::string& error);
    // A handful of blocks with made-up ids, so tests run without the game's files.
    [[nodiscard]] static std::shared_ptr<const BlockRegistry> makeMinimal();

    [[nodiscard]] std::size_t stateCount() const { return states_.size(); }
    // False for the small built-in set used by tests.
    [[nodiscard]] bool isFromGameData() const { return fromGameData_; }
    [[nodiscard]] const std::vector<BlockType>& blocks() const { return blocks_; }
    [[nodiscard]] const StateInfo& state(Block b) const {
        const auto id = static_cast<std::size_t>(stateId(b));
        return states_[id < states_.size() ? id : 0];
    }
    [[nodiscard]] const BlockType& type(Block b) const { return blocks_[state(b).block]; }
    [[nodiscard]] const BlockType& type(int blockIndex) const { return blocks_[static_cast<std::size_t>(blockIndex)]; }

    // "stone" or "minecraft:stone" -> block index.
    [[nodiscard]] std::optional<int> findBlock(std::string_view name) const;
    [[nodiscard]] Block defaultState(int blockIndex) const { return static_cast<Block>(blocks_[static_cast<std::size_t>(blockIndex)].defaultState); }
    // The default state of the block called `name`; nullopt if there is none.
    [[nodiscard]] std::optional<Block> find(std::string_view name) const;
    // "minecraft:oak_stairs[facing=east,half=top]" (properties optional, unnamed ones default).
    [[nodiscard]] std::optional<Block> parse(std::string_view text) const;
    // The same block with one property changed; unchanged if the property or value does not exist.
    [[nodiscard]] Block with(Block b, std::string_view property, std::string_view value) const;
    // The value of a property of this state ("" if the block has none such).
    [[nodiscard]] std::string_view get(Block b, std::string_view property) const;
    // "oak_stairs[facing=east,half=top,...]" for logs and debugging.
    [[nodiscard]] std::string describe(Block b) const;

    [[nodiscard]] bool isAir(Block b) const { return state(b).has(kStateAir); }
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    friend class BlockRegistryBuilder;
    bool fromGameData_ = false;
    std::vector<BlockType> blocks_;
    std::vector<StateInfo> states_;
    std::unordered_map<std::string, int> byName_;
};

// The registry the world code uses. Installed once at start-up (the client) or per test.
[[nodiscard]] const BlockRegistry& blockRegistry();
void installBlockRegistry(std::shared_ptr<const BlockRegistry> registry);
[[nodiscard]] bool hasBlockRegistry();
// The installed registry as a shared pointer (null if none).
[[nodiscard]] std::shared_ptr<const BlockRegistry> currentBlockRegistry();

// Blocks the engine itself needs to know by name, looked up once when the registry is installed. A name the game
// does not have is Air.
struct CommonBlocks {
    Block air{}, stone{}, dirt{}, grassBlock{}, bedrock{}, sand{}, gravel{}, cobblestone{}, water{}, lava{},
        deepslate{}, oakPlanks{}, oakLog{};
};
[[nodiscard]] const CommonBlocks& commonBlocks();

}  // namespace falcon
