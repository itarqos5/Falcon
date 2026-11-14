#include "falcon/world/water_fog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace falcon {

float waterVision(int ticksUnderwater, bool eyesInWater) {
    if (!eyesInWater) return 0.0f;
    if (ticksUnderwater >= 600) return 1.0f;
    const float f = std::clamp(static_cast<float>(ticksUnderwater) / 100.0f, 0.0f, 1.0f);
    const float g = ticksUnderwater < 100 ? 0.0f : std::clamp((static_cast<float>(ticksUnderwater) - 100.0f) / 500.0f, 0.0f, 1.0f);
    return f * 0.6f + g * 0.39999998f;
}

int nextWaterVisionTicks(int ticks, bool eyesInWater, bool spectator) {
    if (eyesInWater) return std::clamp(ticks + (spectator ? 10 : 1), 0, 600);
    return ticks > 0 ? std::clamp(ticks - 10, 0, 600) : ticks;
}

std::array<float, 3> underwaterFogColor(const std::array<float, 3>& c, float vision) {
    if (c[0] == 0.0f || c[1] == 0.0f || c[2] == 0.0f) return c;  // (the game leaves a colour with an empty channel alone)
    const float scale = 1.0f / std::max(c[0], std::max(c[1], c[2]));
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) out[i] = c[i] + vision * (c[i] * scale - c[i]);  // Mth.lerp(vision, c, c * scale)
    return out;
}

float underwaterFogEnd(float biomeEndFactor, float vision) { return 96.0f * biomeEndFactor * std::max(0.25f, vision); }

namespace {

// "#rrggbb" -> 0..1 channels; false when it is not in that form.
bool parseHexColor(const std::string& text, std::array<float, 3>& out) {
    if (text.size() != 7 || text[0] != '#') return false;
    unsigned value = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        value = value * 16 + digit;
    }
    out = {static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f, static_cast<float>(value & 0xFF) / 255.0f};
    return true;
}

}  // namespace

WaterFogTable WaterFogTable::load(const std::filesystem::path& dataDir) {
    WaterFogTable table;
    std::error_code ec;
    const std::filesystem::path dir = dataDir / "data" / "minecraft" / "worldgen" / "biome";
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false, true);
        if (!doc.is_object()) continue;
        const auto attributes = doc.find("attributes");
        if (attributes == doc.end() || !attributes->is_object()) continue;
        WaterFog fog;
        bool any = false;
        if (const auto c = attributes->find("minecraft:visual/water_fog_color"); c != attributes->end() && c->is_string()) {
            any = parseHexColor(c->get<std::string>(), fog.color) || any;
        }
        if (const auto e = attributes->find("minecraft:visual/water_fog_end_distance"); e != attributes->end()) {
            // A number replaces the default end (96): as a factor of it. {"argument": 0.85, "modifier": "multiply"} multiplies it.
            if (e->is_number()) {
                fog.endFactor = e->get<float>() / 96.0f;
                any = true;
            } else if (e->is_object() && e->contains("argument") && (*e)["argument"].is_number()) {
                const float argument = (*e)["argument"].get<float>();
                fog.endFactor = (*e).value("modifier", std::string()) == "multiply" ? argument : argument / 96.0f;
                any = true;
            }
        }
        if (any) table.fogs_[entry.path().stem().string()] = fog;
    }
    return table;
}

const WaterFog& WaterFogTable::forBiome(std::string_view name) const {
    constexpr std::string_view kPrefix = "minecraft:";
    if (name.substr(0, kPrefix.size()) == kPrefix) name.remove_prefix(kPrefix.size());
    const auto it = fogs_.find(std::string(name));
    return it == fogs_.end() ? default_ : it->second;
}

}  // namespace falcon
