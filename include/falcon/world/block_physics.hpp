#pragma once

#include "falcon/world/block_behaviours.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/collision_shapes.hpp"
#include "falcon/world/item_entity.hpp"
#include "falcon/world/item_stack.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace falcon {

// A block that is falling (FallingBlockEntity): sand, gravel, concrete powder, an anvil.
struct FallingBlock {
    Block block = Block::Air;
    double x = 0.0, y = 0.0, z = 0.0;  // the middle of the bottom of the box
    double xo = 0.0, yo = 0.0, zo = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    int time = 0;
    double fallDistance = 0.0;
    bool onGround = false;
    bool cancelDrop = false;  // an anvil that broke on landing
    int id = 0;
    [[nodiscard]] Aabb box() const { return {x - 0.49, y, z - 0.49, x + 0.49, y + 0.98, z + 0.49}; }
};

// Primed TNT (PrimedTnt): a block that burns for four seconds and explodes.
struct PrimedTnt {
    double x = 0.0, y = 0.0, z = 0.0;
    double xo = 0.0, yo = 0.0, zo = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    int fuse = 80;
    bool onGround = false;
    int id = 0;
    [[nodiscard]] Aabb box() const { return {x - 0.49, y, z - 0.49, x + 0.49, y + 0.98, z + 0.49}; }
};

// What the physics needs to know about the player: where they are, and whether they can be hurt (survival and adventure).
struct PhysicsPlayer {
    Aabb box;
    double eyeY = 0.0;
    bool hurtable = false;
};

// The things that happen to blocks by themselves, as the game does them: blocks that fall, fire that burns and spreads (and starts from lava), TNT that burns and
// explodes and sets off the TNT near it. It works on the player's world through the callbacks of Host, so every change tells the neighbours and the renderer.
class BlockPhysics {
public:
    struct Host {
        std::function<bool(int, int, int, Block)> setBlock;                                       // change a block (neighbours are told, the mesh is redone)
        std::function<void(const std::string&, float, float, double, double, double)> sound;     // event, volume, pitch, where
        std::function<std::vector<ItemStack>(Block)> drops;                                       // what a block gives when it is destroyed
        std::function<PhysicsPlayer()> player;
        std::function<void(float)> hurtPlayer;
        std::function<void(double, double, double)> pushPlayer;                                   // adds to the player's velocity
        std::function<void()> beginBatch, endBatch;                                               // many block changes at once (an explosion)
    };

    BlockPhysics(const Level& level, std::shared_ptr<const BlockBehaviours> behaviours, ItemEntities& items, Host host, std::uint32_t seed);

    // One game tick: scheduled block ticks, random ticks near the player, the falling blocks and the TNT.
    void tick();

    // Hooks for the world: a block was placed or changed at (x, y, z).
    void blockPlaced(int x, int y, int z);
    // A neighbour of this block changed: returns what the block turns into (itself if nothing). Schedules a fall for a block that may now fall.
    using Reader = std::function<Block(int, int, int)>;
    [[nodiscard]] Block afterNeighborChange(Block block, int x, int y, int z, const Reader& at);

    // Fire. canPlaceFireAt is BaseFireBlock.canBePlacedAt: the cell is empty and a fire could stand there.
    [[nodiscard]] bool canPlaceFireAt(int x, int y, int z) const;
    [[nodiscard]] Block fireStateAt(int x, int y, int z) const;  // BaseFireBlock.getState
    // TntBlock.prime: the block at (x, y, z) is replaced by primed TNT (the caller has already taken the block away).
    void primeTnt(int x, int y, int z, int fuse = 80);
    // A TNT-sized explosion at a point (power 4 for TNT).
    void explode(double x, double y, double z, float radius);

    [[nodiscard]] const std::vector<FallingBlock>& fallingBlocks() const { return falling_; }
    [[nodiscard]] const std::vector<PrimedTnt>& primedTnt() const { return tnt_; }
    [[nodiscard]] bool isGravityBlock(Block b) const { return behaviours_->isFallable(blockRegistry().state(b).block); }
    void spawnFalling(Block block, double x, double y, double z);  // (also for the /summon command)
    [[nodiscard]] long long gameTime() const { return time_; }
    // The game rule randomTickSpeed (3): how many blocks of each section get a random tick every game tick.
    void setRandomTickSpeed(int speed) { randomTickSpeed_ = std::max(0, speed); }
    [[nodiscard]] int randomTickSpeed() const { return randomTickSpeed_; }

private:
    void schedule(int x, int y, int z, int delay);
    void runScheduled(int x, int y, int z);
    void tryFall(int x, int y, int z);
    void tickFalling(FallingBlock& e);
    void landFalling(FallingBlock& e);
    void tickTnt(PrimedTnt& e);
    void fireTick(int x, int y, int z);
    void checkBurnOut(int x, int y, int z, int chance, int age);
    void lavaRandomTick(int x, int y, int z);
    void randomTicks();
    [[nodiscard]] bool isFree(Block b) const;
    [[nodiscard]] bool canBurn(Block b) const;
    [[nodiscard]] bool fireCanSurvive(Block fire, int x, int y, int z, const Reader& at) const;
    [[nodiscard]] bool validFireLocation(int x, int y, int z, const Reader& at) const;
    [[nodiscard]] int igniteOddsAround(int x, int y, int z) const;
    [[nodiscard]] Block fireWithAge(int x, int y, int z, int age, const Reader& at) const;
    [[nodiscard]] bool touchesWater(int x, int y, int z, const Reader& at) const;
    [[nodiscard]] float seenPercent(double cx, double cy, double cz, const Aabb& box) const;
    [[nodiscard]] int nextInt(int bound) { return bound <= 1 ? 0 : static_cast<int>(rng_() % static_cast<std::uint32_t>(bound)); }
    [[nodiscard]] float nextFloat() { return static_cast<float>(rng_() >> 8) * (1.0f / 16777216.0f); }
    [[nodiscard]] Block block(int x, int y, int z) const { return level_.block(x, y, z); }

    const Level& level_;
    const BlockRegistry& registry_;
    std::shared_ptr<const BlockBehaviours> behaviours_;
    ItemEntities& items_;
    Host host_;
    std::mt19937 rng_;
    long long time_ = 0;
    std::multimap<long long, std::array<int, 3>> due_;
    std::set<std::array<int, 3>> queued_;
    std::vector<FallingBlock> falling_;
    std::vector<PrimedTnt> tnt_;
    int nextId_ = 1;
    int fireBlock_ = -1, soulFireBlock_ = -1;
    int randomTickSpeed_ = 3;
};

}  // namespace falcon
