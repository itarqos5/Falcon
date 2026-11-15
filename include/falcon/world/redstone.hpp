#pragma once

#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/collision_shapes.hpp"
#include "falcon/world/item_stack.hpp"

#include <array>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace falcon {

// Redstone, as the game works it out: what gives power (blocks of redstone, levers, buttons, pressure plates, torches, repeaters, observers), how it travels (dust, solid
// blocks that are powered strongly or weakly) and what takes it (lamps, doors, trapdoors, fence gates, TNT, and the parts that pass it on). It works through the same
// Host as the other physics: every change goes through setBlock, which tells the neighbours, which comes back here.
class Redstone {
public:
    struct Host {
        std::function<bool(int, int, int, Block)> setBlock;
        std::function<void(const std::string&, float, float, double, double, double)> sound;  // event, volume, pitch, where
        std::function<void(const ItemStack&, double, double, double)> drop;                    // an item thrown out at a place
        std::function<void(int, int, int)> primeTnt;                                           // the TNT block there has been taken away: light it
        std::function<bool(const Aabb&, bool)> entityInside;                                  // anything in the box (the second flag: items count too)
    };

    Redstone(const Level& level, Host host, std::uint32_t seed);

    // One game tick: the delayed parts (torches, repeaters, buttons, lamps ...) and the pressure plates.
    void tick();
    // A block was put at (x, y, z) or changed: whatever redstone is near is looked at again, at once.
    void blockChanged(int x, int y, int z);
    // A right click on the block: a lever, button, door, trapdoor or fence gate does its thing. True if it did.
    bool use(int x, int y, int z);
    // The strength of the signal a block gives a neighbour (for the debug screen and the tests): the best of its six neighbours.
    [[nodiscard]] int bestNeighborSignal(int x, int y, int z) const;
    [[nodiscard]] int pending() const { return static_cast<int>(due_.size()); }

private:
    enum Kind : std::uint8_t { kNone, kWire, kTorch, kWallTorch, kLever, kStoneButton, kWoodButton, kStonePlate, kWoodPlate, kLamp, kRedstoneBlock, kRepeater, kObserver, kDoor, kIronDoor, kTrapdoor, kIronTrapdoor, kGate, kTnt };

    [[nodiscard]] Block block(int x, int y, int z) const { return level_.block(x, y, z); }
    [[nodiscard]] Kind kindOf(Block b) const { return kinds_[registry_.state(b).block]; }
    [[nodiscard]] bool isTrue(Block b, const char* property) const { return registry_.get(b, property) == "true"; }
    [[nodiscard]] int faceOf(Block b, const char* property = "facing") const;
    [[nodiscard]] bool conductor(Block b) const;
    [[nodiscard]] int emitted(Block b, int x, int y, int z, int dir, bool direct) const;
    [[nodiscard]] int signalAt(int x, int y, int z, int dir) const;
    [[nodiscard]] int directSignalTo(int x, int y, int z) const;
    [[nodiscard]] bool hasNeighborSignal(int x, int y, int z) const;
    [[nodiscard]] int wireTargetStrength(int x, int y, int z) const;
    [[nodiscard]] int connectingSide(int x, int y, int z, int dir, bool canConnectUp) const;  // 0 none, 1 side, 2 up
    [[nodiscard]] bool connectsTo(Block b, int dir) const;
    [[nodiscard]] Block wireState(int x, int y, int z, Block current) const;
    [[nodiscard]] int repeaterInput(int x, int y, int z, Block b) const;
    [[nodiscard]] bool sturdy(Block b) const;
    [[nodiscard]] int attachedFace(Block b) const;  // the face of the block a lever or button stands on, as a direction from it to the block

    void evaluate(int x, int y, int z);
    void schedule(int x, int y, int z, int delay);
    void runScheduled(int x, int y, int z);
    void process();
    void enqueue(int x, int y, int z);
    void setOpen(int x, int y, int z, Block b, bool open, bool powered, bool sound);
    void press(int x, int y, int z, Block b, bool on);
    void plateCheck(int x, int y, int z);
    void snd(const std::string& event, float volume, float pitch, int x, int y, int z) const {
        if (host_.sound) host_.sound(event, volume, pitch, x + 0.5, y + 0.5, z + 0.5);
    }

    const Level& level_;
    const BlockRegistry& registry_;
    Host host_;
    std::mt19937 rng_;
    std::vector<Kind> kinds_;
    int wireIndex_ = -1;
    long long time_ = 0;
    mutable bool wiresGivePower_ = true;  // (off while a wire works out its own strength, so that dust does not read dust through the signal it gives)
    bool processing_ = false;
    std::multimap<long long, std::array<int, 3>> due_;
    std::set<std::array<int, 3>> scheduled_;
    std::vector<std::array<int, 3>> queue_;
    std::set<std::array<int, 3>> queued_;
    std::set<std::array<int, 3>> plates_;  // pressure plates seen, checked every tick
};

}  // namespace falcon
