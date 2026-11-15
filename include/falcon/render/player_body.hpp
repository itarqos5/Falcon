#pragma once

#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/render/entity_model.hpp"

#include <array>
#include <filesystem>
#include <map>
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
    // The skin: -1 is the plain Steve (wide arms); 0-8 are the nine default skins with slim arms and 9-17 the same with wide arms, in the order of the game's own
    // PlayerSkin.DEFAULT_SKINS (alex, ari, efe, kai, makena, noor, steve, sunny, zuri), which is how a player without a skin of their own gets one.
    int skin = -1;
    // A skin of the player's own (a picture that was downloaded) and whether it is for slim arms; it takes the place of the default skin above.
    const Texture2D* customSkin = nullptr;
    bool customSlim = false;
    double ageTicks = 0.0;             // the arms sway a little as they do in the game
    std::array<float, 3> light{1.0f, 1.0f, 1.0f};  // the light colour where the player stands
    // Cape, Jacket, Left Sleeve, Right Sleeve, Left Pant Leg, Right Pant Leg, Hat: the Skin Customization options.
    std::array<bool, 7> parts = {true, true, true, true, true, true, true};
    // Swimming (Player.getSwimAmount: 0 to 1 and back at 0.09 a tick): the body lies down and the arms crawl.
    float swimAmount = 0.0f;
    bool visuallySwimming = false;  // the swimming pose (not just a swimAmount that is running down)
    bool inWater = false;
    // What is worn, as item names ("iron_helmet"): head, chest, legs, feet; empty for nothing.
    std::array<std::string, 4> armor;
    // Something is held in the right and the left hand: the arm is then raised a little (HumanoidModel.ArmPose.ITEM).
    bool rightArmItem = false;
    bool leftArmItem = false;
};

// Where the hands are after a body was drawn: each arm's place in the camera-relative world as a 4x4 matrix (column-major), the start of what ItemInHandLayer puts an item on.
struct BodyHands {
    std::array<double, 16> right{};
    std::array<double, 16> left{};
    bool valid = false;
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
    void render(const Matrix4f& viewProj, const std::array<double, 3>& cam, const BodyPose& pose, BodyHands* hands = nullptr);

private:
    PlayerBodyRenderer() = default;
    EntityPart model_;
    EntityPart slimModel_;  // the same with arms of three pixels (the slim skins)
    bool haveSlim_ = false;
    std::array<EntityPart, 4> armorModelsSlim_;
    std::array<bool, 4> haveArmorSlim_{};
    std::map<int, std::unique_ptr<Texture2D>> skins_;  // the default skins, loaded when a player wears one; null for one that cannot be read
    const Texture2D& skinTexture(int index);
    std::array<EntityPart, 4> armorModels_;  // helmet, chestplate, leggings, boots (the armor layers of the player model)
    std::array<bool, 4> haveArmorModel_{};
    std::map<std::string, std::unique_ptr<Texture2D>> armorTextures_;  // by "humanoid/iron"; null for one that cannot be read
    const ResourceManager* resources_ = nullptr;
    const Texture2D* armorTexture(const std::string& layer, const std::string& material);
    Texture2D skin_;
    ShaderProgram shader_;
    gl::GLint uProj_ = -1, uLight_ = -1, uSkin_ = -1, uTint_ = -1;
    gl::GLuint vao_ = 0, vbo_ = 0;
};

}  // namespace falcon
