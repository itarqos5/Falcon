// GameSession: the dimensions. The Overworld, the Nether and the End each have a generator, the blocks the player changed in them, the items, mobs and containers that are in them, and a
// file of their own; the player is in one of them at a time, and travelling to another puts the loading screen up while its chunks are made.
#include "falcon/game/game_session.hpp"
#include "game_session_internal.hpp"

#include "falcon/render/hand_renderer.hpp"
#include "falcon/world/nether_decoration.hpp"
#include "falcon/world/world_save.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace falcon {
namespace {
constexpr const char* kSection = "Â§";
}  // namespace

const worldgen::OverworldGenerator* GameSession::activeGenerator() const { return dims_[static_cast<std::size_t>(dimension_)].generator.get(); }

std::filesystem::path GameSession::dimensionSavePath(int dimension) const {
    return savePath_.parent_path() / (dimension == 1 ? "falcon_save_nether.dat" : "falcon_save_end.dat");
}

// The generator, the decoration and the edits of a dimension, made the first time it is needed. False when the game's data for it cannot be read.
bool GameSession::prepareDimension(int dimension) {
    DimensionState& dim = dims_[static_cast<std::size_t>(dimension)];
    if (dim.generator) return true;
    worldgen::GeneratorOptions options;
    options.noiseSettings = dimension == 1 ? "minecraft:nether" : "minecraft:end";
    options.biomePreset = dimension == 1 ? "minecraft:nether" : "minecraft:the_end";
    std::string error;
    std::unique_ptr<worldgen::OverworldGenerator> generator = worldgen::OverworldGenerator::create(dataRoot_, *registry_, info_.seed, error, options);
    if (!generator) {
        std::printf("[falcon] the %s could not be made (%s)\n", dimension == 1 ? "Nether" : "End", error.c_str());
        return false;
    }
    dim.generator = std::move(generator);
    const worldgen::OverworldGenerator* self = dim.generator.get();
    auto decoration = std::make_shared<const NetherDecorator>(*self, *registry_, info_.seed, dimension);
    dim.decorate = [decoration](Chunk& chunk, int cx, int cz, const worldgen::ChunkExtras& extras) { decoration->decorate(chunk, cx, cz, extras); };
    dim.edits = std::make_shared<WorldEdits>();
    // What the player left there before.
    WorldSave saved;
    std::string readError;
    if (info_.madeByFalcon && readWorldSave(dimensionSavePath(dimension), saved, *dim.edits, *registry_, readError)) {
        dim.items = std::move(saved.items);
        dim.containers = std::move(saved.containers);
        dim.mobs = std::move(saved.mobs);
    }
    return true;
}

// What is lying, walking and stored in the dimension the player is in, put away with its dimension.
void GameSession::stashDimension() {
    DimensionState& dim = dims_[static_cast<std::size_t>(dimension_)];
    dim.items = itemEntities_.items();
    dim.containers = storages_.toNbt();
    if (mobSim_) dim.mobs = mobSim_->toNbt();
    dim.visited = true;
}

void GameSession::travelTo(int dimension, double x, double y, double z, bool portal) {
    if (remote_ || dimension < 0 || dimension > 2 || (dimension == dimension_ && !arrival_.pending)) return;
    if (dimension != 0 && !prepareDimension(dimension)) {
        addChatMessage(std::string(kSection) + "cThe game's data for that dimension cannot be read");
        return;
    }
    if (dimension == dimension_) return;
    stashDimension();
    dimension_ = dimension;
    DimensionState& dim = dims_[static_cast<std::size_t>(dimension)];
    itemEntities_.clear();
    storages_.fromNbt(dim.containers);
    if (dim.containers.type != NbtTag::Type::Compound) storages_.clear();
    for (const ItemEntity& item : dim.items) itemEntities_.items().push_back(item);
    if (physics_) physics_->reset();
    fluids_.reset();
    lavaFluids_.reset();
    lavaFluids_.setUltrawarm(dimension == 1);  // (the Nether's lava flows fast and far)
    fluidTouched_.clear();
    if (mobSim_) {
        mobSim_->clear();
        if (dim.mobs.type == NbtTag::Type::Compound) mobSim_->fromNbt(dim.mobs);
    }
    WorldRenderer::DimensionWorld world;
    world.generator = dim.generator;
    world.decorator = dimension == 0 ? dim.decorator : nullptr;
    world.decorate = dim.decorate;
    world.edits = dim.edits;
    world.skylight = dimension != 1;
    world.lods = dimension == 0;
    renderer_.enterDimension(std::move(world));
    renderer_.setEdits(dim.edits);
    edits_ = dim.edits;  // (the world's edits as far as the rest of the session goes: the debug screen, the saving)
    player_.setPosition(x, y, z);
    player_.vx = player_.vy = player_.vz = 0.0;
    player_.fallDistance = 0.0;
    // The loading screen, as the game puts up the "Loading terrain" screen on the way through a portal.
    ready_ = false;
    loadElapsed_ = 0.0;
    progress_ = smoothedProgress_ = 0.0f;
    renderer_.setActiveWorkers(loadWorkerCount());
    arrival_ = {true, dimension, portal, x, y, z};
    portalTicks_ = 0;
    portalOverlay_ = 0.0f;
    portalLocked_ = true;
    fogSmoothSet_ = false;  // (the first fog of a dimension is the right one at once)
    std::printf("[falcon] travelling to the %s at %.1f %.1f %.1f\n", dimension == 0 ? "Overworld" : dimension == 1 ? "Nether" : "End", x, y, z);
}

// A teleport far from the loaded area (/tp): the loading screen holds the player, as the game waits for the chunks of a place it sends someone to, so that they do not fall through
// what has not been made yet.
void GameSession::holdForLoading() {
    if (remote_ || !ready_) return;
    const auto eye = player_.eye();
    if (renderer_.sectionReady(static_cast<int>(std::floor(player_.x)), static_cast<int>(std::floor(eye[1])), static_cast<int>(std::floor(player_.z)))) return;
    ready_ = false;
    loadElapsed_ = 0.0;
    progress_ = smoothedProgress_ = 0.0f;
    renderer_.setActiveWorkers(loadWorkerCount());
}

// The new area has loaded: the player is put where they belong (at the portal that is there, or one made for them; on the End's platform).
void GameSession::completeArrival() {
    if (!arrival_.pending) return;
    arrival_.pending = false;
    const int dimension = arrival_.dimension;
    if (dimension == 2) {
        placeEndPlatform();
    } else if (arrival_.portal) {
        arriveByPortal(arrival_.x, arrival_.y, arrival_.z);
    } else {  // arriving where the world says (the saved place, /execute in ...): out of the ground if it is inside it
        liftOutOfBlocks();
    }
    if (dimension == 2) startDragonFight();
}

// The sky the camera sees in the Nether and the End: no sun, no moon; a fog colour that comes from the biome in the Nether and is fixed in the End.
daycycle::Sky GameSession::dimensionSky(const Camera& cam) const {
    daycycle::Sky sky;
    sky.sunDir = {0.0f, 0.0f, 0.0f};
    sky.stars = 0.0f;
    sky.glow = {0.0f, 0.0f, 0.0f, 0.0f};
    sky.darkSteps = 0.0f;
    if (dimension_ == 1) {
        // The fog colour of a biome (its fog_color attribute), averaged over the biomes within sixteen blocks of the camera, and then eased towards over a couple of seconds, so
        // that walking from one biome into the next changes the colour of the air slowly, as the game's biome blend does, and not at the line between them.
        const auto colorOf = [](std::string_view biome) {
            if (biome == "minecraft:crimson_forest") return std::array<float, 3>{0.2f, 0.012f, 0.012f};
            if (biome == "minecraft:warped_forest") return std::array<float, 3>{0.102f, 0.02f, 0.102f};
            if (biome == "minecraft:soul_sand_valley") return std::array<float, 3>{0.106f, 0.278f, 0.278f};
            if (biome == "minecraft:basalt_deltas") return std::array<float, 3>{0.408f, 0.373f, 0.439f};
            return std::array<float, 3>{0.2f, 0.03f, 0.03f};  // #330808: the wastes
        };
        std::array<float, 3> target{0.2f, 0.03f, 0.03f};
        if (const worldgen::OverworldGenerator* gen = activeGenerator()) {
            std::array<double, 3> sum{0.0, 0.0, 0.0};
            int count = 0;
            for (int dz = -16; dz <= 16; dz += 16) {
                for (int dx = -16; dx <= 16; dx += 16) {
                    const std::array<float, 3> c = colorOf(gen->biomeNameAt(static_cast<int>(std::floor(cam.x)) + dx, static_cast<int>(std::floor(cam.y)), static_cast<int>(std::floor(cam.z)) + dz));
                    for (std::size_t i = 0; i < 3; ++i) sum[i] += static_cast<double>(c[i]);
                    ++count;
                }
            }
            for (std::size_t i = 0; i < 3; ++i) target[i] = static_cast<float>(sum[i] / count);
        }
        const auto now = std::chrono::steady_clock::now();
        if (!fogSmoothSet_) {
            fogSmooth_ = target;
            fogSmoothSet_ = true;
        } else {
            const double seconds = std::clamp(std::chrono::duration<double>(now - fogSmoothAt_).count(), 0.0, 0.5);
            const float blend = static_cast<float>(1.0 - std::exp(-seconds / 2.0));
            for (std::size_t i = 0; i < 3; ++i) fogSmooth_[i] += (target[i] - fogSmooth_[i]) * blend;
        }
        fogSmoothAt_ = now;
        const std::array<float, 3> fog = fogSmooth_;
        sky.horizon = sky.zenith = fog;
        sky.skyLightFactor = 0.0f;
        sky.skyLightColor = {0.478f, 0.478f, 1.0f};  // #7a7aff
        sky.ambient = {0.188f, 0.157f, 0.129f};      // #302821
    } else {
        sky.horizon = {0.094f, 0.075f, 0.094f};  // #181318
        sky.zenith = {0.0f, 0.0f, 0.0f};
        sky.stars = 0.5f;  // (the End's sky is a picture of stars and a purple haze: this is a dark sky with the stars of the night)
        sky.sunDir = {0.0f, 0.0f, 0.0f};
        sky.skyLightFactor = 0.0f;
        sky.skyLightColor = {0.675f, 0.376f, 0.804f};  // #ac60cd
        sky.ambient = {0.247f, 0.278f, 0.247f};        // #3f473f
        sky.sinAngle = 0.0f;
        sky.cosAngle = 1.0f;
    }
    return sky;
}

}  // namespace falcon
