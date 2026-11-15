#pragma once

#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/render/entity_model.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace falcon {

class ResourceManager;

// How a mob stands and moves this frame.
struct MobPose {
    std::array<double, 3> position{};  // the feet, in world coordinates
    float bodyYaw = 0.0f;              // degrees
    float headYaw = 0.0f;              // the head's turn against the body, degrees
    float headPitch = 0.0f;            // degrees, positive looks down
    float walkPosition = 0.0f;         // the walk cycle and how hard the legs swing (0..1)
    float walkSpeed = 0.0f;
    double ageTicks = 0.0;
    // An armor stand: its flags (1 small, 4 has arms, 8 no base plate) and the pose of head, body, left arm, right arm, left leg and right leg in degrees (x, y, z); the defaults are what the
    // game gives a stand that was not posed.
    int standFlags = 0;
    std::array<std::array<float, 3>, 6> standPose{{{0, 0, 0}, {0, 0, 0}, {-10, 0, -10}, {-10, 0, 10}, {-1, 0, -1}, {1, 0, 1}}};
    std::array<float, 3> light{1.0f, 1.0f, 1.0f};  // the light colour where the mob stands
};

// The mobs of a server, drawn from the game's own models (entity-models.json) and pictures (textures/entity): the mob types in the table of mob_renderer.cpp (about fifty of
// them, the ones people meet), each with the model and picture the game gives it, and the way its parts move: a biped swings arms and legs (a zombie holds its arms out), a
// quadruped its four legs, the rest only turn their head. A type that is not in the table is not drawn. A model and its picture are loaded the first time a mob of that type is
// seen.
class MobRenderer {
public:
    // Null (and `error`) when the shader cannot be made.
    [[nodiscard]] static std::unique_ptr<MobRenderer> create(const ResourceManager& resources, const std::filesystem::path& gameDataDir, std::string& error);
    ~MobRenderer();
    MobRenderer(const MobRenderer&) = delete;
    MobRenderer& operator=(const MobRenderer&) = delete;

    // Whether mobs of this type ("zombie", without the namespace) can be drawn.
    [[nodiscard]] static bool supports(const std::string& type);
    [[nodiscard]] static std::vector<std::string> supportedTypes();
    // The box of a type in blocks (width and depth, height); false for a type that is not in the table.
    [[nodiscard]] static bool sizeOf(const std::string& type, float& width, float& height);
    // Whether a mob of this type can be hit (everything but arrows in the air).
    [[nodiscard]] static bool attackable(const std::string& type);

    // Draws a mob into the frame that was just drawn; `viewProj` is that frame's view and projection, `cam` where its camera is. Nothing happens for a type that is not supported
    // or whose files are missing.
    void render(const Matrix4f& viewProj, const std::array<double, 3>& cam, const std::string& type, const MobPose& pose);
    // An item frame: its wooden frame (the glow one has a different back) with the picture of what is in it, turned by `rotation` eighths of a turn; `position` is where the entity is
    // (the middle of the frame's plane), `yaw` the way it faces (0 is south). `icon` may be null for an empty frame.
    void renderItemFrame(const Matrix4f& viewProj, const std::array<double, 3>& cam, bool glow, const std::array<double, 3>& position, float yaw, const Texture2D* icon, int rotation,
                         const std::array<float, 3>& light);
    // A painting: the picture ("kebab": textures/painting/kebab.png) on a square of widthBlocks x heightBlocks, its middle at `position`, facing `yaw` (0 is south).
    void renderPainting(const Matrix4f& viewProj, const std::array<double, 3>& cam, const std::string& name, int widthBlocks, int heightBlocks, const std::array<double, 3>& position, float yaw,
                        const std::array<float, 3>& light);

private:
    MobRenderer() = default;
    struct Loaded {
        bool ok = false;
        EntityPart model;
        std::optional<EntityPart> wool;  // a sheep's fleece, a second model on top
        std::optional<EntityPart> small;  // an armor stand's small model
        std::unique_ptr<Texture2D> texture, woolTexture;
        char kind = 's';
        float scale = 1.0f;
    };
    Loaded& load(const std::string& type);

    const ResourceManager* resources_ = nullptr;
    std::filesystem::path dataDir_;
    std::map<std::string, Loaded> loaded_;
    struct FrameFace {
        std::array<std::array<float, 5>, 4> v;  // x, y, z (in sixteenths, 0..16 across the block the frame is in) and u, v (0..1)
        bool back = false;                      // the picture of the back panel rather than the wood
    };
    struct FrameModel {
        bool tried = false, ok = false;
        std::vector<FrameFace> faces;
        std::unique_ptr<Texture2D> wood, back;
    };
    void loadFrame(FrameModel& frame, bool glow);
    FrameModel plainFrame_, glowFrame_;
    std::map<std::string, std::unique_ptr<Texture2D>> paintingTextures_;  // by file name; null for one the game does not have
    ShaderProgram shader_;
    gl::GLint uProj_ = -1, uLight_ = -1, uSkin_ = -1, uTint_ = -1;
    gl::GLuint vao_ = 0, vbo_ = 0;
};

}  // namespace falcon
