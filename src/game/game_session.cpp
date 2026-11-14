#include "falcon/game/game_session.hpp"

#include "falcon/game/block_placement.hpp"
#include "falcon/world/block_outlines.hpp"
#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/game/menu_background.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/world/recipes.hpp"
#include "falcon/world/water_fog.hpp"
#include "falcon/worldgen/underground.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <thread>

#include <GLFW/glfw3.h>

namespace falcon {
namespace {

constexpr double kReach = 5.0;
constexpr double kRepeatDelay = 0.25;  // holding break or place repeats this often
constexpr int kLoadRadius = 4;         // chunks around the spawn that must be ready before play starts
constexpr double kLoadTimeoutSeconds = 30.0;  // the game's client wait timeout (LevelLoadTracker.CLIENT_WAIT_TIMEOUT_MS)
constexpr double kTickSeconds = 0.05;  // the game runs at 20 ticks a second
constexpr int kMaxTicksPerFrame = 10;  // after a stall the game catches up by at most this many ticks and drops the rest

std::string format(const char* pattern, ...) {
    char buffer[512];
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(buffer, sizeof(buffer), pattern, args);
    va_end(args);
    return buffer;
}

// Chunks are made and meshed on this many threads: about half the logical cores (min 3, max 8), so loading and
// terrain fill-in are quick while the main and render threads - and anything else running - still keep cores.
unsigned workerCount() { return std::clamp(std::thread::hardware_concurrency() / 2, 3u, 8u); }

double megabytes(std::size_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

float percentile(std::vector<float> values, double p) {
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(p * static_cast<double>(values.size())))];
}

float average(const std::vector<float>& values) {
    return values.empty() ? 0.0f : std::accumulate(values.begin(), values.end(), 0.0f) / static_cast<float>(values.size());
}

const char* facing(float yaw) {
    float y = std::fmod(yaw, 360.0f);
    if (y < 0) y += 360.0f;
    static constexpr const char* kNames[4] = {"south (towards +Z)", "west (towards -X)", "north (towards -Z)", "east (towards +X)"};
    return kNames[static_cast<int>((y + 45.0f) / 90.0f) % 4];
}

// The block light (0..15) an item gives off while it is held: the light of the block it places (a torch 14, a lantern 15, glowstone
// 15, a redstone torch 7, ...), 0 for everything else. A block that only glows in some states (a furnace) is judged by its default.
int carriedLightLevel(const ItemStack& stack) {
    if (stack.isEmpty()) return 0;
    const ItemType& type = stack.type();
    if (!type.isBlockItem()) return 0;
    const BlockRegistry& registry = blockRegistry();
    const auto& blocks = registry.blocks();
    if (static_cast<std::size_t>(type.blockIndex) >= blocks.size()) return 0;
    return registry.state(static_cast<Block>(blocks[static_cast<std::size_t>(type.blockIndex)].defaultState)).emission;
}

// The block registry, read from the game's data on first use and then shared by every world this run opens.
std::shared_ptr<const BlockRegistry> loadRegistry(const ScreenServices& services, const ResourceManager& resources) {
    std::shared_ptr<const BlockRegistry> registry = currentBlockRegistry();
    if (!registry || !registry->isFromGameData()) {
        std::string error;
        // The data sits next to the assets: <game>/assets and <game>/data, registries.json, ...
        registry = BlockRegistry::loadFromGameData(resources.dataRoot(), services.gameDir / "falcon_cache", error);
        if (!registry) throw std::runtime_error(error);
        installBlockRegistry(registry);
    }
    // The collision shapes of the same blocks, installed beside the registry. Without them only full cubes collide.
    if (!hasCollisionShapesFromGameData()) {
        std::string error;
        auto shapes = CollisionShapes::loadFromGameData(resources.dataRoot(), services.gameDir / "falcon_cache", *registry, error);
        if (shapes) {
            installCollisionShapes(std::move(shapes));
        } else {
            std::printf("[falcon] collision shapes: %s (only full blocks will collide)\n", error.c_str());
        }
    }
    // The outline shapes: what the cursor can hit (a torch is a small post, not a block) and the lines drawn round it.
    if (!hasBlockOutlinesFromGameData()) {
        std::string error;
        auto outlines = BlockOutlines::loadFromGameData(resources.dataRoot(), services.gameDir / "falcon_cache", *registry, error);
        if (outlines) {
            installBlockOutlines(std::move(outlines));
        } else {
            std::printf("[falcon] block outlines: %s (every block will be aimed at as a full cube)\n", error.c_str());
        }
    }
    return registry;
}

// The item registry, the creative tabs and the recipes, read from the game's data on first use and shared by every world this run
// opens. The recipes (about 2000 small files the first time, one cache file afterwards) load on a background thread: until
// they are there the crafting grids simply show no result.
std::shared_ptr<const ItemRegistry> loadItems(const ScreenServices& services, const ResourceManager& resources, const BlockRegistry& blocks) {
    std::shared_ptr<const ItemRegistry> items = currentItemRegistry();
    if (!items || !items->isFromGameData()) {
        std::string error;
        const std::filesystem::path dataDir = resources.dataRoot();
        const std::filesystem::path cache = services.gameDir / "falcon_cache";
        items = ItemRegistry::loadFromGameData(dataDir, cache, &blocks, error);
        if (!items) throw std::runtime_error(error);
        installItemRegistry(items);
        std::string tabError;
        if (auto tabs = CreativeTabs::loadFromGameData(dataDir, *items, tabError)) {
            installCreativeTabs(std::move(tabs));
        } else {
            std::printf("[falcon] creative tabs: %s\n", tabError.c_str());
        }
        std::thread([items, dataDir, cache] {
            std::string recipeError;
            if (auto recipes = RecipeManager::loadFromGameData(dataDir, cache, *items, recipeError)) {
                installRecipeManager(std::move(recipes));
            } else {
                std::printf("[falcon] recipes: %s\n", recipeError.c_str());
            }
        }).detach();
    }
    return items;
}

// The game-exact world generator for this world's type (normal, amplified), or null for Superflat and when the game's
// data cannot be read (the simple built-in terrain is used then).
std::shared_ptr<const worldgen::OverworldGenerator> makeOverworld(const ResourceManager& resources, const BlockRegistry& registry, const LevelInfo& info) {
    const char* preset = worldgen::worldPresetFor(info.style);
    if (preset == nullptr) return nullptr;
    const std::filesystem::path dataDir = resources.dataRoot();
    worldgen::GeneratorOptions options;
    std::string error;
    if (!worldgen::loadWorldPreset(dataDir, preset, options, error)) {
        std::printf("[falcon] world preset %s unreadable (%s): using the simple terrain\n", preset, error.c_str());
        return nullptr;
    }
    std::unique_ptr<worldgen::OverworldGenerator> generator = worldgen::OverworldGenerator::create(dataDir, registry, info.seed, error, options);
    if (!generator) {
        std::printf("[falcon] the world generator could not start (%s): using the simple terrain\n", error.c_str());
        return nullptr;
    }
    // After the terrain and its surface: the game's caves and canyons, and the ores (see worldgen/underground.hpp).
    auto underground = std::make_shared<const worldgen::Underground>(registry, info.seed);
    const worldgen::OverworldGenerator* self = generator.get();
    generator->setChunkStage([underground, self](std::vector<Block>& blocks, int chunkX, int chunkZ) { underground->apply(blocks, chunkX, chunkZ, *self); });
    return generator;
}

}  // namespace

GameSession::GameSession(ScreenServices& services, const ResourceManager& resources, Window& window, const LevelInfo& info)
    : services_(services),
      window_(window),
      info_(info),
      registry_(loadRegistry(services, resources)),
      models_(resources, *registry_),
      itemIcons_(models_),
      hand_(resources, models_, itemIcons_),
      overworld_(makeOverworld(resources, *registry_, info)),
      renderer_(models_, info.seed, info.style, workerCount(), overworld_) {
    (void)loadItems(services, resources, *registry_);
    models_.setBiomes(overworld_ ? &overworld_->biomes() : nullptr);  // grass, leaves and water follow the biomes
    inventory_.infiniteMaterials = info.gameType == 1;
    MenuPlayer menuPlayer;
    menuPlayer.inventory = &inventory_;
    menuPlayer.infiniteMaterials = info.gameType == 1;
    menuPlayer.dropHook = [this](const ItemStack& thrown) { dropFromPlayer(thrown); };  // (Player.drop: a click outside the menu throws it)
    inventoryMenu_ = std::make_unique<InventoryMenu>(menuPlayer);
    waterFogs_ = WaterFogTable::load(resources.dataRoot());  // the biomes' water fog colours (data/.../worldgen/biome)
    {
        std::string supportError;
        support_ = BlockSupport::loadFromGameData(resources.dataRoot(), *registry_, supportError);
        blockTags_ = std::make_unique<BlockTags>(resources.dataRoot() / "data" / "minecraft" / "tags" / "block", *registry_);
        mining_ = std::make_unique<MiningRules>(*registry_, *blockTags_);
        loot_ = std::make_unique<BlockLoot>(resources.dataRoot(), *registry_);
        {  // the ten crack pictures drawn on a block being dug
            std::vector<Image> stages;
            for (int i = 0; i < 10; ++i) {
                const auto bytes = resources.readBytes(ResourceLocation("minecraft", format("textures/block/destroy_stage_%d.png", i)));
                const auto image = bytes ? decodePng(*bytes) : std::nullopt;
                if (!image) {
                    stages.clear();
                    break;
                }
                stages.push_back(*image);
            }
            renderer_.setBreakImages(stages);
        }
        if (!support_) std::printf("[falcon] plant rules: %s (plants will not pop off)\n", supportError.c_str());
        std::string previewError;
        playerPreview_ = PlayerPreview::create(resources, resources.dataRoot(), previewError);
        if (!playerPreview_) std::printf("[falcon] player model: %s (the inventory will not show the player)\n", previewError.c_str());
        std::string bodyError;
        body_ = PlayerBodyRenderer::create(resources, resources.dataRoot(), bodyError);
        if (!body_) std::printf("[falcon] player model: %s (F5 will not show the player)\n", bodyError.c_str());
    }
    const auto spawn = renderer_.findSpawn();
    std::printf("[falcon] spawn found: %.1f %.1f %.1f (%s)\n", spawn.x, spawn.y, spawn.z, overworld_ ? "game generator" : "simple terrain");
    spawn_ = {spawn.x, spawn.y, spawn.z};
    player_.setPosition(spawn.x, spawn.y, spawn.z);
    player_.canFly = info.gameType == 1 || info.gameType == 3;  // creative and spectator
    player_.spectator = info.gameType == 3;
    player_.flying = player_.spectator;
    // The world's own save: what the player changed comes back as the chunks are made, and the player stands where they left off.
    savePath_ = services.gameDir / "saves" / info.folder / "falcon_save.dat";
    renderer_.setEdits(edits_);
    if (info_.madeByFalcon) loadSave();
}

GameSession::~GameSession() { save(); }

// What loadSave() puts back: the player (where they stood, what they carried and how they were), and the items lying about. The block edits are read into
// `edits_` and applied by the renderer to every chunk it makes.
void GameSession::loadSave() {
    WorldSave saved;
    std::string error;
    if (!readWorldSave(savePath_, saved, *edits_, *registry_, error)) {
        if (!error.empty()) std::printf("[falcon] %s: starting from the spawn\n", error.c_str());
        return;
    }
    std::printf("[falcon] loaded the save: %zu edited blocks in %zu chunks, %zu items lying about\n", edits_->size(), edits_->chunkCount(), saved.items.size());
    if (saved.hasPlayer) {
        const SavedPlayer& p = saved.player;
        if (p.gameType >= 0 && p.gameType != info_.gameType) setGameType(p.gameType);
        player_.setPosition(p.x, p.y, p.z);
        player_.yaw = player_.yawO = p.yaw;
        player_.pitch = player_.pitchO = p.pitch;
        player_.flying = p.flying && player_.canFly;
        vitals_.health = std::clamp(p.health, 1.0f, vitals_.maxHealth);
        vitals_.food = p.food;
        vitals_.saturation = p.saturation;
        vitals_.level = p.level;
        vitals_.progress = p.progress;
        lastHealthShown_ = displayHealth_ = static_cast<int>(std::ceil(vitals_.health));
        for (const auto& [slot, stack] : p.inventory) {
            if (slot >= 0 && slot < Inventory::kSize) inventory_.item(slot) = stack;
        }
        inventory_.setSelected(p.selectedSlot);
    }
    for (const ItemEntity& e : saved.items) itemEntities_.items().push_back(e);
}

void GameSession::save() {
    static const bool forced = std::getenv("FALCON_SAVE") != nullptr;  // (test runs do not save unless asked to)
    if (!ready_ || !info_.madeByFalcon || (!forced && (!savingEnabled_ || pinnedPose_))) return;
    WorldSave out;
    out.hasPlayer = true;
    SavedPlayer& p = out.player;
    // A player who is dead when the world is left starts again at the spawn with full health, as after Respawn.
    p.x = dead_ ? spawn_[0] : player_.x;
    p.y = dead_ ? spawn_[1] : player_.y;
    p.z = dead_ ? spawn_[2] : player_.z;
    p.yaw = player_.yaw;
    p.pitch = player_.pitch;
    p.flying = player_.flying;
    p.health = dead_ ? vitals_.maxHealth : vitals_.health;
    p.food = dead_ ? 20 : vitals_.food;
    p.saturation = vitals_.saturation;
    p.level = vitals_.level;
    p.progress = vitals_.progress;
    p.selectedSlot = inventory_.selected;
    p.gameType = info_.gameType;
    for (int slot = 0; slot < Inventory::kSize; ++slot) {
        if (!inventory_.item(slot).isEmpty()) p.inventory.emplace_back(slot, inventory_.item(slot));
    }
    out.items = itemEntities_.items();
    const std::string error = writeWorldSave(savePath_, out, *edits_, *registry_);
    if (!error.empty()) {
        std::printf("[falcon] could not save the world: %s\n", error.c_str());
        return;
    }
    lastSaveTicks_ = playerAgeTicks_;
    std::printf("[falcon] saved the world: %zu edited blocks in %zu chunks\n", edits_->size(), edits_->chunkCount());
}

int GameSession::renderDistance() const { return std::clamp(services_.options.renderDistance, kMinRenderDistance, kMaxRenderDistance); }

bool GameSession::isDown(const char* action) const {
    const std::size_t index = keyMappingIndex(action);
    if (index >= kKeyMappingCount) return false;
    const KeyId key = services_.options.keyBindings.key(index);
    if (key.isUnbound()) return false;
    return key.type == KeyId::Type::Keyboard ? window_.keyDown(key.code) : window_.mouseDown(key.code);
}

Camera GameSession::camera() const {
    Camera c;
    // Between two ticks the camera is between the player's last two positions, and its eye height eases when crouching.
    const auto eye = player_.cameraPosition(partialTick_);
    c.x = eye[0];
    c.y = eye[1];
    c.z = eye[2];
    c.look = {player_.yaw, player_.pitch};  // the mouse moves the view at once, between ticks too
    c.fov = static_cast<float>(services_.options.fov);
    if (services_.options.bobView && !pinnedPose_) {
        const ViewBob bob = player_.viewBob(partialTick_);
        c.bobX = bob.x;
        c.bobY = bob.y;
        c.bobRollDegrees = bob.rollDegrees;
        c.bobPitchDegrees = bob.pitchDegrees;
    }
    // GameRenderer.bobHurt: a hit tips the view to one side and back over half a second.
    if (hurtTime_ > 0) {
        const float left = std::max(0.0f, static_cast<float>(hurtTime_) - static_cast<float>(partialTick_)) / 10.0f;
        const float tilt = std::sin(left * left * left * left * 3.1415927f) * 14.0f;
        c.bobRollDegrees += hurtDirection_ == 0.0f ? -tilt : tilt;
    }
    // F5 (Camera.setup): from behind or in front, four blocks away along the view, brought closer where a block would be in the way (the nearest
    // hit of eight rays from the corners of a small box round the eye); in front the camera looks back at the player.
    if (perspective_ != 0 && ready_) {
        const bool front = perspective_ == 2;
        const Look look{front ? player_.yaw + 180.0f : player_.yaw, front ? -player_.pitch : player_.pitch};
        const auto forward = look.forward();
        double distance = 4.0;
        for (int i = 0; i < 8; ++i) {
            const std::array<double, 3> from = {c.x + ((i & 1) * 2 - 1) * 0.1, c.y + (((i >> 1) & 1) * 2 - 1) * 0.1, c.z + (((i >> 2) & 1) * 2 - 1) * 0.1};
            if (const auto hit = raycastBlocks(renderer_.level(), from, {-forward[0], -forward[1], -forward[2]}, distance)) distance = std::min(distance, hit->distance);
        }
        c.x -= forward[0] * distance;
        c.y -= forward[1] * distance;
        c.z -= forward[2] * distance;
        c.look = look;
    }
    return c;
}

void GameSession::teleport(double x, double y, double z, float yaw, float pitch) {
    player_.setPosition(x, y, z);
    player_.yaw = player_.yawO = yaw;
    player_.pitch = player_.pitchO = pitch;
    player_.canFly = true;
    player_.flying = true;
    pinnedPose_ = true;
}

void GameSession::startBenchmark(double seconds) {
    benchmark_ = {};
    benchmark_.running = true;
    benchmark_.seconds = seconds;
    pinnedPose_ = true;
    player_.canFly = true;
    player_.flying = true;
}

void GameSession::applyLook(bool active) {
    if (!active) return;
    double dx = 0.0;
    double dy = 0.0;
    window_.takeCursorDelta(dx, dy);
    if (pinnedPose_) return;
    // The game's sensitivity curve (MouseHandler.handleAccumulatedMovement): f = s * 0.6 + 0.2, the turn is f^3 * 8 times the
    // mouse movement, and Player::turn applies the 0.15 degrees: 0.5 ("Sensitivity: 100%") is 0.15 degrees per pixel.
    const double f = services_.options.mouseSensitivity * 0.6000000238418579 + 0.20000000298023224;
    const double f3x8 = f * f * f * 8.0;
    player_.turn(dx * f3x8, dy * f3x8 * (services_.options.invertYMouse ? -1.0 : 1.0));
}

PlayerInput GameSession::sampleInput() const {
    PlayerInput input;
    input.forward = isDown("key.forward");
    input.backward = isDown("key.back");
    input.left = isDown("key.left");
    input.right = isDown("key.right");
    input.jump = isDown("key.jump");
    input.shift = services_.options.toggleCrouch ? sneakLatched_ : isDown("key.sneak");
    input.sprint = services_.options.toggleSprint ? sprintLatched_ : isDown("key.sprint");
    return input;
}

bool GameSession::setBlock(int x, int y, int z, Block block) {
    if (!renderer_.setBlock(x, y, z, block)) return false;
    fluids_.blockChanged(renderer_.level(), x, y, z);  // water next to it looks round again (flows into a hole, or loses its source)
    if (support_) {
        // The game's neighbour updates (updateShape): every block next to a change checks that it can still stand, and a plant that
        // cannot (its soil gone, the other half of a tall plant gone) disappears, which tells ITS neighbours in turn.
        const BlockSupport::Reader at = [this](int bx, int by, int bz) { return renderer_.level().block(bx, by, bz); };
        std::vector<std::array<int, 3>> pending{{x, y, z}};
        for (int guard = 0; !pending.empty() && guard < 256; ++guard) {
            const std::array<int, 3> c = pending.back();
            pending.pop_back();
            static constexpr int kDir[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            for (const auto& d : kDir) {
                const int nx = c[0] + d[0], ny = c[1] + d[1], nz = c[2] + d[2];
                if (ny < kMinY || ny >= kMaxY) continue;
                const Block neighbour = at(nx, ny, nz);
                const Block now = support_->afterNeighborChange(neighbour, nx, ny, nz, at);
                if (now != neighbour && renderer_.setBlock(nx, ny, nz, now)) {
                    fluids_.blockChanged(renderer_.level(), nx, ny, nz);
                    pending.push_back({nx, ny, nz});
                }
            }
        }
    }
    return true;
}

void GameSession::tickPlayer(double seconds, bool steering) {
    tickAccumulator_ += std::max(seconds, 0.0);
    const int due = static_cast<int>(tickAccumulator_ / kTickSeconds);
    tickAccumulator_ -= due * kTickSeconds;
    partialTick_ = std::clamp(tickAccumulator_ / kTickSeconds, 0.0, 1.0);
    if (due == 0) return;
    // A menu that does not stop the game (the inventory) takes the keys: no walking, no jumping, but the world goes on.
    const PlayerInput input = steering ? sampleInput() : PlayerInput{};
    if (steering && services_.options.toggleSprint && !input.forward) sprintLatched_ = false;  // stopping ends a toggled sprint
    for (int i = 0; i < std::min(due, kMaxTicksPerFrame); ++i) {
        playerAgeTicks_ += 1.0;
        if (!pinnedPose_) {  // (a pinned camera, for screenshots, freezes the player; the world goes on)
            const double vx = player_.vx, vy = player_.vy, vz = player_.vz;  // (what it was going at, for a splash)
            player_.tick(input, renderer_.level());
            waterVisionTicks_ = nextWaterVisionTicks(waterVisionTicks_, player_.isUnderWater(), info_.gameType == 3);  // the eyes get used to the water
            if (ready_ && !dead_) {
                footsteps(input.shift);
                if (const int damage = player_.takeFallDamage(); damage > 0 && survivalBars()) fallDamage(damage);
                if (player_.inWater && !wasInWater_) splash(vx, vy, vz);
                wasInWater_ = player_.inWater;
                tickBody();
                pickUpItems();
            }
        }
        tickHealth();
        if (ready_) itemEntities_.tick(renderer_.level());
        // The water that flows: its updates that are due this tick.
        fluids_.tick(renderer_.level(), [this](int bx, int by, int bz, Block b) { return renderer_.setBlock(bx, by, bz, b); });
    }
    // Out of the world (below the void line) the void hurts a survival player (4 a tick, through any protection) until they die; anyone else
    // starts again at the spawn.
    if (!pinnedPose_ && player_.y < static_cast<double>(kMinY) - 64.0) {
        if (survivalBars()) {
            hurt(4.0f, true);
        } else {
            player_.setPosition(spawn_[0], spawn_[1], spawn_[2]);
            player_.vx = player_.vy = player_.vz = 0.0;
            player_.fallDistance = 0.0;
            player_.lastFallDamage = 0;
        }
    }
}

void GameSession::tickHud(double seconds) {
    hudTickAccumulator_ += std::max(seconds, 0.0);
    const int due = static_cast<int>(hudTickAccumulator_ / kTickSeconds);
    hudTickAccumulator_ -= due * kTickSeconds;
    for (int i = 0; i < std::min(due, kMaxTicksPerFrame); ++i) {
        highlight_.tick(inventory_.selectedItem(), services_.options.notificationDisplayTime);
        hand_.tick(inventory_.selectedItem(), player_.yaw, player_.pitch);
        chat_.tick();  // ages chat messages for the fade
    }
}

// A block broken in survival drops its item into the world (Block.popResource): a little above the middle of the block, thrown up a little.
// What drops is the block's loot table (data/minecraft/loot_table/blocks), and only when the tool in the hand is the right one for the block
// (a block of stone dug by hand gives nothing; with a pickaxe it gives cobblestone).
void GameSession::collectDrop(Block broken, int x, int y, int z, const ItemStack& tool, bool correctTool) {
    if (!correctTool) return;
    std::vector<ItemStack> drops;
    if (loot_) {
        drops = loot_->drops(broken, tool, rng_);
    } else if (const Item own = itemRegistry().itemOfBlock(registry_->state(broken).block); own != Item::Air) {
        drops.emplace_back(own, 1);
    }
    const auto jitter = [this] { return static_cast<double>(randomFloat()) * 0.5 - 0.25; };
    for (const ItemStack& drop : drops) {
        itemEntities_.spawn(drop, x + 0.5 + jitter(), y + 0.5 + jitter() - 0.125, z + 0.5 + jitter(), static_cast<double>(randomFloat()) * 0.2 - 0.1, 0.2,
                            static_cast<double>(randomFloat()) * 0.2 - 0.1, 10, randomFloat() * 6.2831855f);
    }
}

void GameSession::damageHeldTool(int amount) {
    ItemStack& tool = inventory_.selectedItem();
    if (tool.isEmpty() || !tool.isDamageable() || amount <= 0) return;
    tool.damage += amount;
    if (tool.damage >= tool.maxDamage()) {  // worn out: it breaks, with its sound
        tool = ItemStack();
        playWorldSound("entity.item.break", SoundSource::Players, 0.8f, 0.8f + 0.4f * randomFloat());
    }
}

void GameSession::breakBlockAt(int x, int y, int z, bool survival) {
    const Block broken = renderer_.level().block(x, y, z);
    setBlock(x, y, z, Block::Air);
    hand_.swing();
    if (registry_->isAir(broken)) return;
    {  // the break sound of the block, heard where it was
        const std::array<double, 3> where = {x + 0.5, y + 0.5, z + 0.5};
        const BlockType& type = registry_->type(broken);
        playBlockSound(broken, "break", breakOrPlaceMix(type.soundVolume, type.soundPitch), SoundSource::Blocks, &where);
    }
    if (!survival) return;
    const ItemStack tool = inventory_.selectedItem();
    collectDrop(broken, x, y, z, tool, !mining_ || mining_->correctForDrops(tool, broken));
    // Item.mineBlock: a tool wears by one use (more for some) from a block that was not instant to dig.
    if (!tool.isEmpty() && tool.type().isTool && registry_->state(broken).hardness != 0.0f) damageHeldTool(tool.type().toolDamagePerBlock);
}

void GameSession::stopDigging() {
    digPos_.reset();
    digProgress_ = 0.0f;
    digTicks_ = 0;
}

// Survival digging, as MultiPlayerGameMode does it. The click starts it (a block that is instant to dig goes at once); each game tick after that takes
// another share off the block until it is gone, then comes a pause of 5 ticks. Aiming at another block, or another item in the hand, starts over.
void GameSession::digSurvival(double dt, bool fresh) {
    ItemStack& tool = inventory_.selectedItem();
    if (!target_ || (digPos_ && (*digPos_ != std::array<int, 3>{target_->x, target_->y, target_->z} || digItem_ != tool.item))) stopDigging();
    if (!target_) return;
    const std::array<int, 3> pos = {target_->x, target_->y, target_->z};
    const auto perTick = [&](Block b) { return mining_ ? mining_->progressPerTick(tool, b, player_.onGround, player_.isUnderWater()) : 1.0f; };
    if (fresh) digClock_ = 0.0;
    digClock_ += dt;
    int ticks = static_cast<int>(digClock_ / kTickSeconds);
    digClock_ -= ticks * kTickSeconds;
    for (; ticks > 0 && digDelay_ > 0; --ticks) --digDelay_;  // (the pause only runs while the button is held)
    if (!digPos_) {
        const Block block = renderer_.level().block(pos[0], pos[1], pos[2]);
        if (registry_->isAir(block) || pos[1] <= kMinY) return;
        if (digDelay_ > 0 && !fresh) return;
        if (perTick(block) >= 1.0f) {
            breakBlockAt(pos[0], pos[1], pos[2], true);
            return;
        }
        digPos_ = pos;
        digProgress_ = 0.0f;
        digTicks_ = 0;
        digItem_ = tool.item;
        hand_.swing();
        return;
    }
    for (; ticks > 0 && digPos_; --ticks) {
        const Block block = renderer_.level().block(pos[0], pos[1], pos[2]);
        if (registry_->isAir(block)) {
            stopDigging();
            break;
        }
        digProgress_ += perTick(block);
        if (digTicks_ % 4 == 0) {  // the hit sound, every fourth tick: quieter and lower than the break
            const std::array<double, 3> where = {pos[0] + 0.5, pos[1] + 0.5, pos[2] + 0.5};
            const BlockType& type = registry_->type(block);
            playBlockSound(block, "hit", {(type.soundVolume + 1.0f) / 8.0f, type.soundPitch * 0.5f}, SoundSource::Blocks, &where);
        }
        ++digTicks_;
        hand_.swing();
        if (digProgress_ >= 1.0f) {
            breakBlockAt(pos[0], pos[1], pos[2], true);
            stopDigging();
            digDelay_ = 5;
        }
    }
}

// Player.drop. Thrown from the hand: out of the face, in the direction of the look, with a little spread. Scattered (what is lost at death): up and
// out in any direction.
void GameSession::dropFromPlayer(const ItemStack& stack, bool scatter) {
    if (stack.isEmpty()) return;
    ++droppedCount_;
    double vx, vy, vz;
    if (scatter) {
        const float f = randomFloat() * 0.5f, g = randomFloat() * 6.2831855f;
        vx = -std::sin(g) * f;
        vy = 0.2;
        vz = std::cos(g) * f;
    } else {
        const float degrees = 0.017453292f;
        const float sp = std::sin(player_.pitch * degrees), cp = std::cos(player_.pitch * degrees);
        const float sy = std::sin(player_.yaw * degrees), cy = std::cos(player_.yaw * degrees);
        const float k = randomFloat() * 6.2831855f, l = 0.02f * randomFloat();
        vx = static_cast<double>(-sy * cp * 0.3f) + std::cos(k) * l;
        vy = static_cast<double>(-sp * 0.3f + 0.1f + (randomFloat() - randomFloat()) * 0.1f);
        vz = static_cast<double>(cy * cp * 0.3f) + std::sin(k) * l;
    }
    itemEntities_.spawn(stack, player_.x, player_.eyeY() - 0.3, player_.z, vx, vy, vz, 40, randomFloat() * 6.2831855f);
}

// ---------------------------------------------------------------------------------------------------------------
// Sounds of the world

void GameSession::playWorldSound(const std::string& event, SoundSource source, float volume, float pitch, const std::array<double, 3>* at) {
    if (at != nullptr) {  // fades linearly with the distance from the head: gone at 16 blocks (more for a louder sound)
        const auto head = player_.eye();
        const double dx = (*at)[0] - head[0], dy = (*at)[1] - head[1], dz = (*at)[2] - head[2];
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double range = 16.0 * std::max(1.0f, volume);
        if (distance >= range) return;
        volume *= static_cast<float>(1.0 - std::max(0.0, distance - 1.0) / (range - 1.0));
    }
    pitch = std::clamp(pitch, 0.5f, 2.0f);
    static const bool log = std::getenv("FALCON_SOUND_LOG") != nullptr;  // (for tests, which run muted: what would have been heard)
    if (log) std::printf("[falcon] sound %s volume %.2f pitch %.2f\n", event.c_str(), static_cast<double>(volume), static_cast<double>(pitch));
    services_.sounds.play(event, source, volume, pitch);
}

void GameSession::playBlockSound(Block block, const char* kind, SoundMix mix, SoundSource source, const std::array<double, 3>* at) {
    const std::string& set = registry_->type(block).sound;
    if (set.empty()) return;
    playWorldSound(blockSoundEvent(set, kind), source, mix.volume, mix.pitch, at);
}

// Entity.applyMovementEmissionAndPlaySound for the player. Nothing sounds while flying or while sneaking on the ground (Player.getMovementEmission);
// otherwise the walked distance runs the clock, and a step is due when it passes the next mark: on a block (not air), with the feet on the ground
// (or edging along while sneaking), not swimming. In water the step is a swimming sound and a muffled step.
void GameSession::footsteps(bool sneakKey) {
    if (player_.flying || player_.spectator || (player_.onGround && sneakKey)) return;
    const double dx = player_.x - player_.xo, dz = player_.z - player_.zo;
    stepClock_.add(std::hypot(dx, dz));
    if (!stepClock_.due()) return;
    const int bx = static_cast<int>(std::floor(player_.x)), by = static_cast<int>(std::floor(player_.y - 0.2)), bz = static_cast<int>(std::floor(player_.z));
    const Block under = renderer_.level().block(bx, by, bz);
    if (registry_->isAir(under)) return;  // (the clock keeps running: the first step on the ground that is met sounds at once)
    const BlockType& type = registry_->type(under);
    const bool onFoot = (player_.onGround || (player_.sneaking && player_.y == player_.yo)) && !player_.isSwimming();
    if (onFoot) {
        stepClock_.played();
        if (player_.inWater) {  // Player.playStepSound: waterSwimSound, then the muffled step
            const float volume = std::min(1.0f, std::sqrt(static_cast<float>(player_.vx * player_.vx * 0.2 + player_.vy * player_.vy + player_.vz * player_.vz * 0.2)) * 0.4f);
            playWorldSound("entity.player.swim", SoundSource::Players, volume, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
            const SoundMix mix = mutedStepMix(type.soundVolume, type.soundPitch);
            playBlockSound(under, "step", mix, SoundSource::Players);
        } else {
            // A thin block on top (snow, a moss carpet) is what the foot lands on, so it sounds instead of the one below.
            Block sounding = under;
            const Block above = renderer_.level().block(bx, by + 1, bz);
            if (!registry_->isAir(above)) {
                const std::string& n = registry_->type(above).name;
                if (n == "snow" || n == "moss_carpet" || n == "pale_moss_carpet") sounding = above;
            }
            const BlockType& stepType = registry_->type(sounding);
            playBlockSound(sounding, "step", stepMix(stepType.soundVolume, stepType.soundPitch), SoundSource::Players);
        }
    } else if (player_.inWater) {
        stepClock_.played();
        const float volume = std::min(1.0f, std::sqrt(static_cast<float>(player_.vx * player_.vx * 0.2 + player_.vy * player_.vy + player_.vz * player_.vz * 0.2)) * 0.4f);
        playWorldSound("entity.player.swim", SoundSource::Players, volume, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
    }
}

// Entity.doWaterSplashEffect: the harder it came down, the louder the splash (and the bigger one from 1.25 blocks a tick).
void GameSession::splash(double vx, double vy, double vz) {
    const float volume = std::min(1.0f, std::sqrt(static_cast<float>(vx * vx * 0.2 + vy * vy + vz * vz * 0.2)) * 0.2f);
    playWorldSound(volume < 0.25f ? "entity.player.splash" : "entity.player.splash.high_speed", SoundSource::Players, volume, 1.0f + (randomFloat() - randomFloat()) * 0.4f);
}

// LivingEntity.causeFallDamage: the cry for the size of the fall, the block's fall sound, then the hurt itself.
void GameSession::fallDamage(int damage) {
    playWorldSound(fallDamageEvent(damage), SoundSource::Players, 1.0f, 1.0f);
    const Block under = renderer_.level().block(static_cast<int>(std::floor(player_.x)), static_cast<int>(std::floor(player_.y - 0.2)), static_cast<int>(std::floor(player_.z)));
    if (!registry_->isAir(under)) {
        const BlockType& type = registry_->type(under);
        playBlockSound(under, "fall", fallMix(type.soundVolume, type.soundPitch), SoundSource::Players);
    }
    hurt(static_cast<float>(damage));
}

// ---------------------------------------------------------------------------------------------------------------
// Health

bool GameSession::hurt(float amount, bool bypassCooldown) {
    if (!survivalBars() || dead_ || amount <= 0.0f) return false;  // creative and spectator players cannot be hurt
    float applied = amount;
    if (!bypassCooldown && invulnerableTime_ > 10) {  // LivingEntity.hurtServer: in the half second after a hit only a stronger one counts, for the difference
        if (amount <= lastHurt_) return false;
        applied = amount - lastHurt_;
        lastHurt_ = amount;
    } else {
        lastHurt_ = amount;
        invulnerableTime_ = 20;
        hurtTime_ = 10;
        hurtDirection_ = randomFloat() < 0.5f ? 0.0f : 180.0f;  // (a hit with no attacker tilts the view to a random side)
        if (vitals_.health - applied > 0.0f) playWorldSound("entity.player.hurt", SoundSource::Players, 1.0f, 1.0f + (randomFloat() - randomFloat()) * 0.2f);
    }
    vitals_.health = std::max(0.0f, vitals_.health - applied);
    if (vitals_.health <= 0.0f) die();
    return true;
}

void GameSession::die() {
    dead_ = true;
    deathRequested_ = true;
    playWorldSound("entity.player.death", SoundSource::Players, 1.0f, 1.0f + (randomFloat() - randomFloat()) * 0.2f);
    // Inventory.dropAll: everything the player carried is scattered where they died.
    for (int slot = 0; slot < Inventory::kSize; ++slot) {
        ItemStack& stack = inventory_.item(slot);
        if (stack.isEmpty()) continue;
        dropFromPlayer(stack, true);
        stack = ItemStack();
    }
}

void GameSession::respawn() {
    dead_ = false;
    vitals_.health = vitals_.maxHealth;
    vitals_.food = 20;
    vitals_.saturation = 5.0f;
    invulnerableTime_ = hurtTime_ = regenTimer_ = 0;
    lastHurt_ = 0.0f;
    lastHealthShown_ = displayHealth_ = static_cast<int>(vitals_.maxHealth);
    player_.setPosition(spawn_[0], spawn_[1], spawn_[2]);
    player_.vx = player_.vy = player_.vz = 0.0;
    player_.fallDistance = 0.0;
    player_.lastFallDamage = 0;
    stepClock_ = StepClock();
}

// The part of a game tick that is about health: the protection and the tilt run down, and a player who is fed (18 or more of food) heals half a
// heart every four seconds (FoodData.tick's slow healing; the fast one needs food to burn, and none is used up yet).
void GameSession::tickHealth() {
    if (invulnerableTime_ > 0) --invulnerableTime_;
    if (hurtTime_ > 0) --hurtTime_;
    if (!survivalBars() || dead_) return;
    if (vitals_.health > 0.0f && vitals_.health < vitals_.maxHealth && vitals_.food >= 18) {
        if (++regenTimer_ >= 80) {
            vitals_.health = std::min(vitals_.maxHealth, vitals_.health + 1.0f);
            regenTimer_ = 0;
        }
    } else {
        regenTimer_ = 0;
    }
}

// Player.aiStep: the items the player's box, grown by a block either side and half a block up and down, touches are taken once their delay is over.
void GameSession::pickUpItems() {
    if (info_.gameType == 3 || dead_) return;
    Aabb reach = player_.boundingBox();
    reach = {reach.minX - 1.0, reach.minY - 0.5, reach.minZ - 1.0, reach.maxX + 1.0, reach.maxY + 0.5, reach.maxZ + 1.0};
    for (ItemEntity& e : itemEntities_.items()) {
        if (e.pickupDelay > 0 || e.stack.isEmpty() || !e.box().intersects(reach)) continue;
        const int before = e.stack.count;
        inventory_.add(e.stack);
        const int taken = before - (e.stack.isEmpty() ? 0 : e.stack.count);
        if (taken > 0) playWorldSound("entity.item.pickup", SoundSource::Players, 0.2f, ((randomFloat() - randomFloat()) * 0.7f + 1.0f) * 2.0f);
    }
}

// The walk cycle and where the body faces (LivingEntity.tick and tickHeadTurn): the body turns towards where the player is going, never more than
// 75 degrees away from where the head looks.
void GameSession::tickBody() {
    const double dx = player_.x - player_.xo, dz = player_.z - player_.zo;
    walkSpeedOld_ = walkSpeed_;
    walkSpeed_ += (std::min(static_cast<float>(std::sqrt(dx * dx + dz * dz)) * 4.0f, 1.0f) - walkSpeed_) * 0.4f;
    walkPosition_ += walkSpeed_;
    const auto wrap = [](float degrees) {
        float d = std::fmod(degrees, 360.0f);
        if (d >= 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    };
    bodyYawOld_ = bodyYaw_;
    float target = bodyYaw_;
    if (dx * dx + dz * dz > 0.0025000002) {
        const float heading = static_cast<float>(std::atan2(dz, dx) * 57.29577951308232) - 90.0f;
        const float away = std::abs(wrap(player_.yaw) - heading);
        target = (95.0f < away && away < 265.0f) ? heading - 180.0f : heading;  // walking backwards faces the other way round
    }
    if (hand_.swingProgress(0.0) > 0.0f) target = player_.yaw;  // an attack or a placed block faces the way the player looks
    bodyYaw_ += wrap(target - bodyYaw_) * 0.3f;
    float diff = std::clamp(wrap(player_.yaw - bodyYaw_), -75.0f, 75.0f);
    bodyYaw_ = player_.yaw - diff;
    if (diff * diff > 2500.0f) bodyYaw_ += diff * 0.2f;
}

bool GameSession::giveItem(const std::string& spec) {
    // "item", "item:count" or "item:count:wear".
    std::vector<std::string> parts;
    for (std::size_t from = 0;;) {
        const std::size_t colon = spec.find(':', from);
        parts.push_back(spec.substr(from, colon == std::string::npos ? std::string::npos : colon - from));
        if (colon == std::string::npos) break;
        from = colon + 1;
    }
    // "minecraft:stone:5" starts with a namespace: join it back.
    if (parts.size() >= 2 && parts[0] == "minecraft") {
        parts[1] = "minecraft:" + parts[1];
        parts.erase(parts.begin());
    }
    const int count = parts.size() > 1 ? std::max(1, std::atoi(parts[1].c_str())) : 1;
    const int wear = parts.size() > 2 ? std::max(0, std::atoi(parts[2].c_str())) : 0;
    const auto item = itemRegistry().find(parts[0]);
    if (!item || *item == Item::Air) return false;
    ItemStack stack(*item, count);
    stack.damage = std::min(wear, std::max(0, stack.maxDamage() - 1));
    const int max = stack.maxStackSize();
    while (!stack.isEmpty()) {  // as many full stacks as it takes
        ItemStack part = stack.split(std::min(stack.count, max));
        if (!inventory_.add(part)) break;
    }
    return true;
}

void GameSession::interact(double dt, bool active) {
    const auto look = Look{player_.yaw, player_.pitch}.forward();
    const std::array<double, 3> eye = player_.eyePosition(partialTick_);
    target_ = raycastBlocks(renderer_.level(), eye, look, kReach);
    breakCooldown_ = std::max(0.0, breakCooldown_ - dt);
    placeCooldown_ = std::max(0.0, placeCooldown_ - dt);
    if (!active) return;
    // Adventure and spectator players cannot break or place blocks.
    if (info_.gameType >= 2) return;

    const bool attack = isDown("key.attack");
    const bool use = isDown("key.use");
    if (!attack) {
        breakCooldown_ = 0.0;
        stopDigging();
    }
    const bool freshPress = attack && !digHeld_;
    digHeld_ = attack;
    if (!use) placeCooldown_ = 0.0;
    if (attack && !target_ && breakCooldown_ <= 0.0) {  // a swing at the air: the attack cooldown starts over
        breakCooldown_ = kRepeatDelay;
        hand_.swing();
        hand_.resetAttackStrength();
    }
    if (attack && !creative()) digSurvival(dt, freshPress);
    if (!target_) return;

    if (attack && creative() && breakCooldown_ <= 0.0) {  // creative: every block goes at once, five ticks apart
        breakCooldown_ = kRepeatDelay;
        if (target_->y > kMinY) {  // the floor of the world stays
            breakBlockAt(target_->x, target_->y, target_->z, false);
            target_ = raycastBlocks(renderer_.level(), eye, look, kReach);
        }
    }
    if (attack && !creative()) target_ = raycastBlocks(renderer_.level(), eye, look, kReach);  // (a block that was dug is gone: aim past it)
    if (use && placeCooldown_ <= 0.0 && target_) {
        placeCooldown_ = kRepeatDelay;
        // BlockPlaceContext: aiming at a block that can be replaced (grass, ferns, dead bushes, snow, water ...) puts the new block in ITS place; any other
        // block gets the new one next to the face that was hit (a flower, a torch, a slab: beside or on top of it).
        const bool replaceClicked = registry_->state(renderer_.level().block(target_->x, target_->y, target_->z)).has(kStateReplaceable);
        const auto place = replaceClicked ? std::array<int, 3>{target_->x, target_->y, target_->z} : target_->placePosition();
        const Block there = renderer_.level().block(place[0], place[1], place[2]);
        const bool inWorld = place[1] >= kMinY && place[1] < kMaxY;
        // Not into the player's own body (whatever the pose).
        const Aabb cell{static_cast<double>(place[0]), static_cast<double>(place[1]), static_cast<double>(place[2]),
                        static_cast<double>(place[0]) + 1.0, static_cast<double>(place[1]) + 1.0, static_cast<double>(place[2]) + 1.0};
        const bool insidePlayer = cell.intersects(player_.boundingBox());
        // The held stack: a block item places its block (in the state blockStateForPlacement() chooses) and, outside
        // creative, is used up one at a time.
        ItemStack& heldStack = inventory_.selectedItem();
        const int heldBlock = heldStack.isEmpty() ? -1 : heldStack.type().blockIndex;
        if (heldBlock >= 0 && inWorld && registry_->state(there).has(kStateReplaceable) && !insidePlayer) {
            Block placed = blockStateForPlacement({*registry_, heldBlock, *target_, player_.yaw, player_.pitch, player_.sneaking});
            // BlockItem.canPlace: a plant needs its soil, and a tall one (DoubleHighBlockItem) the cell above free as well.
            const BlockSupport::Reader at = [this](int bx, int by, int bz) { return renderer_.level().block(bx, by, bz); };
            // A wall torch with nothing sturdy to hang on (a click on the side of a flower, say) falls back to the standing torch, as StandingAndWallBlockItem does.
            if (support_ && !support_->canSurvive(placed, place[0], place[1], place[2], at) && registry_->defaultState(heldBlock) != placed) placed = registry_->defaultState(heldBlock);
            const bool tall = support_ && support_->isLowerHalf(placed);
            const bool roomAbove = !tall || (place[1] + 1 < kMaxY && registry_->state(at(place[0], place[1] + 1, place[2])).has(kStateReplaceable));
            const bool stands = !support_ || support_->canSurvive(placed, place[0], place[1], place[2], at);
            if (!registry_->isAir(placed) && roomAbove && stands) {
                if (tall) {  // a tall plant or a door is two blocks: the upper half first, so the lower one finds its other half
                    renderer_.setBlock(place[0], place[1] + 1, place[2], support_->upperHalf(placed));
                }
                setBlock(place[0], place[1], place[2], placed);
                hand_.swing();
                {  // the place sound of the new block
                    const std::array<double, 3> where = {place[0] + 0.5, place[1] + 0.5, place[2] + 0.5};
                    const BlockType& type = registry_->type(placed);
                    playBlockSound(placed, "place", breakOrPlaceMix(type.soundVolume, type.soundPitch), SoundSource::Blocks, &where);
                }
                if (!creative()) {
                    heldStack.shrink(1);
                    if (heldStack.isEmpty()) heldStack = ItemStack();
                }
            }
        }
    }
}

void GameSession::update(double dt, bool active, int framebufferWidth, int framebufferHeight, bool keepTicking) {
    framebufferWidth_ = framebufferWidth;
    framebufferHeight_ = framebufferHeight;
    const double frameSeconds = dt;  // what the game ticks are made from (the catch-up limit is in ticks, not seconds)
    dt = std::min(dt, 0.25);
    systemMemoryAge_ += dt;
    if (showDebug_ && systemMemoryAge_ >= 1.0) {  // the PC's memory for the debug screen: once a second is plenty
        systemMemory_ = systemMemory();
        systemMemoryAge_ = 0.0;
    }
    fpsAccumulated_ += dt;
    ++fpsFrames_;
    if (fpsAccumulated_ >= 0.5) {
        fps_ = fpsFrames_ / fpsAccumulated_;
        frameMillis_ = 1000.0 * fpsAccumulated_ / fpsFrames_;
        fpsAccumulated_ = 0.0;
        fpsFrames_ = 0;
    }

    const int distance = renderDistance();
    renderer_.setBrightness(static_cast<float>(services_.options.gamma));
    renderer_.setGpuTiming(showDebug_ || benchmark_.running);
    renderer_.setSmoothLighting(services_.options.ambientOcclusion);
    renderer_.setCutoutLeaves(services_.options.cutoutLeaves);
    renderer_.update(player_.x, player_.z, distance, ready_ ? 1.5 : 6.0);
    // (The old idle call to trimWorkingSet() was removed: SetProcessWorkingSetSize(-1,-1) empties every page out of RAM, so the
    // next frame faults them all back in - a ~0.7 s stall that showed up as a stutter whenever the world went quiet. The memory it
    // saved is not worth the hitch; the working set is only trimmed once, after the initial load, from the client loop.)
    if (!ready_) {
        // The game's LevelLoadTracker: wait for the chunks around the spawn, then for the section the player is in to be on the GPU
        // (nobody should open their eyes in empty space), and give up waiting after 30 seconds so this can never stay for good.
        loadElapsed_ += dt;
        const float chunks = renderer_.loadProgress(std::min(distance, kLoadRadius));
        const auto eye = player_.eye();
        const bool playerSection =
            player_.spectator || renderer_.sectionReady(static_cast<int>(std::floor(eye[0])), static_cast<int>(std::floor(eye[1])), static_cast<int>(std::floor(eye[2])));
        progress_ = chunks >= 1.0f ? (playerSection ? 1.0f : 0.97f) : chunks * 0.97f;
        // The bar eases towards the real progress: 0.2 of the gap each game tick (LevelLoadingScreen.tick).
        smoothedProgress_ += (progress_ - smoothedProgress_) * static_cast<float>(1.0 - std::pow(0.8, dt / kTickSeconds));
        const bool loaded = chunks >= 1.0f && playerSection && loadElapsed_ >= minLoadSeconds_;
        const bool timedOut = loadElapsed_ >= kLoadTimeoutSeconds;
        if (loaded || timedOut) {
            ready_ = true;
            progress_ = smoothedProgress_ = 1.0f;
            if (timedOut && !loaded) std::printf("[falcon] the spawn area did not finish loading in %.0f s: starting anyway\n", loadElapsed_);
            std::printf("[falcon] spawn area loaded in %.2f s\n", loadElapsed_);
        }
        return;
    }

    if (benchmark_.running) {
        // A fixed flight over the terrain: along +X at 24 blocks a second, turning slowly, 25 blocks above the ground.
        benchmark_.elapsed += dt;
        if (benchmark_.frame.empty()) benchmark_.startRss = processWorkingSetBytes();
        benchmark_.frame.push_back(static_cast<float>(dt * 1000.0));
        benchmark_.gpu.push_back(static_cast<float>(renderer_.gpuMillis()));
        benchmark_.peakRss = std::max(benchmark_.peakRss, processWorkingSetBytes());
        player_.yaw = player_.yawO = static_cast<float>(std::fmod(benchmark_.elapsed / benchmark_.seconds * 360.0, 360.0));
        player_.pitch = player_.pitchO = 20.0f;
        const double groundHeight = renderer_.surfaceHeight(static_cast<int>(std::floor(player_.x)), static_cast<int>(std::floor(player_.z)));
        player_.setPosition(player_.x + 24.0 * dt, std::max(groundHeight, 63.0) + 25.0, player_.z);
        if (benchmark_.elapsed >= benchmark_.seconds) {
            benchmark_.running = false;
            benchmark_.done = true;
            benchmark_.endRss = processWorkingSetBytes();
            const RenderStats s = renderer_.stats();
            benchmark_.finalStats = format("%d chunks, %d sections drawn of %d, %zu quads drawn of %zu, blocks %.1f MB, meshes %.1f MB; %d draw calls for %d runs (%s, %s)",
                                           s.chunksLoaded, s.sectionsDrawn, s.sectionsLoaded, s.quadsDrawn, s.quadsTotal, megabytes(s.levelBytes),
                                           megabytes(s.meshBytes), s.drawCalls, s.drawCommands, s.multiDraw ? "multi-draw" : "one call per run",
                                           s.occlusionCulling ? "occlusion culling" : "frustum culling only");
        }
    }

    applyLook(active);
    // The game's clock runs while the player steers and while the inventory is open; the pause menu and the others stop it.
    if (active || keepTicking) tickPlayer(frameSeconds, active);  // (a pinned camera, for screenshots, freezes the player, not the world)
    if (active || keepTicking) tickHud(frameSeconds);
    if (playerAgeTicks_ - lastSaveTicks_ >= 6000.0) {  // every five minutes of play, as the game does
        lastSaveTicks_ = playerAgeTicks_;
        save();
    }
    interact(dt, active);
}

bool GameSession::keyIs(const char* action, int key) const {
    const std::size_t index = keyMappingIndex(action);
    return index < kKeyMappingCount && services_.options.keyBindings.key(index) == KeyId::keyboard(key);
}

void GameSession::onKey(int key, int mods) {
    for (int slot = 0; slot < 9; ++slot) {
        if (info_.gameType != 3 && keyIs(format("key.hotbar.%d", slot + 1).c_str(), key)) inventory_.setSelected(slot);  // (a spectator has no inventory to choose from)
    }
    // The debug key (F3): the debug screen is shown or hidden when it is let go, unless a chord (F3 + H, F3 + F4 ...) was used while it was
    // down (KeyboardHandler: handledDebugKey).
    if (keyIs("key.debug.overlay", key) || keyIs("key.debug.modifier", key)) {
        if (!debugHeld_) {  // (a held key repeats: only the first press starts a new chord)
            debugHeld_ = true;
            debugCombo_ = false;
        }
    } else if (debugHeld_) {
        // F3 + H: advanced tooltips (the item id and its durability under the name).
        if (keyIs("key.debug.showAdvancedTooltips", key)) {
            debugCombo_ = true;
            advancedTooltips_ = !advancedTooltips_;
            addChatMessage("§e§l" + services_.language.get("debug.prefix") + "§r " + services_.language.get(advancedTooltips_ ? "debug.advanced_tooltips.on" : "debug.advanced_tooltips.off"));
        }
        // F3 + F4: the game mode switcher (the in-game screen opens it); without cheats the game says why not.
        if (keyIs("key.debug.switchGameMode", key)) {
            debugCombo_ = true;
            if (canSwitchGameMode()) {
                switcherRequested_ = true;
            } else if (ready_) {
                addChatMessage("§e§l" + services_.language.get("debug.prefix") + "§r " + services_.language.get("debug.gamemodes.error"));
            }
        }
        // F3 + N: spectator mode, and back to the mode before it.
        if (keyIs("key.debug.spectate", key)) {
            debugCombo_ = true;
            if (canSwitchGameMode()) {
                if (info_.gameType != 3) {
                    setGameType(3);
                } else {
                    setGameType(previousGameType_ >= 0 && previousGameType_ != 3 ? previousGameType_ : 1);
                }
            } else if (ready_) {
                addChatMessage("§e§l" + services_.language.get("debug.prefix") + "§r " + services_.language.get("debug.creative_spectator.error"));
            }
        }
    }
    if (services_.options.toggleCrouch && keyIs("key.sneak", key)) sneakLatched_ = !sneakLatched_;
    if (services_.options.toggleSprint && keyIs("key.sprint", key)) sprintLatched_ = !sprintLatched_;
    if (keyIs("key.pickItem", key) && target_) pickBlock(renderer_.level().block(target_->x, target_->y, target_->z));
    // Q throws the held item (Ctrl + Q the whole stack) out in front of the player.
    if (keyIs("key.drop", key) && ready_ && info_.gameType != 3 && !dead_) {
        const ItemStack thrown = inventory_.removeFromSelected((mods & GLFW_MOD_CONTROL) != 0);
        if (!thrown.isEmpty()) {
            dropFromPlayer(thrown);
            hand_.swing();
        }
    }
    // F5: first person, from behind, from the front, and round again.
    if (keyIs("key.togglePerspective", key) && ready_) perspective_ = (perspective_ + 1) % 3;
    // F swaps the held item with the offhand one.
    if (keyIs("key.swapOffhand", key) && ready_ && info_.gameType != 3) std::swap(inventory_.selectedItem(), inventory_.offhand());
}

void GameSession::drawPlayerPreview(GuiGraphics& g, int x0, int y0, int x1, int y1, float scale, float mouseX, float mouseY) {
    if (!playerPreview_) return;
    PlayerPreview::View view;
    view.x0 = x0;
    view.y0 = y0;
    view.x1 = x1;
    view.y1 = y1;
    view.guiScale = std::max(g.guiScale(), 1);
    view.scale = scale;
    view.mouseX = mouseX;
    view.mouseY = mouseY;
    view.ageTicks = playerAgeTicks_ + partialTick_;
    view.parts = services_.options.modelParts;
    const Image picture = playerPreview_->render(view);
    // The picture goes through a texture of its own, replaced every frame (it is small), and is drawn 1:1 over the box.
    g.textures().add("falcon:player_preview", picture);
    g.blit(g.textures().get("falcon:player_preview"), x0, y0, 0.0f, 0.0f, x1 - x0, y1 - y0, x1 - x0, y1 - y0);
}

void GameSession::onKeyReleased(int key) {
    if ((keyIs("key.debug.overlay", key) || keyIs("key.debug.modifier", key)) && debugHeld_) {
        debugHeld_ = false;
        if (!debugCombo_) showDebug_ = !showDebug_;
        debugCombo_ = false;
    }
}

void GameSession::requestGameType(int gameType) {
    if (canSwitchGameMode()) setGameType(gameType);
}

// The server's change of game mode as the player sees it: what a mode gives (infinite materials and flight in creative, flight and
// no clipping in spectator), what it takes (a survival or adventure player stops flying) and what it forbids (building).
void GameSession::setGameType(int gameType) {
    gameType = std::clamp(gameType, 0, 3);
    if (gameType == info_.gameType) return;
    static constexpr const char* kModeNames[4] = {"survival", "creative", "adventure", "spectator"};
    std::printf("[falcon] game mode: %s -> %s\n", kModeNames[info_.gameType], kModeNames[gameType]);
    previousGameType_ = info_.gameType;
    info_.gameType = gameType;
    inventory_.infiniteMaterials = gameType == 1;
    inventoryMenu_->player().infiniteMaterials = gameType == 1;
    player_.canFly = gameType == 1 || gameType == 3;
    player_.spectator = gameType == 3;
    if (gameType == 3) {
        player_.flying = true;
    } else if (gameType != 1) {
        player_.flying = false;
    }
    sneakLatched_ = sprintLatched_ = false;
}

void GameSession::onMouseButton(int button) {
    const std::size_t index = keyMappingIndex("key.pickItem");
    if (index >= kKeyMappingCount || !ready_) return;
    const KeyId bound = services_.options.keyBindings.key(index);
    if (bound.type == KeyId::Type::Mouse && bound.code == button && target_) pickBlock(renderer_.level().block(target_->x, target_->y, target_->z));
}

// ServerGamePacketListenerImpl.tryPickItem: if the inventory has the block's item, that slot is selected (a hotbar one) or
// swapped into the hotbar; with infinite materials (creative) a missing item is put into the hotbar instead.
void GameSession::pickBlock(Block block) {
    const int blockIndex = registry_->state(block).block;
    const Item item = itemRegistry().itemOfBlock(blockIndex);
    if (item == Item::Air) return;
    const ItemStack wanted(item, 1);
    const int slot = inventory_.findSlotMatching(wanted);
    if (slot != -1) {
        if (Inventory::isHotbarSlot(slot)) {
            inventory_.setSelected(slot);
        } else {
            inventory_.pickSlot(slot);
        }
    } else if (creative()) {
        inventory_.addAndPick(wanted);
    }
}

void GameSession::onScroll(double dy) {
    if (dy == 0.0 || info_.gameType == 3) return;
    inventory_.setSelected(inventory_.selected - (dy > 0 ? 1 : -1));
}

void GameSession::renderWorld(int framebufferWidth, int framebufferHeight) {
    const Camera cam = camera();
    // A torch (or any light-giving block) in the hand lights the blocks round the player while it is held.
    // (the main hand or the offhand: either one lights the way)
    const int carried = ready_ && info_.gameType != 3 ? std::max(carriedLightLevel(inventory_.selectedItem()), carriedLightLevel(inventory_.offhand())) : 0;
    renderer_.setCarriedLight(static_cast<int>(std::floor(cam.x)), static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)), carried);
    // Under water the sky is gone and the world fades into the biome's water fog (see water_fog.hpp).
    const bool eyesInWater = ready_ && player_.isUnderWater();
    if (eyesInWater) {
        const std::string_view biome = overworld_ ? overworld_->biomeNameAt(static_cast<int>(std::floor(cam.x)), static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)))
                                                  : std::string_view();
        const WaterFog& fog = waterFogs_.forBiome(biome);
        const float vision = waterVision(waterVisionTicks_, true);
        renderer_.setUnderwater(true, underwaterFogColor(fog.color, vision), kWaterFogStart, underwaterFogEnd(fog.endFactor, vision));
    } else {
        renderer_.setUnderwater(false);
    }
    renderer_.render(cam, framebufferWidth, framebufferHeight, renderDistance());
    if (ready_ && target_) renderer_.drawSelection(cam, framebufferWidth, framebufferHeight, target_->x, target_->y, target_->z);
    if (ready_ && digPos_ && destroyStage(digProgress_) >= 0) renderer_.drawBreakOverlay(cam, framebufferWidth, framebufferHeight, (*digPos_)[0], (*digPos_)[1], (*digPos_)[2], destroyStage(digProgress_));
    if (ready_) {
        const std::array<double, 3> camPos = {cam.x, cam.y, cam.z};
        hand_.renderGroundItems(renderer_, renderer_.viewProjection(), camPos, itemEntities_.items(), partialTick_);
        if (perspective_ != 0 && body_ && info_.gameType != 3) {  // F5: the player seen from outside
            const float t = static_cast<float>(partialTick_);
            const auto wrap = [](float degrees) {
                float d = std::fmod(degrees, 360.0f);
                if (d >= 180.0f) d -= 360.0f;
                if (d < -180.0f) d += 360.0f;
                return d;
            };
            BodyPose pose;
            pose.position = player_.position(partialTick_);
            pose.bodyYaw = bodyYawOld_ + wrap(bodyYaw_ - bodyYawOld_) * t;
            pose.headYaw = wrap(player_.yaw - pose.bodyYaw);
            pose.headPitch = player_.pitch;
            pose.walkPosition = walkPosition_ - walkSpeed_ * (1.0f - t);
            pose.walkSpeed = std::min(1.0f, walkSpeedOld_ + (walkSpeed_ - walkSpeedOld_) * t);
            pose.attackProgress = hand_.swingProgress(partialTick_);
            pose.crouching = player_.sneaking;
            pose.ageTicks = playerAgeTicks_ + partialTick_;
            pose.parts = services_.options.modelParts;
            int sky = 15, block = 0;
            renderer_.lightAtWithCarried(static_cast<int>(std::floor(pose.position[0])), static_cast<int>(std::floor(pose.position[1] + 1.0)), static_cast<int>(std::floor(pose.position[2])), sky, block);
            pose.light = entityLightColor(sky, block, static_cast<float>(services_.options.gamma));
            body_->render(renderer_.viewProjection(), camPos, pose);
        }
    }
    if (ready_ && perspective_ == 0 && !hudHidden_ && info_.gameType != 3) {  // the hand (F1 hides it, and spectators have none)
        HandFrame frame;
        frame.width = framebufferWidth;
        frame.height = framebufferHeight;
        frame.partialTick = partialTick_;
        frame.yaw = player_.yaw;
        frame.pitch = player_.pitch;
        frame.rightHanded = services_.options.mainHandRight;
        frame.sleeve = services_.options.modelParts[frame.rightHanded ? 3 : 2];  // right_sleeve, left_sleeve
        frame.brightness = static_cast<float>(services_.options.gamma);
        renderer_.lightAt(static_cast<int>(std::floor(cam.x)), static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)), frame.sky, frame.block);
        frame.block = std::max(frame.block, carried);  // the hand holding the torch is lit by it
        if (cam.bobX != 0.0f || cam.bobY != 0.0f || cam.bobRollDegrees != 0.0f || cam.bobPitchDegrees != 0.0f) {
            frame.bob = bobTransform(cam.bobX, cam.bobY, cam.bobRollDegrees, cam.bobPitchDegrees);
        }
        hand_.render(renderer_, frame);
    }
    renderer_.endFrame();
}

std::vector<std::string> GameSession::debugLines() const {
    const RenderStats s = renderer_.stats();
    const auto eye = player_.eye();
    std::vector<std::string> lines;
    lines.push_back(format("Falcon 0.1.0 (Minecraft 26.3) - %s", info_.name.c_str()));
    lines.push_back(format("%.0f fps (%.1f ms)  cpu %.1f  gpu %.1f", fps_, frameMillis_, cpuMillis_, renderer_.gpuMillis()));
    lines.push_back("");
    lines.push_back(format("XYZ: %.3f / %.3f / %.3f", player_.x, player_.y, player_.z));
    const int bx = static_cast<int>(std::floor(player_.x));
    const int by = static_cast<int>(std::floor(player_.y));
    const int bz = static_cast<int>(std::floor(player_.z));
    lines.push_back(format("Block: %d %d %d   Chunk: %d %d in %d %d", bx, by, bz, bx & 15, bz & 15, chunkOf(bx), chunkOf(bz)));
    lines.push_back(format("Facing: %s (%.1f / %.1f)", facing(player_.yaw), static_cast<double>(player_.yaw), static_cast<double>(player_.pitch)));
    if (overworld_) {
        const std::string_view biome = overworld_->biomeNameAt(static_cast<int>(std::floor(player_.x)), static_cast<int>(std::floor(player_.y)), static_cast<int>(std::floor(player_.z)));
        lines.push_back("Biome: " + std::string(biome));
    }
    lines.push_back(format("%s%s%s%s%s%s", player_.flying ? "flying " : "", player_.onGround ? "on ground " : "", player_.inWater ? "in water " : "",
                           player_.isSwimming() ? "swimming " : "", player_.sprinting ? "sprinting " : "", player_.sneaking ? "sneaking" : ""));
    lines.push_back(format("Speed: %.2f blocks/s   fall distance: %.1f", std::hypot(player_.x - player_.xo, player_.z - player_.zo) * 20.0, player_.fallDistance));
    lines.push_back("");
    lines.push_back(format("Render distance: %d  chunks: %d", renderDistance(), s.chunksLoaded));
    lines.push_back(format("Sections drawn: %d / %d", s.sectionsDrawn, s.sectionsLoaded));
    lines.push_back(format("Quads: %zu / %zu", s.quadsDrawn, s.quadsTotal));
    lines.push_back(format("Jobs waiting: %d  uploads: %d", s.jobsWaiting, s.uploadsThisFrame));
    lines.push_back(format("Water: %zu waiting (tick %lld)", fluids_.pending(), fluids_.gameTime()));
    lines.push_back(format("RAM: %.0f MB  block data %.1f MB", megabytes(processWorkingSetBytes()), megabytes(s.levelBytes)));
    lines.push_back(format("GPU: meshes %.1f MB, textures %.2f MB", megabytes(s.meshBytes), megabytes(s.textureBytes)));
    lines.push_back(format("Chunk cache: %zu chunks, %.1f MB", s.cachedChunks, megabytes(s.cacheBytes)));
    (void)eye;
    return lines;
}

// The graphics lines are read once (the OpenGL strings and the card's own memory never change); the rest is cheap.
void GameSession::refreshSystemLines() {
    if (systemMemory_.total == 0) systemMemory_ = systemMemory();
    if (!gpuLines_.empty()) return;
    const auto glString = [](GLenum name) {
        const char* text = reinterpret_cast<const char*>(gl::GetString(name));
        return std::string(text != nullptr ? text : "");
    };
    const std::string vendor = glString(gl::VENDOR), renderer = glString(gl::RENDERER), version = glString(gl::VERSION);
    gpuLines_.push_back(format("Display: %dx%d (%s)", framebufferWidth_, framebufferHeight_, vendor.c_str()));
    gpuLines_.push_back(renderer);
    if (const auto adapter = findGpuAdapter(renderer); adapter && adapter->dedicatedBytes > 0) gpuLines_.push_back("Video memory: " + formatByteSize(adapter->dedicatedBytes));
    gpuLines_.push_back("OpenGL " + version);
}

// The right-hand side (vanilla's too): the program, the PC, the screen and the card, and the block looked at.
std::vector<std::string> GameSession::debugLinesRight() const {
    std::vector<std::string> lines;
    const SystemInfo& pc = systemInfo();
    lines.push_back(format("C++20 (GCC %d.%d.%d), %d bit", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__, static_cast<int>(sizeof(void*) * 8)));
    if (systemMemory_.total > 0) {
        lines.push_back("System RAM: " + formatByteSize(systemMemory_.total) + " (" + formatByteSize(systemMemory_.available) + " free)");
    }
    lines.push_back("");
    if (!pc.cpu.empty()) lines.push_back(format("CPU: %dx %s", pc.logicalProcessors, pc.cpu.c_str()));
    lines.push_back("OS: " + pc.os);
    lines.push_back("");
    lines.insert(lines.end(), gpuLines_.begin(), gpuLines_.end());
    if (target_) {
        const Block b = renderer_.level().block(target_->x, target_->y, target_->z);
        lines.push_back("");
        lines.push_back(format("Targeted Block: %d, %d, %d", target_->x, target_->y, target_->z));
        const std::string state = registry_->describe(b);  // "oak_log[axis=y]": the name, then one line a property
        const std::size_t open = state.find('[');
        lines.push_back("minecraft:" + state.substr(0, open));
        for (std::size_t at = open == std::string::npos ? state.size() : open + 1; at < state.size();) {
            std::size_t end = state.find_first_of(",]", at);
            if (end == std::string::npos) end = state.size();
            std::string property = state.substr(at, end - at);
            if (const std::size_t eq = property.find('='); eq != std::string::npos) property.replace(eq, 1, ": ");
            lines.push_back(property);
            at = end + 1;
        }
    }
    return lines;
}

// Draws the two columns, each line on its translucent strip. A right-hand line that would run into the left-hand one of its row is cut with
// "..." so the two columns never write over each other.
void GameSession::drawDebugScreen(GuiGraphics& g, int guiWidth) {
    refreshSystemLines();
    const std::vector<std::string> left = debugLines();
    std::vector<int> leftWidth(left.size(), 0);
    float y = 2.0f;
    for (std::size_t i = 0; i < left.size(); ++i, y += 10.0f) {
        if (left[i].empty()) continue;
        leftWidth[i] = g.textWidth(left[i]);
        g.fill(1, static_cast<int>(y) - 1, 2 + leftWidth[i] + 1, static_cast<int>(y) + 9, 0x90505050u);
        g.text(left[i], 2.0f, y, 0xFFE0E0E0u);
    }
    const std::vector<std::string> right = debugLinesRight();
    y = 2.0f;
    for (std::size_t i = 0; i < right.size(); ++i, y += 10.0f) {
        std::string line = right[i];
        if (line.empty()) continue;
        const int room = guiWidth - 4 - (i < leftWidth.size() && leftWidth[i] > 0 ? leftWidth[i] + 6 : 0);
        if (g.textWidth(line) > room) {
            while (line.size() > 1 && g.textWidth(line + "...") > room) line.pop_back();
            line += "...";
        }
        const int w = g.textWidth(line);
        const int x = guiWidth - 2 - w;
        g.fill(x - 1, static_cast<int>(y) - 1, x + w + 1, static_cast<int>(y) + 9, 0x90505050u);
        g.text(line, static_cast<float>(x), y, 0xFFE0E0E0u);
    }
}

void GameSession::renderLoading(GuiGraphics& g, int guiWidth, int guiHeight, float deltaTicks) {
    // LevelLoadingScreen, as the game draws it when a world opens (the "other" reason: not a portal): the panorama blurred by the
    // Menu Background Blur option with the menu tint on top; then, centred, a map of the chunks around the spawn with the text
    // above it and a 200 x 2 progress bar under the text.
    renderMenuBackground(g, services_.panorama, services_.options, guiWidth, guiHeight, deltaTicks);

    constexpr int kCell = 2;            // one chunk is a 2 x 2 square
    constexpr int kMapRadius = 5 + 11 + 1;  // max(5, 3) + ChunkLevel.RADIUS_AROUND_FULL_CHUNK (11) + 1: chunks either side of the middle
    constexpr int kMapCells = kMapRadius * 2 + 1;
    constexpr int kMapSize = kMapCells * kCell;
    constexpr int kLineHeight = 9;
    constexpr int kBarWidth = 200;
    const int cx = guiWidth / 2, cy = guiHeight / 2;

    // The chunk map: the colour of each chunk's status. Where nothing has been asked for yet it is black, like an unknown status.
    // (The game's colours for EMPTY, FEATURES, LIGHT and FULL: our chunks are made, lit and drawn in that order.)
    const auto stateColor = [](ChunkLoadState state) -> std::uint32_t {
        switch (state) {
            case ChunkLoadState::Queued: return 0xFF000000u | 5526612u;       // EMPTY
            case ChunkLoadState::Generated: return 0xFF000000u | 2213376u;    // FEATURES
            case ChunkLoadState::Lit: return 0xFF000000u | 16769184u;         // LIGHT
            case ChunkLoadState::Ready: return 0xFF000000u | 16777215u;       // FULL
            case ChunkLoadState::None: break;
        }
        return 0xFF000000u;
    };
    const int centerChunkX = chunkOf(static_cast<int>(std::floor(player_.x)));
    const int centerChunkZ = chunkOf(static_cast<int>(std::floor(player_.z)));
    const int left = cx - kMapSize / 2, top = cy - kMapSize / 2;
    for (int mx = 0; mx < kMapCells; ++mx) {
        for (int mz = 0; mz < kMapCells; ++mz) {
            const ChunkLoadState state = renderer_.chunkLoadState({centerChunkX + mx - kMapRadius, centerChunkZ + mz - kMapRadius});
            const int px = left + mx * kCell, py = top + mz * kCell;
            g.fill(px, py, px + kCell, py + kCell, stateColor(state));
        }
    }

    // The text sits above the map (map radius * 2 + three lines up from the middle); the bar right under the text.
    const std::string text = services_.language.get("multiplayer.downloadingTerrain");
    const int textY = cy - kMapRadius * 2 - kLineHeight * 3;
    g.text(text, static_cast<float>(cx - g.textWidth(text) / 2), static_cast<float>(textY), kWhite);
    const int barX = cx - kBarWidth / 2, barY = textY + kLineHeight + 3;
    g.fill(barX, barY, barX + kBarWidth, barY + 2, 0xFF000000u);
    g.fill(barX, barY, barX + static_cast<int>(std::lround(std::clamp(smoothedProgress_, 0.0f, 1.0f) * static_cast<float>(kBarWidth))), barY + 2, 0xFF00FF00u);
}

void GameSession::renderHud(GuiGraphics& g, int guiWidth, int guiHeight, bool chatFocused) {
    chatFont_ = &g.font();  // so a sent message can be word-wrapped
    if (!ready_) return;  // the loading screen (renderLoading) takes the place of the HUD until the world is ready

    // F1 hides the crosshair and the hotbar (the debug screen stays, which is what people take screenshots with).
    if (!hudHidden_) {
        // Crosshair: 15x15 in the middle (only in first person).
        if (perspective_ == 0) g.blitSprite("hud/crosshair", (guiWidth - 15) / 2, (guiHeight - 15) / 2, 15, 15);
        if (info_.gameType != 3) drawHotbar(g, guiWidth, guiHeight);  // (a spectator has no hotbar: no inventory)
        if (survivalBars()) drawSurvivalBars(g, guiWidth, guiHeight);
    }

    if (showDebug_) drawDebugScreen(g, guiWidth);

    // The chat: faded on the HUD, full and scrollable when its screen is open (F1 hides it unless it is open).
    if (!hudHidden_ || chatFocused) chat_.render(g, guiWidth, guiHeight, services_.options, chatFocused);
}

// Hud.extractItemHotbar and extractSelectedItemName: the 182x22 bar at the bottom middle with the nine hotbar stacks, the selected
// slot ringed, the offhand stack in its own little frame to the left, and the fading name of the held item above.
void GameSession::drawHotbar(GuiGraphics& g, int guiWidth, int guiHeight) {
    const HotbarLayout layout = computeHotbarLayout(guiWidth, guiHeight, survivalBars());  // (the name sits higher where the bars are)
    g.blitSprite("hud/hotbar", layout.left, layout.top, 182, 22);
    g.blitSprite("hud/hotbar_selection", layout.selectionX(inventory_.selected), layout.selectionY(), 24, 23);
    const ItemStack& offhand = inventory_.offhand();
    if (!offhand.isEmpty()) {
        g.blitSprite("hud/hotbar_offhand_left", layout.offhandSpriteX, layout.offhandSpriteY, 29, 24);
        drawItemStack(g, itemIcons_, offhand, layout.offhandItemX, layout.offhandItemY);
    }
    for (int slot = 0; slot < 9; ++slot) drawItemStack(g, itemIcons_, inventory_.item(slot), layout.slotX(slot), layout.slotY());

    const int alpha = highlight_.alpha();
    if (alpha > 0) {
        const ItemStack& stack = highlight_.stack();
        const std::string name = itemDisplayName(stack, services_.language);
        const int width = g.textWidth(name);
        const int x = (guiWidth - width) / 2;
        // textWithBackdrop: a box of the background colour (half black) behind the text, both faded with the name.
        g.fill(x - 2, layout.nameY - 2, x + width + 2, layout.nameY + Font::kLineHeight + 2, (static_cast<std::uint32_t>(alpha * 128 / 255) << 24));
        g.text(name, static_cast<float>(x), static_cast<float>(layout.nameY), (static_cast<std::uint32_t>(alpha) << 24) | (rarityColor(stack.type().rarity) & 0xFFFFFFu));
    }
}

// Hud.extractPlayerHealth (hearts and food, 9x9 sprites) and ExperienceBar (the 182x5 strip, the level in green with a black outline above it).
void GameSession::drawSurvivalBars(GuiGraphics& g, int guiWidth, int guiHeight) {
    const SurvivalLayout l = computeSurvivalLayout(guiWidth, guiHeight, vitals_.maxHealth);
    const long long tick = static_cast<long long>(playerAgeTicks_);
    const bool hardcore = info_.hardcore;
    const int maxHearts = static_cast<int>(std::ceil(vitals_.maxHealth / 2.0f));
    // The hearts blink after a hit or a heal, and the hearts just lost stay white for a moment (the game's own bookkeeping in extractPlayerHealth).
    const int health = static_cast<int>(std::ceil(vitals_.health));
    const bool blink = healthBlinkTime_ > tick && ((healthBlinkTime_ - tick) / 3) % 2 == 1;
    const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (health < lastHealthShown_ && invulnerableTime_ > 0) {
        lastHealthMillis_ = now;
        healthBlinkTime_ = tick + 20;
    } else if (health > lastHealthShown_ && invulnerableTime_ > 0) {
        lastHealthMillis_ = now;
        healthBlinkTime_ = tick + 10;
    }
    if (now - lastHealthMillis_ > 1000.0) {
        displayHealth_ = health;
        lastHealthMillis_ = now;
    }
    lastHealthShown_ = health;
    const auto heartSprite = [hardcore](const char* kind, bool blinking) {
        return std::string("hud/heart/") + (hardcore ? "hardcore_" : "") + kind + (blinking ? "_blinking" : "");
    };
    for (int heart = maxHearts - 1; heart >= 0; --heart) {
        int y = l.heartY(heart);
        if (heartsShake(vitals_.health)) y += tickJitter(tick, heart, 2);
        const int x = l.heartX(heart);
        g.blitSprite(std::string("hud/heart/container") + (hardcore ? "_hardcore" : "") + (blink ? "_blinking" : ""), x, y, 9, 9);
        if (blink && displayHealth_ > heart * 2) g.blitSprite(heartSprite(heart * 2 + 1 == displayHealth_ ? "half" : "full", true), x, y, 9, 9);
        const BarFill fill = heartFill(heart, vitals_.health);
        if (fill != BarFill::Empty) g.blitSprite(heartSprite(fill == BarFill::Full ? "full" : "half", false), x, y, 9, 9);
    }
    for (int icon = 0; icon < 10; ++icon) {
        int y = l.rowY;
        if (foodShakes(vitals_.saturation, vitals_.food, tick)) y += tickJitter(tick, icon, 3) - 1;
        const int x = l.foodX(icon);
        g.blitSprite("hud/food_empty", x, y, 9, 9);
        const BarFill fill = foodFill(icon, vitals_.food);
        if (fill != BarFill::Empty) g.blitSprite(fill == BarFill::Full ? "hud/food_full" : "hud/food_half", x, y, 9, 9);
    }
    // The experience strip: the sprite is 182 wide and only the filled part of it is drawn.
    g.blitSprite("hud/experience_bar_background", l.xpLeft, l.xpTop, 182, 5);
    if (const int filled = experienceFillWidth(vitals_.progress); filled > 0) {
        g.pushScissor(l.xpLeft, l.xpTop, l.xpLeft + filled, l.xpTop + 5);
        g.blitSprite("hud/experience_bar_progress", l.xpLeft, l.xpTop, 182, 5);
        g.popScissor();
    }
    if (vitals_.level > 0) {
        const std::string level = std::to_string(vitals_.level);
        const float x = static_cast<float>((guiWidth - g.textWidth(level)) / 2);
        const float y = static_cast<float>(l.levelBaselineY);
        for (const auto& [dx, dy] : {std::pair{1.0f, 0.0f}, std::pair{-1.0f, 0.0f}, std::pair{0.0f, 1.0f}, std::pair{0.0f, -1.0f}}) g.text(level, x + dx, y + dy, 0xFF000000u, false);
        g.text(level, x, y, 0xFF80FF20u, false);
    }
}

void GameSession::addChatMessage(const std::string& text) {
    if (chatFont_ != nullptr) chat_.addMessage(text, *chatFont_, services_.options);
}

void GameSession::chatInput(const std::string& text) {
    if (text.empty()) return;
    chat_.remember(text);
    if (text[0] == '/') {
        // There is no server and no command engine yet: a command is acknowledged the way the game shows an unknown one.
        addChatMessage("§cUnknown or incomplete command, see below for error");
        addChatMessage("§c" + text + "§r§c<--[HERE]");
    } else {
        addChatMessage("<" + services_.playerName + "> " + text);
    }
}

std::vector<std::string> GameSession::benchmarkReport() const {
    const Benchmark& b = benchmark_;
    std::vector<std::string> lines;
    if (b.frame.empty()) {
        lines.push_back("benchmark: no frames were measured");
        return lines;
    }
    const float avgFrame = average(b.frame);
    lines.push_back(format("benchmark: %zu frames over %.1f s of flying at render distance %d", b.frame.size(), b.elapsed, renderDistance()));
    lines.push_back(format("  frame time (wall clock, includes the pacing sleep): avg %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f", static_cast<double>(avgFrame),
                           static_cast<double>(percentile(b.frame, 0.5)), static_cast<double>(percentile(b.frame, 0.95)),
                           static_cast<double>(percentile(b.frame, 0.99)), static_cast<double>(percentile(b.frame, 1.0))));
    lines.push_back(format("  CPU work per frame: avg %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f", static_cast<double>(average(b.cpu)), static_cast<double>(percentile(b.cpu, 0.5)),
                           static_cast<double>(percentile(b.cpu, 0.95)), static_cast<double>(percentile(b.cpu, 0.99)), static_cast<double>(percentile(b.cpu, 1.0))));
    lines.push_back(format("  GPU time of the world pass: avg %.2f ms, p50 %.2f, p95 %.2f, max %.2f", static_cast<double>(average(b.gpu)), static_cast<double>(percentile(b.gpu, 0.5)),
                           static_cast<double>(percentile(b.gpu, 0.95)), static_cast<double>(percentile(b.gpu, 1.0))));
    const double busy = std::max(static_cast<double>(average(b.cpu)), static_cast<double>(average(b.gpu)));
    if (busy > 0.0) lines.push_back(format("  headroom: the slower of CPU and GPU averages %.2f ms, so roughly %.0f fps if nothing else limited it", busy, 1000.0 / busy));
    lines.push_back(format("  RAM (working set): start %.0f MB, end %.0f MB, peak %.0f MB", megabytes(b.startRss), megabytes(b.endRss), megabytes(b.peakRss)));
    lines.push_back("  " + b.finalStats);
    return lines;
}

}  // namespace falcon
