#include "falcon/ui/sprite_manager.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace falcon {
using nlohmann::json;

namespace {

SpriteScaling parseScaling(const std::optional<std::string>& mcmeta, int textureWidth,
                           int textureHeight) {
    SpriteScaling s;
    s.width = textureWidth;
    s.height = textureHeight;
    if (!mcmeta) return s;

    const json meta = json::parse(*mcmeta, nullptr, /*allow_exceptions=*/false);
    if (!meta.is_object() || !meta.contains("gui")) return s;
    const json& gui = meta["gui"];
    if (!gui.is_object() || !gui.contains("scaling")) return s;
    const json& scaling = gui["scaling"];
    if (!scaling.is_object()) return s;

    const std::string type = scaling.value("type", "stretch");
    if (type == "tile") {
        s.type = SpriteScaling::Type::Tile;
    } else if (type == "nine_slice") {
        s.type = SpriteScaling::Type::NineSlice;
    }
    s.width = scaling.value("width", s.width);
    s.height = scaling.value("height", s.height);
    s.stretchInner = scaling.value("stretch_inner", false);

    if (scaling.contains("border")) {
        const json& border = scaling["border"];
        if (border.is_number_integer()) {
            s.left = s.top = s.right = s.bottom = border.get<int>();
        } else if (border.is_object()) {
            s.left = border.value("left", 0);
            s.top = border.value("top", 0);
            s.right = border.value("right", 0);
            s.bottom = border.value("bottom", 0);
        }
    }
    return s;
}

}  // namespace

ResourceLocation SpriteManager::pngOf(const std::string& id) {
    return ResourceLocation("minecraft", "textures/gui/sprites/" + id + ".png");
}

ResourceLocation SpriteManager::mcmetaOf(const std::string& id) { return pngOf(id).decorated("", ".mcmeta"); }

std::optional<SpriteData> SpriteManager::decode(const ResourceManager& resources, const std::string& id) {
    const auto bytes = resources.readBytes(pngOf(id));
    if (!bytes) return std::nullopt;
    auto image = decodePng(*bytes);
    if (!image) return std::nullopt;
    SpriteData data;
    data.scaling = parseScaling(resources.readText(mcmetaOf(id)), image->width, image->height);
    data.image = std::move(*image);
    return data;
}

const Sprite& SpriteManager::get(const std::string& id) {
    if (const auto it = cache_.find(id); it != cache_.end()) return *it->second;

    const auto data = decode(resources_, id);
    if (!data) throw std::runtime_error("missing or undecodable GUI sprite " + pngOf(id).toString());

    auto sprite = std::make_unique<Sprite>();
    sprite->texture = Texture2D(data->image, FilterMode::Nearest);
    sprite->scaling = data->scaling;
    return *cache_.emplace(id, std::move(sprite)).first->second;
}

std::vector<std::string> SpriteManager::keys() const {
    std::vector<std::string> keys;
    keys.reserve(cache_.size());
    for (const auto& entry : cache_) keys.push_back(entry.first);
    return keys;
}

bool SpriteManager::replace(const std::string& id, Texture2D& fresh, const SpriteScaling& scaling) {
    const auto it = cache_.find(id);
    if (it == cache_.end()) return false;
    it->second->texture.swap(fresh);
    it->second->scaling = scaling;
    return true;
}

}  // namespace falcon
