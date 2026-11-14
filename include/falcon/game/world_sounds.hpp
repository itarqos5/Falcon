#pragma once

#include <string>

namespace falcon {

// When the player's feet make a step sound (Entity.applyMovementEmissionAndPlaySound): the distance walked, times 0.6, adds up in `moveDist`; a sound is
// due once it is past `nextStep`, and after a sound the next one is due at the next whole number. (A jump that lands counts: the distance walked
// in the air was added while no sound could play, so it is due the moment the feet touch down.)
class StepClock {
public:
    // One tick's move: the blocks walked along the ground (on a ladder the whole move counts instead).
    void add(double distance) { moveDist_ += static_cast<float>(distance * 0.6000000238418579); }
    [[nodiscard]] bool due() const { return moveDist_ > nextStep_; }
    // A step sound was played: the next one is due at the next whole number of the clock.
    void played() { nextStep_ = static_cast<float>(static_cast<int>(moveDist_) + 1); }
    [[nodiscard]] float moveDist() const { return moveDist_; }
    [[nodiscard]] float nextStep() const { return nextStep_; }

private:
    float moveDist_ = 0.0f;
    float nextStep_ = 1.0f;
};

// The name of a block's sound: "block.<sound set>.<kind>" with kind break, place, step, hit or fall (sounds.json).
[[nodiscard]] inline std::string blockSoundEvent(const std::string& soundSet, const char* kind) { return "block." + soundSet + "." + kind; }

// The volume and pitch a sound is played with, from the block's SoundType (its own volume and pitch) and what the sound is for.
struct SoundMix {
    float volume = 1.0f;
    float pitch = 1.0f;
};
// A block broken or placed (the sound is made by the block, heard near it): half way up to full volume, and lower.
[[nodiscard]] inline SoundMix breakOrPlaceMix(float typeVolume, float typePitch) { return {(typeVolume + 1.0f) / 2.0f, typePitch * 0.8f}; }
// A footstep, and the muffled one under water.
[[nodiscard]] inline SoundMix stepMix(float typeVolume, float typePitch) { return {typeVolume * 0.15f, typePitch}; }
[[nodiscard]] inline SoundMix mutedStepMix(float typeVolume, float typePitch) { return {typeVolume * 0.05f, typePitch * 0.8f}; }
// Landing hard (LivingEntity.playBlockFallSound).
[[nodiscard]] inline SoundMix fallMix(float typeVolume, float typePitch) { return {typeVolume * 0.5f, typePitch * 0.75f}; }

// The player's cry when a fall hurts: a small one up to 4 points of damage, a big one above.
[[nodiscard]] inline const char* fallDamageEvent(int damage) { return damage > 4 ? "entity.player.big_fall" : "entity.player.small_fall"; }

}  // namespace falcon
