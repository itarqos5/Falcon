#pragma once

#include "apex/assets/resource_manager.hpp"

#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace apex {

// One line of an event in sounds.json: either a sound file or a redirect to another event.
struct SoundEntry {
    std::string name;        // "random/click_stereo" (a file under sounds/) or an event id
    float volume = 1.0f;
    float pitch = 1.0f;
    int weight = 1;
    bool stream = false;     // streamed from disk instead of preloaded (music)
    bool isEvent = false;    // "type": "event": `name` is another event
};

struct SoundEvent {
    std::vector<SoundEntry> entries;
    bool replace = false;  // "replace": true - a pack's version replaces earlier ones instead of adding to them
};

// A concrete sound chosen for an event.
struct PickedSound {
    std::string name;  // file under sounds/, without extension
    float volume = 1.0f;
    float pitch = 1.0f;
    bool stream = false;
};

// minecraft:sounds.json: maps event ids ("ui.button.click", "music.menu", ...) to weighted
// lists of sounds.
class SoundRegistry {
public:
    // Merges every pack's sounds.json over the game's: an event a pack lists gets its sounds added
    // to the earlier ones, or replaces them when the pack says "replace": true. An unreadable or
    // missing sounds.json gives an empty registry (everything stays silent).
    static SoundRegistry load(const ResourceManager& resources);
    // Parses one sounds.json text (used by tests).
    static SoundRegistry parse(const std::string& jsonText);
    // Applies `later` on top of this registry the way a resource pack's sounds.json is applied.
    void merge(SoundRegistry later);

    [[nodiscard]] const SoundEvent* find(const std::string& eventId) const;
    [[nodiscard]] std::size_t size() const { return events_.size(); }

    // Weighted random choice among the event's entries, following redirects (their volume and
    // pitch multiply). Nullopt if the event is unknown or empty.
    [[nodiscard]] std::optional<PickedSound> pick(const std::string& eventId,
                                                  std::mt19937& rng) const;

private:
    [[nodiscard]] std::optional<PickedSound> pickInternal(const std::string& eventId,
                                                          std::mt19937& rng, int depth) const;

    std::unordered_map<std::string, SoundEvent> events_;
};

}  // namespace apex
