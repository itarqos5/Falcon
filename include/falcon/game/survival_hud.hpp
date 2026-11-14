#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace falcon {

// What the survival bars show. There is no damage, hunger or experience yet, so these hold a full health and food bar and an empty experience
// bar; they are the place where those will go, and `--vitals` sets them for screenshots.
struct Vitals {
    float health = 20.0f;
    float maxHealth = 20.0f;
    int food = 20;
    float saturation = 5.0f;
    int level = 0;
    float progress = 0.0f;  // 0..1 of the way to the next level
};

enum class BarFill { Empty, Half, Full };

// Where the survival bars sit on a GUI of the given size (Hud.extractPlayerHealth, ExperienceBar / ContextualBar). Hearts run from the left of the
// hotbar, ten to a row, 8 pixels apart; the food bar runs from the right of the hotbar to the left; the experience bar is the 182x5 strip
// between the hotbar and the hearts, and the level number sits above its middle.
struct SurvivalLayout {
    int left = 0;         // x of the first heart (the left edge of the hotbar)
    int right = 0;        // the right edge of the hotbar, where the first food icon ends
    int rowY = 0;         // y of the first row of hearts and of the food icons
    int rowHeight = 11;   // between rows of hearts (shrinks with many rows)
    int xpLeft = 0, xpTop = 0;
    int levelBaselineY = 0;
    [[nodiscard]] int heartX(int heart) const { return left + (heart % 10) * 8; }
    [[nodiscard]] int heartY(int heart) const { return rowY - (heart / 10) * rowHeight; }
    [[nodiscard]] int foodX(int icon) const { return right - icon * 8 - 9; }
};

[[nodiscard]] inline SurvivalLayout computeSurvivalLayout(int guiWidth, int guiHeight, float maxHealth = 20.0f) {
    SurvivalLayout l;
    l.left = guiWidth / 2 - 91;
    l.right = guiWidth / 2 + 91;
    l.rowY = guiHeight - 39;
    const int rows = static_cast<int>(std::ceil(maxHealth / 2.0f / 10.0f));
    l.rowHeight = std::max(10 - (rows - 2), 3);
    l.xpLeft = (guiWidth - 182) / 2;
    l.xpTop = guiHeight - 24 - 5;
    l.levelBaselineY = guiHeight - 24 - 9 - 2;
    return l;
}

// A heart: it is full if both its halves are within the health, half if only the first one is (the health is rounded up).
[[nodiscard]] inline BarFill heartFill(int heart, float health) {
    const int hp = static_cast<int>(std::ceil(health));
    if (heart * 2 + 1 < hp) return BarFill::Full;
    if (heart * 2 + 1 == hp) return BarFill::Half;
    return BarFill::Empty;
}

// A food icon (the empty one is always drawn under it).
[[nodiscard]] inline BarFill foodFill(int icon, int food) {
    if (icon * 2 + 1 < food) return BarFill::Full;
    if (icon * 2 + 1 == food) return BarFill::Half;
    return BarFill::Empty;
}

// How much of the 182-pixel experience bar is filled (the game multiplies by 183 and cuts at the sprite).
[[nodiscard]] inline int experienceFillWidth(float progress) {
    return std::clamp(static_cast<int>(progress * 183.0f), 0, 182);
}

// The little shakes: hearts jump up and down when the health is 4 or less, and the food icons when the saturation is used up (the game rolls a
// fresh random number for every icon every tick, here a hash of the tick and the icon does the same job).
[[nodiscard]] inline int tickJitter(long long tick, int icon, int bound) {
    std::uint64_t x = static_cast<std::uint64_t>(tick) * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(icon + 1) * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 31;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 29;
    return static_cast<int>(x % static_cast<std::uint64_t>(bound));
}
[[nodiscard]] inline bool heartsShake(float health) { return std::ceil(health) <= 4.0f; }
[[nodiscard]] inline bool foodShakes(float saturation, int food, long long tick) { return saturation <= 0.0f && tick % (food * 3 + 1) == 0; }

}  // namespace falcon
