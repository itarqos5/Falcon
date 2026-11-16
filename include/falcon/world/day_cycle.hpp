#pragma once

// The day and the night: what the game's clock says about the sky. The time of day counts ticks, 24000 to a day (0 is sunrise, 6000 noon, 12000 sunset, 18000 midnight), and the
// sun, the moon, the stars, the colour of the sky and the fog, the glow at sunrise and how much of the sky's light reaches the ground all follow from it.
//
// The game keeps these as keyframed tracks in its own data (data/minecraft/timeline/day.json, and the base colours in dimension_type/overworld.json): `Timeline` reads
// those at run time and evaluates them the way the game does (linear between keyframes, round the day, with the cubic Bezier easing the sun and the stars use). Without the
// files `sky()` gives the older closed-form approximation, which agrees to within a tick or two.

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace falcon::daycycle {

inline constexpr long long kTicksPerDay = 24000;
inline constexpr long long kNoon = 6000;

// How far the sun has turned, 0 to 1: 0 at noon, 0.5 at midnight. It is not a straight line of the time: dawn and dusk go by quicker than noon and night.
[[nodiscard]] float timeOfDay(double dayTime);

// What the sky looks like at a time of day.
struct Sky {
    std::array<float, 3> horizon{0.753f, 0.847f, 1.0f};  // the sky at the horizon, which is also the colour of the fog
    std::array<float, 3> zenith{0.471f, 0.655f, 1.0f};   // the sky overhead
    std::array<float, 3> sunDir{0.0f, 1.0f, 0.0f};       // towards the sun (the moon is opposite); (0, 0, 0): there is no sun or moon (under water, the Nether)
    float sinAngle = 0.0f, cosAngle = 1.0f;              // of the stars' angle: they turn with the sun
    float stars = 0.0f;                                  // how bright the stars are, 0 to 0.5
    std::array<float, 4> glow{0.0f, 0.0f, 0.0f, 0.0f};   // the colour and strength of the sunrise or the sunset at the horizon (strength 0: none)
    float moonPhase = 0.0f;                              // 0 full ... 4 new, in eighths of a month
    float darkSteps = 0.0f;                              // how many levels of sky light the night takes away for the rules of the game (spawning, sleeping): 0 by day, 11 at midnight
    float skyLightFactor = 1.0f;                         // how much of the sky's light the light map lets through (the game's SkyFactor)
    std::array<float, 3> skyLightColor{1.0f, 1.0f, 1.0f};  // the colour of the sky's light: blue at night
    std::array<float, 3> ambient{0.0f, 0.0f, 0.0f};        // the light every place has, however dark (the dimension's ambient light colour)
};
// The sky for a time by the closed-form approximation (the tick count, with the part of the tick that has passed).
[[nodiscard]] Sky sky(double dayTime);

// 0 (full moon) to 7: the moon goes through its eight phases in eight days.
[[nodiscard]] int moonPhase(long long dayTime);

// What /time takes: ticks ("6000", "12000t"), seconds ("5s"), days ("1d", "0.5d") or one of day (1000), noon (6000), night (13000) and midnight (18000).
[[nodiscard]] std::optional<long long> parseTime(const std::string& text);

// The game's own day: its tracks, read from its data.
class Timeline {
public:
    // Reads <dataRoot>/data/minecraft/timeline/day.json and dimension_type/overworld.json. Null, with `error`, when the first is missing or unusable.
    [[nodiscard]] static std::shared_ptr<const Timeline> loadFromGameData(const std::filesystem::path& dataRoot, std::string& error);
    // The same from the text of the two files (the second may be empty: the base colours of the Overworld are then the ones the game ships).
    [[nodiscard]] static std::shared_ptr<const Timeline> fromJson(const std::string& dayJson, const std::string& dimensionJson, std::string& error);

    [[nodiscard]] Sky sky(double dayTime) const;
    [[nodiscard]] std::size_t trackCount() const { return tracks_.size(); }

private:
    struct Key {
        double ticks = 0.0;
        std::array<double, 4> value{};  // a number in [0]; a colour as r, g, b, a in 0..1
    };
    struct Track {
        std::vector<Key> keys;
        bool constant = false;                // ease "constant": each value holds until the next keyframe
        bool bezier = false;                  // ease cubic_bezier
        std::array<double, 4> curve{};        // its control points x1, y1, x2, y2
    };
    [[nodiscard]] std::optional<std::array<double, 4>> evaluate(const std::string& name, double ticks) const;

    std::map<std::string, Track> tracks_;
    double period_ = static_cast<double>(kTicksPerDay);
    std::array<float, 3> baseSky_{0.470588f, 0.654902f, 1.0f};  // #78a7ff
    std::array<float, 3> baseFog_{0.752941f, 0.847059f, 1.0f};  // #c0d8ff
    std::array<float, 3> baseAmbient_{0.039216f, 0.039216f, 0.039216f};  // #0a0a0a
};

}  // namespace falcon::daycycle
