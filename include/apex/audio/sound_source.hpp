#pragma once

#include <string>

namespace apex {

// The game's sound categories (net.minecraft.sounds.SoundSource), in the game's order.
enum class SoundSource {
    Master,
    Music,
    Records,
    Weather,
    Blocks,
    Hostile,
    Neutral,
    Players,
    Ambient,
    Voice,
    Ui,
};

inline constexpr int kSoundSourceCount = 11;

// The category's name as used in options.txt and language keys ("master", "record", "ui", ...).
[[nodiscard]] inline const char* soundSourceName(SoundSource source) {
    switch (source) {
        case SoundSource::Master: return "master";
        case SoundSource::Music: return "music";
        case SoundSource::Records: return "record";
        case SoundSource::Weather: return "weather";
        case SoundSource::Blocks: return "block";
        case SoundSource::Hostile: return "hostile";
        case SoundSource::Neutral: return "neutral";
        case SoundSource::Players: return "player";
        case SoundSource::Ambient: return "ambient";
        case SoundSource::Voice: return "voice";
        case SoundSource::Ui: return "ui";
    }
    return "master";
}

// Language key of the category's volume slider caption ("soundCategory.<name>").
[[nodiscard]] inline std::string soundSourceLangKey(SoundSource source) {
    return std::string("soundCategory.") + soundSourceName(source);
}

}  // namespace apex
