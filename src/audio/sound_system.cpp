#include "apex/audio/sound_system.hpp"

#include <algorithm>

namespace apex {
namespace {

constexpr float kButtonClickVolume = 0.25f;  // SimpleSoundInstance.forUI(event, pitch) default volume
constexpr float kPitchMin = 0.5f;            // SoundEngine.PITCH_MIN / PITCH_MAX
constexpr float kPitchMax = 2.0f;

}  // namespace

SoundSystem::SoundSystem(const ResourceManager& resources, AudioEngine& engine,
                         const GameOptions& options, std::uint32_t seed)
    : SoundSystem(resources, engine, options, seed, SoundRegistry::load(resources)) {}

SoundSystem::SoundSystem(const ResourceManager& resources, AudioEngine& engine,
                         const GameOptions& options, std::uint32_t seed, SoundRegistry registry)
    : resources_(resources),
      engine_(engine),
      options_(options),
      registry_(std::move(registry)),
      rng_(seed) {}

void SoundSystem::reloadResources(std::optional<SoundRegistry> registry, const ResourceManager& next) {
    if (registry) registry_ = std::move(*registry);
    for (auto it = cache_.begin(); it != cache_.end();) {
        const ResourceLocation file("minecraft", "sounds/" + it->first + ".ogg");
        const bool changed = !it->second || resources_.providers(file) != next.providers(file);
        it = changed ? cache_.erase(it) : std::next(it);  // misses are forgotten too: they may exist now
    }
}

float SoundSystem::calculateVolume(float instanceVolume, SoundSource source) const {
    const float category = std::clamp(static_cast<float>(options_.finalVolume(source)), 0.0f, 1.0f);
    return std::clamp(instanceVolume, 0.0f, 1.0f) * category;
}

std::shared_ptr<const PcmSound> SoundSystem::loadSound(const std::string& name) {
    if (const auto it = cache_.find(name); it != cache_.end()) return it->second;
    std::shared_ptr<const PcmSound> sound;
    if (const auto bytes = resources_.readBytes(ResourceLocation("minecraft", "sounds/" + name + ".ogg"))) {
        if (auto decoded = decodeOgg(*bytes)) sound = std::make_shared<PcmSound>(std::move(*decoded));
    }
    cache_.emplace(name, sound);  // cache misses too, so a missing file is only tried once
    return sound;
}

std::uint64_t SoundSystem::play(const std::string& event, SoundSource source, float volume,
                                float pitch) {
    const auto picked = registry_.pick(event, rng_);
    if (!picked) return 0;
    const auto sound = loadSound(picked->name);
    if (!sound) return 0;

    const float gain = calculateVolume(volume * picked->volume, source);
    if (gain <= 0.0f) return 0;  // silent sounds are not started
    return engine_.playSound(sound, gain, std::clamp(pitch * picked->pitch, kPitchMin, kPitchMax));
}

void SoundSystem::playButtonClick() {
    play("ui.button.click", SoundSource::Ui, kButtonClickVolume, 1.0f);
}

SoundSystem::MusicStatus SoundSystem::startMusic(const std::string& event) {
    const auto picked = registry_.pick(event, rng_);
    if (!picked) return MusicStatus::NotStarted;

    auto bytes = resources_.readBytes(ResourceLocation("minecraft", "sounds/" + picked->name + ".ogg"));
    if (!bytes) return MusicStatus::NotStarted;
    auto stream = OggStream::open(std::move(*bytes));
    if (!stream) return MusicStatus::NotStarted;

    musicTrack_ = picked->name;
    musicEntryVolume_ = picked->volume;
    const float gain = calculateVolume(musicEntryVolume_, SoundSource::Music);
    engine_.playMusic(std::move(stream), gain);
    return gain > 0.0f ? MusicStatus::Started : MusicStatus::StartedSilently;
}

void SoundSystem::stopMusic() {
    engine_.stopMusic();
    musicTrack_.clear();
}

void SoundSystem::refreshMusicGain() {
    if (engine_.musicPlaying()) {
        engine_.setMusicVolume(calculateVolume(musicEntryVolume_, SoundSource::Music));
    }
}

const char* SoundSystem::previewEvent(SoundSource source) {
    switch (source) {
        case SoundSource::Records: return "block.note_block.guitar";
        case SoundSource::Weather: return "entity.lightning_bolt.thunder";
        case SoundSource::Blocks: return "block.grass.place";
        case SoundSource::Hostile: return "entity.zombie.ambient";
        case SoundSource::Neutral: return "entity.cow.ambient";
        case SoundSource::Players: return "entity.generic.eat";
        case SoundSource::Ambient: return "ambient.cave";
        case SoundSource::Ui: return "ui.button.click";
        default: return "";  // master, music and voice have no preview
    }
}

void SoundSystem::preview(SoundSource source, float value) {
    // SoundPreviewHandler.preview: moving to another category stops the running preview; a new
    // one only starts when none is playing.
    if (source != previewSource_ && previewId_ != 0) engine_.stopSound(previewId_);
    previewSource_ = source;
    if (previewId_ != 0 && engine_.isSoundActive(previewId_)) return;

    const std::string event = previewEvent(source);
    if (event.empty()) return;
    // The preview is a UI sound whose instance volume is the slider's new value.
    previewId_ = play(event, SoundSource::Ui, value, 1.0f);
}

void SoundSystem::onVolumeChanged(SoundSource source, double newValue) {
    if (source == SoundSource::Master || source == SoundSource::Music) refreshMusicGain();
    preview(source, static_cast<float>(newValue));
}

}  // namespace apex
