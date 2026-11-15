#include "falcon/render/mob_renderer.hpp"

#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_manager.hpp"

#include "entity_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace falcon {

namespace {

using namespace entity_draw;

// What the game gives each type of mob: the model (entity-models.json, "minecraft:<model>#main"), the picture (textures/entity/<texture>.png), how its parts move (a: a biped that
// holds its arms out, as a zombie does; b: a biped; q: a quadruped; s: only the head turns) and the size it is drawn at. Every row was checked against the game's own files.
struct MobSpec {
    const char* type;
    const char* model;
    const char* texture;
    char kind;
    float scale;
};

constexpr MobSpec kMobs[] = {
    {"zombie", "zombie", "zombie/zombie", 'a', 1.0f},
    {"husk", "husk", "zombie/husk", 'a', 1.0625f},
    {"drowned", "drowned", "zombie/drowned", 'a', 1.0f},
    {"zombified_piglin", "zombified_piglin", "piglin/zombified_piglin", 'a', 1.0f},
    {"zombie_villager", "zombie_villager", "zombie_villager/zombie_villager", 'a', 1.0f},
    {"skeleton", "skeleton", "skeleton/skeleton", 'b', 1.0f},
    {"stray", "stray", "skeleton/stray", 'b', 1.0f},
    {"bogged", "bogged", "skeleton/bogged", 'b', 1.0f},
    {"wither_skeleton", "wither_skeleton", "skeleton/wither_skeleton", 'b', 1.2f},
    {"piglin", "piglin", "piglin/piglin", 'b', 1.0f},
    {"piglin_brute", "piglin_brute", "piglin/piglin_brute", 'b', 1.0f},
    {"enderman", "enderman", "enderman/enderman", 'b', 1.0f},
    {"villager", "villager", "villager/villager", 'b', 1.0f},
    {"wandering_trader", "wandering_trader", "wandering_trader/wandering_trader", 'b', 1.0f},
    {"pillager", "pillager", "illager/pillager", 'b', 1.0f},
    {"vindicator", "vindicator", "illager/vindicator", 'b', 1.0f},
    {"evoker", "evoker", "illager/evoker", 'b', 1.0f},
    {"illusioner", "illusioner", "illager/illusioner", 'b', 1.0f},
    {"witch", "witch", "witch/witch", 'b', 1.0f},
    {"iron_golem", "iron_golem", "iron_golem/iron_golem", 'b', 1.0f},
    {"creeper", "creeper", "creeper/creeper", 'q', 1.0f},
    {"pig", "pig", "pig/pig_temperate", 'q', 1.0f},
    {"cow", "cow", "cow/cow_temperate", 'q', 1.0f},
    {"mooshroom", "mooshroom", "cow/mooshroom_red", 'q', 1.0f},
    {"sheep", "sheep", "sheep/sheep", 'q', 1.0f},
    {"wolf", "wolf", "wolf/wolf", 'q', 1.0f},
    {"horse", "horse", "horse/horse_brown", 'q', 1.0f},
    {"donkey", "donkey", "horse/donkey", 'q', 1.0f},
    {"mule", "mule", "horse/mule", 'q', 1.0f},
    {"goat", "goat", "goat/goat", 'q', 1.0f},
    {"fox", "fox", "fox/fox", 'q', 1.0f},
    {"ocelot", "ocelot", "cat/ocelot", 'q', 1.0f},
    {"cat", "cat", "cat/cat_tabby", 'q', 1.0f},
    {"llama", "llama", "llama/llama_brown", 'q', 1.0f},
    {"polar_bear", "polar_bear", "bear/polarbear", 'q', 1.0f},
    {"panda", "panda", "panda/panda", 'q', 1.0f},
    {"hoglin", "hoglin", "hoglin/hoglin", 'q', 1.0f},
    {"chicken", "chicken", "chicken/chicken_temperate", 'b', 1.0f},
    {"rabbit", "rabbit", "rabbit/rabbit_brown", 's', 1.0f},
    {"spider", "spider", "spider/spider", 's', 1.0f},
    {"cave_spider", "cave_spider", "spider/cave_spider", 's', 0.7f},
    {"bat", "bat", "bat/bat", 's', 1.0f},
    {"blaze", "blaze", "blaze/blaze", 's', 1.0f},
    {"ghast", "ghast", "ghast/ghast", 's', 4.5f},
    {"snow_golem", "snow_golem", "snow_golem/snow_golem", 's', 1.0f},
    {"silverfish", "silverfish", "silverfish/silverfish", 's', 1.0f},
    {"endermite", "endermite", "endermite/endermite", 's', 1.0f},
    {"slime", "slime", "slime/slime", 's', 1.0f},
    {"squid", "squid", "squid/squid", 's', 1.0f},
    {"turtle", "turtle", "turtle/turtle", 's', 1.0f},
    {"parrot", "parrot", "parrot/parrot_red_blue", 's', 1.0f},
    {"bee", "bee", "bee/bee", 's', 1.0f},
    {"strider", "strider", "strider/strider", 's', 1.0f},
    {"phantom", "phantom", "phantom/phantom", 's', 1.0f},
    {"warden", "warden", "warden/warden", 'b', 1.0f},
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
    EntityPart model = loaded.model;  // posed on a copy
    const float p = pose.walkPosition * 0.6662f, s = pose.walkSpeed;
    if (EntityPart* head = model.find("head")) {
        head->rotation[1] = pose.headYaw * kDegrees;
        head->rotation[0] = pose.headPitch * kDegrees;
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
    // LivingEntityRenderer: to the mob's place, turned about Y, the model's y-down mirrored space flipped, dropped by 1.501 (and the size of the type).
    const double k = static_cast<double>(loaded.scale);
    Xf entity = mul(scaling({-k, -k, k}), translation({0.0, -1.501, 0.0}));
    entity = mul(rotY((180.0 - static_cast<double>(pose.bodyYaw)) * static_cast<double>(kDegrees)), entity);
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

}  // namespace falcon
