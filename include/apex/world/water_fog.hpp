#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace apex {

// What the world looks like from under water, as the game works it out (WaterFogEnvironment, FogRenderer.computeFogColor and
// LocalPlayer.getWaterVision). Underwater the far terrain fades into a blue-ish fog that starts right at the eyes: at first it
// is dark and thick (24 blocks of sight), and over the next 30 seconds (600 ticks) it clears and brightens - the player's eyes "get
// used to it".

// LocalPlayer.getWaterVision(): 0 the moment the eyes go under, 1 after 600 ticks under; 0 when the eyes are in no water.
[[nodiscard]] float waterVision(int ticksUnderwater, bool eyesInWater);
// LocalPlayer.tick: the ticks counter goes up one a tick with the eyes under water (ten for a spectator) to 600, and down ten a tick
// out of it.
[[nodiscard]] int nextWaterVisionTicks(int ticks, bool eyesInWater, bool spectator);

// The fog colour (r, g, b, 0..1) under water: the biome's water fog colour, brightened with the vision until its strongest channel is
// full (so "0x050533" becomes a vivid blue after the eyes have got used to the water).
[[nodiscard]] std::array<float, 3> underwaterFogColor(const std::array<float, 3>& biomeColor, float vision);
// How far the fog reaches (blocks): 96 times the biome's factor, scaled by the vision down to a quarter.
[[nodiscard]] float underwaterFogEnd(float biomeEndFactor, float vision);
inline constexpr float kWaterFogStart = -8.0f;  // the game's default start: the fog is already thickening at the eyes

// A biome's own water fog: a colour (default 0x050533) and a factor for how far it reaches (default 1; the swamp's is 0.85).
struct WaterFog {
    std::array<float, 3> color = {5.0f / 255.0f, 5.0f / 255.0f, 51.0f / 255.0f};
    float endFactor = 1.0f;
};

// The water fog of every biome, read from the game's biome files (data/minecraft/worldgen/biome/*.json: the attributes
// "minecraft:visual/water_fog_color" and "minecraft:visual/water_fog_end_distance").
class WaterFogTable {
public:
    // `dataDir` is the folder that holds data/minecraft/...; a missing folder gives an empty table (every biome the default).
    static WaterFogTable load(const std::filesystem::path& dataDir);
    // "minecraft:swamp" or "swamp"; an unknown biome has the default fog.
    [[nodiscard]] const WaterFog& forBiome(std::string_view name) const;
    [[nodiscard]] std::size_t size() const { return fogs_.size(); }

private:
    std::unordered_map<std::string, WaterFog> fogs_;
    WaterFog default_;
};

}  // namespace apex
