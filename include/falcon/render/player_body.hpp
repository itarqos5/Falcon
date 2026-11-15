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

// How an arm is held (HumanoidModel.ArmPose): by what is in the hand and what the player is doing with it.
enum class ArmPose : std::uint8_t { Empty, Item, Block, BowAndArrow, ThrowTrident, Spyglass, TootHorn, Brush, CrossbowHold, CrossbowCharge };

// How the player's body stands and moves this frame (what the game's player renderer and PlayerModel.setupAnim work from).
struct BodyPose {
    std::array<double, 3> position{};  // the feet, in world coordinates
    float bodyYaw = 0.0f;              // degrees: where the body faces (it follows the movement, the head turns against it)
    float headYaw = 0.0f;              // the head's turn against the body, degrees
    float headPitch = 0.0f;            // degrees, positive looks down
    float walkPosition = 0.0f;         // the walk cycle (limb swing) and how hard the limbs swing (0..1)
    float walkSpeed = 0.0f;
    float attackProgress = 0.0f;       // 0..1 through the swing of the right arm; 0 = not swinging
    // Sleeping in a bed (LivingEntityRenderer.submit / setupRotations): the body lies on its back along the bed, turned by sleepYaw degrees (from the way the bed faces), and moved back
    // along it so that the head is at the pillow (bedStep is the way the bed faces; hasBed false: no bed was found, the body's own yaw is used and nothing is moved).
    bool sleeping = false;
    float sleepYaw = 0.0f;
    bool hasBed = false;
    int bedStepX = 0, bedStepZ = 0;
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
    // How the right and the left arm are held (the arm that holds the main hand's item is the main arm: the left one for a left-handed player).
    ArmPose rightArmPose = ArmPose::Empty;
    ArmPose leftArmPose = ArmPose::Empty;
    float crossbowTicks = 0.0f;  // how long a crossbow has been drawn (CrossbowCharge) and how long it takes (ticks)
    float crossbowMax = 25.0f;
    bool leftHanded = false;     // the main arm is the left one: it is the one that swings
    bool swingOffHand = false;   // the swing in progress is of the off hand: the other arm
    bool riding = false;         // sitting on a boat, a minecart or a mount: the legs go forward
    bool fallFlying = false;     // gliding with elytra: the head looks along the flight, the limbs swing less (limbScale) and the body leans (glideLean degrees about X, then glideTurn radians about Y)
    float glideLean = 0.0f;
    float glideTurn = 0.0f;
    float limbScale = 1.0f;
    float hurt = 0.0f;           // 1 while the red flash of a hit shows
    float deathAngle = 0.0f;     // how far a dying body has fallen over, degrees
    // The cape (the account's, a 64x32 picture): null for none; how it hangs and swings (AvatarRenderState.capeFlap / capeLean / capeLean2, degrees). It is not drawn over elytra.
    const Texture2D* cape = nullptr;
    float capeFlap = 0.0f, capeLean = 0.0f, capeLean2 = 0.0f;
    bool elytra = false;         // elytra are worn: the wings are drawn, held as the three angles say (ElytraAnimationState)
    float elytraX = 0.2617994f, elytraY = 0.0f, elytraZ = -0.2617994f;
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
    EntityPart capeModel_;          // the cape (minecraft:player#cape)
    bool haveCape_ = false;
    EntityPart elytraModel_;        // the wings (minecraft:elytra#main)
    bool haveElytra_ = false;
    std::unique_ptr<Texture2D> elytraTexture_;
    bool elytraTextureTried_ = false;
    const Texture2D* elytraTexture();
    gl::GLint uHurt_ = -1;
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
