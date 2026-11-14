#include "apex/audio/sound_registry.hpp"

#include <nlohmann/json.hpp>

namespace apex {
using nlohmann::json;

namespace {

// Reads sounds.json as a stream of tokens and builds the events directly, instead of first
// building a JSON tree of the whole 600 KB file and throwing it away (which costs several MB of
// RAM that never comes back). The shape it understands:
//   { "<event id>": { "sounds": [ "<name>" | { "name", "volume", "pitch", "weight", "stream",
//                                              "type" }, ... ], ...anything else is skipped } }
class SoundsHandler final : public nlohmann::json_sax<json> {
public:
    explicit SoundsHandler(std::unordered_map<std::string, SoundEvent>& out) : out_(out) {}

    bool null() override { return true; }
    bool boolean(bool value) override {
        if (top() == Context::Entry && key_ == "stream") entry_.stream = value;
        if (top() == Context::Event && key_ == "replace") event_.replace = value;
        return true;
    }
    bool number_integer(number_integer_t value) override { return number(static_cast<double>(value)); }
    bool number_unsigned(number_unsigned_t value) override { return number(static_cast<double>(value)); }
    bool number_float(number_float_t value, const string_t&) override { return number(value); }
    bool string(string_t& value) override {
        if (top() == Context::Sounds) {  // a bare "name" entry
            entry_ = SoundEntry{};
            entry_.name = value;
            finishEntry();
        } else if (top() == Context::Entry) {
            if (key_ == "name") entry_.name = value;
            if (key_ == "type") entry_.isEvent = value == "event";
        }
        return true;
    }
    bool binary(binary_t&) override { return true; }

    bool start_object(std::size_t) override {
        switch (top()) {
            case Context::None: stack_.push_back(Context::Root); break;
            case Context::Root:
                stack_.push_back(Context::Event);
                eventId_ = key_;
                event_ = SoundEvent{};
                hasSounds_ = false;
                break;
            case Context::Sounds:
                stack_.push_back(Context::Entry);
                entry_ = SoundEntry{};
                break;
            default: stack_.push_back(Context::Skip); break;  // anything else is ignored
        }
        return true;
    }
    bool key(string_t& value) override {
        key_ = value;
        return true;
    }
    bool end_object() override { return pop(); }

    bool start_array(std::size_t) override {
        if (top() == Context::Event && key_ == "sounds") {
            stack_.push_back(Context::Sounds);
            hasSounds_ = true;
        } else {
            stack_.push_back(Context::Skip);
        }
        return true;
    }
    bool end_array() override { return pop(); }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return false;
    }

private:
    enum class Context { None, Root, Event, Sounds, Entry, Skip };

    [[nodiscard]] Context top() const { return stack_.empty() ? Context::None : stack_.back(); }

    bool number(double value) {
        if (top() != Context::Entry) return true;
        if (key_ == "volume") entry_.volume = static_cast<float>(value);
        if (key_ == "pitch") entry_.pitch = static_cast<float>(value);
        if (key_ == "weight") entry_.weight = static_cast<int>(value);
        return true;
    }

    void finishEntry() {
        if (!entry_.name.empty() && entry_.weight > 0) event_.entries.push_back(std::move(entry_));
        entry_ = SoundEntry{};
    }

    bool pop() {
        const Context finished = top();
        stack_.pop_back();
        if (finished == Context::Entry) finishEntry();
        if (finished == Context::Event && hasSounds_) out_.emplace(eventId_, std::move(event_));
        return true;
    }

    std::unordered_map<std::string, SoundEvent>& out_;
    std::vector<Context> stack_;
    std::string key_;
    std::string eventId_;
    SoundEvent event_;
    SoundEntry entry_;
    bool hasSounds_ = false;
};

}  // namespace

SoundRegistry SoundRegistry::parse(const std::string& jsonText) {
    SoundRegistry registry;
    SoundsHandler handler(registry.events_);
    if (!json::sax_parse(jsonText, &handler)) registry.events_.clear();  // malformed: stay silent
    return registry;
}

void SoundRegistry::merge(SoundRegistry later) {
    for (auto& [id, event] : later.events_) {
        const auto it = events_.find(id);
        if (it == events_.end() || event.replace) {
            events_.insert_or_assign(id, std::move(event));
        } else {
            auto& entries = it->second.entries;
            entries.insert(entries.end(), std::make_move_iterator(event.entries.begin()),
                           std::make_move_iterator(event.entries.end()));
        }
    }
}

SoundRegistry SoundRegistry::load(const ResourceManager& resources) {
    SoundRegistry registry;
    for (const std::string& text : resources.readAllText(ResourceLocation("minecraft", "sounds.json"))) {
        registry.merge(parse(text));
    }
    return registry;
}

const SoundEvent* SoundRegistry::find(const std::string& eventId) const {
    const auto it = events_.find(eventId);
    return it != events_.end() ? &it->second : nullptr;
}

std::optional<PickedSound> SoundRegistry::pick(const std::string& eventId, std::mt19937& rng) const {
    return pickInternal(eventId, rng, 0);
}

std::optional<PickedSound> SoundRegistry::pickInternal(const std::string& eventId,
                                                       std::mt19937& rng, int depth) const {
    const SoundEvent* event = find(eventId);
    if (event == nullptr || event->entries.empty() || depth > 8) return std::nullopt;

    int total = 0;
    for (const SoundEntry& e : event->entries) total += e.weight;
    std::uniform_int_distribution<int> dist(0, total - 1);
    int roll = dist(rng);

    const SoundEntry* chosen = &event->entries.back();
    for (const SoundEntry& e : event->entries) {
        if (roll < e.weight) {
            chosen = &e;
            break;
        }
        roll -= e.weight;
    }

    if (!chosen->isEvent) {
        return PickedSound{chosen->name, chosen->volume, chosen->pitch, chosen->stream};
    }
    auto inner = pickInternal(chosen->name, rng, depth + 1);
    if (!inner) return std::nullopt;
    inner->volume *= chosen->volume;
    inner->pitch *= chosen->pitch;
    inner->stream = inner->stream || chosen->stream;
    return inner;
}

}  // namespace apex
