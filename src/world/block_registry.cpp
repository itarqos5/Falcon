#include "falcon/world/block_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace falcon {
namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

static_assert(sizeof(StateInfo) == 28 && std::is_trivially_copyable_v<StateInfo>, "StateInfo is stored raw in the cache");

namespace {
std::vector<int> strides(const BlockType& t);
}  // namespace

class BlockRegistryBuilder {
public:
    static void addBlock(BlockRegistry& r, BlockType type) {
        r.byName_[type.name] = static_cast<int>(r.blocks_.size());
        r.blocks_.push_back(std::move(type));
    }
    static void finish(BlockRegistry& r) {
        for (std::size_t i = 0; i < r.blocks_.size(); ++i) {
            const BlockType& t = r.blocks_[i];
            const bool air = t.name == "air" || t.name == "cave_air" || t.name == "void_air";
            const bool alwaysWater = t.name == "water" || t.name == "seagrass" || t.name == "tall_seagrass" || t.name == "kelp" ||
                                     t.name == "kelp_plant" || t.name == "bubble_column";
            int waterloggedProperty = -1;
            int levelProperty = -1;
            for (std::size_t p = 0; p < t.properties.size(); ++p) {
                if (t.properties[p].name == "waterlogged") waterloggedProperty = static_cast<int>(p);
                if (t.properties[p].name == "level") levelProperty = static_cast<int>(p);
            }
            const bool isFluidBlock = t.name == "water" || t.name == "lava";
            const auto st = strides(t);
            for (int s = 0; s < t.stateCount && t.firstState + s < static_cast<int>(r.states_.size()); ++s) {
                StateInfo& info = r.states_[static_cast<std::size_t>(t.firstState + s)];
                info.block = static_cast<std::uint16_t>(i);
                if (air) info.flags |= kStateAir;
                if (t.name == "lava") info.flags |= kStateLava;
                bool water = alwaysWater;
                if (waterloggedProperty >= 0) {
                    const auto& values = t.properties[static_cast<std::size_t>(waterloggedProperty)].values;
                    const int v = (s / st[static_cast<std::size_t>(waterloggedProperty)]) % static_cast<int>(values.size());
                    water = water || values[static_cast<std::size_t>(v)] == "true";
                }
                if (water) info.flags |= kStateWater;
                if (isFluidBlock && levelProperty >= 0) {
                    const auto& values = t.properties[static_cast<std::size_t>(levelProperty)].values;
                    const int v = (s / st[static_cast<std::size_t>(levelProperty)]) % static_cast<int>(values.size());
                    info.fluidLevel = static_cast<std::uint8_t>(std::atoi(values[static_cast<std::size_t>(v)].c_str()));
                }
            }
        }
    }
    static std::vector<StateInfo>& states(BlockRegistry& r) { return r.states_; }
    static std::vector<BlockType>& blocks(BlockRegistry& r) { return r.blocks_; }
    static void reindex(BlockRegistry& r) {
        r.byName_.clear();
        for (std::size_t i = 0; i < r.blocks_.size(); ++i) r.byName_[r.blocks_[i].name] = static_cast<int>(i);
    }
};

namespace {

std::string_view stripNamespace(std::string_view name) {
    constexpr std::string_view kPrefix = "minecraft:";
    if (name.substr(0, kPrefix.size()) == kPrefix) name.remove_prefix(kPrefix.size());
    return name;
}

// ---------------------------------------------------------------------------------------------------------------
// gameplay-facts.json: {"states":[{"id":0,"emission":0,...}, ...]}, far too big for a DOM, so it is streamed.
class FactsHandler {
public:
    using number_integer_t = json::number_integer_t;
    using number_unsigned_t = json::number_unsigned_t;
    using number_float_t = json::number_float_t;
    using string_t = json::string_t;
    using binary_t = json::binary_t;

    FactsHandler(std::vector<StateInfo>& states, std::vector<std::string>& item, std::vector<std::string>& sound, std::vector<std::array<float, 2>>& soundMix,
                 const std::vector<char>& wantStrings)
        : states_(states), item_(item), sound_(sound), soundMix_(soundMix), want_(wantStrings) {}

    bool null() { return true; }
    bool boolean(bool v) { return value(v ? 1.0 : 0.0); }
    bool number_integer(number_integer_t v) { return value(static_cast<double>(v)); }
    bool number_unsigned(number_unsigned_t v) { return value(static_cast<double>(v)); }
    bool number_float(number_float_t v, const string_t&) { return value(v); }
    bool string(string_t& v) {
        if (depth_ == 3 && keys_[3] == "render_shape") renderModel_ = v == "MODEL";
        else if (depth_ == 3 && keys_[3] == "push") cur_.push = pushCode(v);
        else if (depth_ == 3 && keys_[3] == "item") curItem_ = v;
        else if (depth_ == 5 && keys_[3] == "sound" && keys_[4] == "step" && keys_[5] == "id") curSound_ = v;
        return true;
    }
    bool binary(binary_t&) { return true; }
    bool start_object(std::size_t) {
        ++depth_;
        if (depth_ == 3) beginState();
        return true;
    }
    bool end_object() {
        if (depth_ == 3) endState();
        --depth_;
        return true;
    }
    bool start_array(std::size_t) {
        ++depth_;
        return true;
    }
    bool end_array() {
        --depth_;
        return true;
    }
    bool key(string_t& v) {
        if (depth_ < static_cast<int>(keys_.size())) keys_[static_cast<std::size_t>(depth_)] = v;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& e) {
        error = e.what();
        return false;
    }

    std::string error;
    std::size_t seen = 0;

private:
    static std::uint8_t pushCode(const std::string& v) {
        if (v == "DESTROY") return 1;
        if (v == "BLOCK") return 2;
        if (v == "IGNORE") return 3;
        if (v == "PUSH_ONLY") return 4;
        return 0;  // NORMAL / PUSH_PULL
    }
    void beginState() {
        cur_ = StateInfo{};
        curFlags_ = 0;
        curId_ = -1;
        renderModel_ = false;
        curItem_.clear();
        curSound_.clear();
        curSoundMix_ = {1.0f, 1.0f};
    }
    void endState() {
        if (curId_ < 0 || static_cast<std::size_t>(curId_) >= states_.size()) return;
        if (renderModel_) curFlags_ |= kStateHasModel;
        cur_.flags = static_cast<std::uint16_t>(curFlags_);
        states_[static_cast<std::size_t>(curId_)] = cur_;
        if (static_cast<std::size_t>(curId_) < want_.size() && want_[static_cast<std::size_t>(curId_)]) {
            item_[static_cast<std::size_t>(curId_)] = curItem_;
            sound_[static_cast<std::size_t>(curId_)] = curSound_;
            soundMix_[static_cast<std::size_t>(curId_)] = curSoundMix_;
        }
        ++seen;
    }
    void flag(bool on, StateFlag f) {
        if (on) curFlags_ |= f;
    }
    bool value(double v) {
        if (depth_ == 4 && keys_[3] == "sound") {  // the sound type's own volume and pitch ("sound": {"volume": 1.0, "pitch": 1.0, ...})
            if (keys_[4] == "volume") curSoundMix_[0] = static_cast<float>(v);
            else if (keys_[4] == "pitch") curSoundMix_[1] = static_cast<float>(v);
            return true;
        }
        if (depth_ != 3) return true;
        const std::string& k = keys_[3];
        if (k == "id") curId_ = static_cast<int>(v);
        else if (k == "emission") cur_.emission = static_cast<std::uint8_t>(std::clamp(v, 0.0, 15.0));
        else if (k == "dampening") cur_.dampening = static_cast<std::uint8_t>(std::clamp(v, 0.0, 15.0));
        else if (k == "hardness") cur_.hardness = static_cast<float>(v);
        else if (k == "explosion_resistance") cur_.explosionResistance = static_cast<float>(v);
        else if (k == "friction") cur_.friction = static_cast<float>(v);
        else if (k == "speed_factor") cur_.speedFactor = static_cast<float>(v);
        else if (k == "jump_factor") cur_.jumpFactor = static_cast<float>(v);
        else if (k == "solid_render") flag(v != 0, kStateOpaqueCube);
        else if (k == "can_occlude") flag(v != 0, kStateCanOcclude);
        else if (k == "liquid") flag(v != 0, kStateLiquid);
        else if (k == "replaceable") flag(v != 0, kStateReplaceable);
        else if (k == "full_collision") flag(v != 0, kStateFullCollision);
        else if (k == "block_entity") flag(v != 0, kStateBlockEntity);
        else if (k == "random_ticks") flag(v != 0, kStateRandomTicks);
        else if (k == "skylight_down") flag(v != 0, kStateSkylightDown);
        else if (k == "requires_tool") flag(v != 0, kStateRequiresTool);
        else if (k == "ignited_by_lava") flag(v != 0, kStateIgnitedByLava);
        else if (k == "offset") flag(v != 0, kStateHasOffset);
        else if (k == "shape_occlusion") flag(v != 0, kStateShapeOcclusion);
        return true;
    }

    std::vector<StateInfo>& states_;
    std::vector<std::string>& item_;
    std::vector<std::string>& sound_;
    std::vector<std::array<float, 2>>& soundMix_;
    std::array<float, 2> curSoundMix_{1.0f, 1.0f};
    const std::vector<char>& want_;
    std::array<std::string, 8> keys_;
    int depth_ = 0;
    StateInfo cur_;
    unsigned curFlags_ = 0;
    int curId_ = -1;
    bool renderModel_ = false;
    std::string curItem_, curSound_;
};

// facts.json: {"states":[ [...], ... ], "<name>": {"minecraft:stone": 1, ...}} - the map is the default state id of
// every block. Only that map is wanted.
class DefaultsHandler {
public:
    using number_integer_t = json::number_integer_t;
    using number_unsigned_t = json::number_unsigned_t;
    using number_float_t = json::number_float_t;
    using string_t = json::string_t;
    using binary_t = json::binary_t;

    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_integer(number_integer_t v) { return take(static_cast<std::int64_t>(v)); }
    bool number_unsigned(number_unsigned_t v) { return take(static_cast<std::int64_t>(v)); }
    bool number_float(number_float_t, const string_t&) { return true; }
    bool string(string_t&) { return true; }
    bool binary(binary_t&) { return true; }
    bool start_object(std::size_t) {
        ++depth_;
        return true;
    }
    bool end_object() {
        --depth_;
        return true;
    }
    bool start_array(std::size_t) {
        ++depth_;
        return true;
    }
    bool end_array() {
        --depth_;
        return true;
    }
    bool key(string_t& v) {
        key_ = v;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& e) {
        error = e.what();
        return false;
    }

    std::unordered_map<std::string, int> defaults;
    std::string error;

private:
    bool take(std::int64_t v) {
        if (depth_ == 2 && key_.rfind("minecraft:", 0) == 0) defaults[std::string(stripNamespace(key_))] = static_cast<int>(v);
        return true;
    }
    int depth_ = 0;
    std::string key_;
};

// ---------------------------------------------------------------------------------------------------------------
// The binary cache.
constexpr char kCacheMagic[4] = {'F', 'B', 'R', '4'};  // (4: the sound volume and pitch are kept)

struct Fingerprint {
    std::uint64_t size[4] = {};
    std::int64_t time[4] = {};
    bool operator==(const Fingerprint&) const = default;
};

std::optional<Fingerprint> fingerprintOf(const fs::path& dir) {
    Fingerprint f;
    const char* names[4] = {"registries.json", "model-states.json", "gameplay-facts.json", "facts.json"};
    for (int i = 0; i < 4; ++i) {
        std::error_code ec;
        const fs::path p = dir / names[i];
        f.size[i] = fs::file_size(p, ec);
        if (ec) return std::nullopt;
        f.time[i] = static_cast<std::int64_t>(fs::last_write_time(p, ec).time_since_epoch().count());
        if (ec) return std::nullopt;
    }
    return f;
}

template <typename T>
void writePod(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
bool readPod(std::istream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}
void putString(std::ostream& out, const std::string& s) {
    writePod(out, static_cast<std::uint16_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
bool getString(std::istream& in, std::string& s) {
    std::uint16_t n = 0;
    if (!readPod(in, n)) return false;
    s.resize(n);
    in.read(s.data(), n);
    return static_cast<bool>(in);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

std::optional<int> BlockRegistry::findBlock(std::string_view name) const {
    const auto it = byName_.find(std::string(stripNamespace(name)));
    if (it == byName_.end()) return std::nullopt;
    return it->second;
}

std::optional<Block> BlockRegistry::find(std::string_view name) const {
    const auto index = findBlock(name);
    if (!index) return std::nullopt;
    return defaultState(*index);
}

namespace {
// Strides of the properties: the last property changes fastest.
std::vector<int> strides(const BlockType& t) {
    std::vector<int> s(t.properties.size(), 1);
    for (int i = static_cast<int>(t.properties.size()) - 2; i >= 0; --i) {
        s[static_cast<std::size_t>(i)] = s[static_cast<std::size_t>(i) + 1] * static_cast<int>(t.properties[static_cast<std::size_t>(i) + 1].values.size());
    }
    return s;
}
int propertyIndex(const BlockType& t, std::string_view name) {
    for (std::size_t i = 0; i < t.properties.size(); ++i) {
        if (t.properties[i].name == name) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

std::string_view BlockRegistry::get(Block b, std::string_view property) const {
    const BlockType& t = type(b);
    const int p = propertyIndex(t, property);
    if (p < 0) return {};
    const auto st = strides(t);
    const int local = stateId(b) - t.firstState;
    const auto& values = t.properties[static_cast<std::size_t>(p)].values;
    const int v = (local / st[static_cast<std::size_t>(p)]) % static_cast<int>(values.size());
    return values[static_cast<std::size_t>(v)];
}

Block BlockRegistry::with(Block b, std::string_view property, std::string_view value) const {
    const BlockType& t = type(b);
    const int p = propertyIndex(t, property);
    if (p < 0) return b;
    const auto& values = t.properties[static_cast<std::size_t>(p)].values;
    const auto it = std::find(values.begin(), values.end(), value);
    if (it == values.end()) return b;
    const auto st = strides(t);
    const int local = stateId(b) - t.firstState;
    const int current = (local / st[static_cast<std::size_t>(p)]) % static_cast<int>(values.size());
    const int target = static_cast<int>(it - values.begin());
    return static_cast<Block>(stateId(b) + (target - current) * st[static_cast<std::size_t>(p)]);
}

std::optional<Block> BlockRegistry::parse(std::string_view text) const {
    const std::size_t bracket = text.find('[');
    const auto base = find(text.substr(0, bracket));
    if (!base) return std::nullopt;
    Block result = *base;
    if (bracket == std::string_view::npos) return result;
    std::string_view props = text.substr(bracket + 1);
    if (!props.empty() && props.back() == ']') props.remove_suffix(1);
    while (!props.empty()) {
        const std::size_t comma = props.find(',');
        const std::string_view pair = props.substr(0, comma);
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos) result = with(result, pair.substr(0, eq), pair.substr(eq + 1));
        if (comma == std::string_view::npos) break;
        props.remove_prefix(comma + 1);
    }
    return result;
}

std::string BlockRegistry::describe(Block b) const {
    const BlockType& t = type(b);
    std::string out = t.name;
    if (!t.properties.empty()) {
        out += '[';
        for (std::size_t i = 0; i < t.properties.size(); ++i) {
            if (i) out += ',';
            out += t.properties[i].name + "=" + std::string(get(b, t.properties[i].name));
        }
        out += ']';
    }
    return out;
}

std::size_t BlockRegistry::memoryBytes() const {
    std::size_t total = states_.capacity() * sizeof(StateInfo) + blocks_.capacity() * sizeof(BlockType);
    for (const BlockType& t : blocks_) {
        total += t.name.capacity() + t.item.capacity() + t.sound.capacity() + t.properties.capacity() * sizeof(BlockProperty);
        for (const BlockProperty& p : t.properties) {
            total += p.name.capacity();
            for (const std::string& v : p.values) total += sizeof(std::string) + v.capacity();
        }
    }
    return total + byName_.size() * 64;
}

// ---------------------------------------------------------------------------------------------------------------

std::shared_ptr<const BlockRegistry> BlockRegistry::loadFromGameData(const fs::path& dir, const fs::path& cacheDir,
                                                                     std::string& error) {
    const auto fingerprint = fingerprintOf(dir);
    if (!fingerprint) {
        error = "the game's data files (registries.json, model-states.json, gameplay-facts.json, facts.json) are not in " + dir.string();
        return nullptr;
    }
    const fs::path cachePath = cacheDir / "blocks.cache";

    // 1. The cache, when it was made from these very files.
    {
        std::ifstream in(cachePath, std::ios::binary);
        char magic[4] = {};
        Fingerprint stored;
        std::uint32_t blockCount = 0, stateCount = 0;
        if (in.read(magic, 4) && std::memcmp(magic, kCacheMagic, 4) == 0 && readPod(in, stored) && stored == *fingerprint &&
            readPod(in, blockCount) && readPod(in, stateCount) && blockCount < 100000 && stateCount <= 65536) {
            auto reg = std::make_shared<BlockRegistry>();
            bool ok = true;
            BlockRegistryBuilder::blocks(*reg).resize(blockCount);
            for (BlockType& t : BlockRegistryBuilder::blocks(*reg)) {
                std::uint8_t propCount = 0;
                ok = ok && getString(in, t.name) && getString(in, t.item) && getString(in, t.sound) && readPod(in, t.soundVolume) && readPod(in, t.soundPitch) &&
                     readPod(in, t.firstState) && readPod(in, t.stateCount) && readPod(in, t.defaultState) && readPod(in, propCount);
                if (!ok) break;
                t.properties.resize(propCount);
                for (BlockProperty& p : t.properties) {
                    std::uint8_t valueCount = 0;
                    ok = ok && getString(in, p.name) && readPod(in, valueCount);
                    if (!ok) break;
                    p.values.resize(valueCount);
                    for (std::string& v : p.values) ok = ok && getString(in, v);
                }
                if (!ok) break;
            }
            if (ok) {
                BlockRegistryBuilder::states(*reg).resize(stateCount);
                in.read(reinterpret_cast<char*>(BlockRegistryBuilder::states(*reg).data()),
                        static_cast<std::streamsize>(stateCount * sizeof(StateInfo)));
                ok = static_cast<bool>(in);
            }
            if (ok) {
                BlockRegistryBuilder::reindex(*reg);
                reg->fromGameData_ = true;
                return reg;
            }
        }
    }

    // 2. Build it from the exported JSON.
    auto reg = std::make_shared<BlockRegistry>();
    try {
        // Block order and names.
        std::vector<std::pair<int, std::string>> order;
        {
            std::ifstream in(dir / "registries.json");
            const json registries = json::parse(in);
            const json& entries = registries.at("minecraft:block").at("entries");
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                order.emplace_back(it.value().at("protocol_id").get<int>(), std::string(stripNamespace(it.key())));
            }
            std::sort(order.begin(), order.end());
        }
        // Properties and state counts.
        {
            std::ifstream in(dir / "model-states.json");
            const ordered_json modelStates = ordered_json::parse(in);
            int next = 0;
            for (const auto& [protocol, name] : order) {
                (void)protocol;
                BlockType t;
                t.name = name;
                t.firstState = static_cast<std::uint16_t>(next);
                const auto entry = modelStates.find("minecraft:" + name);
                if (entry == modelStates.end()) {
                    t.stateCount = 1;
                } else {
                    const auto& states = entry->at("states");
                    t.stateCount = static_cast<std::uint16_t>(states.size());
                    for (auto prop = entry->at("schema").begin(); prop != entry->at("schema").end(); ++prop) {
                        BlockProperty p;
                        p.name = prop.key();
                        // Values in the order the states list them (it is the game's own numbering).
                        for (const auto& state : states) {
                            const auto& v = state.at(p.name);
                            const std::string text = v.is_string() ? v.get<std::string>() : v.dump();
                            if (std::find(p.values.begin(), p.values.end(), text) == p.values.end()) p.values.push_back(text);
                        }
                        t.properties.push_back(std::move(p));
                    }
                    // Check that the list really is a mixed-radix count with the last property fastest.
                    const auto st = strides(t);
                    std::size_t product = 1;
                    for (const BlockProperty& p : t.properties) product *= p.values.size();
                    if (product != states.size()) throw std::runtime_error("block " + name + ": state list is not a full property product");
                    for (std::size_t i = 0; i < states.size(); ++i) {
                        for (std::size_t p = 0; p < t.properties.size(); ++p) {
                            const auto& v = states[i].at(t.properties[p].name);
                            const std::string text = v.is_string() ? v.get<std::string>() : v.dump();
                            const std::size_t want = (i / static_cast<std::size_t>(st[p])) % t.properties[p].values.size();
                            if (t.properties[p].values[want] != text) throw std::runtime_error("block " + name + ": states are not in property order");
                        }
                    }
                }
                next += t.stateCount;
                BlockRegistryBuilder::addBlock(*reg, std::move(t));
            }
            BlockRegistryBuilder::states(*reg).assign(static_cast<std::size_t>(next), StateInfo{});
            if (next > 65535) throw std::runtime_error("more block states than a 16-bit id holds");
        }
        // Default states.
        {
            std::ifstream in(dir / "facts.json");
            DefaultsHandler handler;
            if (!json::sax_parse(in, &handler)) throw std::runtime_error("facts.json: " + handler.error);
            for (BlockType& t : BlockRegistryBuilder::blocks(*reg)) {
                const auto it = handler.defaults.find(t.name);
                t.defaultState = it != handler.defaults.end() && it->second >= t.firstState && it->second < t.firstState + t.stateCount
                                     ? static_cast<std::uint16_t>(it->second)
                                     : t.firstState;
            }
        }
        // Per-state facts (streamed).
        {
            auto& states = BlockRegistryBuilder::states(*reg);
            std::vector<std::string> item(states.size()), sound(states.size());
            std::vector<std::array<float, 2>> soundMix(states.size(), std::array<float, 2>{1.0f, 1.0f});
            std::vector<char> want(states.size(), 0);
            for (const BlockType& t : BlockRegistryBuilder::blocks(*reg)) want[t.defaultState] = 1;
            std::ifstream in(dir / "gameplay-facts.json");
            FactsHandler handler(states, item, sound, soundMix, want);
            if (!json::sax_parse(in, &handler)) throw std::runtime_error("gameplay-facts.json: " + handler.error);
            if (handler.seen != states.size()) {
                throw std::runtime_error("gameplay-facts.json has " + std::to_string(handler.seen) + " states, expected " + std::to_string(states.size()));
            }
            for (BlockType& t : BlockRegistryBuilder::blocks(*reg)) {
                const std::string& i = item[t.defaultState];
                t.item = i == "minecraft:air" && t.name != "air" ? std::string() : std::string(stripNamespace(i));
                // "minecraft:block.stone.step" -> "stone"
                std::string s(stripNamespace(sound[t.defaultState]));
                if (s.rfind("block.", 0) == 0) s.erase(0, 6);
                if (s.size() > 5 && s.compare(s.size() - 5, 5, ".step") == 0) s.erase(s.size() - 5);
                t.sound = std::move(s);
                t.soundVolume = soundMix[t.defaultState][0];
                t.soundPitch = soundMix[t.defaultState][1];
            }
        }
        BlockRegistryBuilder::finish(*reg);
        reg->fromGameData_ = true;
    } catch (const std::exception& e) {
        error = std::string("reading the block data failed: ") + e.what();
        return nullptr;
    }

    // 3. Save the cache (failing to is not an error).
    {
        std::error_code ec;
        fs::create_directories(cacheDir, ec);
        const fs::path tmp = cachePath.string() + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(kCacheMagic, 4);
            writePod(out, *fingerprint);
            writePod(out, static_cast<std::uint32_t>(reg->blocks_.size()));
            writePod(out, static_cast<std::uint32_t>(reg->states_.size()));
            for (const BlockType& t : reg->blocks_) {
                putString(out, t.name);
                putString(out, t.item);
                putString(out, t.sound);
                writePod(out, t.soundVolume);
                writePod(out, t.soundPitch);
                writePod(out, t.firstState);
                writePod(out, t.stateCount);
                writePod(out, t.defaultState);
                writePod(out, static_cast<std::uint8_t>(t.properties.size()));
                for (const BlockProperty& p : t.properties) {
                    putString(out, p.name);
                    writePod(out, static_cast<std::uint8_t>(p.values.size()));
                    for (const std::string& v : p.values) putString(out, v);
                }
            }
            out.write(reinterpret_cast<const char*>(reg->states_.data()), static_cast<std::streamsize>(reg->states_.size() * sizeof(StateInfo)));
            out.close();
            if (out) {
                fs::rename(tmp, cachePath, ec);
                if (ec) fs::remove(tmp, ec);
            }
        }
    }
    return reg;
}

std::shared_ptr<const BlockRegistry> BlockRegistry::extended(const BlockRegistry& base, const std::vector<ExtraBlock>& extra) {
    auto reg = std::make_shared<BlockRegistry>(base);
    for (const ExtraBlock& e : extra) {
        if (reg->byName_.count(e.name) != 0 || reg->states_.size() >= 65535) continue;
        BlockType type;
        type.name = e.name;
        type.firstState = static_cast<std::uint16_t>(reg->states_.size());
        type.stateCount = 1;
        type.defaultState = type.firstState;
        type.item = e.name;
        type.sound = e.sound;
        StateInfo state;
        state.block = static_cast<std::uint16_t>(reg->blocks_.size());
        state.flags = kStateHasModel | kStateFullCollision | (e.noOcclusion ? kStateSkylightDown : (kStateOpaqueCube | kStateCanOcclude)) | (e.requiresTool ? kStateRequiresTool : 0);
        state.emission = static_cast<std::uint8_t>(std::clamp(e.light, 0, 15));
        state.dampening = e.noOcclusion ? 0 : 15;
        state.hardness = e.hardness;
        state.explosionResistance = e.resistance;
        reg->states_.push_back(state);
        BlockRegistryBuilder::addBlock(*reg, std::move(type));
    }
    return reg;
}

std::shared_ptr<const BlockRegistry> BlockRegistry::makeMinimal() {
    auto reg = std::make_shared<BlockRegistry>();
    int next = 0;
    auto add = [&](const char* name, unsigned flags, std::vector<BlockProperty> props = {}) {
        BlockType t;
        t.name = name;
        t.firstState = static_cast<std::uint16_t>(next);
        int count = 1;
        for (const BlockProperty& p : props) count *= static_cast<int>(p.values.size());
        t.stateCount = static_cast<std::uint16_t>(count);
        t.defaultState = t.firstState;
        t.properties = std::move(props);
        t.item = name;
        t.sound = "stone";
        StateInfo info;
        info.flags = static_cast<std::uint16_t>(flags);
        info.dampening = (flags & kStateOpaqueCube) ? 15 : 0;
        info.hardness = (flags & kStateOpaqueCube) ? 1.5f : 0.0f;
        for (int i = 0; i < count; ++i) BlockRegistryBuilder::states(*reg).push_back(info);
        next += count;
        BlockRegistryBuilder::addBlock(*reg, std::move(t));
    };
    const unsigned solid = kStateOpaqueCube | kStateCanOcclude | kStateFullCollision | kStateHasModel;
    add("air", kStateReplaceable | kStateSkylightDown);
    add("stone", solid);
    add("dirt", solid);
    add("grass_block", solid, {{"snowy", {"true", "false"}}});
    BlockRegistryBuilder::blocks(*reg).back().defaultState = static_cast<std::uint16_t>(next - 1);
    add("bedrock", solid);
    add("sand", solid);
    add("gravel", solid);
    add("cobblestone", solid);
    add("water", kStateLiquid | kStateReplaceable | kStateHasModel | kStateSkylightDown, {{"level", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"}}});
    add("lava", kStateLiquid | kStateReplaceable | kStateHasModel, {{"level", {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"}}});
    add("deepslate", solid, {{"axis", {"x", "y", "z"}}});
    BlockRegistryBuilder::blocks(*reg).back().defaultState = static_cast<std::uint16_t>(next - 2);
    add("oak_planks", solid);
    add("oak_log", solid, {{"axis", {"x", "y", "z"}}});
    BlockRegistryBuilder::blocks(*reg).back().defaultState = static_cast<std::uint16_t>(next - 2);
    add("bricks", solid);
    add("stone_bricks", solid);
    add("sandstone", solid);
    add("glowstone", solid);
    BlockRegistryBuilder::states(*reg).back().emission = 15;
    add("torch", kStateHasModel);
    BlockRegistryBuilder::states(*reg).back().emission = 14;
    BlockRegistryBuilder::finish(*reg);
    return reg;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
std::shared_ptr<const BlockRegistry> g_registry;
CommonBlocks g_common;
std::once_flag g_fallbackOnce;

Block lookup(const BlockRegistry& r, std::string_view name) { return r.find(name).value_or(Block::Air); }
}  // namespace

void installBlockRegistry(std::shared_ptr<const BlockRegistry> registry) {
    g_registry = std::move(registry);
    if (!g_registry) {
        g_common = CommonBlocks{};
        return;
    }
    const BlockRegistry& r = *g_registry;
    g_common = {lookup(r, "air"),    lookup(r, "stone"),     lookup(r, "dirt"),        lookup(r, "grass_block"),
                lookup(r, "bedrock"), lookup(r, "sand"),     lookup(r, "gravel"),      lookup(r, "cobblestone"),
                lookup(r, "water"),   lookup(r, "lava"),     lookup(r, "deepslate"),   lookup(r, "oak_planks"),
                lookup(r, "oak_log")};
}

bool hasBlockRegistry() { return g_registry != nullptr; }

std::shared_ptr<const BlockRegistry> currentBlockRegistry() { return g_registry; }

const BlockRegistry& blockRegistry() {
    if (!g_registry) std::call_once(g_fallbackOnce, [] { installBlockRegistry(BlockRegistry::makeMinimal()); });
    return *g_registry;
}

const CommonBlocks& commonBlocks() {
    (void)blockRegistry();
    return g_common;
}

}  // namespace falcon
