// The clock (world/day_cycle.hpp): where the sun is at the times the game's players know, how dark it gets, and what /time understands.
#include "falcon/world/day_cycle.hpp"
#include "test_logic.hpp"

#include <cmath>
#include <string>

namespace {
bool near(float a, float b, float tolerance = 1.0e-3f) { return std::fabs(a - b) <= tolerance; }
}  // namespace

void testDayCycle() {
    using namespace falcon::daycycle;
    // The sun's turn: 0 at noon, half a turn at midnight, and the values the game's formula gives at sunrise and at the end of the day.
    LCHECK(near(timeOfDay(6000), 0.0f));
    LCHECK(near(timeOfDay(18000), 0.5f));
    LCHECK(near(timeOfDay(0), 0.7845f));
    LCHECK(near(timeOfDay(13000), 0.2599f));
    LCHECK(near(timeOfDay(30000), timeOfDay(6000)));  // a day is 24000 ticks

    // Noon: full sky, no stars, the sun overhead, no sunrise glow, nothing taken off the sky light.
    const Sky noon = sky(6000);
    LCHECK(near(noon.zenith[0], 0.471f) && near(noon.zenith[2], 1.0f));
    LCHECK(near(noon.horizon[0], 0.753f));
    LCHECK(near(noon.darkSteps, 0.0f));
    LCHECK(near(noon.stars, 0.0f));
    LCHECK(near(noon.glow[3], 0.0f));
    LCHECK(near(noon.sunDir[1], 1.0f) && near(noon.sunDir[0], 0.0f));

    // Midnight: a black zenith, the stars out, the sun under the world, eleven levels of sky light gone.
    const Sky midnight = sky(18000);
    LCHECK(near(midnight.zenith[0], 0.0f) && near(midnight.zenith[2], 0.0f));
    LCHECK(near(midnight.darkSteps, 11.0f));
    LCHECK(near(midnight.stars, 0.5f));
    LCHECK(near(midnight.skyLightFactor, 0.2f));
    LCHECK(near(midnight.sunDir[1], -1.0f));

    // Sunrise: the sun is up in the east (+x) just above the horizon and the glow is there; sunset has it in the west.
    const Sky dawn = sky(0);
    LCHECK(dawn.sunDir[0] > 0.9f && dawn.sunDir[1] > 0.0f && dawn.sunDir[1] < 0.3f);
    LCHECK(dawn.glow[3] > 0.1f);
    const Sky dusk = sky(12500);
    LCHECK(dusk.sunDir[0] < -0.9f);
    LCHECK(dusk.glow[3] > 0.1f);
    // The sky never goes dark by day and never stays light at night.
    for (long long t = 0; t < kTicksPerDay; t += 250) {
        const Sky s = sky(static_cast<double>(t));
        LCHECK(s.darkSteps >= 0.0f && s.darkSteps <= 11.0f);
        LCHECK(s.zenith[2] >= 0.0f && s.zenith[2] <= 1.0f);
    }
    LCHECK(sky(1000).darkSteps < 1.0f);
    LCHECK(sky(15000).darkSteps > 10.0f);

    // The moon goes through eight phases in eight days.
    LCHECK(moonPhase(0) == 0);
    LCHECK(moonPhase(kTicksPerDay) == 1);
    LCHECK(moonPhase(kTicksPerDay * 4 + 5) == 4);
    LCHECK(moonPhase(kTicksPerDay * 8) == 0);

    // What /time takes.
    LCHECK(parseTime("day") == 1000);
    LCHECK(parseTime("noon") == 6000);
    LCHECK(parseTime("night") == 13000);
    LCHECK(parseTime("midnight") == 18000);
    LCHECK(parseTime("6000") == 6000);
    LCHECK(parseTime("10t") == 10);
    LCHECK(parseTime("5s") == 100);
    LCHECK(parseTime("1d") == 24000);
    LCHECK(parseTime("0.5d") == 12000);
    LCHECK(!parseTime("").has_value());
    LCHECK(!parseTime("abc").has_value());
    LCHECK(!parseTime("10x").has_value());

    // The game's own tracks, here a cut-down day: the sun's angle eased between two noons, the sky going dark between 12000 and 14000 and light again between 22000 and 24000,
    // the fog the same, a sunrise glow, and the dimension's base colours.
    const std::string dayJson = R"({
        "period_ticks": 24000,
        "tracks": {
            "minecraft:visual/sun_angle": {"ease": {"cubic_bezier": [0.362, 0.241, 0.638, 0.759]},
                "keyframes": [{"ticks": 6000, "value": 360.0}, {"ticks": 6000, "value": 0.0}]},
            "minecraft:visual/sky_color": {"modifier": "multiply", "keyframes": [
                {"ticks": 0, "value": "#ffffff"}, {"ticks": 12000, "value": "#ffffff"}, {"ticks": 14000, "value": "#000000"}, {"ticks": 22000, "value": "#000000"}]},
            "minecraft:visual/sunrise_sunset_color": {"keyframes": [
                {"ticks": 1000, "value": "#00ffe533"}, {"ticks": 12000, "value": "#00ffe533"}, {"ticks": 13000, "value": "#ffe47e33"}]},
            "minecraft:visual/star_brightness": {"modifier": "maximum", "keyframes": [{"ticks": 6000, "value": 0.0}, {"ticks": 18000, "value": 0.5}]},
            "minecraft:gameplay/sky_light_level": {"ease": "constant", "keyframes": [{"ticks": 0, "value": 1.0}, {"ticks": 13000, "value": 0.2666}]},
            "minecraft:gameplay/bees_stay_in_hive": {"keyframes": [{"ticks": 12542, "value": true}]}
        }})";
    const std::string dimensionJson = R"({"attributes": {"minecraft:visual/sky_color": "#78a7ff", "minecraft:visual/fog_color": "#c0d8ff",
        "minecraft:visual/ambient_light_color": "#0a0a0a"}})";
    std::string error;
    const auto timeline = Timeline::fromJson(dayJson, dimensionJson, error);
    LCHECK(timeline != nullptr);
    if (timeline != nullptr) {
        LCHECK(timeline->trackCount() == 5);  // (the track about bees is none of the sky's)
        const Sky tNoon = timeline->sky(6000);
        LCHECK(near(tNoon.sunDir[1], 1.0f) && near(tNoon.sunDir[0], 0.0f));
        LCHECK(near(tNoon.zenith[0], 0.4706f) && near(tNoon.zenith[1], 0.6549f) && near(tNoon.zenith[2], 1.0f));  // #78a7ff
        LCHECK(near(tNoon.ambient[0], 0.0392f));
        // The Bezier eases the sun like the closed form does, to within a tick or two.
        for (const double t : {0.0, 3000.0, 9000.0, 12000.0, 15000.0, 18000.0, 21000.0, 23000.0}) {
            const Sky a = timeline->sky(t);
            const Sky b = sky(t);
            LCHECK(near(a.sunDir[0], b.sunDir[0], 0.02f) && near(a.sunDir[1], b.sunDir[1], 0.02f));
        }
        // Midnight: the sun straight down, the sky black, the stars at the keyframe's brightness, the sky light at 0.2666 so 11 levels gone.
        const Sky tMidnight = timeline->sky(18000);
        LCHECK(near(tMidnight.sunDir[1], -1.0f, 0.01f));
        LCHECK(near(tMidnight.zenith[0], 0.0f));
        LCHECK(near(tMidnight.stars, 0.5f));
        LCHECK(near(tMidnight.darkSteps, 11.0f, 0.05f));
        // Half way through the dusk the sky is half as bright; the glow is linear between its keyframes (and a colour's alpha is its strength).
        const Sky dusk2 = timeline->sky(13000);
        LCHECK(near(dusk2.zenith[2], 0.5f, 0.01f));
        LCHECK(near(dusk2.glow[3], 1.0f, 0.01f) && near(dusk2.glow[0], 0.8941f, 0.01f));
        LCHECK(near(timeline->sky(12500).glow[3], 0.5f, 0.02f));
        // The day goes round: the keyframes after the last one carry on over midnight to the first.
        LCHECK(near(timeline->sky(23000).zenith[2], 0.5f, 0.01f));
        LCHECK(near(timeline->sky(23000 + 24000).zenith[2], 0.5f, 0.01f));
    }
    std::string badError;
    LCHECK(Timeline::fromJson("{}", "", badError) == nullptr);
    LCHECK(!badError.empty());
    LCHECK(Timeline::fromJson("not json", "", badError) == nullptr);
}
