#pragma once

#include <algorithm>
#include <cmath>

namespace falcon {

// What the game keeps of an avatar for its cape (ClientAvatarState): the point the cloak hangs from, which trails the body (a quarter of the way to it every tick), and the walking
// numbers that make the cape sway (how far it has walked and its bob).
struct CloakState {
    double x = 0.0, y = 0.0, z = 0.0;
    double xo = 0.0, yo = 0.0, zo = 0.0;
    float walkDist = 0.0f, walkDistO = 0.0f;
    float bob = 0.0f, bobO = 0.0f;
    bool started = false;

    // One game tick: where the avatar is, how far it moved horizontally (its motion) and whether it stands on the ground (a body in the air does not bob).
    void tick(double px, double py, double pz, double deltaX, double deltaZ, bool grounded) {
        if (!started) {
            started = true;
            x = xo = px;
            y = yo = py;
            z = zo = pz;
        }
        bobO = bob;
        walkDistO = walkDist;
        const float horizontal = static_cast<float>(std::sqrt(deltaX * deltaX + deltaZ * deltaZ));
        walkDist += horizontal * 0.6f;
        bob += ((grounded ? std::min(0.1f, horizontal) : 0.0f) - bob) * 0.4f;
        // ClientAvatarState.moveCloak: the point is not let go more than ten blocks from the body, and goes a quarter of the way each tick.
        xo = x;
        yo = y;
        zo = z;
        const double dx = px - x, dy = py - y, dz = pz - z;
        if (dx > 10.0 || dx < -10.0) x = xo = px;
        if (dz > 10.0 || dz < -10.0) z = zo = pz;
        if (dy > 10.0 || dy < -10.0) y = yo = py;
        x += dx * 0.25;
        y += dy * 0.25;
        z += dz * 0.25;
    }
};

// The three numbers the cape model is posed by (AvatarRenderer.extractCapeState): how far it is lifted (flap), how far it swings back (lean) and sideways (lean2), in degrees.
struct CapeAngles {
    float flap = 0.0f, lean = 0.0f, lean2 = 0.0f;
};

// `partial` is how far the frame is into the tick; the position is the avatar's last and present one; `bodyYaw` is where its body faces (degrees); `fallFlyingScale` (0 to 1) is how far
// into a glide it is (the cape lies flat along the body).
inline CapeAngles capeAngles(const CloakState& c, float partial, double xo, double x, double yo, double y, double zo, double z, float bodyYawO, float bodyYaw, float fallFlyingScale) {
    const auto lerp = [](double t, double a, double b) { return a + t * (b - a); };
    const double t = static_cast<double>(partial);
    const double dx = lerp(t, c.xo, c.x) - lerp(t, xo, x);
    const double dy = lerp(t, c.yo, c.y) - lerp(t, yo, y);
    const double dz = lerp(t, c.zo, c.z) - lerp(t, zo, z);
    float turn = std::fmod(bodyYaw - bodyYawO, 360.0f);  // Mth.rotLerp: the short way round
    if (turn >= 180.0f) turn -= 360.0f;
    if (turn < -180.0f) turn += 360.0f;
    const float yaw = bodyYawO + partial * turn;
    const double sine = static_cast<double>(std::sin(static_cast<double>(yaw * 0.017453292f)));
    const double cosine = -static_cast<double>(std::cos(static_cast<double>(yaw * 0.017453292f)));
    CapeAngles out;
    out.flap = std::clamp(static_cast<float>(dy) * 10.0f, -6.0f, 32.0f);
    out.lean = static_cast<float>(dx * sine + dz * cosine) * 100.0f;
    out.lean *= 1.0f - fallFlyingScale;
    out.lean = std::clamp(out.lean, 0.0f, 150.0f);
    out.lean2 = std::clamp(static_cast<float>(dx * cosine - dz * sine) * 100.0f, -20.0f, 20.0f);
    const float bob = static_cast<float>(lerp(t, c.bobO, c.bob));
    const float walked = static_cast<float>(lerp(t, c.walkDistO, c.walkDist));
    out.flap += static_cast<float>(std::sin(static_cast<double>(walked * 6.0f))) * 32.0f * bob;
    return out;
}

}  // namespace falcon
