#include "apex/world/item_stack.hpp"

#include <cmath>

namespace apex {
namespace {

// Mth.hsvToRgb.
std::uint32_t hsvToRgb(float hue, float saturation, float value) {
    const int i = static_cast<int>(hue * 6.0f) % 6;
    const float f = hue * 6.0f - static_cast<float>(i);
    const float f1 = value * (1.0f - saturation);
    const float f2 = value * (1.0f - f * saturation);
    const float f3 = value * (1.0f - (1.0f - f) * saturation);
    float r = 0, g = 0, b = 0;
    switch (i) {
        case 0: r = value; g = f3; b = f1; break;
        case 1: r = f2; g = value; b = f1; break;
        case 2: r = f1; g = value; b = f3; break;
        case 3: r = f1; g = f2; b = value; break;
        case 4: r = f3; g = f1; b = value; break;
        default: r = value; g = f1; b = f2; break;
    }
    const auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(static_cast<int>(v * 255.0f), 0, 255)); };
    return (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

}  // namespace

// Item.getBarWidth: 13 pixels when new, shrinking with the wear.
int ItemStack::barWidth() const {
    const int max = maxDamage();
    if (max <= 0) return 0;
    const float width = 13.0f - static_cast<float>(damage) * 13.0f / static_cast<float>(max);
    return std::clamp(static_cast<int>(std::floor(width + 0.5f)), 0, 13);
}

// Item.getBarColor: hue from green (new) to red (nearly broken).
std::uint32_t ItemStack::barColor() const {
    const int max = maxDamage();
    if (max <= 0) return 0xFFFFFF;
    const float f = std::max(0.0f, (static_cast<float>(max) - static_cast<float>(damage)) / static_cast<float>(max));
    return hsvToRgb(f / 3.0f, 1.0f, 1.0f);
}

}  // namespace apex
