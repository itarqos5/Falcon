#include "apex/gfx/texture_manager.hpp"

#include <stdexcept>

namespace apex {

ResourceLocation TextureManager::locationOf(const std::string& path) {
    return ResourceLocation("minecraft", "textures/" + path);
}

std::optional<Image> TextureManager::decode(const ResourceManager& resources, const std::string& path) {
    const auto bytes = resources.readBytes(locationOf(path));
    if (!bytes) return std::nullopt;
    return decodePng(*bytes);
}

const Texture2D& TextureManager::get(const std::string& path) {
    if (const auto it = cache_.find(path); it != cache_.end()) return *it->second;

    const ResourceLocation loc = locationOf(path);
    const auto bytes = resources_.readBytes(loc);
    if (!bytes) throw std::runtime_error("missing texture " + loc.toString());
    const auto image = decodePng(*bytes);
    if (!image) throw std::runtime_error("cannot decode texture " + loc.toString());

    auto texture = std::make_unique<Texture2D>(*image, FilterMode::Nearest);
    return *cache_.emplace(path, std::move(texture)).first->second;
}

void TextureManager::add(const std::string& path, const Image& image) {
    cache_[path] = std::make_unique<Texture2D>(image, FilterMode::Nearest);
}

std::vector<std::string> TextureManager::keys() const {
    std::vector<std::string> keys;
    keys.reserve(cache_.size());
    for (const auto& entry : cache_) keys.push_back(entry.first);
    return keys;
}

bool TextureManager::replace(const std::string& path, Texture2D& fresh) {
    const auto it = cache_.find(path);
    if (it == cache_.end()) return false;
    it->second->swap(fresh);
    return true;
}

}  // namespace apex
