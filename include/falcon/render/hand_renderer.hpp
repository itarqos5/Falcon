#pragma once

#include "falcon/assets/resource_manager.hpp"
#include "falcon/gfx/shader.hpp"
#include "falcon/gfx/texture.hpp"
#include "falcon/render/block_models.hpp"
#include "falcon/render/camera.hpp"
#include "falcon/render/item_icons.hpp"
#include "falcon/world/block_physics.hpp"
#include "falcon/world/item_entity.hpp"
#include "falcon/world/item_stack.hpp"
#include "falcon/world/mesher.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace falcon {

class WorldRenderer;

// The game's light map colour for a sky and a block light level (0..15) and the Brightness option: what the world's shader multiplies a surface by.
[[nodiscard]] std::array<float, 3> entityLightColor(int sky, int block, float gamma);
// What the sky does to the light of everything drawn apart from the world meshes (its factor and colour, the ambient light): set each frame from the day cycle.
void setEntitySkyLight(float factor, const std::array<float, 3>& color, const std::array<float, 3>& ambient);

// What the hand needs to know about the frame.
struct HandFrame {
    int width = 854;
    int height = 480;
    double partialTick = 0.0;
    float yaw = 0.0f;    // the view, degrees
    float pitch = 0.0f;
    bool rightHanded = true;  // which side the main hand is on (the Main Hand option)
    bool sleeve = true;       // the sleeve layer of that arm is shown (Skin Customization)
    int sky = 15;             // light at the player's eyes, 0..15
    int block = 0;
    float brightness = 0.5f;  // the Brightness option
    Matrix4f bob = Matrix4f::identity();  // view bobbing (identity when off)
    // An item being eaten or drunk (ItemUseAnimation EAT / DRINK): the hand brings it to the mouth. useDuration is the whole use in ticks (0: not being used) and useRemaining
    // the ticks left, counted as the game does: ticks left - the part of this tick + 1.
    int useDuration = 0;
    float useRemaining = 0.0f;
};

// The player's hand in first person: the bare arm (from the skin) or the held item at the bottom right (or left) of the
// screen, with the game's equip, swing and sway animations. The numbers are those of the game's ItemInHandRenderer: the
// arm and item are placed by a chain of translations and rotations that depends on the swing and equip progress.
class HandRenderer {
public:
    HandRenderer(const ResourceManager& resources, BlockModels& models, ItemIcons& icons);
    ~HandRenderer();
    HandRenderer(const HandRenderer&) = delete;
    HandRenderer& operator=(const HandRenderer&) = delete;

    // Once per game tick, with the stack in the main hand and the view angles.
    void tick(const ItemStack& held, float yaw, float pitch);
    // An attack, a block broken or placed: starts the swing (a swing already past its middle restarts).
    void swing();
    // Attacking the air starts the attack cooldown over (the hand dips a little while it recovers).
    void resetAttackStrength() { attackTicker_ = 0; }

    // Draws the hand over the world (clears the depth buffer first). With `offhand` it draws that stack in the other hand instead: mirrored, at rest
    // (the game's ItemInHandRenderer.renderArmWithItem for InteractionHand.OFF_HAND, which has no swing or equip animation of its own).
    void render(WorldRenderer& world, const HandFrame& frame, const ItemStack* offhand = nullptr);
    // Draws the items lying in the world (ItemEntityRenderer): each bobs and turns slowly, a bigger stack is drawn as a few copies. `viewProj` is the
    // frame's view and projection (the world is drawn relative to the camera, which is at `cam`).
    void renderGroundItems(WorldRenderer& world, const Matrix4f& viewProj, const std::array<double, 3>& cam, const std::vector<ItemEntity>& items, double partialTick);

    // Draws an item in the hand of a player seen from outside (ItemInHandLayer): `arm` is the arm's place in the camera-relative world (PlayerBodyRenderer's BodyHands); the item is
    // turned a quarter and a half over, moved to the palm and then placed as its own model says for the third-person view.
    void renderItemInHand(WorldRenderer& world, const Matrix4f& viewProj, const ItemStack& stack, const std::array<double, 16>& arm, bool left, int sky, int block);

    // Draws the blocks that fall and the TNT that burns (FallingBlockRenderer, TntRenderer): a whole block, the TNT swelling and flashing white at the end.
    void renderBlockEntities(WorldRenderer& world, const Matrix4f& viewProj, const std::vector<FallingBlock>& falling, const std::vector<PrimedTnt>& tnt, const std::array<double, 3>& cam,
                             double partialTick);

    // For tests: the progress values the animation has now.
    [[nodiscard]] float swingProgress(double partialTick) const;
    [[nodiscard]] float equipProgress(double partialTick) const;

private:
    struct HeldModel {
        std::vector<MeshVertex> vertices;  // in the model's own 0..1 space
        DisplayTransform firstRight, firstLeft;
        DisplayTransform ground;      // lying in the world
        DisplayTransform thirdRight;  // in the right hand of the player seen from outside
        DisplayTransform thirdLeft;   // and in the left
        bool empty = true;
    };
    const HeldModel& modelFor(const ItemStack& stack);
    HeldModel buildHeldModel(const ItemStack& stack);
    void drawArm(const HandFrame& frame, float side, float swing, float equip, float swayX, float swayY);

    BlockModels& models_;
    ItemIcons& icons_;
    bool slim_ = false;
    Texture2D skin_;
public:
    // The arm in a skin of the player's own (wide arms: the hand is drawn with the wide arm).
    void setSkin(const Image& skin) { skin_ = Texture2D(skin, FilterMode::Nearest); }
private:
    ShaderProgram armShader_;
    gl::GLint uProj_ = -1, uLight_ = -1, uSkin_ = -1;
    gl::GLuint armVao_ = 0, armVbo_ = 0;
    std::unordered_map<std::string, HeldModel> heldModels_;

    // The animation state, as the game's.
    ItemStack shown_;                 // the stack being drawn (the old one until it has been lowered)
    float mainHandHeight_ = 0.0f, oMainHandHeight_ = 0.0f;
    bool swinging_ = false;
    int swingTime_ = 0;
    float attackAnim_ = 0.0f, oAttackAnim_ = 0.0f;
    float xBob_ = 0.0f, yBob_ = 0.0f, xBobO_ = 0.0f, yBobO_ = 0.0f;
    int attackTicker_ = 100;
    bool started_ = false;
    ItemStack held_;
};

}  // namespace falcon
