#include "falcon/world/day_cycle.hpp"

#include "falcon/worldgen/json_tree.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace falcon::daycycle {

namespace {
constexpr double kPi = 3.14159265358979323846;

// "#rrggbb" or "#aarrggbb" as r, g, b, a in 0..1.
bool parseColor(const std::string& text, std::array<double, 4>& out) {
    if (text.size() != 7 && text.size() != 9) return false;
    if (text[0] != '#') return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(text.c_str() + 1, &end, 16);
    if (end == text.c_str() + 1 || *end != '\0') return false;
    const bool hasAlpha = text.size() == 9;
    const unsigned a = hasAlpha ? (v >> 24) & 0xFF : 0xFF;
    out = {static_cast<double>((v >> 16) & 0xFF) / 255.0, static_cast<double>((v >> 8) & 0xFF) / 255.0, static_cast<double>(v & 0xFF) / 255.0,
           static_cast<double>(a) / 255.0};
    return true;
}

// CubicBezier (the game's easing for the sun, the moon and the stars): y at the x where the curve through (0,0), (x1,y1), (x2,y2), (1,1) has `progress`.
double bezierEase(const std::array<double, 4>& c, double progress) {
    const auto point = [](double t, double p1, double p2) { return 3.0 * (1.0 - t) * (1.0 - t) * t * p1 + 3.0 * (1.0 - t) * t * t * p2 + t * t * t; };
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (point(mid, c[0], c[2]) < progress) lo = mid;
        else hi = mid;
    }
    return point((lo + hi) / 2.0, c[1], c[3]);
}

const worldgen::JsonValue* member(const worldgen::JsonValue& v, const char* key) { return v.isObject() ? v.find(key) : nullptr; }

std::array<float, 3> rgb(const std::array<double, 4>& c) { return {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])}; }
}  // namespace

float timeOfDay(double dayTime) {
    double d = dayTime / static_cast<double>(kTicksPerDay) - 0.25;
    d -= std::floor(d);  // Mth.frac
    const double e = 0.5 - std::cos(d * kPi) / 2.0;
    return static_cast<float>((d * 2.0 + e) / 3.0);
}

int moonPhase(long long dayTime) {
    const long long day = dayTime / kTicksPerDay;
    return static_cast<int>(((day % 8) + 8) % 8);
}

Sky sky(double dayTime) {
    Sky s;
    const float t = timeOfDay(dayTime);
    const float angle = t * 2.0f * static_cast<float>(kPi);
    const float c = std::cos(angle);
    // The light of the sky: 1 by day, 0 at night, with a quick change round dusk and dawn.
    const float f = std::clamp(c * 2.0f + 0.5f, 0.0f, 1.0f);
    s.zenith = {0.471f * f, 0.655f * f, 1.0f * f};
    s.horizon = {0.753f * (f * 0.94f + 0.06f), 0.847f * (f * 0.94f + 0.06f), 1.0f * (f * 0.91f + 0.09f)};
    s.darkSteps = (1.0f - f) * 11.0f;
    s.skyLightFactor = std::clamp(c * 2.0f + 0.2f, 0.0f, 1.0f) * 0.8f + 0.2f;  // Level.getSkyDarken
    // The sun climbs in the east (+x) and goes down in the west; at noon it is overhead.
    s.sinAngle = std::sin(angle);
    s.cosAngle = c;
    s.sunDir = {-s.sinAngle, c, 0.0f};
    const float starLight = std::clamp(1.0f - (c * 2.0f + 0.25f), 0.0f, 1.0f);
    s.stars = starLight * starLight * 0.5f;
    // A glow along the horizon while the sun is within a few degrees of it.
    if (c >= -0.4f && c <= 0.4f) {
        const float h = c / 0.4f * 0.5f + 0.5f;
        float i = 1.0f - (1.0f - std::sin(h * static_cast<float>(kPi))) * 0.99f;
        i *= i;
        s.glow = {h * 0.3f + 0.7f, h * h * 0.7f + 0.2f, 0.2f, i};
    }
    s.moonPhase = static_cast<float>(moonPhase(static_cast<long long>(std::floor(dayTime))));
    return s;
}

std::optional<long long> parseTime(const std::string& text) {
    if (text == "day") return 1000;
    if (text == "noon") return 6000;
    if (text == "night") return 13000;
    if (text == "midnight") return 18000;
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return std::nullopt;
    const std::string unit(end);
    double factor = 1.0;
    if (unit.empty() || unit == "t") factor = 1.0;
    else if (unit == "s") factor = 20.0;
    else if (unit == "d") factor = static_cast<double>(kTicksPerDay);
    else return std::nullopt;
    return static_cast<long long>(std::llround(value * factor));
}

// ---- Timeline ----------------------------------------------------------------------------------------------

std::shared_ptr<const Timeline> Timeline::fromJson(const std::string& dayJson, const std::string& dimensionJson, std::string& error) {
    worldgen::JsonValue day;
    if (!worldgen::parseJson(dayJson, day, error)) return nullptr;
    const worldgen::JsonValue* tracks = member(day, "tracks");
    if (tracks == nullptr || !tracks->isObject()) {
        error = "the day timeline has no tracks";
        return nullptr;
    }
    auto timeline = std::make_shared<Timeline>();
    if (const worldgen::JsonValue* period = member(day, "period_ticks"); period != nullptr) {
        double p = 0.0;
        if (period->toDouble(p) && p > 0.0) timeline->period_ = p;
    }
    for (const auto& [name, trackJson] : tracks->members) {
        if (name.rfind("minecraft:visual/", 0) != 0 && name != "minecraft:gameplay/sky_light_level") continue;  // only what the sky and the light need
        const worldgen::JsonValue* keys = member(trackJson, "keyframes");
        if (keys == nullptr || !keys->isArray() || keys->items.empty()) continue;
        Track track;
        if (const worldgen::JsonValue* ease = member(trackJson, "ease"); ease != nullptr) {
            if (ease->isString() && ease->text == "constant") track.constant = true;
            if (const worldgen::JsonValue* curve = member(*ease, "cubic_bezier"); curve != nullptr && curve->isArray() && curve->items.size() == 4) {
                track.bezier = true;
                for (std::size_t i = 0; i < 4; ++i) {
                    if (!curve->items[i].toDouble(track.curve[i])) track.bezier = false;
                }
            }
        }
        bool valid = true;
        for (const worldgen::JsonValue& keyJson : keys->items) {
            Key key;
            const worldgen::JsonValue* ticks = member(keyJson, "ticks");
            const worldgen::JsonValue* value = member(keyJson, "value");
            if (ticks == nullptr || value == nullptr || !ticks->toDouble(key.ticks)) {
                valid = false;
                break;
            }
            if (value->isString()) {
                if (!parseColor(value->text, key.value)) {
                    valid = false;
                    break;
                }
            } else if (!value->toDouble(key.value[0])) {
                valid = false;
                break;
            }
            track.keys.push_back(key);
        }
        if (valid) timeline->tracks_[name] = std::move(track);
    }
    if (timeline->tracks_.empty()) {
        error = "the day timeline has no usable tracks";
        return nullptr;
    }
    if (!dimensionJson.empty()) {
        worldgen::JsonValue dimension;
        std::string ignored;
        if (worldgen::parseJson(dimensionJson, dimension, ignored)) {
            if (const worldgen::JsonValue* attributes = member(dimension, "attributes"); attributes != nullptr) {
                const auto read = [&](const char* key, std::array<float, 3>& into) {
                    const worldgen::JsonValue* v = member(*attributes, key);
                    std::array<double, 4> c{};
                    if (v != nullptr && v->isString() && parseColor(v->text, c)) into = rgb(c);
                };
                read("minecraft:visual/sky_color", timeline->baseSky_);
                read("minecraft:visual/fog_color", timeline->baseFog_);
                read("minecraft:visual/ambient_light_color", timeline->baseAmbient_);
            }
        }
    }
    return timeline;
}

std::shared_ptr<const Timeline> Timeline::loadFromGameData(const std::filesystem::path& dataRoot, std::string& error) {
    const auto read = [](const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    };
    const std::filesystem::path root = dataRoot / "data" / "minecraft";
    const std::string day = read(root / "timeline" / "day.json");
    if (day.empty()) {
        error = "missing " + (root / "timeline" / "day.json").string();
        return nullptr;
    }
    return fromJson(day, read(root / "dimension_type" / "overworld.json"), error);
}

std::optional<std::array<double, 4>> Timeline::evaluate(const std::string& name, double ticks) const {
    const auto it = tracks_.find(name);
    if (it == tracks_.end()) return std::nullopt;
    const Track& track = it->second;
    const std::vector<Key>& keys = track.keys;
    if (keys.size() == 1) return keys[0].value;
    double t = std::fmod(ticks, period_);
    if (t < 0.0) t += period_;
    if (t < keys.front().ticks) t += period_;  // before the first keyframe the last one is still going, over the turn of the day
    std::size_t i = 0;
    for (std::size_t j = 0; j < keys.size(); ++j) {
        if (keys[j].ticks <= t) i = j;
    }
    const Key& a = keys[i];
    const Key& b = i + 1 < keys.size() ? keys[i + 1] : keys.front();
    const double bTicks = i + 1 < keys.size() ? b.ticks : b.ticks + period_;
    if (track.constant || bTicks <= a.ticks) return a.value;
    double progress = std::clamp((t - a.ticks) / (bTicks - a.ticks), 0.0, 1.0);
    if (track.bezier) progress = bezierEase(track.curve, progress);
    std::array<double, 4> out{};
    for (std::size_t c = 0; c < 4; ++c) out[c] = a.value[c] + (b.value[c] - a.value[c]) * progress;
    return out;
}

Sky Timeline::sky(double dayTime) const {
    Sky s = daycycle::sky(dayTime);  // what a track does not say stays as the approximation has it
    const auto track = [&](const char* name) { return evaluate(std::string("minecraft:visual/") + name, dayTime); };
    if (const auto v = track("sun_angle")) {
        const float angle = static_cast<float>((*v)[0] * kPi / 180.0);
        s.sunDir = {-std::sin(angle), std::cos(angle), 0.0f};
    }
    if (const auto v = track("star_angle")) {
        const float angle = static_cast<float>((*v)[0] * kPi / 180.0);
        s.sinAngle = std::sin(angle);
        s.cosAngle = std::cos(angle);
    }
    if (const auto v = track("sky_color")) {
        s.zenith = {baseSky_[0] * static_cast<float>((*v)[0]), baseSky_[1] * static_cast<float>((*v)[1]), baseSky_[2] * static_cast<float>((*v)[2])};
    }
    if (const auto v = track("fog_color")) {
        s.horizon = {baseFog_[0] * static_cast<float>((*v)[0]), baseFog_[1] * static_cast<float>((*v)[1]), baseFog_[2] * static_cast<float>((*v)[2])};
    }
    if (const auto v = track("star_brightness")) s.stars = static_cast<float>((*v)[0]);
    if (const auto v = track("sunrise_sunset_color")) s.glow = {static_cast<float>((*v)[0]), static_cast<float>((*v)[1]), static_cast<float>((*v)[2]), static_cast<float>((*v)[3])};
    if (const auto v = track("sky_light_factor")) s.skyLightFactor = static_cast<float>((*v)[0]);
    if (const auto v = track("sky_light_color")) s.skyLightColor = rgb(*v);
    if (const auto v = evaluate("minecraft:gameplay/sky_light_level", dayTime)) s.darkSteps = static_cast<float>((1.0 - (*v)[0]) * 15.0);
    s.ambient = baseAmbient_;
    return s;
}

}  // namespace falcon::daycycle
