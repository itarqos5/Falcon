#pragma once

#include "falcon/assets/image.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/block_models.hpp"
#include "falcon/render/entity_model.hpp"
#include "falcon/world/item_registry.hpp"
#include "falcon/world/item_stack.hpp"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace falcon {

// One drawable part of an item's picture: a model file and the colours of its tint indices.
struct IconPart {
    std::string model;                 // "item/diamond_pickaxe", "block/stone"
    std::vector<std::uint32_t> tints;  // tint index -> 0xRRGGBB (an index past the end is not tinted)
    // A model the game draws with code instead of geometry ("banner", "chest", "shield", "head", ...): `model` is then its
    // base model, and the picture is a stand-in.
    std::string special;
    std::string specialDetail;         // the banner's colour, the head's kind
    std::string specialTexture;        // a chest's texture ("normal", "ender" ...)
};

struct ResolvedItemModel {
    std::vector<IconPart> parts;  // drawn in order (a composite has several)
    bool empty = false;           // the "empty" model, or one that shows nothing
    bool missing = false;         // there is no definition for the item at all
};

// What an items/<name>.json definition shows in a menu: the "model" itself; for a condition its off branch, for a select on the
// display context its "gui" case, for any other select or range_dispatch its fallback (else the first case); a composite is all
// its models. `colors` (may be null) gives the grass colour of "grass" tints.
// `context` is the display context being rendered for: "gui" for the menu/hotbar icon, "firstperson_righthand" /
// "firstperson_lefthand" for the held item (a spear, for instance, has a flat icon but a 3D in-hand model).
[[nodiscard]] ResolvedItemModel resolveItemModel(const nlohmann::json& definition, const BlockModels* colors, const std::string& context = "gui");

// The pictures of items that menus and the hotbar show. Flat items (the "generated" models) are their layer textures put on top
// of each other and tinted; block models are rasterised like the game's "gui" display transform says (30 degrees tilt, 225 turn,
// 0.625 scale for a block), on the CPU, from the same baked models the world uses; models the game draws with code (banners,
// chests, beds, skulls, shields...) get a stand-in picture. A model that cannot be found shows the missing texture.
// Pictures are made the first time they are asked for and kept in one texture atlas.
class ItemIcons {
public:
    static constexpr int kMaxSize = 64;  // the atlas cell stride; an icon renders at 16 * guiScale pixels, up to this
    static constexpr int kColumns = 32;
    static constexpr int kRows = 16;  // room for 512 icons; the oldest are reused after that

    explicit ItemIcons(BlockModels& models);
    ~ItemIcons();
    ItemIcons(const ItemIcons&) = delete;
    ItemIcons& operator=(const ItemIcons&) = delete;

    struct Slot {
        float u = 0.0f;  // top-left of the icon in the atlas texture, in pixels
        float v = 0.0f;
    };
    // Where the picture of an item is (made now if it is new). Needs a GL context (the atlas is made with the first picture).
    [[nodiscard]] Slot slot(const ItemStack& stack);
    [[nodiscard]] const Texture2D& texture() const { return atlas_; }
    // The GUI scale in use: icons are rendered at 16 * guiScale pixels so they draw at 1:1 (crisp, not resampled). When the
    // scale changes the icons are re-made at the new size. Call once a frame before drawing items.
    void setScale(int guiScale);
    // How many pixels of the atlas one icon fills now (the source size to blit from: see drawItem).
    [[nodiscard]] int iconPixels() const { return iconPixels_.load(std::memory_order_relaxed); }

    // Asks for the picture of an item to be made in the background, so that slot() finds it ready. Reading an item's files costs
    // some milliseconds, a whole creative tab some dozens of them, which would show as a hitch if it were all done in one frame.
    // Cheap to call every frame for items that are on screen or about to be.
    void prefetch(const ItemStack& stack);
    // For tests: waits until everything asked for with prefetch() is made, and how many pictures wait to be put in the atlas.
    void waitForPrefetch();
    [[nodiscard]] std::size_t readyCount();

    // The picture itself, `size` x `size`, transparent around the item (no GL needed).
    [[nodiscard]] Image render(const ItemStack& stack, int size);
    // The same for an item model by name (the items/<name>.json that an item's "item_model" component names): a name that has no
    // definition gives the missing texture on a cube, like the game.
    [[nodiscard]] Image renderModel(const std::string& itemModel, int size);
    [[nodiscard]] std::string kindOfModel(const std::string& itemModel);
    // What an item's definition resolves to (cached); for tests and reports.
    // What an item's definition resolves to for a display context (default "gui"; held items pass a first-person context so a
    // spear uses its 3D in-hand model instead of its flat icon). Cached per (item, context).
    [[nodiscard]] const ResolvedItemModel& resolved(const std::string& itemModel, const std::string& context = "gui");
    // How a picture was made, for the report: "flat", "block", "stand-in", "missing", "empty".
    [[nodiscard]] std::string kindOf(const ItemStack& stack);
    [[nodiscard]] std::size_t cachedCount() const { return cell_.size(); }

private:
    struct Rendered;
    [[nodiscard]] Rendered renderPart(const IconPart& part, int size);

    void workerLoop();

    BlockModels& models_;
    Texture2D atlas_;
    std::unordered_map<std::string, int> cell_;  // main thread only
    int next_ = 0;
    std::atomic<int> iconPixels_{48};  // read by the background worker too

    // Making a picture touches these: one picture at a time, from the main thread or the worker.
    std::mutex renderMutex_;
    std::unordered_map<std::string, ResolvedItemModel> resolved_;
    std::unordered_map<std::string, Image> textures_;  // item textures, first animation frame
    std::optional<EntityPart> chestModel_;  // the game's chest model ("minecraft:chest#main"), read the first time a chest icon is made
    bool chestModelTried_ = false;

    // The background work.
    std::mutex queueMutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<ItemStack> queue_;
    std::unordered_set<std::string> queued_;  // asked for and not yet in ready_
    std::unordered_map<std::string, Image> ready_;
    bool busy_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace falcon
