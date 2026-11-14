#pragma once

#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/render/entity_model.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace falcon {

class ResourceManager;

// How the player's body stands and moves this frame (what the game's player renderer and PlayerModel.setupAnim work from).
struct BodyPose {
    std::array<double, 3> position{};  // the feet, in world coordinates
    float bodyYaw = 0.0f;              // degrees: where the body faces (it follows the movement, the head turns against it)
    float headYaw = 0.0f;              // the head's turn against the body, degrees
    float headPitch = 0.0f;            // degrees, positive looks down
    float walkPosition = 0.0f;         // the walk cycle (limb swing) and how hard the limbs swing (0..1)
    float walkSpeed = 0.0f;
    float attackProgress = 0.0f;       // 0..1 through the swing of the right arm; 0 = not swinging
    bool crouching = false;
    double ageTicks = 0.0;             // the arms sway a little as they do in the game
    std::array<float, 3> light{1.0f, 1.0f, 1.0f};  // the light colour where the player stands
    // Cape, Jacket, Left Sleeve, Right Sleeve, Left Pant Leg, Right Pant Leg, Hat: the Skin Customization options.
    std::array<bool, 7> parts = {true, true, true, true, true, true, true};
};

// The player seen from outside (F5): the game's real player model (entity-models.json) in the default skin, in the world, animated the way the
// game animates a humanoid: head following the view, arms and legs swinging as it walks, the crouching pose, the swing of an attack.
class PlayerBodyRenderer {
public:
    // Null (and `error`) when the model or the skin cannot be read.
    [[nodiscard]] static std::unique_ptr<PlayerBodyRenderer> create(const ResourceManager& resources, const std::filesystem::path& gameDataDir, std::string& error);
    ~PlayerBodyRenderer();
    PlayerBodyRenderer(const PlayerBodyRenderer&) = delete;
    PlayerBodyRenderer& operator=(const PlayerBodyRenderer&) = delete;

    // Draws the body into the frame that was just drawn; `viewProj` is that frame's view and projection, `cam` where its camera is.
    void render(const Matrix4f& viewProj, const std::array<double, 3>& cam, const BodyPose& pose);

private:
    PlayerBodyRenderer() = default;
    EntityPart model_;
    Texture2D skin_;
    ShaderProgram shader_;
    gl::GLint uProj_ = -1, uLight_ = -1, uSkin_ = -1;
    gl::GLuint vao_ = 0, vbo_ = 0;
};

}  // namespace falcon
