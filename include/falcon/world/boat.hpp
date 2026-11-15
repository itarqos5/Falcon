#pragma once

#include "falcon/world/chunk.hpp"
#include "falcon/world/collision_shapes.hpp"

#include <cstdint>

namespace falcon {

// The keys that steer a boat: the rider's left, right, forward and back (AbstractBoat.setInput).
struct BoatInput {
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
};

// Where a boat is, as AbstractBoat.Status says: afloat, under the surface (still or flowing water), on the ground, or in the air.
enum class BoatStatus : std::uint8_t { InWater, UnderWater, UnderFlowingWater, OnLand, InAir };

// A boat (or raft) driven by the player on this machine, with the game's own motion: AbstractBoat.tick as the driver's client runs it (the status from the water and ground round
// it, floatBoat, controlBoat and the move). The server does not move a boat that a player rides: it checks what this says.
// Not in this port: bubble columns, the damage of a hard landing (which only the server deals), and other entities that push.
class BoatBody {
public:
    static constexpr double kWidth = 1.375;
    static constexpr double kHeight = 0.5625;

    double x = 0.0, y = 0.0, z = 0.0;
    float yaw = 0.0f;                // degrees, as the player's
    double vx = 0.0, vy = 0.0, vz = 0.0;  // per tick
    float deltaRotation = 0.0f;      // how fast it is turning, degrees a tick
    bool onGround = false;
    bool horizontalCollision = false;
    bool paddleLeft = false, paddleRight = false;  // what the paddles do (ServerboundPaddleBoatPacket)

    // One game tick with the keys the rider holds.
    void tick(const Level& level, const BoatInput& input);
    [[nodiscard]] Aabb boundingBox() const;
    [[nodiscard]] BoatStatus status() const { return status_; }

    // Where a rider sits (AbstractBoat.getPassengerAttachmentPoint and Entity.positionRider): above the boat's floor by a third of its height (0.889 of it for a raft), the first of
    // two a little forward and the second back, turned with the boat; less the player's own attachment of 0.6. `index` is the rider's place, `riders` how many there are.
    [[nodiscard]] std::array<double, 3> seat(bool raft, int index, int riders) const;

private:
    [[nodiscard]] BoatStatus findStatus(const Level& level);
    [[nodiscard]] bool checkInWater(const Level& level);
    [[nodiscard]] bool underwaterStatus(const Level& level, BoatStatus& out);
    [[nodiscard]] float waterLevelAbove(const Level& level) const;
    [[nodiscard]] float groundFriction(const Level& level) const;
    void floatBoat(const Level& level);
    void controlBoat(const BoatInput& input);
    void move(const Level& level);

    BoatStatus status_ = BoatStatus::InAir;
    BoatStatus oldStatus_ = BoatStatus::InAir;
    double waterLevel_ = 0.0;
    float landFriction_ = 0.0f;
    double lastYd_ = 0.0;
};

}  // namespace falcon
