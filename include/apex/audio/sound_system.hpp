#pragma once

#include "apex/assets/resource_manager.hpp"
#include "apex/audio/audio_engine.hpp"
#include "apex/audio/sound_registry.hpp"
#include "apex/audio/sound_source.hpp"
#include "apex/game/game_options.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

namespace apex {

// The game's sound manager for a client with no world loaded: resolves sound events from
// sounds.json, applies the category / master volume sliders, and plays through the AudioEngine.
class SoundSystem {
public:
    SoundSystem(const ResourceManager& resources, AudioEngine& engine, const GameOptions& options,
                std::uint32_t seed);
    // The same with a registry that was already parsed (e.g. on another thread).
    SoundSystem(const ResourceManager& resources, AudioEngine& engine, const GameOptions& options,
                std::uint32_t seed, SoundRegistry registry);

    // SoundEngine.calculateVolume: clamp(instance volume) * clamp(category * master slider).
    [[nodiscard]] float calculateVolume(float instanceVolume, SoundSource source) const;

    // Plays a non-positional sound event ("relative", no attenuation). `volume` is the instance
    // volume (before the sound entry's own volume). Returns the voice id, 0 if silent/unknown.
    std::uint64_t play(const std::string& event, SoundSource source, float volume = 1.0f,
                       float pitch = 1.0f);

    // AbstractWidget.playButtonClickSound: SimpleSoundInstance.forUI(ui.button.click, 1.0F),
    // which has the default UI volume of 0.25.
    void playButtonClick();

    enum class MusicStatus { Started, StartedSilently, NotStarted };
    // Picks a track of `event` (e.g. "music.menu") and starts streaming it. The category gain
    // is applied live, so a track started at 0% volume plays silently until it is raised.
    MusicStatus startMusic(const std::string& event);
    void stopMusic();
    [[nodiscard]] bool musicActive() const { return engine_.musicPlaying(); }
    [[nodiscard]] const std::string& currentMusicTrack() const { return musicTrack_; }

    // A volume slider moved: re-applies live gains and, with no world loaded (always, here),
    // plays the category's preview sound (SoundPreviewHandler).
    void onVolumeChanged(SoundSource source, double newValue);

    [[nodiscard]] const SoundRegistry& registry() const { return registry_; }
    [[nodiscard]] AudioEngine& engine() { return engine_; }

    // Resource packs are about to change: `next` is the resource view that will replace the one
    // this system reads from. Adopts a new registry (if the packs changed sounds.json) and forgets
    // each decoded sound whose file the new packs change, so the next play loads the new one.
    // Sounds already playing finish as they are.
    void reloadResources(std::optional<SoundRegistry> registry, const ResourceManager& next);

    // The event previewed when a category slider moves ("" for categories with no preview).
    [[nodiscard]] static const char* previewEvent(SoundSource source);

private:
    [[nodiscard]] std::shared_ptr<const PcmSound> loadSound(const std::string& name);
    void refreshMusicGain();
    void preview(SoundSource source, float value);

    const ResourceManager& resources_;
    AudioEngine& engine_;
    const GameOptions& options_;
    SoundRegistry registry_;
    std::mt19937 rng_;
    std::unordered_map<std::string, std::shared_ptr<const PcmSound>> cache_;

    std::string musicTrack_;
    float musicEntryVolume_ = 1.0f;

    std::uint64_t previewId_ = 0;
    SoundSource previewSource_ = SoundSource::Master;
};

}  // namespace apex
