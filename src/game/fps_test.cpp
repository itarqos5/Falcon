#include "falcon/game/fps_test.hpp"

#include "falcon/render/lod_renderer.hpp"

#include <atomic>
#include <optional>

namespace falcon {
namespace fps_test {
namespace {

std::optional<GameOptions> saved;  // the options as they were before the test: the copy start() keeps and stop() puts back
std::atomic<bool> testRunning{false};
double frameFps = 0.0, frameMillis = 0.0;

}  // namespace

void applyLowest(GameOptions& o) {
    o.renderDistance = 2;  // (the least the world can be drawn at, and the least block data in memory)
    o.simulationDistance = 5;
    o.biomeBlendRadius = 0;
    o.ambientOcclusion = false;  // (the light is baked into the chunks' vertices: off is the cheap mesh)
    o.cloudStatus = CloudStatus::Off;
    o.cloudRange = 2;
    o.particles = ParticleStatus::Minimal;
    o.weatherRadius = 3;
    o.entityShadows = false;
    o.entityDistanceScaling = 0.5;
    o.mipmapLevels = 0;
    o.textureFiltering = TextureFiltering::None;
    o.maxAnisotropyBit = 1;
    o.cutoutLeaves = false;  // solid leaves: no cutout pass in their shader
    o.improvedTransparency = false;
    o.menuBackgroundBlurriness = 0;
    o.lodsEnabled = false;  // (their tiles are given back: the most memory a world holds)
    o.lodChunkRadius = kLodMinChunks;
    o.lodDetail = kLodDetailLow;
    o.skyEnabled = false;  // nothing above the horizon is drawn at all
    o.sunEnabled = false;
    o.moonEnabled = false;
    o.starsEnabled = false;
    o.enableVsync = false;  // (the frame is not held back for the display)
    o.framerateLimit = 260;
    o.vignette = false;
    o.attackIndicator = AttackIndicator::Off;
    o.chunkSectionFadeInTime = 0.0;
    o.showAutosaveIndicator = false;
    o.bobView = false;
    o.screenEffectScale = 0.0;
    o.fovEffectScale = 0.0;
    o.graphicsPreset = GraphicsPreset::Custom;
}

void start(const GameOptions& current, GameOptions& options) {
    if (!saved) saved = current;
    applyLowest(options);
    testRunning = true;
    frameFps = 0.0;
    frameMillis = 0.0;
}

bool stop(GameOptions& options) {
    testRunning = false;
    if (!saved) return false;
    options = *saved;
    saved.reset();
    return true;
}

bool running() { return testRunning; }

const GameOptions* backup() { return saved ? &*saved : nullptr; }

void setFrame(double fps, double millis) {
    frameFps = fps;
    frameMillis = millis;
}

double fps() { return frameFps; }

double millis() { return frameMillis; }

bool useAllCores() { return testRunning; }

}  // namespace fps_test
}  // namespace falcon
