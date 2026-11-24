#pragma once

#include "falcon/game/game_options.hpp"

namespace falcon {

// The Highest FPS Test of Video Settings' Developer page: every option at the least the game can run with, every core of the PC on every job, the sky, sun, moon and stars off,
// and a small label in the corner with the frame rate (which costs nothing next to the F3 screen). It is not a screen of its own, so it keeps running in the world; clicking the
// label (or Restore on the Developer page) puts every option back exactly as it was.
namespace fps_test {

// Every option at what draws the highest frame rate: the render distance and the simulation at their least, no LODs (their tiles are the most memory a world holds), no
// clouds, no smooth lighting, no mipmaps, no sky (nor sun, moon or stars), no frame rate cap and no vsync, and the small touches (shadows, the vignette, the chunk fade) off.
void applyLowest(GameOptions& options);

// Starts the test with `options` as they are now: the copy that puts them back is kept here, then the options are changed to their least.
void start(const GameOptions& current, GameOptions& options);
// Puts back the options the test was started with (false, and nothing happens, when it is not running).
bool stop(GameOptions& options);
[[nodiscard]] bool running();
// The options to put back (empty when the test is not running).
[[nodiscard]] const GameOptions* backup();
// The frame rate the label shows: call every frame while the game runs.
void setFrame(double fps, double millis);
[[nodiscard]] double fps();
[[nodiscard]] double millis();
// While the test runs every job the game hands out (chunks, lighting, LOD tiles) uses every core of the PC, where it otherwise leaves one or two free.
[[nodiscard]] bool useAllCores();

}  // namespace fps_test
}  // namespace falcon
