#pragma once

#include "apex/assets/resource_manager.hpp"
#include "apex/gfx/texture.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace apex {

// How a GUI sprite is resized, from the "gui.scaling" block of its .png.mcmeta.
struct SpriteScaling {
    enum class Type { Stretch, Tile, NineSlice };
    Type type = Type::Stretch;
    int width = 0;   // declared sprite size (defaults to the texture size)
    int height = 0;
    int left = 0;    // nine-slice border widths
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool stretchInner = false;  // stretch (instead of tile) the nine-slice edges and centre
};

struct Sprite {
    Texture2D texture;
    SpriteScaling scaling;
};

// A sprite's pixels and scaling, decoded but not yet uploaded to GL.
struct SpriteData {
    Image image;
    SpriteScaling scaling;
};

// Loads and caches GUI sprites from minecraft:textures/gui/sprites/<id>.png.
class SpriteManager {
public:
    explicit SpriteManager(const ResourceManager& resources) : resources_(resources) {}

    // `id` like "widget/button". Throws std::runtime_error if the sprite is missing.
    [[nodiscard]] const Sprite& get(const std::string& id);

    // ---- reloading resources (see ResourceReloader) ----
    // The ids loaded so far.
    [[nodiscard]] std::vector<std::string> keys() const;
    // The PNG and its .mcmeta for an id, so a reload can tell whether a resource pack changes it.
    [[nodiscard]] static ResourceLocation pngOf(const std::string& id);
    [[nodiscard]] static ResourceLocation mcmetaOf(const std::string& id);
    // Reads a sprite without touching GL or the cache (safe on any thread).
    [[nodiscard]] static std::optional<SpriteData> decode(const ResourceManager& resources, const std::string& id);
    // Swaps a new picture and scaling into the cached sprite for `id`; the Sprite keeps its
    // address, so references from get() stay valid. False if `id` is not cached.
    bool replace(const std::string& id, Texture2D& fresh, const SpriteScaling& scaling);

private:
    const ResourceManager& resources_;
    std::unordered_map<std::string, std::unique_ptr<Sprite>> cache_;
};

}  // namespace apex
