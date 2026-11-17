#include "falcon/render/block_models.hpp"
#include "falcon/util/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace falcon {
using nlohmann::json;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string stripNamespace(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) name.erase(0, 10);
    return name;
}

Image blank(int size, std::uint32_t rgba) {
    Image image;
    image.width = size;
    image.height = size;
    image.rgba.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        image.rgba[i] = static_cast<unsigned char>(rgba >> 24);
        image.rgba[i + 1] = static_cast<unsigned char>(rgba >> 16);
        image.rgba[i + 2] = static_cast<unsigned char>(rgba >> 8);
        image.rgba[i + 3] = static_cast<unsigned char>(rgba);
    }
    return image;
}

// The game's "missing texture": magenta and black squares.
Image missingTile(int size) {
    Image image = blank(size, 0x000000FF);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (((x * 2 / size) + (y * 2 / size)) % 2 == 0) {
                unsigned char* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
                p[0] = 0xF8;
                p[1] = 0x00;
                p[2] = 0xF8;
            }
        }
    }
    return image;
}

// The top square of a picture that may be a strip of animation frames.
Image firstFrame(const Image& image) {
    if (image.width == image.height || image.width <= 0 || image.height < image.width) return image;
    Image frame;
    frame.width = image.width;
    frame.height = image.width;
    frame.rgba.assign(image.rgba.begin(), image.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.width) * 4));
    return frame;
}

Image resizeNearest(const Image& image, int size) {
    if (image.width == size && image.height == size) return image;
    Image out = blank(size, 0);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int sx = std::min(image.width - 1, x * image.width / size);
            const int sy = std::min(image.height - 1, y * image.height / size);
            std::copy_n(&image.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(sx)) * 4], 4,
                        &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4]);
        }
    }
    return out;
}

// Mip levels below a block picture (the game's default "Mipmap Levels" is 4: 16, 8, 4, 2, 1 pixels for a 16 px texture).
constexpr int kMipLevels = 4;

RenderType classify(const Image& image) {
    bool anyClear = false;
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        const unsigned char a = image.rgba[i];
        if (a == 255) continue;
        if (a != 0) return RenderType::Translucent;
        anyClear = true;
    }
    return anyClear ? RenderType::Cutout : RenderType::Opaque;
}

ResourceLocation locationOf(std::string name, const char* folder, const char* extension) {
    std::string ns = "minecraft";
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos) {
        ns = name.substr(0, colon);
        name.erase(0, colon + 1);
    }
    return ResourceLocation(ns, std::string(folder) + name + extension);
}

// The corner tables of the game's model baker: for every face, its four corners in counter-clockwise order seen from
// outside, each as (x, y, z) choosing the element's minimum (0) or maximum (1) on that axis.
struct FaceCorners {
    int sel[4][3];
};
constexpr FaceCorners kCorners[kFaceCount] = {
    /* east  */ {{{1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}}},
    /* west  */ {{{0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}}},
    /* up    */ {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
    /* down  */ {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}},
    /* south */ {{{0, 1, 1}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}}},
    /* north */ {{{1, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}}},
};

int faceFromName(const std::string& name) {
    if (name == "east") return kEast;
    if (name == "west") return kWest;
    if (name == "up") return kUp;
    if (name == "down") return kDown;
    if (name == "south") return kSouth;
    if (name == "north") return kNorth;
    return -1;
}

using Vec3 = std::array<double, 3>;

// Rotation of a point about an axis through `origin`, angle in degrees (positive: counter-clockwise looking down the axis).
Vec3 rotateAbout(Vec3 p, const Vec3& origin, int axis, double degrees, bool rescale) {
    const double a = degrees * kPi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    for (int i = 0; i < 3; ++i) p[static_cast<std::size_t>(i)] -= origin[static_cast<std::size_t>(i)];
    const int i1 = (axis + 1) % 3;  // the two other axes, in right-handed order
    const int i2 = (axis + 2) % 3;
    const double u = p[static_cast<std::size_t>(i1)];
    const double v = p[static_cast<std::size_t>(i2)];
    p[static_cast<std::size_t>(i1)] = u * c - v * s;
    p[static_cast<std::size_t>(i2)] = u * s + v * c;
    if (rescale) {
        const double k = 1.0 / std::max(0.1, std::abs(c));
        p[static_cast<std::size_t>(i1)] *= k;
        p[static_cast<std::size_t>(i2)] *= k;
    }
    for (int i = 0; i < 3; ++i) p[static_cast<std::size_t>(i)] += origin[static_cast<std::size_t>(i)];
    return p;
}

// A blockstate variant's rotation: x first, then y (the game's order). y turns clockwise seen from above (north -> east),
// x turns the north face towards the bottom.
Vec3 variantRotate(Vec3 p, int xDeg, int yDeg) {
    for (double& c : p) c -= 0.5;
    if (xDeg != 0) {
        const double a = xDeg * kPi / 180.0;
        const double c = std::cos(a);
        const double s = std::sin(a);
        const double y = p[1] * c + p[2] * s;
        const double z = -p[1] * s + p[2] * c;
        p[1] = y;
        p[2] = z;
    }
    if (yDeg != 0) {
        const double a = yDeg * kPi / 180.0;
        const double c = std::cos(a);
        const double s = std::sin(a);
        const double x = p[0] * c - p[2] * s;
        const double z = p[0] * s + p[2] * c;
        p[0] = x;
        p[2] = z;
    }
    for (double& c : p) c += 0.5;
    return p;
}

int nearestFace(const Vec3& n) {
    const double ax = std::abs(n[0]), ay = std::abs(n[1]), az = std::abs(n[2]);
    if (ay >= ax && ay >= az) return n[1] >= 0 ? kUp : kDown;
    if (ax >= az) return n[0] >= 0 ? kEast : kWest;
    return n[2] >= 0 ? kSouth : kNorth;
}

// The default picture region of a face when the model gives none: where the face sits in the block, in 1/16.
std::array<double, 4> defaultUv(int face, const Vec3& from, const Vec3& to) {
    switch (face) {
        case kDown: return {from[0], 16 - to[2], to[0], 16 - from[2]};
        case kUp: return {from[0], from[2], to[0], to[2]};
        case kNorth: return {16 - to[0], 16 - to[1], 16 - from[0], 16 - from[1]};
        case kSouth: return {from[0], 16 - to[1], to[0], 16 - from[1]};
        case kWest: return {from[2], 16 - to[1], to[2], 16 - from[1]};
        default: return {16 - to[2], 16 - to[1], 16 - from[2], 16 - from[1]};  // east
    }
}

// The picture coordinates a point of a face has when the picture is locked to the world (uvlock), in 1/16.
void lockedUv(int face, const Vec3& p, double& u, double& v) {
    const double x = p[0] * 16, y = p[1] * 16, z = p[2] * 16;
    switch (face) {
        case kDown: u = x; v = 16 - z; break;
        case kUp: u = x; v = z; break;
        case kNorth: u = 16 - x; v = 16 - y; break;
        case kSouth: u = x; v = 16 - y; break;
        case kWest: u = z; v = 16 - y; break;
        default: u = 16 - z; v = 16 - y; break;  // east
    }
}

bool nearly(double a, double b) { return std::abs(a - b) < 1e-4; }

}  // namespace

struct BlockModels::RawModel {
    std::map<std::string, std::string> textures;  // merged: the child's entries win over its parents'
    std::shared_ptr<json> elements;               // the nearest ancestor's "elements" (null: none)
    std::map<std::string, DisplayTransform> display;  // merged per context: the child's entry replaces its parent's
    bool generated = false;
    bool builtinEntity = false;
    std::string guiLight;  // "" until some model in the chain says
    bool ambientOcclusion = true;  // "ambientocclusion": inherited from the parent unless the model sets it (block/cross turns it off)
};

TintSource tintSourceOf(const std::string& n) {
    if (n == "grass_block" || n == "short_grass" || n == "tall_grass" || n == "fern" || n == "large_fern" || n == "sugar_cane" ||
        n == "potted_fern" || n == "bush" || n == "pink_petals_unused") {
        return TintSource::Grass;
    }
    if (n == "water" || n == "water_cauldron" || n == "bubble_column") return TintSource::Water;
    if (n == "short_dry_grass" || n == "tall_dry_grass" || n == "leaf_litter") return TintSource::DryFoliage;
    if (n == "birch_leaves" || n == "spruce_leaves" || n == "lily_pad") return TintSource::Constant;
    if (n == "vine" || n == "oak_leaves" || n == "jungle_leaves" || n == "acacia_leaves" || n == "dark_oak_leaves" || n == "mangrove_leaves") {
        return TintSource::Foliage;
    }
    if (n.size() > 7 && n.compare(n.size() - 7, 7, "_leaves") == 0) return TintSource::Foliage;
    return TintSource::None;
}

namespace {
std::uint32_t constantTint(const std::string& n) {
    if (n == "birch_leaves") return 0x80A755;
    if (n == "spruce_leaves") return 0x619961;
    if (n == "lily_pad") return 0x208030;
    return 0xFFFFFF;
}
std::uint32_t pixel(const Image& map, int x, int y) {
    x = std::clamp(x, 0, map.width - 1);
    y = std::clamp(y, 0, map.height - 1);
    const unsigned char* p = &map.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(x)) * 4];
    return (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
}
}  // namespace

BlockModels::BlockModels(const ResourceManager& resources, const BlockRegistry& registry)
    : resources_(resources), registry_(registry) {
    slots_ = std::make_unique<std::atomic<const StateRender*>[]>(registry.stateCount());
    for (std::size_t i = 0; i < registry.stateCount(); ++i) slots_[i].store(nullptr, std::memory_order_relaxed);
    // The size of every layer: the size of the stone picture (16 in the game, bigger in a high-resolution pack).
    if (const auto bytes = resources_.readBytes(locationOf("block/stone", "textures/", ".png"))) {
        if (const auto stone = decodePng(*bytes); stone && stone->width > 0) tile_ = stone->width;
    }
    // Layer 0 is the "missing" picture.
    layers_.push_back(missingTile(tile_));
    layerMips_.push_back(generateMipLevels(layers_[0], kMipLevels, MipmapStrategy::Mean));
    layerTypes_.push_back(RenderType::Opaque);
    layerOf_["missingno"] = 0;
    pendingFlag_.store(true);

    const auto loadMap = [&](const char* name) {
        const auto bytes = resources_.readBytes(locationOf(name, "textures/colormap/", ".png"));
        if (!bytes) return Image{};
        auto image = decodePng(*bytes);
        return image ? std::move(*image) : Image{};
    };
    grassMap_ = loadMap("grass");
    foliageMap_ = loadMap("foliage");
    dryFoliageMap_ = loadMap("dry_foliage");
}

BlockModels::~BlockModels() = default;

// ---------------------------------------------------------------------------------------------------------------
// Pictures

std::uint16_t BlockModels::layerFor(const std::string& texture) const {
    std::string name = stripNamespace(texture);
    std::lock_guard<std::mutex> lock(textureMutex_);
    if (const auto it = layerOf_.find(name); it != layerOf_.end()) return it->second;
    if (layers_.size() >= 4000) return 0;
    Image tile;
    Image strip;  // the whole picture, when it is a strip of animation frames
    if (const auto bytes = resources_.readBytes(locationOf(name, "textures/", ".png"))) {
        if (auto image = decodePng(*bytes); image && !image->empty()) {
            tile = resizeNearest(firstFrame(*image), tile_);
            if (image->height > image->width && image->width > 0) strip = std::move(*image);
        }
    }
    const bool missing = tile.empty();
    if (missing) {
        falcon::log::line("block texture %s not found\n", name.c_str());
        tile = missingTile(tile_);
    }
    const auto layer = static_cast<std::uint16_t>(layers_.size());
    const RenderType kind = missing ? RenderType::Opaque : classify(tile);
    // The smaller mip levels, made the way the game makes them: each texture's .png.mcmeta says how ("dark_cutout" for leaves,
    // "strict_cutout" for flowers, "mean" for glass; nothing: cutout if it has transparent texels, else mean). Besides averaging in
    // linear light this keeps the holes of leaves and plants the same size at every distance, and fills the colours of transparent
    // texels so no black mixes into the edges.
    MipmapStrategy strategy = MipmapStrategy::Auto;
    float alphaBias = 0.0f;
    json animation;  // the .mcmeta "animation" section, if there is one
    if (!missing) {
        if (const auto meta = resources_.readText(locationOf(name, "textures/", ".png.mcmeta"))) {
            const json doc = json::parse(*meta, nullptr, false, true);
            if (doc.is_object()) {
                if (const auto a = doc.find("animation"); a != doc.end() && a->is_object()) animation = *a;
                if (const auto t = doc.find("texture"); t != doc.end() && t->is_object()) {
                    if (const auto s = t->find("mipmap_strategy"); s != t->end() && s->is_string()) strategy = parseMipmapStrategy(s->get<std::string>());
                    if (const auto b = t->find("alpha_cutoff_bias"); b != t->end() && b->is_number()) alphaBias = b->get<float>();
                }
            }
        }
    }
    // A picture that is a strip of frames plays them (water, lava, fire ...): every frame is made ready with its mip levels, animate() swaps them in.
    if (!missing && !strip.empty() && strip.height >= strip.width * 2) {
        Animation anim;
        anim.layer = layer;
        const int frameCount = strip.height / strip.width;
        for (int f = 0; f < frameCount; ++f) {
            Image frame;
            frame.width = frame.height = strip.width;
            const std::size_t bytes = static_cast<std::size_t>(strip.width) * static_cast<std::size_t>(strip.width) * 4;
            frame.rgba.assign(strip.rgba.begin() + static_cast<std::ptrdiff_t>(bytes * static_cast<std::size_t>(f)), strip.rgba.begin() + static_cast<std::ptrdiff_t>(bytes * static_cast<std::size_t>(f + 1)));
            anim.frames.push_back(generateMipLevels(resizeNearest(frame, tile_), kMipLevels, strategy, alphaBias));
        }
        const int defaultTime = std::max(1, animation.is_object() ? animation.value("frametime", 1) : 1);
        if (animation.is_object() && animation.contains("frames") && animation["frames"].is_array()) {
            for (const json& entry : animation["frames"]) {
                const int index = entry.is_number_integer() ? entry.get<int>() : entry.is_object() ? entry.value("index", 0) : 0;
                if (index < 0 || index >= frameCount) continue;
                anim.order.push_back(index);
                anim.ticks.push_back(std::max(1, entry.is_object() ? entry.value("time", defaultTime) : defaultTime));
            }
        }
        if (anim.order.empty()) {
            for (int f = 0; f < frameCount; ++f) {
                anim.order.push_back(f);
                anim.ticks.push_back(defaultTime);
            }
        }
        for (const int t : anim.ticks) anim.total += t;
        animations_.push_back(std::move(anim));
    }
    std::vector<Image> mips = generateMipLevels(std::move(tile), kMipLevels, strategy, alphaBias);
    layerTypes_.push_back(kind);
    layers_.push_back(mips[0]);
    layerMips_.push_back(std::move(mips));
    layerOf_[name] = layer;
    pendingFlag_.store(true, std::memory_order_release);
    return layer;
}

RenderType BlockModels::layerType(std::uint16_t layer) const {
    std::lock_guard<std::mutex> lock(textureMutex_);
    return layer < layerTypes_.size() ? layerTypes_[layer] : RenderType::Opaque;
}

Image BlockModels::layerImage(std::uint16_t layer) const {
    std::lock_guard<std::mutex> lock(textureMutex_);
    return layer < layers_.size() ? layers_[layer] : missingTile(tile_);
}

std::size_t BlockModels::layerCount() const {
    std::lock_guard<std::mutex> lock(textureMutex_);
    return layers_.size();
}

void BlockModels::uploadPending() {
    std::lock_guard<std::mutex> lock(textureMutex_);
    if (!pendingFlag_.load(std::memory_order_acquire)) return;
    if (static_cast<int>(layers_.size()) > arrayCapacity_) {
        int capacity = std::max(256, arrayCapacity_);
        while (capacity < static_cast<int>(layers_.size())) capacity *= 2;
        array_ = TextureArray2D(tile_, capacity);
        arrayCapacity_ = capacity;
        uploaded_ = 0;
    }
    for (std::size_t i = uploaded_; i < layers_.size(); ++i) {
        array_.setLayer(static_cast<int>(i), layers_[i]);
        array_.setLayerMips(static_cast<int>(i), layerMips_[i]);  // (the game's mip levels, not the GPU's box filter)
    }
    uploaded_ = layers_.size();
    pendingFlag_.store(false, std::memory_order_release);
}

void BlockModels::animate(double ticks) {
    std::lock_guard<std::mutex> lock(textureMutex_);
    for (Animation& a : animations_) {
        if (static_cast<std::size_t>(a.layer) >= uploaded_ || a.total <= 0) continue;
        int t = static_cast<int>(std::fmod(ticks, static_cast<double>(a.total)));
        std::size_t shown = 0;
        for (; shown + 1 < a.order.size() && t >= a.ticks[shown]; ++shown) t -= a.ticks[shown];
        if (static_cast<int>(shown) == a.shown) continue;
        a.shown = static_cast<int>(shown);
        const std::vector<Image>& frame = a.frames[static_cast<std::size_t>(a.order[shown])];
        array_.setLayer(a.layer, frame[0]);
        array_.setLayerMips(a.layer, frame);
    }
}

void BlockModels::bindTextures(unsigned unit) const { array_.bind(unit); }

// ---------------------------------------------------------------------------------------------------------------
// Tints

std::uint32_t BlockModels::grassColor(double temperature, double downfall) const {
    if (grassMap_.empty()) return 0x91BD59;
    temperature = std::clamp(temperature, 0.0, 1.0);
    downfall = std::clamp(downfall, 0.0, 1.0) * temperature;
    return pixel(grassMap_, static_cast<int>((1.0 - temperature) * (grassMap_.width - 1)), static_cast<int>((1.0 - downfall) * (grassMap_.height - 1)));
}

std::uint32_t BlockModels::foliageColor(double temperature, double downfall) const {
    if (foliageMap_.empty()) return 0x77AB2F;
    temperature = std::clamp(temperature, 0.0, 1.0);
    downfall = std::clamp(downfall, 0.0, 1.0) * temperature;
    return pixel(foliageMap_, static_cast<int>((1.0 - temperature) * (foliageMap_.width - 1)), static_cast<int>((1.0 - downfall) * (foliageMap_.height - 1)));
}

std::uint32_t BlockModels::tintColor(const StateRender& state, int, int, int) const {
    switch (static_cast<TintSource>(state.tintSource)) {
        case TintSource::Grass: return grassColor(0.8, 0.4);
        case TintSource::Foliage: return foliageColor(0.8, 0.4);
        case TintSource::DryFoliage: return dryFoliageMap_.empty() ? 0xAE9F57 : pixel(dryFoliageMap_, dryFoliageMap_.width / 5, dryFoliageMap_.height / 3);
        case TintSource::Water: return 0x3F76E4;
        case TintSource::Constant: return state.tintConstant;
        case TintSource::None: break;
    }
    return 0xFFFFFF;
}

// ---------------------------------------------------------------------------------------------------------------
// Models

std::shared_ptr<const BlockModels::RawModel> BlockModels::loadModel(const std::string& name) const {
    if (const auto it = rawModels_.find(name); it != rawModels_.end()) return it->second;
    rawModels_[name] = nullptr;  // also stops a parent cycle
    const auto text = resources_.readText(locationOf(name, "models/", ".json"));
    if (!text) return nullptr;
    const json doc = json::parse(*text, nullptr, false, true);
    if (doc.is_discarded() || !doc.is_object()) return nullptr;
    auto raw = std::make_shared<RawModel>();
    if (const auto parent = doc.find("parent"); parent != doc.end() && parent->is_string()) {
        const std::string p = stripNamespace(parent->get<std::string>());
        if (p.rfind("builtin/", 0) != 0) {
            if (const auto base = loadModel(p)) {
                raw->textures = base->textures;
                raw->elements = base->elements;
                raw->display = base->display;
                raw->generated = base->generated;
                raw->builtinEntity = base->builtinEntity;
                raw->guiLight = base->guiLight;
                raw->ambientOcclusion = base->ambientOcclusion;
            }
        } else if (p == "builtin/generated") {
            raw->generated = true;
        } else if (p == "builtin/entity") {
            raw->builtinEntity = true;
        }
    }
    if (const auto light = doc.find("gui_light"); light != doc.end() && light->is_string()) raw->guiLight = light->get<std::string>();
    if (const auto ao = doc.find("ambientocclusion"); ao != doc.end() && ao->is_boolean()) raw->ambientOcclusion = ao->get<bool>();
    if (const auto display = doc.find("display"); display != doc.end() && display->is_object()) {
        for (auto it = display->begin(); it != display->end(); ++it) {
            if (!it.value().is_object()) continue;
            DisplayTransform t;
            t.present = true;
            const auto read = [&](const char* key, std::array<float, 3>& out) {
                const auto v = it.value().find(key);
                if (v == it.value().end() || !v->is_array() || v->size() != 3) return;
                for (std::size_t i = 0; i < 3; ++i) out[i] = (*v)[i].is_number() ? (*v)[i].get<float>() : out[i];
            };
            read("rotation", t.rotation);
            read("translation", t.translation);
            read("scale", t.scale);
            for (float& v : t.translation) v = std::clamp(v, -80.0f, 80.0f);  // ItemTransform clamps to +-5 blocks
            for (float& v : t.scale) v = std::min(v, 4.0f);
            raw->display[it.key()] = t;
        }
    }
    if (const auto textures = doc.find("textures"); textures != doc.end() && textures->is_object()) {
        for (auto it = textures->begin(); it != textures->end(); ++it) {
            const json& tv = it.value();
            if (tv.is_string()) {
                raw->textures[it.key()] = tv.get<std::string>();
            } else if (tv.is_object() && tv.contains("sprite") && tv["sprite"].is_string()) {
                raw->textures[it.key()] = (tv.value("force_translucent", false) ? "~" : "") + tv["sprite"].get<std::string>();  // "~": drawn see-through
            }
        }
    }
    if (const auto elements = doc.find("elements"); elements != doc.end() && elements->is_array()) {
        raw->elements = std::make_shared<json>(*elements);
    }
    rawModels_[name] = raw;
    return raw;
}

QuadList BlockModels::bakeModel(const std::string& rawName, int xRot, int yRot, bool uvlock) const {
    const std::string name = stripNamespace(rawName);
    const std::string key = name + "|" + std::to_string(xRot) + "|" + std::to_string(yRot) + (uvlock ? "|L" : "");
    if (const auto it = bakedModels_.find(key); it != bakedModels_.end()) return it->second;
    auto quads = std::make_shared<std::vector<ModelQuad>>();
    const auto model = loadModel(name);
    if (model && model->elements) {
        const auto resolve = [&](std::string ref) -> std::string {
            for (int guard = 0; guard < 16 && !ref.empty() && ref[0] == '#'; ++guard) {
                const auto it = model->textures.find(ref.substr(1));
                if (it == model->textures.end()) return "missingno";
                ref = it->second;
            }
            return ref.empty() || ref[0] == '#' ? "missingno" : ref;
        };
        const auto readVec = [](const json& j, Vec3& out) {
            if (!j.is_array() || j.size() != 3) return false;
            for (std::size_t i = 0; i < 3; ++i) out[i] = j[i].get<double>();
            return true;
        };
        for (const json& element : *model->elements) {
            Vec3 from{}, to{};
            if (!element.is_object() || !readVec(element.value("from", json()), from) || !readVec(element.value("to", json()), to)) continue;
            const bool shade = element.value("shade", true);
            const int shadeOverride = element.contains("shade_direction_override") && element["shade_direction_override"].is_string()
                                          ? faceFromName(element["shade_direction_override"].get<std::string>())
                                          : -1;
            bool rotated = false;
            Vec3 origin{8, 8, 8};
            int axis = 1;
            double angle = 0;
            bool rescale = false;
            if (const auto rot = element.find("rotation"); rot != element.end() && rot->is_object()) {
                readVec(rot->value("origin", json()), origin);
                const std::string a = rot->value("axis", std::string("y"));
                axis = a == "x" ? 0 : a == "z" ? 2 : 1;
                angle = rot->value("angle", 0.0);
                rescale = rot->value("rescale", false);
                rotated = angle != 0.0;
            }
            const auto faces = element.find("faces");
            if (faces == element.end() || !faces->is_object()) continue;
            for (auto fi = faces->begin(); fi != faces->end(); ++fi) {
                const int dir = faceFromName(fi.key());
                const json& f = fi.value();
                if (dir < 0 || !f.is_object()) continue;
                ModelQuad quad;
                std::string texture = resolve(f.value("texture", std::string("#missing")));
                const bool forceTranslucent = !texture.empty() && texture[0] == '~';
                if (forceTranslucent) texture.erase(0, 1);
                quad.layer = layerFor(texture);
                quad.type = forceTranslucent ? RenderType::Translucent : layerType(quad.layer);
                quad.tint = static_cast<std::int8_t>(f.value("tintindex", -1));
                quad.shade = shade;
                quad.ao = model->ambientOcclusion;
                if (shadeOverride >= 0) {
                    static constexpr Vec3 kNormals[kFaceCount] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    Vec3 n = variantRotate({kNormals[shadeOverride][0] + 0.5, kNormals[shadeOverride][1] + 0.5, kNormals[shadeOverride][2] + 0.5}, xRot, yRot);
                    for (double& v : n) v -= 0.5;
                    quad.shadeFace = static_cast<std::int8_t>(nearestFace(n));
                }
                // Where the picture is taken from (1/16 of the texture) and how it is turned.
                std::array<double, 4> uv = defaultUv(dir, from, to);
                if (const auto u = f.find("uv"); u != f.end() && u->is_array() && u->size() == 4) {
                    for (std::size_t i = 0; i < 4; ++i) uv[i] = (*u)[i].get<double>();
                }
                const int turn = ((f.value("rotation", 0) / 90) % 4 + 4) % 4;
                // The corner order of the game: vertex 0 takes (u0, v0), 1 (u0, v1), 2 (u1, v1), 3 (u1, v0); turning shifts it.
                const auto cornerU = [&](int i) { const int s = (i + turn) % 4; return s == 0 || s == 1 ? uv[0] : uv[2]; };
                const auto cornerV = [&](int i) { const int s = (i + turn) % 4; return s == 0 || s == 3 ? uv[1] : uv[3]; };
                for (int i = 0; i < 4; ++i) {
                    Vec3 p;
                    for (int a = 0; a < 3; ++a) {
                        const double lo = from[static_cast<std::size_t>(a)];
                        const double hi = to[static_cast<std::size_t>(a)];
                        p[static_cast<std::size_t>(a)] = (kCorners[dir].sel[i][a] ? hi : lo) / 16.0;
                    }
                    if (rotated) {
                        const Vec3 o{origin[0] / 16.0, origin[1] / 16.0, origin[2] / 16.0};
                        p = rotateAbout(p, o, axis, angle, rescale);
                    }
                    p = variantRotate(p, xRot, yRot);
                    for (int a = 0; a < 3; ++a) quad.pos[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)] = static_cast<float>(p[static_cast<std::size_t>(a)]);
                    quad.uv[static_cast<std::size_t>(i)] = {static_cast<float>(cornerU(i) / 16.0), static_cast<float>(cornerV(i) / 16.0)};
                }
                // Which way it faces now: from its corners.
                const auto sub = [](const std::array<float, 3>& a, const std::array<float, 3>& b) { return Vec3{double(a[0] - b[0]), double(a[1] - b[1]), double(a[2] - b[2])}; };
                const Vec3 e1 = sub(quad.pos[1], quad.pos[0]);
                const Vec3 e2 = sub(quad.pos[2], quad.pos[0]);
                const Vec3 normal{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                quad.face = static_cast<std::uint8_t>(nearestFace(normal));
                {
                    const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                    const double along = std::max({std::abs(normal[0]), std::abs(normal[1]), std::abs(normal[2])});
                    quad.axial = length > 1e-12 && along / length > 0.99999;
                }
                if (uvlock && (xRot != 0 || yRot != 0)) {
                    for (int i = 0; i < 4; ++i) {
                        const Vec3 p{quad.pos[static_cast<std::size_t>(i)][0], quad.pos[static_cast<std::size_t>(i)][1], quad.pos[static_cast<std::size_t>(i)][2]};
                        double u = 0, v = 0;
                        lockedUv(quad.face, p, u, v);
                        quad.uv[static_cast<std::size_t>(i)] = {static_cast<float>(u / 16.0), static_cast<float>(v / 16.0)};
                    }
                }
                if (const auto cull = f.find("cullface"); cull != f.end() && cull->is_string()) {
                    const int c = faceFromName(cull->get<std::string>());
                    if (c >= 0) {
                        static constexpr Vec3 kNormals[kFaceCount] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                        Vec3 n = variantRotate({kNormals[c][0] + 0.5, kNormals[c][1] + 0.5, kNormals[c][2] + 0.5}, xRot, yRot);
                        for (double& v : n) v -= 0.5;
                        quad.cullFace = static_cast<std::int8_t>(nearestFace(n));
                    }
                }
                quads->push_back(quad);
            }
        }
    }
    bakedModels_[key] = quads;
    return quads;
}

std::optional<ItemModelInfo> BlockModels::itemModelInfo(const std::string& rawName) const {
    const std::string name = stripNamespace(rawName);
    std::lock_guard<std::mutex> lock(bakeMutex_);
    const auto model = loadModel(name);
    if (!model) return std::nullopt;
    ItemModelInfo info;
    info.generated = model->generated;
    info.builtinEntity = model->builtinEntity;
    info.display = model->display;
    if (!model->guiLight.empty()) info.guiLight = model->guiLight;
    const auto resolve = [&](std::string ref) -> std::string {
        for (int guard = 0; guard < 16 && !ref.empty() && ref[0] == '#'; ++guard) {
            const auto it = model->textures.find(ref.substr(1));
            if (it == model->textures.end()) return {};
            ref = it->second;
        }
        return ref.empty() || ref[0] == '#' ? std::string() : ref;
    };
    if (model->generated) {
        for (int i = 0; i < 16; ++i) {
            const auto it = model->textures.find("layer" + std::to_string(i));
            if (it == model->textures.end()) break;
            std::string texture = resolve(it->second);
            if (texture.empty()) texture = "missingno";
            info.layers.push_back(stripNamespace(texture));
        }
    } else if (model->elements) {
        info.quads = bakeModel(name, 0, 0, false);
    }
    if (const auto p = model->textures.find("particle"); p != model->textures.end()) info.particle = stripNamespace(resolve(p->second));
    return info;
}

QuadList BlockModels::modelQuads(const std::string& model, int xRotation, int yRotation, bool uvlock) const {
    std::lock_guard<std::mutex> lock(bakeMutex_);
    return bakeModel(model, xRotation, yRotation, uvlock);
}

// ---------------------------------------------------------------------------------------------------------------
// Blockstates

const BlockModels::BlockStateSpec& BlockModels::blockState(const std::string& blockName) const {
    if (const auto it = blockStates_.find(blockName); it != blockStates_.end()) return it->second;
    BlockStateSpec& spec = blockStates_[blockName];
    const auto text = resources_.readText(locationOf(blockName, "blockstates/", ".json"));
    if (!text) return spec;
    const json doc = json::parse(*text, nullptr, false, true);
    if (doc.is_discarded() || !doc.is_object()) return spec;
    const auto readVariants = [](const json& j) {
        std::vector<VariantSpec> out;
        const auto one = [&](const json& v) {
            if (!v.is_object()) return;
            VariantSpec s;
            s.model = v.value("model", std::string());
            s.x = v.value("x", 0);
            s.y = v.value("y", 0);
            s.uvlock = v.value("uvlock", false);
            s.weight = std::max(1, v.value("weight", 1));
            if (!s.model.empty()) out.push_back(std::move(s));
        };
        if (j.is_array()) {
            for (const json& v : j) one(v);
        } else {
            one(j);
        }
        return out;
    };
    spec.found = true;
    if (const auto variants = doc.find("variants"); variants != doc.end() && variants->is_object()) {
        for (auto it = variants->begin(); it != variants->end(); ++it) spec.variants.emplace_back(it.key(), readVariants(it.value()));
    }
    if (const auto multipart = doc.find("multipart"); multipart != doc.end() && multipart->is_array()) {
        spec.multipart = true;
        for (const json& part : *multipart) {
            if (!part.is_object() || !part.contains("apply")) continue;
            PartSpec p;
            if (part.contains("when")) p.when = std::make_shared<json>(part["when"]);
            p.variants = readVariants(part["apply"]);
            spec.parts.push_back(std::move(p));
        }
    }
    return spec;
}

bool BlockModels::matchesKey(const std::string& key, Block block) const {
    if (key.empty()) return true;
    std::size_t pos = 0;
    while (pos <= key.size()) {
        const std::size_t comma = std::min(key.find(',', pos), key.size());
        const std::string pair = key.substr(pos, comma - pos);
        const std::size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            if (registry_.get(block, std::string_view(pair).substr(0, eq)) != std::string_view(pair).substr(eq + 1)) return false;
        }
        pos = comma + 1;
    }
    return true;
}

bool BlockModels::matchesWhen(const json& when, Block block) const {
    if (!when.is_object()) return true;
    if (const auto any = when.find("OR"); any != when.end() && any->is_array()) {
        return std::any_of(any->begin(), any->end(), [&](const json& c) { return matchesWhen(c, block); });
    }
    if (const auto all = when.find("AND"); all != when.end() && all->is_array()) {
        return std::all_of(all->begin(), all->end(), [&](const json& c) { return matchesWhen(c, block); });
    }
    for (auto it = when.begin(); it != when.end(); ++it) {
        const std::string wanted = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
        const std::string_view have = registry_.get(block, it.key());
        bool ok = false;
        std::size_t pos = 0;
        while (pos <= wanted.size()) {
            const std::size_t bar = std::min(wanted.find('|', pos), wanted.size());
            std::string_view option = std::string_view(wanted).substr(pos, bar - pos);
            const bool negate = !option.empty() && option[0] == '!';
            if (negate) option.remove_prefix(1);
            if ((have == option) != negate) ok = true;
            pos = bar + 1;
        }
        if (!ok) return false;
    }
    return true;
}

const StateRender* BlockModels::bake(Block block) const {
    auto out = std::make_unique<StateRender>();
    const StateInfo& info = registry_.state(block);
    const BlockType& type = registry_.type(block);
    out->tintSource = static_cast<std::uint8_t>(tintSourceOf(type.name));
    out->tintConstant = constantTint(type.name);
    if (type.name == "redstone_wire") {  // RedStoneWireBlock.COLORS: the dust is dark red when it is off and brighter the stronger the signal
        const float f = static_cast<float>(std::atoi(std::string(registry_.get(block, "power")).c_str())) / 15.0f;
        const auto channel = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        out->tintSource = static_cast<std::uint8_t>(TintSource::Constant);
        out->tintConstant = (channel(f * 0.6f + (f > 0.0f ? 0.4f : 0.3f)) << 16) | (channel(f * f * 0.7f - 0.5f) << 8) | channel(f * f * 0.6f - 0.7f);
    }
    const bool leaves = type.name.size() > 7 && type.name.compare(type.name.size() - 7, 7, "_leaves") == 0;
    // A face between two of the same see-through block is not drawn: glass and ice panes, and - the big one for forests - leaves,
    // so a solid blob of leaves is just its outer shell instead of every interior face (this is what the game's Fast graphics does).
    out->cullSame = (type.name.find("glass") != std::string::npos && type.name.find("pane") == std::string::npos) || type.name == "ice" ||
                    type.name == "blue_ice" || type.name == "frosted_ice" || type.name == "slime_block" || type.name == "honey_block" || leaves;
    out->leaves = leaves;

    if (info.has(kStateLiquid) && (type.name == "water" || type.name == "lava")) {  // (their render shape says "invisible")
        out->kind = RenderKind::Fluid;
        out->isLava = type.name == "lava";
        out->fluidStill = layerFor(out->isLava ? "block/lava_still" : "block/water_still");
        out->fluidFlow = layerFor(out->isLava ? "block/lava_flow" : "block/water_flow");
        out->fluidTint = out->isLava ? -1 : 0;
        out->type = out->isLava ? RenderType::Opaque : RenderType::Translucent;
        out->tintSource = static_cast<std::uint8_t>(out->isLava ? TintSource::None : TintSource::Water);
    } else if (!info.has(kStateHasModel) || info.has(kStateAir)) {
        out->kind = RenderKind::Nothing;
    } else {
        const BlockStateSpec& spec = blockState(type.name);
        std::vector<const std::vector<VariantSpec>*> chosen;
        if (spec.multipart) {
            for (const PartSpec& part : spec.parts) {
                if (!part.when || matchesWhen(*part.when, block)) chosen.push_back(&part.variants);
            }
        } else {
            for (const auto& [key, variants] : spec.variants) {
                if (matchesKey(key, block)) {
                    chosen.push_back(&variants);
                    break;
                }
            }
        }
        for (const auto* variants : chosen) {
            ModelPart part;
            for (const VariantSpec& v : *variants) {
                ModelVariant mv;
                mv.quads = bakeModel(v.model, v.x, v.y, v.uvlock);
                mv.weight = v.weight;
                part.totalWeight += v.weight;
                part.variants.push_back(std::move(mv));
            }
            if (!part.variants.empty()) out->parts.push_back(std::move(part));
        }
        out->kind = out->parts.empty() ? RenderKind::Nothing : RenderKind::Model;

        // A single plain cube is merged greedily instead of drawn quad by quad.
        if (out->kind == RenderKind::Model && out->parts.size() == 1 && out->parts[0].variants.size() == 1 &&
            out->parts[0].variants[0].quads->size() == 6) {
            std::array<bool, kFaceCount> seen{};
            bool cube = true;
            RenderType type2 = RenderType::Opaque;
            for (const ModelQuad& q : *out->parts[0].variants[0].quads) {
                const int face = q.face;
                if (seen[static_cast<std::size_t>(face)] || q.cullFace != face || !q.shade || q.shadeFace >= 0 || !q.ao) {
                    cube = false;
                    break;
                }
                seen[static_cast<std::size_t>(face)] = true;
                // All four corners on the face plane, covering it completely, picture unturned.
                static constexpr int kAxis[kFaceCount] = {0, 0, 1, 1, 2, 2};
                static constexpr float kPlane[kFaceCount] = {1, 0, 1, 0, 1, 0};
                for (int i = 0; i < 4 && cube; ++i) {
                    for (int a = 0; a < 3; ++a) {
                        const float c = q.pos[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)];
                        if (a == kAxis[face] ? !nearly(c, kPlane[face]) : !(nearly(c, 0) || nearly(c, 1))) cube = false;
                    }
                }
                static constexpr float kUv[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
                for (int i = 0; i < 4 && cube; ++i) {
                    if (!nearly(q.uv[static_cast<std::size_t>(i)][0], kUv[i][0]) || !nearly(q.uv[static_cast<std::size_t>(i)][1], kUv[i][1])) cube = false;
                }
                if (!cube) break;
                out->faceLayer[static_cast<std::size_t>(face)] = q.layer;
                out->faceTint[static_cast<std::size_t>(face)] = q.tint;
                if (static_cast<int>(q.type) > static_cast<int>(type2)) type2 = q.type;
            }
            if (cube) {
                out->kind = RenderKind::Cube;
                out->type = type2;
            }
        }
    }
    const StateRender* raw = out.get();
    owned_.push_back(std::move(out));
    baked_.fetch_add(1, std::memory_order_relaxed);
    return raw;
}

const StateRender& BlockModels::render(Block block) const {
    const std::size_t id = stateId(block);
    std::atomic<const StateRender*>& slot = slots_[id < registry_.stateCount() ? id : 0];
    const StateRender* ready = slot.load(std::memory_order_acquire);
    if (ready != nullptr) return *ready;
    std::lock_guard<std::mutex> lock(bakeMutex_);
    ready = slot.load(std::memory_order_relaxed);
    if (ready == nullptr) {
        ready = bake(id < registry_.stateCount() ? block : Block::Air);
        slot.store(ready, std::memory_order_release);
    }
    return *ready;
}

std::vector<ModelQuad> BlockModels::iconQuads(Block block) const {
    std::vector<ModelQuad> out;
    const StateRender& state = render(block);
    if (state.kind == RenderKind::Fluid) {
        ModelQuad q;
        q.layer = state.fluidStill;
        q.tint = state.fluidTint;
        q.type = state.type;
        static constexpr int kSel[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        for (int i = 0; i < 4; ++i) {
            q.pos[static_cast<std::size_t>(i)] = {static_cast<float>(kSel[i][0]), 1.0f, static_cast<float>(kSel[i][1])};
            q.uv[static_cast<std::size_t>(i)] = {static_cast<float>(kSel[i][0]), static_cast<float>(kSel[i][1])};
        }
        q.face = kUp;
        out.push_back(q);
        return out;
    }
    for (const ModelPart& part : state.parts) {
        if (!part.variants.empty() && part.variants[0].quads) out.insert(out.end(), part.variants[0].quads->begin(), part.variants[0].quads->end());
    }
    return out;
}

}  // namespace falcon

// ---------------------------------------------------------------------------------------------------------------
// Biome tints

#include "falcon/world/chunk.hpp"
#include "falcon/worldgen/biome.hpp"
#include "falcon/worldgen/legacy_noise.hpp"

namespace falcon {

void BlockModels::setBiomes(const worldgen::BiomeRegistry* biomes) {
    biomeColors_.clear();
    if (biomes == nullptr) return;
    biomeColors_.resize(biomes->size());
    for (std::size_t id = 0; id < biomes->size(); ++id) {
        const worldgen::BiomeInfo& b = biomes->info(static_cast<worldgen::BiomeId>(id));
        BiomeColor& c = biomeColors_[id];
        const double t = std::clamp(static_cast<double>(b.temperature), 0.0, 1.0);
        const double d = std::clamp(static_cast<double>(b.downfall), 0.0, 1.0);
        c.grass = b.grassColor ? *b.grassColor : grassColor(t, d);
        c.foliage = b.foliageColor ? *b.foliageColor : foliageColor(t, d);
        if (b.dryFoliageColor) {
            c.dry = *b.dryFoliageColor;
        } else if (!dryFoliageMap_.empty()) {
            const double dd = d * t;
            c.dry = pixel(dryFoliageMap_, static_cast<int>((1.0 - t) * 255.0), static_cast<int>((1.0 - dd) * 255.0));
        }
        c.water = b.waterColor ? *b.waterColor : 0x3F76E4u;
        c.grassModifier = static_cast<std::uint8_t>(b.grassColorModifier);
    }
}

bool BlockModels::fillTints(const std::shared_ptr<const BiomeGrid>* biomes, int originX, int originY, int originZ, TintGrid& out) const {
    if (biomeColors_.empty() || biomes == nullptr || !biomes[4]) return false;
    constexpr int kRadius = 2;  // the game's default biome blend: a 5 x 5 average
    const int y = originY + 8;
    const worldgen::BiomeTemperatureNoise& noise = worldgen::BiomeTemperatureNoise::instance();
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            unsigned grass[3] = {}, foliage[3] = {}, dry[3] = {}, water[3] = {};
            int count = 0;
            for (int dz = -kRadius; dz <= kRadius; ++dz) {
                for (int dx = -kRadius; dx <= kRadius; ++dx) {
                    const int bx = x + dx, bz = z + dz;
                    const std::shared_ptr<const BiomeGrid>& grid = biomes[(((bx >> 4) + 1) + 3 * ((bz >> 4) + 1))] ? biomes[(((bx >> 4) + 1) + 3 * ((bz >> 4) + 1))] : biomes[4];
                    const std::size_t id = grid->at(bx & 15, y, bz & 15);
                    const BiomeColor& c = id < biomeColors_.size() ? biomeColors_[id] : biomeColors_[0];
                    std::uint32_t g = c.grass;
                    if (c.grassModifier == static_cast<std::uint8_t>(worldgen::GrassColorModifier::DarkForest)) {
                        g = ((g & 0xFEFEFE) + 0x28340A) >> 1;
                    } else if (c.grassModifier == static_cast<std::uint8_t>(worldgen::GrassColorModifier::Swamp)) {
                        const double n = noise.biomeInfo((originX + bx) * 0.0225, (originZ + bz) * 0.0225);
                        g = n < -0.1 ? 0x4C3A0Au : 0x6A7039u;
                    }
                    for (int k = 0; k < 3; ++k) {
                        const int shift = 16 - 8 * k;
                        grass[k] += (g >> shift) & 0xFF;
                        foliage[k] += (c.foliage >> shift) & 0xFF;
                        dry[k] += (c.dry >> shift) & 0xFF;
                        water[k] += (c.water >> shift) & 0xFF;
                    }
                    ++count;
                }
            }
            const auto pack = [count](const unsigned (&s)[3]) {
                return ((s[0] / static_cast<unsigned>(count)) << 16) | ((s[1] / static_cast<unsigned>(count)) << 8) | (s[2] / static_cast<unsigned>(count));
            };
            const std::size_t i = static_cast<std::size_t>(z * 16 + x);
            out.grass[i] = pack(grass);
            out.foliage[i] = pack(foliage);
            out.dryFoliage[i] = pack(dry);
            out.water[i] = pack(water);
        }
    }
    return true;
}

}  // namespace falcon
