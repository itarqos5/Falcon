#pragma once

#include "apex/assets/resource_manager.hpp"
#include "apex/gfx/texture.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace apex {

// Loads and caches PNG textures from <ns>/textures/<path>. Textures use nearest filtering,
// like Minecraft's default GUI textures.
class TextureManager {
public:
    explicit TextureManager(const ResourceManager& resources) : resources_(resources) {}

    // `path` is relative to textures/, e.g. "gui/title/minecraft.png". Throws if missing.
    [[nodiscard]] const Texture2D& get(const std::string& path);

    // Registers a texture built in code under `path`, replacing any cached one. Call before the
    // texture is first drawn (references from get() are invalidated for that path).
    void add(const std::string& path, const Image& image);

    // ---- reloading resources (see ResourceReloader) ----
    // The paths loaded so far.
    [[nodiscard]] std::vector<std::string> keys() const;
    // Where a path's PNG comes from, so a reload can tell whether a resource pack changes it.
    [[nodiscard]] static ResourceLocation locationOf(const std::string& path);
    // Decodes a texture without touching GL or the cache (safe on any thread).
    [[nodiscard]] static std::optional<Image> decode(const ResourceManager& resources, const std::string& path);
    // Swaps `fresh` into the cached texture for `path`; the Texture2D object keeps its address, so
    // references from get() stay valid. False if `path` is not cached.
    bool replace(const std::string& path, Texture2D& fresh);

private:
    const ResourceManager& resources_;
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> cache_;
};

}  // namespace apex
