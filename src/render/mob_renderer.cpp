#include "falcon/render/mob_renderer.hpp"

#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_manager.hpp"

#include "entity_draw.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace falcon {

namespace {

using namespace entity_draw;

// What the game gives each type of mob: the model (entity-models.json, "minecraft:<model>#main"), the picture (textures/entity/<texture>.png), how its parts move (a: a biped that
// holds its arms out, as a zombie does; b: a biped; q: a quadruped; s: only the head turns; B: a boat; A: an arrow, which flies pointing the way it goes), the size it is drawn at and the size of its box (what
// can be hit). Every row was checked against the game's own files.
struct MobSpec {
    const char* type;
    const char* model;
    const char* texture;
    char kind;
    float scale;
    float width;
    float height;
};

constexpr MobSpec kMobs[] = {
    {"zombie", "zombie", "zombie/zombie", 'a', 1.0f, 0.6f, 1.95f},
    {"husk", "husk", "zombie/husk", 'a', 1.0625f, 0.6f, 1.95f},
    {"drowned", "drowned", "zombie/drowned", 'a', 1.0f, 0.6f, 1.95f},
    {"zombified_piglin", "zombified_piglin", "piglin/zombified_piglin", 'a', 1.0f, 0.6f, 1.95f},
    {"zombie_villager", "zombie_villager", "zombie_villager/zombie_villager", 'a', 1.0f, 0.6f, 1.95f},
    {"skeleton", "skeleton", "skeleton/skeleton", 'b', 1.0f, 0.6f, 1.99f},
    {"stray", "stray", "skeleton/stray", 'b', 1.0f, 0.6f, 1.99f},
    {"bogged", "bogged", "skeleton/bogged", 'b', 1.0f, 0.6f, 1.99f},
    {"wither_skeleton", "wither_skeleton", "skeleton/wither_skeleton", 'b', 1.2f, 0.7f, 2.4f},
    {"piglin", "piglin", "piglin/piglin", 'b', 1.0f, 0.6f, 1.95f},
    {"piglin_brute", "piglin_brute", "piglin/piglin_brute", 'b', 1.0f, 0.6f, 1.95f},
    {"enderman", "enderman", "enderman/enderman", 'b', 1.0f, 0.6f, 2.9f},
    {"villager", "villager", "villager/villager", 'b', 1.0f, 0.6f, 1.95f},
    {"wandering_trader", "wandering_trader", "wandering_trader/wandering_trader", 'b', 1.0f, 0.6f, 1.95f},
    {"pillager", "pillager", "illager/pillager", 'b', 1.0f, 0.6f, 1.95f},
    {"vindicator", "vindicator", "illager/vindicator", 'b', 1.0f, 0.6f, 1.95f},
    {"evoker", "evoker", "illager/evoker", 'b', 1.0f, 0.6f, 1.95f},
    {"illusioner", "illusioner", "illager/illusioner", 'b', 1.0f, 0.6f, 1.95f},
    {"witch", "witch", "witch/witch", 'b', 1.0f, 0.6f, 1.95f},
    {"iron_golem", "iron_golem", "iron_golem/iron_golem", 'b', 1.0f, 1.4f, 2.7f},
    {"creeper", "creeper", "creeper/creeper", 'q', 1.0f, 0.6f, 1.7f},
    {"pig", "pig", "pig/pig_temperate", 'q', 1.0f, 0.9f, 0.9f},
    {"cow", "cow", "cow/cow_temperate", 'q', 1.0f, 0.9f, 1.4f},
    {"mooshroom", "mooshroom", "cow/mooshroom_red", 'q', 1.0f, 0.9f, 1.4f},
    {"sheep", "sheep", "sheep/sheep", 'q', 1.0f, 0.9f, 1.3f},
    {"wolf", "wolf", "wolf/wolf", 'q', 1.0f, 0.6f, 0.85f},
    {"horse", "horse", "horse/horse_brown", 'q', 1.0f, 1.3964844f, 1.6f},
    {"donkey", "donkey", "horse/donkey", 'q', 1.0f, 1.3964844f, 1.5f},
    {"mule", "mule", "horse/mule", 'q', 1.0f, 1.3964844f, 1.6f},
    {"goat", "goat", "goat/goat", 'q', 1.0f, 0.9f, 1.3f},
    {"fox", "fox", "fox/fox", 'q', 1.0f, 0.6f, 0.7f},
    {"ocelot", "ocelot", "cat/ocelot", 'q', 1.0f, 0.6f, 0.7f},
    {"cat", "cat", "cat/cat_tabby", 'q', 1.0f, 0.6f, 0.7f},
    {"llama", "llama", "llama/llama_brown", 'q', 1.0f, 0.9f, 1.87f},
    {"polar_bear", "polar_bear", "bear/polarbear", 'q', 1.0f, 1.4f, 1.4f},
    {"panda", "panda", "panda/panda", 'q', 1.0f, 1.3f, 1.25f},
    {"hoglin", "hoglin", "hoglin/hoglin", 'q', 1.0f, 1.3964844f, 1.4f},
    {"chicken", "chicken", "chicken/chicken_temperate", 'b', 1.0f, 0.4f, 0.7f},
    {"rabbit", "rabbit", "rabbit/rabbit_brown", 's', 1.0f, 0.4f, 0.5f},
    {"spider", "spider", "spider/spider", 's', 1.0f, 1.4f, 0.9f},
    {"cave_spider", "cave_spider", "spider/cave_spider", 's', 0.7f, 0.7f, 0.5f},
    {"bat", "bat", "bat/bat", 's', 1.0f, 0.5f, 0.9f},
    {"blaze", "blaze", "blaze/blaze", 's', 1.0f, 0.6f, 1.8f},
    {"ghast", "ghast", "ghast/ghast", 's', 4.5f, 4.0f, 4.0f},
    {"snow_golem", "snow_golem", "snow_golem/snow_golem", 's', 1.0f, 0.7f, 1.9f},
    {"silverfish", "silverfish", "silverfish/silverfish", 's', 1.0f, 0.4f, 0.3f},
    {"endermite", "endermite", "endermite/endermite", 's', 1.0f, 0.4f, 0.3f},
    {"slime", "slime", "slime/slime", 's', 1.0f, 2.04f, 2.04f},
    {"squid", "squid", "squid/squid", 's', 1.0f, 0.8f, 0.8f},
    {"turtle", "turtle", "turtle/turtle", 's', 1.0f, 1.2f, 0.4f},
    {"parrot", "parrot", "parrot/parrot_red_blue", 's', 1.0f, 0.5f, 0.9f},
    {"bee", "bee", "bee/bee", 's', 1.0f, 0.7f, 0.6f},
    {"strider", "strider", "strider/strider", 's', 1.0f, 0.9f, 1.7f},
    {"phantom", "phantom", "phantom/phantom", 's', 1.0f, 0.9f, 0.5f},
    {"warden", "warden", "warden/warden", 'b', 1.0f, 0.9f, 2.9f},
    {"oak_boat", "boat/oak", "boat/oak", 'B', 1.0f, 1.375f, 0.5625f},
    {"oak_chest_boat", "chest_boat/oak", "chest_boat/oak", 'B', 1.0f, 1.375f, 0.5625f},
    {"spruce_boat", "boat/spruce", "boat/spruce", 'B', 1.0f, 1.375f, 0.5625f},
    {"spruce_chest_boat", "chest_boat/spruce", "chest_boat/spruce", 'B', 1.0f, 1.375f, 0.5625f},
    {"birch_boat", "boat/birch", "boat/birch", 'B', 1.0f, 1.375f, 0.5625f},
    {"birch_chest_boat", "chest_boat/birch", "chest_boat/birch", 'B', 1.0f, 1.375f, 0.5625f},
    {"jungle_boat", "boat/jungle", "boat/jungle", 'B', 1.0f, 1.375f, 0.5625f},
    {"jungle_chest_boat", "chest_boat/jungle", "chest_boat/jungle", 'B', 1.0f, 1.375f, 0.5625f},
    {"acacia_boat", "boat/acacia", "boat/acacia", 'B', 1.0f, 1.375f, 0.5625f},
    {"acacia_chest_boat", "chest_boat/acacia", "chest_boat/acacia", 'B', 1.0f, 1.375f, 0.5625f},
    {"dark_oak_boat", "boat/dark_oak", "boat/dark_oak", 'B', 1.0f, 1.375f, 0.5625f},
    {"dark_oak_chest_boat", "chest_boat/dark_oak", "chest_boat/dark_oak", 'B', 1.0f, 1.375f, 0.5625f},
    {"mangrove_boat", "boat/mangrove", "boat/mangrove", 'B', 1.0f, 1.375f, 0.5625f},
    {"mangrove_chest_boat", "chest_boat/mangrove", "chest_boat/mangrove", 'B', 1.0f, 1.375f, 0.5625f},
    {"cherry_boat", "boat/cherry", "boat/cherry", 'B', 1.0f, 1.375f, 0.5625f},
    {"cherry_chest_boat", "chest_boat/cherry", "chest_boat/cherry", 'B', 1.0f, 1.375f, 0.5625f},
    {"pale_oak_boat", "boat/pale_oak", "boat/pale_oak", 'B', 1.0f, 1.375f, 0.5625f},
    {"pale_oak_chest_boat", "chest_boat/pale_oak", "chest_boat/pale_oak", 'B', 1.0f, 1.375f, 0.5625f},
    {"bamboo_raft", "boat/bamboo", "boat/bamboo", 'B', 1.0f, 1.375f, 0.5625f},
    {"bamboo_chest_raft", "chest_boat/bamboo", "chest_boat/bamboo", 'B', 1.0f, 1.375f, 0.5625f},
    {"minecart", "minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"chest_minecart", "chest_minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"furnace_minecart", "furnace_minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"hopper_minecart", "hopper_minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"tnt_minecart", "tnt_minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"spawner_minecart", "spawner_minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"command_block_minecart", "command_block_minecart", "minecart/minecart", 'M', 1.0f, 0.98f, 0.7f},
    {"armor_stand", "armor_stand", "armorstand/armorstand", 'S', 1.0f, 0.5f, 1.975f},
    {"ender_dragon", "ender_dragon", "enderdragon/dragon", 'D', 1.0f, 16.0f, 8.0f},
    {"end_crystal", "end_crystal", "end_crystal/end_crystal", 'C', 1.0f, 2.0f, 2.0f},
    {"arrow", "arrow", "projectiles/arrow", 'A', 1.0f, 0.5f, 0.5f},
    {"spectral_arrow", "arrow", "projectiles/arrow_spectral", 'A', 1.0f, 0.5f, 0.5f},
};

const MobSpec* specOf(const std::string& type) {
    for (const MobSpec& spec : kMobs) {
        if (type == spec.type) return &spec;
    }
    return nullptr;
}

// A layer of a model (a sheep's fleece) follows the limbs of the body: every part takes the pose of the part of the same name.
void copyPose(EntityPart& dst, EntityPart& whole) {
    for (EntityPart& part : dst.children) {
        if (const EntityPart* src = whole.find(part.name)) {
            part.offset = src->offset;
            part.rotation = src->rotation;
            part.scale = src->scale;
        }
    }
}

}  // namespace

bool MobRenderer::supports(const std::string& type) { return specOf(type) != nullptr; }

bool MobRenderer::sizeOf(const std::string& type, float& width, float& height) {
    const MobSpec* spec = specOf(type);
    if (spec == nullptr) return false;
    width = spec->width;
    height = spec->height;
    return true;
}

bool MobRenderer::attackable(const std::string& type) {
    const MobSpec* spec = specOf(type);
    return spec != nullptr && spec->kind != 'A';  // (an arrow in the air is not something to hit)
}

std::vector<std::string> MobRenderer::supportedTypes() {
    std::vector<std::string> out;
    for (const MobSpec& spec : kMobs) out.emplace_back(spec.type);
    return out;
}

std::unique_ptr<MobRenderer> MobRenderer::create(const ResourceManager& resources, const std::filesystem::path& gameDataDir, std::string&) {
    auto out = std::unique_ptr<MobRenderer>(new MobRenderer());
    out->resources_ = &resources;
    out->dataDir_ = gameDataDir;
    out->shader_ = ShaderProgram("mob", kVertex, kFragment);
    out->uProj_ = out->shader_.uniform("uProj");
    out->uLight_ = out->shader_.uniform("uLight");
    out->uSkin_ = out->shader_.uniform("uSkin");
    out->uTint_ = out->shader_.uniform("uTint");
    out->uHurt_ = out->shader_.uniform("uHurt");
    gl::GenVertexArrays(1, &out->vao_);
    gl::GenBuffers(1, &out->vbo_);
    gl::BindVertexArray(out->vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, out->vbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, sizeof(Vertex), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, gl::FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(5 * sizeof(float)));
    gl::BindVertexArray(0);
    return out;
}

MobRenderer::~MobRenderer() {
    if (vao_ != 0) gl::DeleteVertexArrays(1, &vao_);
    if (vbo_ != 0) gl::DeleteBuffers(1, &vbo_);
}

// The model and the picture of a type, read the first time one is wanted (a type whose files are missing is remembered as not drawable).
MobRenderer::Loaded& MobRenderer::load(const std::string& type) {
    const auto found = loaded_.find(type);
    if (found != loaded_.end()) return found->second;
    Loaded& entry = loaded_[type];
    const MobSpec* spec = specOf(type);
    if (spec == nullptr) return entry;
    std::string error;
    auto model = loadEntityModel(dataDir_, std::string("minecraft:") + spec->model + "#main", error);
    if (!model) return entry;
    const auto bytes = resources_->readBytes(ResourceLocation("minecraft", std::string("textures/entity/") + spec->texture + ".png"));
    auto image = bytes ? decodePng(*bytes) : std::nullopt;
    if (!image || image->empty()) return entry;
    entry.model = std::move(*model);
    entry.texture = std::make_unique<Texture2D>(*image, FilterMode::Nearest);
    entry.kind = spec->kind;
    entry.scale = spec->scale;
    if (type == "armor_stand") {  // the small stand has a model of its own
        std::string smallError;
        if (auto small = loadEntityModel(dataDir_, "minecraft:armor_stand_small#main", smallError)) entry.small = std::move(*small);
    }
    if (type == "sheep") {  // the fleece: a second model with its own (grey) picture, which is coloured by the sheep's dye (white here)
        std::string woolError;
        const auto woolBytes = resources_->readBytes(ResourceLocation("minecraft", "textures/entity/sheep/sheep_wool.png"));
        auto woolImage = woolBytes ? decodePng(*woolBytes) : std::nullopt;
        if (auto wool = loadEntityModel(dataDir_, "minecraft:sheep#wool", woolError); wool && woolImage && !woolImage->empty()) {
            entry.wool = std::move(*wool);
            entry.woolTexture = std::make_unique<Texture2D>(*woolImage, FilterMode::Nearest);
        }
    }
    entry.ok = true;
    return entry;
}

void MobRenderer::render(const Matrix4f& viewProj, const std::array<double, 3>& cam, const std::string& type, const MobPose& pose) {
    Loaded& loaded = load(type);
    if (!loaded.ok) return;
    EntityPart model = loaded.kind == 'S' && (pose.standFlags & 1) != 0 && loaded.small ? *loaded.small : loaded.model;  // posed on a copy
    if (loaded.kind == 'S') {  // ArmorStandModel.setupAnim: every part turned as the pose says; the sticks follow the body; arms and the base plate only when the flags say so
        const auto turn = [&](const char* name, const std::array<float, 3>& degrees) {
            if (EntityPart* part = model.find(name)) part->rotation = {degrees[0] * kDegrees, degrees[1] * kDegrees, degrees[2] * kDegrees};
        };
        turn("head", pose.standPose[0]);
        turn("body", pose.standPose[1]);
        turn("left_arm", pose.standPose[2]);
        turn("right_arm", pose.standPose[3]);
        turn("left_leg", pose.standPose[4]);
        turn("right_leg", pose.standPose[5]);
        for (const char* stick : {"right_body_stick", "left_body_stick", "shoulder_stick"}) turn(stick, pose.standPose[1]);
        const bool arms = (pose.standFlags & 4) != 0, plate = (pose.standFlags & 8) == 0;
        model.children.erase(std::remove_if(model.children.begin(), model.children.end(), [&](const EntityPart& c) { return (!arms && (c.name == "left_arm" || c.name == "right_arm")) || (!plate && c.name == "base_plate"); }),
                             model.children.end());
    }
    const float p = pose.walkPosition * 0.6662f, s = pose.walkSpeed;
    if (loaded.kind == 'a' || loaded.kind == 'b' || loaded.kind == 'q' || loaded.kind == 's') {
        if (EntityPart* head = model.find("head")) {
            head->rotation[1] = pose.headYaw * kDegrees;
            head->rotation[0] = pose.headPitch * kDegrees;
        }
    }
    if (loaded.kind == 'D') {  // EnderDragonModel.setupAnim: the wings beat about once a second and a half
        const float f = static_cast<float>(pose.ageTicks) * 0.075f * 2.0f * kPi * 0.5f / 1.0f;
        const float xr = 0.125f - std::cos(f) * 0.2f, wing = -(std::sin(f) + 0.125f) * 0.8f, tip = (std::sin(f + 2.0f) + 0.5f) * 0.75f;
        if (EntityPart* part = model.find("left_wing")) part->rotation = {xr, -0.25f, wing};
        if (EntityPart* part = model.find("left_wing_tip")) part->rotation[2] = tip;
        if (EntityPart* part = model.find("right_wing")) part->rotation = {xr, 0.25f, -wing};
        if (EntityPart* part = model.find("right_wing_tip")) part->rotation[2] = -tip;
        if (EntityPart* part = model.find("head")) part->rotation[0] = pose.headPitch * kDegrees * 0.5f;
        if (EntityPart* part = model.find("jaw")) part->rotation[0] = (std::sin(f * 0.5f) * 0.5f + 0.5f) * 0.4f;
        for (int i = 0; i < 12; ++i) {  // the tail sways, the neck follows the head
            if (EntityPart* part = model.find(("tail" + std::to_string(i)).c_str())) part->rotation[1] = std::sin(f * 0.5f - static_cast<float>(i) * 0.4f) * 0.08f;
        }
    }
    if (loaded.kind == 'C') {  // EndCrystalRenderer: the glass cubes turn and bob over the base
        const float t = static_cast<float>(pose.ageTicks);
        if (EntityPart* part = model.find("outer_glass")) {
            part->rotation[1] = t * 3.0f * kDegrees;
            part->offset[1] = 24.0f + std::sin(t * 0.2f) * 2.0f;
        }
        if (EntityPart* part = model.find("inner_glass")) part->rotation = {0.6f * std::sin(t * 0.1f), t * 3.0f * kDegrees, 0.6f * std::cos(t * 0.1f)};
        if (EntityPart* part = model.find("cube")) part->rotation = {0.6f * std::cos(t * 0.1f), t * 3.0f * kDegrees, 0.6f * std::sin(t * 0.1f)};
    }
    const float swingA = std::cos(p) * 1.4f * s, swingB = std::cos(p + kPi) * 1.4f * s;
    if (loaded.kind == 'q') {  // QuadrupedModel.setupAnim: the legs on a diagonal go together
        if (EntityPart* leg = model.find("right_hind_leg")) leg->rotation[0] = swingA;
        if (EntityPart* leg = model.find("left_hind_leg")) leg->rotation[0] = swingB;
        if (EntityPart* leg = model.find("right_front_leg")) leg->rotation[0] = swingB;
        if (EntityPart* leg = model.find("left_front_leg")) leg->rotation[0] = swingA;
    } else if (loaded.kind == 'a' || loaded.kind == 'b') {  // HumanoidModel.setupAnim
        if (EntityPart* leg = model.find("right_leg")) leg->rotation[0] = swingA;
        if (EntityPart* leg = model.find("left_leg")) leg->rotation[0] = swingB;
        EntityPart* rightArm = model.find("right_arm");
        EntityPart* leftArm = model.find("left_arm");
        const float age = static_cast<float>(pose.ageTicks);
        if (loaded.kind == 'a') {  // AnimationUtils.animateZombieArms: out in front, with the little drift of the arms
            if (rightArm != nullptr) {
                rightArm->rotation[0] = -kPi / 2.0f + std::sin(age * 0.067f) * 0.05f;
                rightArm->rotation[1] = -0.1f;
                rightArm->rotation[2] = std::cos(age * 0.09f) * 0.05f + 0.05f;
            }
            if (leftArm != nullptr) {
                leftArm->rotation[0] = -kPi / 2.0f - std::sin(age * 0.067f) * 0.05f;
                leftArm->rotation[1] = 0.1f;
                leftArm->rotation[2] = -(std::cos(age * 0.09f) * 0.05f + 0.05f);
            }
        } else {
            if (rightArm != nullptr) rightArm->rotation[0] = std::cos(p + kPi) * 2.0f * s * 0.5f;
            if (leftArm != nullptr) leftArm->rotation[0] = std::cos(p) * 2.0f * s * 0.5f;
        }
    }
    const double k = static_cast<double>(loaded.scale);
    Xf entity;
    if (loaded.kind == 'B') {  // BoatRenderer: up by 0.375, turned about Y, the model's y-down mirrored space flipped, and a quarter turn
        entity = mul(scaling({-1.0, -1.0, 1.0}), rotY(90.0 * static_cast<double>(kDegrees)));
        entity = mul(rotY((180.0 - static_cast<double>(pose.bodyYaw)) * static_cast<double>(kDegrees)), entity);
        entity = mul(translation({0.0, 0.375, 0.0}), entity);
    } else if (loaded.kind == 'M') {  // AbstractMinecartRenderer: up by 0.375, turned about Y along the rail, tilted on a slope, the model's mirrored space flipped
        entity = mul(rotZ(-static_cast<double>(pose.headPitch) * static_cast<double>(kDegrees)), scaling({-1.0, -1.0, 1.0}));
        entity = mul(rotY((180.0 - static_cast<double>(pose.bodyYaw)) * static_cast<double>(kDegrees)), entity);
        entity = mul(translation({0.0, 0.375, 0.0}), entity);
    } else if (loaded.kind == 'A') {  // ArrowRenderer: turned about Y (the yaw less a quarter turn) and tilted about Z (the pitch, up being positive): its length is along its flight
        entity = mul(rotY((static_cast<double>(pose.bodyYaw) - 90.0) * static_cast<double>(kDegrees)), rotZ(static_cast<double>(pose.headPitch) * static_cast<double>(kDegrees)));
    } else {  // LivingEntityRenderer: turned about Y, the model's y-down mirrored space flipped, dropped by 1.501 (and the size of the type)
        double sx = 1.0, sy = 1.0;
        if (pose.swell > 0.0f) {  // CreeperRenderer.scale: it swells and shivers
            const float g = std::clamp(pose.swell, 0.0f, 1.0f);
            const double shiver = 1.0 + static_cast<double>(std::sin(g * 100.0f) * g * 0.01f);
            const double f = static_cast<double>(g * g * g * g);
            sx = (1.0 + f * 0.4) * shiver;
            sy = (1.0 + f * 0.1) / shiver;
        }
        entity = mul(scaling({-k * sx, -k * sy, k * sx}), translation({0.0, -1.501, 0.0}));
        if (pose.death > 0.0f && loaded.kind != 'D') {  // LivingEntityRenderer.setupRotations: down on its side over the first second
            const float f = std::min(std::sqrt(std::min((pose.death - 1.0f) / 20.0f * 1.6f, 1.6f)), 1.0f);
            entity = mul(rotZ(static_cast<double>(f) * 90.0 * static_cast<double>(kDegrees)), entity);
        }
        entity = mul(rotY((180.0 - static_cast<double>(pose.bodyYaw)) * static_cast<double>(kDegrees)), entity);
    }
    entity = mul(translation({pose.position[0] - cam[0], pose.position[1] - cam[1], pose.position[2] - cam[2]}), entity);
    const std::array<bool, 7> all = {true, true, true, true, true, true, true};
    std::vector<Vertex> vertices;
    vertices.reserve(1024);
    collect(model, Xf{}, entity, all, vertices);
    if (vertices.empty()) return;

    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    shader_.use();
    gl::UniformMatrix4fv(uProj_, 1, 0, viewProj.m.data());
    gl::Uniform3f(uLight_, pose.light[0], pose.light[1], pose.light[2]);
    gl::Uniform1i(uSkin_, 0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto draw = [&](const std::vector<Vertex>& list, const Texture2D& texture, float r, float g, float b) {
        if (list.empty()) return;
        texture.bind(0);
        gl::Uniform3f(uTint_, r, g, b);
        gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(list.size() * sizeof(Vertex)), list.data(), gl::STREAM_DRAW);
        gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(list.size()));
    };
    // The game's red over a mob that was hit (OverlayTexture: 30% red, as long as its hurt time runs, and while it dies): the shader mixes it into what is lit.
    gl::Uniform1f(uHurt_, pose.hurt > 0.0f ? 1.0f : 0.0f);
    draw(vertices, *loaded.texture, 1.0f, 1.0f, 1.0f);
    if (loaded.wool && loaded.woolTexture) {
        EntityPart fleece = *loaded.wool;
        copyPose(fleece, model);
        std::vector<Vertex> list;
        collect(fleece, Xf{}, entity, all, list);
        draw(list, *loaded.woolTexture, 0.976f, 1.0f, 0.996f);  // (white wool: DyeColor.WHITE's 0xF9FFFE)
    }
    gl::BindVertexArray(0);
}

// The model of an item frame (block/template_item_frame: a few boxes) with its two pictures, read the first time one is drawn.
void MobRenderer::loadFrame(FrameModel& frame, bool glow) {
    frame.tried = true;
    const auto text = [&](const std::string& path) { return resources_->readText(ResourceLocation("minecraft", path)); };
    const auto own = text(std::string("models/block/") + (glow ? "glow_item_frame" : "item_frame") + ".json");
    const auto shape = text("models/block/template_item_frame.json");
    if (!own || !shape) return;
    const nlohmann::json ownJson = nlohmann::json::parse(*own, nullptr, false), shapeJson = nlohmann::json::parse(*shape, nullptr, false);
    if (!ownJson.is_object() || !shapeJson.is_object() || !ownJson.contains("textures") || !shapeJson.contains("elements")) return;
    const auto picture = [&](const char* name) -> std::unique_ptr<Texture2D> {
        if (!ownJson["textures"].contains(name) || !ownJson["textures"][name].is_string()) return nullptr;
        std::string path = ownJson["textures"][name].get<std::string>();
        if (path.compare(0, 10, "minecraft:") == 0) path.erase(0, 10);
        const auto bytes = resources_->readBytes(ResourceLocation("minecraft", "textures/" + path + ".png"));
        const auto image = bytes ? decodePng(*bytes) : std::nullopt;
        return image ? std::make_unique<Texture2D>(*image, FilterMode::Nearest) : nullptr;
    };
    frame.wood = picture("wood");
    frame.back = picture("back");
    if (!frame.wood || !frame.back) return;
    for (const nlohmann::json& element : shapeJson["elements"]) {
        const float x0 = element["from"][0].get<float>(), y0 = element["from"][1].get<float>(), z0 = element["from"][2].get<float>();
        const float x1 = element["to"][0].get<float>(), y1 = element["to"][1].get<float>(), z1 = element["to"][2].get<float>();
        for (const auto& [name, face] : element["faces"].items()) {
            FrameFace f;
            f.back = face.value("texture", std::string()) == "#back";
            const float u0 = face["uv"][0].get<float>() / 16.0f, v0 = face["uv"][1].get<float>() / 16.0f, u1 = face["uv"][2].get<float>() / 16.0f, v1 = face["uv"][3].get<float>() / 16.0f;
            // the corners as someone outside the face sees them: top left, top right, bottom right, bottom left
            std::array<std::array<float, 3>, 4> c{};
            if (name == "north") c = {{{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}}};
            else if (name == "south") c = {{{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}}};
            else if (name == "west") c = {{{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}}};
            else if (name == "east") c = {{{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}}};
            else if (name == "up") c = {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}};
            else if (name == "down") c = {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}};
            else continue;
            const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
            for (std::size_t i = 0; i < 4; ++i) f.v[i] = {c[i][0], c[i][1], c[i][2], us[i], vs[i]};
            frame.faces.push_back(f);
        }
    }
    frame.ok = !frame.faces.empty();
}

void MobRenderer::renderItemFrame(const Matrix4f& viewProj, const std::array<double, 3>& cam, bool glow, const std::array<double, 3>& position, float yaw, const Texture2D* icon, int rotation,
                                  const std::array<float, 3>& light) {
    FrameModel& frame = glow ? glowFrame_ : plainFrame_;
    if (!frame.tried) loadFrame(frame, glow);
    if (!frame.ok) return;
    // ItemFrameRenderer: the model's block is centred 0.46875 in front of the entity (the entity is the middle of the frame's plane, against the wall) and turned by 180 - yaw
    const double radians = static_cast<double>(yaw) * static_cast<double>(kDegrees);
    const Xf place = mul(translation({position[0] - cam[0] - std::sin(radians) * 0.46875, position[1] - cam[1], position[2] - cam[2] + std::cos(radians) * 0.46875}),
                         rotY((180.0 - static_cast<double>(yaw)) * static_cast<double>(kDegrees)));
    std::vector<Vertex> wood, back;
    for (const FrameFace& face : frame.faces) {
        std::vector<Vertex>& list = face.back ? back : wood;
        Vertex v[4];
        for (std::size_t i = 0; i < 4; ++i) {
            const V3 p = apply(place, {(face.v[i][0] - 8.0) / 16.0, (face.v[i][1] - 8.0) / 16.0, (face.v[i][2] - 8.0) / 16.0});
            v[i] = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), face.v[i][3], face.v[i][4], 1.0f};
        }
        list.insert(list.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
    std::vector<Vertex> item;
    if (icon != nullptr) {  // the picture of the item, half a block square, a hair in front of the back panel and turned by the frame's rotation
        const Xf turn = mul(place, rotZ(static_cast<double>(rotation & 7) * 45.0 * static_cast<double>(kDegrees)));
        const double z = 0.4475;
        const double corner[4][2] = {{0.25, 0.25}, {-0.25, 0.25}, {-0.25, -0.25}, {0.25, -0.25}};
        const float us[4] = {0.0f, 1.0f, 1.0f, 0.0f}, vs[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        Vertex v[4];
        for (std::size_t i = 0; i < 4; ++i) {
            const V3 p = apply(turn, {corner[i][0], corner[i][1], z});
            v[i] = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), us[i], vs[i], 1.0f};
        }
        item.insert(item.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    shader_.use();
    gl::UniformMatrix4fv(uProj_, 1, 0, viewProj.m.data());
    gl::Uniform3f(uLight_, light[0], light[1], light[2]);
    gl::Uniform1i(uSkin_, 0);
    gl::Uniform3f(uTint_, 1.0f, 1.0f, 1.0f);
    gl::Uniform1f(uHurt_, 0.0f);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto draw = [&](const std::vector<Vertex>& list, const Texture2D& texture) {
        if (list.empty()) return;
        texture.bind(0);
        gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(list.size() * sizeof(Vertex)), list.data(), gl::STREAM_DRAW);
        gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(list.size()));
    };
    draw(wood, *frame.wood);
    draw(back, *frame.back);
    if (icon != nullptr) draw(item, *icon);
    gl::BindVertexArray(0);
}

// A painting: the picture on a flat square of the painting's size, with the back picture on the other side. `position` is the middle of it (where the entity is), `yaw` the way it
// faces (0 faces south, a quarter turn at a time as the game turns paintings). Nothing is drawn for a picture the game's files do not have.
void MobRenderer::renderPainting(const Matrix4f& viewProj, const std::array<double, 3>& cam, const std::string& name, int widthBlocks, int heightBlocks, const std::array<double, 3>& position,
                                 float yaw, const std::array<float, 3>& light) {
    const auto textureOf = [&](const std::string& file) -> Texture2D* {
        auto it = paintingTextures_.find(file);
        if (it == paintingTextures_.end()) {
            std::unique_ptr<Texture2D> texture;
            const auto bytes = resources_->readBytes(ResourceLocation("minecraft", "textures/painting/" + file + ".png"));
            if (const auto image = bytes ? decodePng(*bytes) : std::nullopt) texture = std::make_unique<Texture2D>(*image, FilterMode::Nearest);
            it = paintingTextures_.emplace(file, std::move(texture)).first;
        }
        return it->second.get();
    };
    Texture2D* front = textureOf(name);
    if (front == nullptr || widthBlocks <= 0 || heightBlocks <= 0) return;
    Texture2D* back = textureOf("back");
    const double radians = static_cast<double>(yaw) * static_cast<double>(kDegrees);
    const double fx = -std::sin(radians), fz = std::cos(radians);   // the way it faces
    const double rx = std::cos(radians), rz = std::sin(radians);    // to the right of someone who looks at it
    const double hw = widthBlocks / 2.0, hh = heightBlocks / 2.0;
    const double cx = position[0] - cam[0], cy = position[1] - cam[1], cz = position[2] - cam[2];
    const auto quad = [&](double depth, bool flip) {
        std::vector<Vertex> v;
        const double ox = cx + fx * depth, oz = cz + fz * depth;
        const auto corner = [&](double sx, double sy, float u, float vv) {
            const double s = flip ? -sx : sx;
            return Vertex{static_cast<float>(ox + rx * s * hw), static_cast<float>(cy + sy * hh), static_cast<float>(oz + rz * s * hw), flip ? 1.0f - u : u, vv, 1.0f};
        };
        const Vertex a = corner(-1, 1, 0.0f, 0.0f), b = corner(1, 1, 1.0f, 0.0f), c = corner(1, -1, 1.0f, 1.0f), d = corner(-1, -1, 0.0f, 1.0f);
        v.insert(v.end(), {a, b, c, a, c, d});
        return v;
    };
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    shader_.use();
    gl::UniformMatrix4fv(uProj_, 1, 0, viewProj.m.data());
    gl::Uniform3f(uLight_, light[0], light[1], light[2]);
    gl::Uniform1i(uSkin_, 0);
    gl::Uniform3f(uTint_, 1.0f, 1.0f, 1.0f);
    gl::Uniform1f(uHurt_, 0.0f);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto draw = [&](const std::vector<Vertex>& list, const Texture2D& texture) {
        texture.bind(0);
        gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(list.size() * sizeof(Vertex)), list.data(), gl::STREAM_DRAW);
        gl::DrawArrays(gl::TRIANGLES, 0, static_cast<gl::GLsizei>(list.size()));
    };
    draw(quad(0.03125, false), *front);                       // the picture, on the front face (1/32 of a block out)
    if (back != nullptr) draw(quad(-0.03125, true), *back);   // the back
    gl::BindVertexArray(0);
}

}  // namespace falcon
