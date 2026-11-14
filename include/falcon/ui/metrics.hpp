#pragma once

namespace falcon::metrics {

// The sizes Minecraft's menus are built from, in GUI units. Use these instead of bare numbers so
// every screen lines up the same way.
inline constexpr int kButtonHeight = 20;
inline constexpr int kButtonWidth = 150;      // the default button, and one half of a two-column row
inline constexpr int kWideButtonWidth = 200;  // Done, Singleplayer, ...
inline constexpr int kFullRowWidth = 310;     // a full-width option row
inline constexpr int kSmallButtonWidth = 98;  // two fit under a 200 wide button
inline constexpr int kColumnGap = 10;         // between the two columns of an option row
inline constexpr int kButtonGap = 4;          // between stacked buttons
inline constexpr int kFooterGap = 8;          // between buttons side by side in a footer
inline constexpr int kHeaderHeight = 33;      // option screens: the title strip
inline constexpr int kFooterHeight = 33;      // ... and the strip with Done
inline constexpr int kRowHeight = 25;         // one row of an options list
inline constexpr int kLineHeight = 9;         // the font's line height

}  // namespace falcon::metrics
