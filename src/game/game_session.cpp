#include "falcon/game/game_session.hpp"
#include "falcon/util/log.hpp"
#include "game_session_internal.hpp"

#include "falcon/game/block_placement.hpp"
#include "falcon/world/block_outlines.hpp"
#include "falcon/game/creative_tabs.hpp"
#include "falcon/game/item_names.hpp"
#include "falcon/game/item_render.hpp"
#include "falcon/game/menu_background.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/platform/sampling_profiler.hpp"
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
            falcon::log::line("collision shapes: %s (only full blocks will collide)\n", error.c_str());
        }
    }
    // The outline shapes: what the cursor can hit (a torch is a small post, not a block) and the lines drawn round it.
    if (!hasBlockOutlinesFromGameData()) {
        std::string error;
        auto outlines = BlockOutlines::loadFromGameData(resources.dataRoot(), services.gameDir / "falcon_cache", *registry, error);
        if (outlines) {
            installBlockOutlines(std::move(outlines));
        } else {
            falcon::log::line("block outlines: %s (every block will be aimed at as a full cube)\n", error.c_str());
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
            falcon::log::line("creative tabs: %s\n", tabError.c_str());
        }
        std::thread([items, dataDir, cache] {
            std::string recipeError;
            if (auto recipes = RecipeManager::loadFromGameData(dataDir, cache, *items, recipeError)) {
                installRecipeManager(std::move(recipes));
            } else {
                falcon::log::line("recipes: %s\n", recipeError.c_str());
            }
        }).detach();
    }
    return items;
}

// The game-exact world generator for this world's type (normal, amplified), or null for Superflat and when the game's
// data cannot be read (the simple built-in terrain is used then).
std::shared_ptr<const worldgen::OverworldGenerator> makeOverworld(const ResourceManager& resources, const BlockRegistry& registry, const LevelInfo& info) {
    if (info.remote) return nullptr;  // (a server makes the world)
    const char* preset = worldgen::worldPresetFor(info.style);
    if (preset == nullptr) return nullptr;
    const std::filesystem::path dataDir = resources.dataRoot();
    worldgen::GeneratorOptions options;
    std::string error;
    if (!worldgen::loadWorldPreset(dataDir, preset, options, error)) {
        falcon::log::line("world preset %s unreadable (%s): using the simple terrain\n", preset, error.c_str());
        return nullptr;
    }
    std::unique_ptr<worldgen::OverworldGenerator> generator = worldgen::OverworldGenerator::create(dataDir, registry, info.seed, error, options);
    if (!generator) {
        falcon::log::line("the world generator could not start (%s): using the simple terrain\n", error.c_str());
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
      resources_(resources),
      window_(window),
      info_(info),
      registry_(loadRegistry(services, resources)),
      models_(resources, *registry_),
      itemIcons_(models_),
      hand_(resources, models_, itemIcons_),
      overworld_(makeOverworld(resources, *registry_, info)),
      renderer_(models_, info.seed, info.style, loadWorkerCount(), overworld_) {
    (void)loadItems(services, resources, *registry_);
    dataRoot_ = resources.dataRoot();
    advancementTree_ = AdvancementTree::load(dataRoot_);
    if (!services.playerUuid.empty()) {  // the account's own skin (what anyone sees of an account is public)
        localSkins_ = std::make_unique<net::SkinFetcher>();
        localSkins_->requestProfile(services.playerUuid);
    }
    models_.setBiomes(overworld_ ? &overworld_->biomes() : nullptr);  // grass, leaves and water follow the biomes
    inventory_.infiniteMaterials = info.gameType == 1;
    MenuPlayer menuPlayer;
    menuPlayer.inventory = &inventory_;
    menuPlayer.infiniteMaterials = info.gameType == 1;
    menuPlayer.dropHook = [this](const ItemStack& thrown) {
        if (!remote_) dropFromPlayer(thrown);  // (on a server the click is told to it and it throws the item)
    };  // (Player.drop: a click outside the menu throws it)
    inventoryMenu_ = std::make_unique<InventoryMenu>(menuPlayer);
    waterFogs_ = WaterFogTable::load(resources.dataRoot());  // the biomes' water fog colours (data/.../worldgen/biome)
    {
        std::string supportError;
        support_ = BlockSupport::loadFromGameData(resources.dataRoot(), *registry_, supportError);
        {  // blocks that fall, fire and TNT
            std::string behaviourError;
            behaviours_ = BlockBehaviours::loadFromGameData(resources.dataRoot(), *registry_, behaviourError);
            if (!behaviours_) {
                falcon::log::line("block behaviours: %s (sand will not fall, fire and TNT will not work)\n", behaviourError.c_str());
            } else {
                BlockPhysics::Host host;
                host.setBlock = [this](int x, int y, int z, Block b) { return setBlock(x, y, z, b); };
                host.sound = [this](const std::string& event, float volume, float pitch, double x, double y, double z) {
                    const std::array<double, 3> at = {x, y, z};
                    playWorldSound(event, SoundSource::Blocks, volume, pitch, &at);
                };
                host.drops = [this](Block b) {
                    if (!gameRuleOn("block_drops")) return std::vector<ItemStack>{};
                    std::mt19937 random(rng_());
                    return loot_ ? loot_->drops(b, ItemStack(), random) : std::vector<ItemStack>{};
                };
                host.player = [this] {
                    PhysicsPlayer p;
                    p.box = player_.boundingBox();
                    p.eyeY = player_.eyeY();
                    p.hurtable = survivalBars();
                    return p;
                };
                host.hurtPlayer = [this](float amount) { hurt(amount); };
                host.light = [this](int lx, int ly, int lz, int& sky, int& blockLight) { renderer_.lightAt(lx, ly, lz, sky, blockLight); };
                host.pushPlayer = [this](double dx, double dy, double dz) {
                    player_.vx += dx;
                    player_.vy += dy;
                    player_.vz += dz;
                };
                host.beginBatch = [this] { renderer_.beginBatch(); };
                host.endBatch = [this] { renderer_.endBatch(); };
                physics_ = std::make_unique<BlockPhysics>(renderer_.level(), behaviours_, itemEntities_, std::move(host), static_cast<std::uint32_t>(rng_()));
                Redstone::Host redstone;
                redstone.setBlock = [this](int x, int y, int z, Block b) { return setBlock(x, y, z, b); };
                redstone.sound = [this](const std::string& event, float volume, float pitch, double x, double y, double z) {
                    const std::array<double, 3> at = {x, y, z};
                    playWorldSound(event, SoundSource::Blocks, volume, pitch, &at);
                };
                redstone.drop = [this](const ItemStack& stack, double x, double y, double z) { itemEntities_.spawn(stack, x, y, z, 0.0, 0.1, 0.0, 10, randomFloat() * 6.2831855f); };
                redstone.primeTnt = [this](int x, int y, int z) {
                    if (physics_) physics_->primeTnt(x, y, z);
                };
                redstone.entityInside = [this](const Aabb& box, bool items) {
                    if (info_.gameType != 3 && player_.boundingBox().intersects(box)) return true;
                    if (items) {
                        for (const ItemEntity& e : itemEntities_.items()) {
                            if (!e.stack.isEmpty() && e.box().intersects(box)) return true;
                        }
                    }
                    return false;
                };
                redstone_ = std::make_unique<Redstone>(renderer_.level(), std::move(redstone), static_cast<std::uint32_t>(rng_()));
                if (const char* speed = std::getenv("FALCON_RANDOM_TICK_SPEED")) physics_->setRandomTickSpeed(std::atoi(speed));  // (tests: lava starts fires in seconds)
            }
        }
        {
            std::string commandError;
            commands_ = CommandTree::loadFromGameData(resources.dataRoot(), commandError);
            if (!commands_) falcon::log::line("commands: %s (the chat cannot run commands)\n", commandError.c_str());
        }
        {
            std::string timelineError;
            timeline_ = daycycle::Timeline::loadFromGameData(resources.dataRoot(), timelineError);
            if (!timeline_) falcon::log::line("day cycle: %s (using the approximation)\n", timelineError.c_str());
        }
        blockTags_ = std::make_unique<BlockTags>(resources.dataRoot() / "data" / "minecraft" / "tags" / "block", *registry_);
        mining_ = std::make_unique<MiningRules>(*registry_, *blockTags_);
        loot_ = std::make_unique<BlockLoot>(resources.dataRoot(), *registry_);
        {  // the mobs: what they need of the world
            MobsHost mobHost;
            mobHost.sound = [this](const std::string& event, double x, double y, double z, float volume, float pitch, bool hostile) {
                const std::array<double, 3> at = {x, y, z};
                playWorldSound(event, hostile ? SoundSource::Hostile : SoundSource::Neutral, volume, pitch, &at);
            };
            mobHost.hurtPlayer = [this](float amount, double fromX, double fromZ, float knockback) { mobHitsPlayer(amount, fromX, fromZ, knockback); };
            mobHost.explode = [this](double x, double y, double z, float power) {
                if (physics_) physics_->explode(x, y, z, power);
            };
            mobHost.dropItem = [this](ItemStack stack, double x, double y, double z) {
                if (!gameRuleOn("mob_drops")) return;
                itemEntities_.spawn(std::move(stack), x, y, z, static_cast<double>(randomFloat()) * 0.2 - 0.1, 0.2, static_cast<double>(randomFloat()) * 0.2 - 0.1, 10, randomFloat() * 6.2831855f);
            };
            mobHost.addExperience = [this](int points) { addExperience(points); };
            mobHost.burnPlayer = [this](int ticks) { burnTicks_ = std::max(burnTicks_, ticks); };
            mobHost.surfaceY = [this](int x, int z) { return renderer_.surfaceHeight(x, z); };
            mobHost.light = [this](int x, int y, int z, int& sky, int& block) { renderer_.lightAt(x, y, z, sky, block); };
            mobSim_ = std::make_unique<Mobs>(std::move(mobHost), static_cast<std::uint64_t>(info_.seed));
        }
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
        if (!support_) falcon::log::line("plant rules: %s (plants will not pop off)\n", supportError.c_str());
        std::string previewError;
        playerPreview_ = PlayerPreview::create(resources, resources.dataRoot(), previewError);
        if (!playerPreview_) falcon::log::line("player model: %s (the inventory will not show the player)\n", previewError.c_str());
        std::string bodyError;
        body_ = PlayerBodyRenderer::create(resources, resources.dataRoot(), bodyError);
        if (!body_) falcon::log::line("player model: %s (F5 will not show the player)\n", bodyError.c_str());
        std::string mobError;
        mobs_ = MobRenderer::create(resources, resources.dataRoot(), mobError);
        if (!mobs_) falcon::log::line("mobs: %s (they will not be drawn)\n", mobError.c_str());
    }
    const auto spawn = renderer_.findSpawn();
    falcon::log::line("spawn found: %.1f %.1f %.1f (%s)\n", spawn.x, spawn.y, spawn.z, overworld_ ? "game generator" : "simple terrain");
    spawn_ = {spawn.x, spawn.y, spawn.z};
    player_.setPosition(spawn.x, spawn.y, spawn.z);
    player_.canFly = info.gameType == 1 || info.gameType == 3;  // creative and spectator
    player_.spectator = info.gameType == 3;
    player_.flying = player_.spectator;
    // The world's own save: what the player changed comes back as the chunks are made, and the player stands where they left off.
    savePath_ = services.gameDir / "saves" / info.folder / "falcon_save.dat";
    renderer_.setEdits(edits_);
    lavaFluids_.setFizz([this](int fx, int fy, int fz) { fizzAt(fx, fy, fz); });
    dims_[0].generator = overworld_;
    dims_[0].decorator = renderer_.decoratorPtr();
    dims_[0].edits = edits_;
    dims_[0].visited = true;
    if (info_.madeByFalcon) loadSave();
    if (const char* start = std::getenv("FALCON_START_DIMENSION"); start != nullptr && !remote_) {  // (a test aid: begin in the Nether (1) or the End (2))
        const int d = std::atoi(start);
        if (d == 1) travelTo(1, spawn_[0] / 8.0, 70.0, spawn_[2] / 8.0, false);
        else if (d == 2) travelTo(2, 100.5, 49.0, 0.5, false);
    }
}

GameSession::~GameSession() {
    if (remote_) net::setComponentTranslator(nullptr);
    save();
}

// What loadSave() puts back: the player (where they stood, what they carried and how they were), and the items lying about. The block edits are read into
// `edits_` and applied by the renderer to every chunk it makes.
void GameSession::loadSave() {
    WorldSave saved;
    std::string error;
    if (!readWorldSave(savePath_, saved, *edits_, *registry_, error)) {
        if (!error.empty()) falcon::log::line("%s: starting from the spawn\n", error.c_str());
        return;
    }
    falcon::log::line("loaded the save: %zu edited blocks in %zu chunks, %zu items lying about\n", edits_->size(), edits_->chunkCount(), saved.items.size());
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
    storages_.fromNbt(saved.containers);
    for (const auto& [slot, stack] : saved.enderChest) enderChest_.item(slot) = stack;
    for (const auto& [rule, value] : saved.gameRules) {  // /gamerule settings come back and take effect again
        gameRules_[rule] = value;
        applyGameRule(rule, value);
    }
    if (saved.difficulty >= 0 && saved.difficulty <= 3) difficulty_ = saved.difficulty;
    stats_ = saved.stats;
    advancements_.insert(saved.advancements.begin(), saved.advancements.end());
    if (mobSim_ && saved.mobs.type == NbtTag::Type::Compound) mobSim_->fromNbt(saved.mobs);
    dayTime_ = saved.dayTime >= 0 ? saved.dayTime : daycycle::kNoon;  // (a world from before there was a clock was always noon)
    gameTime_ = saved.gameTime;
    dragonStarted_ = saved.dragonState >= 1;
    dragonDead_ = saved.dragonState >= 2;
    if ((saved.dimension == 1 || saved.dimension == 2) && saved.hasPlayer) travelTo(saved.dimension, saved.player.x, saved.player.y, saved.player.z, false);  // (back in the Nether or the End)
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
    stashDimension();  // (what is lying about and walking now goes into its dimension's own part)
    out.items = dims_[0].items;
    out.containers = dims_[0].containers;
    for (int slot = 0; slot < enderChest_.size(); ++slot) {
        if (!enderChest_.item(slot).isEmpty()) out.enderChest.emplace_back(slot, enderChest_.item(slot));
    }
    out.gameRules = gameRules_;
    out.difficulty = difficulty_;
    out.stats = stats_;
    out.advancements.assign(advancements_.begin(), advancements_.end());
    out.dayTime = dayTime_;
    out.mobs = dims_[0].mobs;
    out.dimension = dead_ ? 0 : dimension_;
    out.dragonState = dragonDead_ ? 2 : dragonStarted_ ? 1 : 0;
    out.gameTime = gameTime_;
    const std::string error = writeWorldSave(savePath_, out, *dims_[0].edits, *registry_);
    for (int d = 1; d <= 2; ++d) {  // the Nether and the End, if the player has been
        const DimensionState& dim = dims_[static_cast<std::size_t>(d)];
        if (!dim.visited || !dim.edits) continue;
        WorldSave other;
        other.items = dim.items;
        other.containers = dim.containers;
        other.mobs = dim.mobs;
        const std::string otherError = writeWorldSave(dimensionSavePath(d), other, *dim.edits, *registry_);
        if (!otherError.empty()) falcon::log::line("could not save the %s: %s\n", d == 1 ? "Nether" : "End", otherError.c_str());
    }
    if (!error.empty()) {
        falcon::log::line("could not save the world: %s\n", error.c_str());
        return;
    }
    lastSaveTicks_ = playerAgeTicks_;
    falcon::log::line("saved the world: %zu edited blocks in %zu chunks\n", edits_->size(), edits_->chunkCount());
}

int GameSession::renderDistance() const { return std::clamp(services_.options.renderDistance, kMinRenderDistance, kMaxRenderDistance); }

std::string GameSession::describe() const {
    static const char* const kModes[] = {"survival", "creative", "adventure", "spectator"};
    const RenderStats rs = renderer_.stats();
    char buffer[480];
    std::snprintf(buffer, sizeof(buffer), "world '%s' (seed %lld), %s, game mode %s, %s at %.1f %.1f %.1f, render distance %d, %d chunks loaded, %d jobs waiting, %.0f fps", info_.name.c_str(),
                  static_cast<long long>(info_.seed), dimension_ == 0 ? "Overworld" : dimension_ == 1 ? "Nether" : "End", kModes[std::clamp(info_.gameType, 0, 3)], ready_ ? "playing" : "loading", player_.x, player_.y, player_.z,
                  renderDistance(), rs.chunksLoaded, rs.jobsWaiting, fps_);
    return buffer;
}

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
    c.fov = static_cast<float>(services_.options.fov) * (fovModifierO_ + (fovModifier_ - fovModifierO_) * static_cast<float>(partialTick_));
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
        const float tilt = std::sin(left * left * left * left * 3.1415927f) * 14.0f * static_cast<float>(services_.options.damageTiltStrength);
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
    profiler::registerThread("main");  // the frame thread; the worker threads registered themselves when they started (only with FALCON_SAMPLE set)
    profiler::start();
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
    const double lookScale = usingSpyglass_ ? static_cast<double>(fovModifier_) : 1.0;
    player_.turn(dx * f3x8 * lookScale * (services_.options.invertXMouse ? -1.0 : 1.0), dy * f3x8 * lookScale * (services_.options.invertYMouse ? -1.0 : 1.0));
    if (boat_ && riding()) clampRiderYaw(boat_->yaw);  // (Entity.turn: the vehicle is told, and a boat holds the rider's head within 105 degrees)
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
    const Block old = renderer_.level().block(x, y, z);
    if (!renderer_.setBlock(x, y, z, block)) return false;
    if (remote_) return true;
    storageBlockReplaced(old, block, x, y, z);  // (a chest that goes spills what it held)  // on a server what happens to the neighbours is the server's to say (it sends the changes)
    fluids_.blockChanged(renderer_.level(), x, y, z);  // water next to it looks round again (flows into a hole, or loses its source)
    lavaFluids_.blockChanged(renderer_.level(), x, y, z);
    if (registry_->state(block).has(kStateLiquid)) fluidReact(x, y, z);  // (water in the Nether boils, lava next to water sets)
    if (physics_) physics_->blockPlaced(x, y, z);  // sand may fall, fire starts to burn
    if (!portalEdit_) {  // fire in a frame of obsidian lights a portal; a frame or a portal that is broken puts the portal out
        if (isBlockNamed(block, "fire")) tryLightPortal(x, y, z);
        else if (isBlockNamed(old, "obsidian") || isBlockNamed(old, "nether_portal")) checkPortalsAround(x, y, z);
    }
    if (redstone_) redstone_->blockChanged(x, y, z);  // power runs through what is near
    if (support_ || physics_) {
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
                Block now = support_ ? support_->afterNeighborChange(neighbour, nx, ny, nz, at) : neighbour;
                if (physics_ && now == neighbour) now = physics_->afterNeighborChange(neighbour, nx, ny, nz, at);  // (a block over a hole falls, fire follows what burns)
                if (now != neighbour && renderer_.setBlock(nx, ny, nz, now)) {
                    fluids_.blockChanged(renderer_.level(), nx, ny, nz);
                    lavaFluids_.blockChanged(renderer_.level(), nx, ny, nz);
                    pending.push_back({nx, ny, nz});
                }
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Sounds of the world

// ---------------------------------------------------------------------------------------------------------------
// Health

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
    renderLogAge_ += dt;
    if (ready_ && renderLogAge_ >= 10.0) {  // how the world draws, every ten seconds: what a log of a slow or a crashed run is read for
        renderLogAge_ = 0.0;
        const RenderStats rs = renderer_.stats();
        const LodStats ls = renderer_.lodStats();
        const auto perJob = [](const RenderStats::StageTime& t) { return t.jobs > 0 ? t.milliseconds / static_cast<double>(t.jobs) : 0.0; };
        falcon::log::info("render", "%.0f fps (%.1f ms); chunks %d, sections %d of %d drawn, %zu of %zu quads, %d draw calls, meshes %.1f MB, block data %.1f MB, textures %.2f MB, process %.0f MB",
                          fps_, frameMillis_, rs.chunksLoaded, rs.sectionsDrawn, rs.sectionsLoaded, rs.quadsDrawn, rs.quadsTotal, rs.drawCalls, static_cast<double>(rs.meshBytes) / 1048576.0,
                          static_cast<double>(rs.levelBytes) / 1048576.0, static_cast<double>(rs.textureBytes) / 1048576.0, static_cast<double>(processWorkingSetBytes()) / 1048576.0);
        falcon::log::info("chunks", "%d jobs waiting, %d in flight; per job: terrain %.1f ms, decoration %.1f ms, light %.1f ms, mesh %.1f ms; %zu cached chunks (%.1f MB)", rs.jobsWaiting, rs.jobsInFlight,
                          perJob(rs.terrainTime), perJob(rs.decorateTime), perJob(rs.lightTime), perJob(rs.meshTime), rs.cachedChunks, static_cast<double>(rs.cacheBytes) / 1048576.0);
        if (services_.options.lodsEnabled) {
            falcon::log::info("lod", "%d tiles drawn of %d loaded (%d being built), %d levels, %ld triangles, %.1f MB", ls.tilesDrawn, ls.tilesLoaded, ls.tilesBuilding, ls.levels, ls.trianglesDrawn, ls.megabytes);
        }
    }
    if (fpsAccumulated_ >= 0.5) {
        fps_ = fpsFrames_ / fpsAccumulated_;
        frameMillis_ = 1000.0 * fpsAccumulated_ / fpsFrames_;
        fpsAccumulated_ = 0.0;
        fpsFrames_ = 0;
    }

    pumpRemote();  // what a server sent since the last frame
    pumpSkins();
    openTestContainer();
    wearTestItem();
    const int distance = renderDistance();
    renderer_.setBrightness(static_cast<float>(services_.options.gamma));
    renderer_.setGpuTiming(showDebug_ || benchmark_.running);
    renderer_.setSmoothLighting(services_.options.ambientOcclusion);
    renderer_.setCutoutLeaves(services_.options.cutoutLeaves);
    renderer_.setLods({services_.options.lodsEnabled, services_.options.lodChunkRadius, services_.options.lodDetail});
    {   // A spyglass looks far, and what is far is a LOD: the chunks it looks at are made and drawn as real chunks while it is up (out to 10 times the render distance, at most 56
        // chunks), in a cone as wide as the zoomed view.
        constexpr float pi = 3.14159265358979f;
        // (Test aids: FALCON_TEST_ZOOM holds a spyglass to the eye of a pinned camera, where no tick would zoom it; FALCON_NO_ZOOM_CHUNKS leaves the cone out, to compare.)
        static const bool testZoom = std::getenv("FALCON_TEST_ZOOM") != nullptr, noZoomChunks = std::getenv("FALCON_NO_ZOOM_CHUNKS") != nullptr;
        if (testZoom && pinnedPose_) {
            usingSpyglass_ = true;
            fovModifier_ = fovModifierO_ = 0.1f;
        }
        const bool zoom = usingSpyglass_ && ready_ && perspective_ == 0 && info_.gameType != 3 && !remote_ && !noZoomChunks;
        const float yaw = static_cast<float>(player_.yaw) * pi / 180.0f;
        const float verticalFov = static_cast<float>(services_.options.fov) * fovModifier_ * pi / 180.0f;
        const float horizontalHalf = std::atan(std::tan(verticalFov * 0.5f) * lastAspect_);
        const float halfAngle = std::max(horizontalHalf, verticalFov * 0.5f) * 180.0f / pi + 5.0f;
        const int reach = std::min(60, static_cast<int>(std::lround(static_cast<double>(distance) / std::max(0.1, static_cast<double>(fovModifier_)))));
        // (FALCON_TEST_CONE=<degrees> draws a cone of real chunks of that half-width with the normal view, to put real chunks beside LODs.)
        static const char* const testCone = std::getenv("FALCON_TEST_CONE");
        float coneHalf = 0.0f, coneTurn = 0.0f;  // ("16" or "16,25": half-width, and how many degrees the cone is turned from where the camera looks)
        if (testCone != nullptr && pinnedPose_ && ready_ && std::sscanf(testCone, "%f,%f", &coneHalf, &coneTurn) >= 1) {
            const float turned = yaw + coneTurn * pi / 180.0f;
            renderer_.setZoom(player_.x, player_.z, -std::sin(turned), std::cos(turned), coneHalf, 40);
        } else renderer_.setZoom(player_.x, player_.z, -std::sin(yaw), std::cos(yaw), halfAngle, zoom ? reach : 0);
    }
    renderer_.update(player_.x, player_.z, distance, ready_ ? 1.5 : 6.0);
    if (ready_) {  // far tiles are all work for the threads: while some are being made, more of the PC makes them
        const unsigned workers = services_.options.lodsEnabled && renderer_.lodStats().tilesBuilding > 0 ? lodWorkerCount() : workerCount();
        if (workers != renderer_.activeWorkers()) renderer_.setActiveWorkers(workers);
    }
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
            completeArrival();
            renderer_.setActiveWorkers(workerCount());  // playing: about half of the PC (it all made the spawn area)
            progress_ = smoothedProgress_ = 1.0f;
            if (timedOut && !loaded) falcon::log::line("the spawn area did not finish loading in %.0f s: starting anyway\n", loadElapsed_);
            falcon::log::line("spawn area loaded in %.2f s\n", loadElapsed_);
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
            profiler::finish();
            benchmark_.endRss = processWorkingSetBytes();
            const RenderStats s = renderer_.stats();
            benchmark_.finalStats = format("%d chunks, %d sections drawn of %d, %zu quads drawn of %zu, blocks %.1f MB, meshes %.1f MB; %d draw calls for %d runs (%s, %s)",
                                           s.chunksLoaded, s.sectionsDrawn, s.sectionsLoaded, s.quadsDrawn, s.quadsTotal, megabytes(s.levelBytes),
                                           megabytes(s.meshBytes), s.drawCalls, s.drawCommands, s.multiDraw ? "multi-draw" : "one call per run",
                                           s.occlusionCulling ? "occlusion culling" : "frustum culling only");
            const auto stage = [](const char* name, const RenderStats::StageTime& t) {
                return t.jobs > 0 ? format("%s %ld x %.2f ms = %.0f ms", name, t.jobs, t.milliseconds / static_cast<double>(t.jobs), t.milliseconds) : std::string(name) + " none";
            };
            if (services_.options.lodsEnabled) {
                const LodStats lod = renderer_.lodStats();
                benchmark_.finalStats += format("; LODs: %d tiles drawn of %d (%d building), %ld triangles, %d levels, %.1f MB", lod.tilesDrawn, lod.tilesLoaded, lod.tilesBuilding,
                                                lod.trianglesDrawn, lod.levels, lod.megabytes);
            }
            benchmark_.workerStats = stage("terrain", s.terrainTime) + "; " + stage("decorate", s.decorateTime) + "; " + stage("light", s.lightTime) + "; " + stage("mesh", s.meshTime) + "; " +
                                     stage("pack", s.packTime);
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
        if (debugChord(key)) return;
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
    if (keyIs("key.drop", key) && ready_ && info_.gameType != 3 && !dead_ && remote_) {  // on a server it takes the item (DROP_ITEM, DROP_ALL_ITEMS)
        if (!inventory_.selectedItem().isEmpty()) {
            remote_->sendAction((mods & GLFW_MOD_CONTROL) != 0 ? 4 : 3);
            swingArm();
        }
    } else if (keyIs("key.drop", key) && ready_ && info_.gameType != 3 && !dead_) {
        const ItemStack thrown = inventory_.removeFromSelected((mods & GLFW_MOD_CONTROL) != 0);
        if (!thrown.isEmpty()) {
            dropFromPlayer(thrown);
            hand_.swing();
        }
    }
    // F5: first person, from behind, from the front, and round again.
    if (keyIs("key.togglePerspective", key) && ready_) perspective_ = (perspective_ + 1) % 3;
    // F swaps the held item with the offhand one.
    if (keyIs("key.swapOffhand", key) && ready_ && info_.gameType != 3) {
        if (remote_) remote_->sendAction(6);  // SWAP_ITEM_WITH_OFFHAND: the server swaps and sends the slots
        else std::swap(inventory_.selectedItem(), inventory_.offhand());
    }
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
    falcon::log::line("game mode: %s -> %s\n", kModeNames[info_.gameType], kModeNames[gameType]);
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
    // ScrollWheelHandler.onMouseScroll: the notches (all of them one, with the discrete wheel) times the sensitivity add up, and a hotbar slot is moved for each whole one.
    const double step = (services_.options.discreteMouseScroll ? (dy > 0 ? 1.0 : -1.0) : dy) * services_.options.mouseWheelSensitivity;
    if (scrollAccumulator_ != 0.0 && (step > 0) != (scrollAccumulator_ > 0)) scrollAccumulator_ = 0.0;
    scrollAccumulator_ += step;
    const int whole = static_cast<int>(scrollAccumulator_);
    if (whole == 0) return;
    scrollAccumulator_ -= whole;
    inventory_.setSelected(inventory_.selected - (whole > 0 ? 1 : -1));
}

void GameSession::addChatMessage(const std::string& text) {
    if (chatFont_ != nullptr) chat_.addMessage(text, *chatFont_, services_.options);
}

void GameSession::chatInput(const std::string& text) {
    if (text.empty()) return;
    chat_.remember(text);
    if (remote_) {  // on a server it is the server that answers
        if (text[0] == '/') remote_->sendCommand(text.substr(1));
        else remote_->sendChat(text);
        return;
    }
    if (text[0] == '/') {
        runCommand(text.substr(1));
    } else {
        addChatMessage("<" + services_.playerName + "> " + text);
    }
}

void GameSession::startUsingSpyglass() {
    if (usingSpyglass_) return;
    usingSpyglass_ = true;
    playWorldSound("item.spyglass.use", SoundSource::Players, 1.0f, 1.0f);
    addStat("used/spyglass");
}

void GameSession::stopUsingSpyglass() {
    if (!usingSpyglass_) return;
    usingSpyglass_ = false;
    playWorldSound("item.spyglass.stop_using", SoundSource::Players, 1.0f, 1.0f);
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
    if (!b.workerStats.empty()) lines.push_back("  worker time (jobs x average = total, summed over the threads): " + b.workerStats);
    return lines;
}

}  // namespace falcon
