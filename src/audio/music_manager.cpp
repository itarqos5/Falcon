#include "falcon/audio/music_manager.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>

namespace falcon {

MusicManager::MusicManager(SoundSystem& sounds, const GameOptions& options, std::uint32_t seed)
    : sounds_(sounds), options_(options), rng_(seed) {}

int MusicManager::nextMenuDelay() {
    if (options_.musicFrequency == MusicFrequency::Constant) return kConstantDelay;
    if (situation_ != Situation::Menu) {  // Musics.GAME / CREATIVE: 10 to 20 minutes apart (5 to 1 minutes if frequent)
        const bool frequent = options_.musicFrequency == MusicFrequency::Frequent;
        std::uniform_int_distribution<int> world(frequent ? 1200 : 12000, frequent ? 6000 : 24000);
        return world(rng_);
    }
    // DEFAULT / FREQUENT cap delays at 24000 / 12000 ticks, far above the menu's 600.
    std::uniform_int_distribution<int> dist(kMenuMinDelay, kMenuMaxDelay);
    return dist(rng_);
}

void MusicManager::update(float deltaTicks) {
    tickAccumulator_ += deltaTicks;
    int ticks = 0;
    while (tickAccumulator_ >= 1.0f && ticks < 20) {  // never try to catch up more than a second
        tickAccumulator_ -= 1.0f;
        tick();
        ++ticks;
    }
    if (ticks == 20) tickAccumulator_ = 0.0f;
}

void MusicManager::frequencyChanged() { nextSongDelay_ = nextMenuDelay(); }

int MusicManager::maxDelay() const {
    if (situation_ == Situation::Menu) return kMenuMaxDelay;
    return options_.musicFrequency == MusicFrequency::Frequent ? 6000 : 24000;
}

void MusicManager::setSituation(Situation situation) {
    if (situation == situation_) return;
    situation_ = situation;
    sounds_.stopMusic();  // the old situation's track ends
    currentMusic_ = false;
    nextSongDelay_ = kStartingDelay;  // the first song comes a few seconds in
}

void MusicManager::tick() {
    if (currentMusic_ && !sounds_.musicActive()) {
        // The track finished (or never started): schedule the next one.
        currentMusic_ = false;
        nextSongDelay_ = std::min(nextSongDelay_, nextMenuDelay());
    }
    nextSongDelay_ = std::min(nextSongDelay_, maxDelay());
    if (!currentMusic_ && --nextSongDelay_ <= 0) startPlaying();
}

void MusicManager::startPlaying() {
    const char* event = situation_ == Situation::Menu ? "music.menu" : situation_ == Situation::Creative ? "music.creative" : "music.game";
    const auto status = sounds_.startMusic(event);
    currentMusic_ = true;  // the game keeps the instance even if it failed to start
    if (status != SoundSystem::MusicStatus::NotStarted) ++tracksStarted_;
    std::printf("[falcon] music: %s (%s)\n", sounds_.currentMusicTrack().c_str(),
                status == SoundSystem::MusicStatus::Started
                    ? "started"
                    : (status == SoundSystem::MusicStatus::StartedSilently ? "started silently"
                                                                          : "could not start"));
    std::fflush(stdout);
    if (status == SoundSystem::MusicStatus::Started && onTrackStarted) {
        onTrackStarted(sounds_.currentMusicTrack());
    }
    nextSongDelay_ = INT_MAX;  // no new song while this one lives
}

}  // namespace falcon
