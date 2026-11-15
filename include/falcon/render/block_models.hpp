#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_manager.hpp"
#include "falcon/gfx/mipmaps.hpp"
#include "falcon/gfx/texture_array.hpp"
#include "falcon/world/block_render.hpp"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace falcon {
namespace worldgen {
class BiomeRegistry;
}


// How a model is placed in one "display context" (ItemTransform): rotation in degrees, translation in 1/16 block and a
// scale, applied around the middle of the model as translate, rotate (X then Y then Z), scale.
struct DisplayTransform {
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool present = false;  // the model (or a parent) says something about this context
};

// What a model file (models/<name>.json) says, with the parent chain merged: the geometry, or for flat items the textures
// of their layers, and how to show it in each display context ("gui", "ground", "fixed", "head", "on_shelf",
// "firstperson_righthand", "firstperson_lefthand", "thirdperson_righthand", "thirdperson_lefthand").
struct ItemModelInfo {
    bool generated = false;      // parent builtin/generated: flat layers made from layer0, layer1, ... textures
    bool builtinEntity = false;  // parent builtin/entity: drawn by code in the game (chests, banners, ...), no geometry here
    std::vector<std::string> layers;  // generated: "item/diamond_pickaxe", ... in layer order
    QuadList quads;                   // baked "elements" (null for generated and entity models)
    std::map<std::string, DisplayTransform> display;
    std::string guiLight = "side";    // "side": lit like a block, "front": flat
    std::string particle;             // the model's particle texture (a stand-in picture for models drawn by code)
    [[nodiscard]] DisplayTransform transform(const std::string& context) const {
        const auto it = display.find(context);
        return it == display.end() ? DisplayTransform{} : it->second;
    }
};

// The block models of the game, read from its assets (through the resource packs in use) and baked on first use:
// blockstates/<block>.json choose a model for a state, models/block/*.json (with parents, element rotation, face
// rotation, tint indices, cull faces) become ModelQuads. Pictures get a layer of one texture array the first time
// something uses them. The baking part needs no OpenGL; uploading does. Safe to call from several threads.
class BlockModels final : public BlockRenderProvider {
public:
    BlockModels(const ResourceManager& resources, const BlockRegistry& registry);
    ~BlockModels() override;

    [[nodiscard]] const StateRender& render(Block block) const override;

    // ---- pictures ----
    [[nodiscard]] int tileSize() const { return tile_; }
    // The layer of a block texture ("minecraft:block/stone", "block/stone"); a magenta layer if it is missing.
    std::uint16_t layerFor(const std::string& texture) const;
    [[nodiscard]] RenderType layerType(std::uint16_t layer) const;
    [[nodiscard]] Image layerImage(std::uint16_t layer) const;
    [[nodiscard]] std::size_t layerCount() const;
    // Whether new pictures have been loaded since the last uploadPending().
    [[nodiscard]] bool hasPending() const { return pendingFlag_.load(std::memory_order_acquire); }
    // Main thread, with a GL context: makes the texture array hold every picture loaded so far.
    void uploadPending();
    // Main thread, with a GL context: puts the right frame into every animated picture (fire, water, lava ...) for this moment, `ticks` being game ticks (20 a second).
    void animate(double ticks);
    void bindTextures(unsigned unit) const;
    [[nodiscard]] std::size_t gpuBytes() const { return array_.gpuBytes(); }

    // ---- tints ----
    // The colour (0xRRGGBB) a tint source gives at a position. For now one fixed plains-like colour per source;
    // biomes will refine this.
    [[nodiscard]] std::uint32_t tintColor(const StateRender& state, int x, int y, int z) const override;
    [[nodiscard]] std::uint32_t grassColor(double temperature, double downfall) const;
    [[nodiscard]] std::uint32_t foliageColor(double temperature, double downfall) const;
    // Lets tints follow the biomes of the world generator (call before any meshing starts; the registry must outlive this).
    void setBiomes(const worldgen::BiomeRegistry* biomes);
    bool fillTints(const std::shared_ptr<const BiomeGrid>* biomes, int originX, int originY, int originZ, TintGrid& out) const override;

    // ---- for item icons and held items ----
    // The quads of a model ("minecraft:block/stone", "block/oak_stairs_inner") rotated like a blockstate variant.
    [[nodiscard]] QuadList modelQuads(const std::string& model, int xRotation = 0, int yRotation = 0, bool uvlock = false) const;
    // The quads that stand for a block state in a menu: its first variant of every part.
    [[nodiscard]] std::vector<ModelQuad> iconQuads(Block block) const;

    // The model file `name` ("item/diamond_pickaxe", "minecraft:block/stone") merged with its parents, for item pictures and
    // held items; nullopt if there is no such model.
    [[nodiscard]] std::optional<ItemModelInfo> itemModelInfo(const std::string& name) const;
    [[nodiscard]] const ResourceManager& resources() const { return resources_; }

    [[nodiscard]] const BlockRegistry& registry() const { return registry_; }
    [[nodiscard]] std::size_t bakedStates() const { return baked_.load(); }

private:
    struct RawModel;
    struct VariantSpec {
        std::string model;
        int x = 0, y = 0, weight = 1;
        bool uvlock = false;
    };
    struct PartSpec {
        // Empty: always. Otherwise the part applies when the condition matches the state.
        std::shared_ptr<nlohmann::json> when;
        std::vector<VariantSpec> variants;
    };
    struct BlockStateSpec {
        bool multipart = false;
        std::vector<std::pair<std::string, std::vector<VariantSpec>>> variants;  // "facing=north,half=top" -> models
        std::vector<PartSpec> parts;
        bool found = false;
    };

    const StateRender* bake(Block block) const;
    const BlockStateSpec& blockState(const std::string& blockName) const;
    std::shared_ptr<const RawModel> loadModel(const std::string& name) const;
    QuadList bakeModel(const std::string& name, int x, int y, bool uvlock) const;
    bool matchesWhen(const nlohmann::json& when, Block block) const;
    bool matchesKey(const std::string& key, Block block) const;

    ResourceManager resources_;
    const BlockRegistry& registry_;
    int tile_ = 16;

    // Baked states: one slot per state id; filled lazily under bakeMutex_.
    mutable std::mutex bakeMutex_;
    mutable std::unique_ptr<std::atomic<const StateRender*>[]> slots_;
    mutable std::vector<std::unique_ptr<StateRender>> owned_;
    mutable std::atomic<std::size_t> baked_{0};
    mutable std::unordered_map<std::string, BlockStateSpec> blockStates_;
    mutable std::unordered_map<std::string, std::shared_ptr<const RawModel>> rawModels_;
    mutable std::unordered_map<std::string, QuadList> bakedModels_;

    // Pictures.
    mutable std::mutex textureMutex_;
    mutable std::unordered_map<std::string, std::uint16_t> layerOf_;
    mutable std::vector<Image> layers_;
    mutable std::vector<std::vector<Image>> layerMips_;  // per layer: its mip chain made the way the game makes it (level 0 is layers_[i])
    mutable std::vector<RenderType> layerTypes_;
    mutable std::size_t uploaded_ = 0;
    // A picture that is a strip of frames (the .mcmeta animation): each frame with its mip levels, the order they play in and for how many ticks each.
    struct Animation {
        std::uint16_t layer = 0;
        std::vector<std::vector<Image>> frames;
        std::vector<int> order, ticks;
        int total = 0;
        int shown = -1;
    };
    mutable std::vector<Animation> animations_;
    mutable std::atomic<bool> pendingFlag_{false};
    TextureArray2D array_;
    int arrayCapacity_ = 0;

    Image grassMap_, foliageMap_, dryFoliageMap_;
    struct BiomeColor {
        std::uint32_t grass = 0x91BD59, foliage = 0x77AB2F, dry = 0xAE9F57, water = 0x3F76E4;
        std::uint8_t grassModifier = 0;  // worldgen::GrassColorModifier
    };
    std::vector<BiomeColor> biomeColors_;  // by biome id; empty: no biomes, plains colours everywhere
};

[[nodiscard]] TintSource tintSourceOf(const std::string& blockName);

}  // namespace falcon
