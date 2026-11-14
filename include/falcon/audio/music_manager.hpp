#pragma once

#include "falcon/audio/sound_system.hpp"
#include "falcon/game/game_options.hpp"

#include <cstdint>
#include <functional>
#include <random>
#include <string>

namespace falcon {

// The game's MusicManager for the menus (no world loaded, so the situational music is always
// Musics.MENU): the first track starts 5 s in, then a random 1-30 s gap follows each track.
// Tracks are never looped or restarted, and keep playing across every menu screen.
class MusicManager {
public:
    static constexpr int kStartingDelay = 100;  // ticks (5 s)
    static constexpr int kMenuMinDelay = 20;    // Musics.MENU: 1 s ..
    static constexpr int kMenuMaxDelay = 600;   //             .. 30 s
    static constexpr int kConstantDelay = 100;  // MusicFrequency.CONSTANT

    MusicManager(SoundSystem& sounds, const GameOptions& options, std::uint32_t seed);

    // Advances by real time; runs one tick() per 50 ms.
    void update(float deltaTicks);
    void tick();

    // Called when the Music Frequency option changes (MusicManager.setMinutesBetweenSongs).
    void frequencyChanged();

    [[nodiscard]] int nextSongDelay() const { return nextSongDelay_; }
    [[nodiscard]] bool hasCurrentTrack() const { return currentMusic_; }
    [[nodiscard]] int tracksStarted() const { return tracksStarted_; }

    // What is going on decides which music plays: the menu tracks, or the tracks of a world (creative mode has its own).
    // Changing it stops the current track and schedules the next one soon.
    enum class Situation { Menu, Survival, Creative };
    void setSituation(Situation situation);
    [[nodiscard]] Situation situation() const { return situation_; }

    // MusicFrequency.getNextSongDelay for the menu music.
    [[nodiscard]] int nextMenuDelay();

    // Called when a track actually starts playing (not when it starts silently), with its sound
    // name like "music/game/ebb" - where the "now playing" toast hooks in.
    std::function<void(const std::string& track)> onTrackStarted;

private:
    void startPlaying();
    [[nodiscard]] int maxDelay() const;  // the longest wait before a new song in this situation, in ticks

    SoundSystem& sounds_;
    const GameOptions& options_;
    std::mt19937 rng_;
    float tickAccumulator_ = 0.0f;
    int nextSongDelay_ = kStartingDelay;
    bool currentMusic_ = false;  // a track was started and has not been seen to end yet
    int tracksStarted_ = 0;
    Situation situation_ = Situation::Menu;
};

}  // namespace falcon
