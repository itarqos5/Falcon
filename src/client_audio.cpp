#include "falcon/client.hpp"
#include "client_internal.hpp"

#include "falcon/audio/audio_engine.hpp"
#include "falcon/audio/music_manager.hpp"
#include "falcon/audio/sound_system.hpp"
#include "falcon/assets/asset_locator.hpp"
#include "falcon/assets/image.hpp"
#include "falcon/assets/resource_manager.hpp"
#include "falcon/gfx/gl.hpp"
#include "falcon/gfx/gui_renderer.hpp"
#include "falcon/gfx/panorama_renderer.hpp"
#include "falcon/gfx/texture_manager.hpp"
#include "falcon/platform/dialog.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/platform/window.hpp"
#include "falcon/resource_reloader.hpp"
#include "falcon/text/font.hpp"
#include "falcon/text/language.hpp"
#include "falcon/ui/gui_graphics.hpp"
#include "falcon/game/game_options.hpp"
#include "falcon/ui/gui_scale.hpp"
#include "falcon/game/now_playing_toast.hpp"
#include "falcon/game/game_session.hpp"
#include "falcon/game/game_ui.hpp"
#include "falcon/game/death_screen.hpp"
#include "falcon/gfx/frame_queue.hpp"
#include "falcon/game/in_game_screen.hpp"
#include "falcon/game/lighting_audit.hpp"
#include "falcon/game/vanilla_compare.hpp"
#include "falcon/game/screen_services.hpp"
#include "falcon/util/options_file.hpp"
#include "falcon/util/task_pool.hpp"
#include "falcon/util/text_util.hpp"
#include "falcon/game/edition_badge.hpp"
#include "falcon/game/panorama.hpp"
#include "falcon/game/splash_manager.hpp"
#include "falcon/ui/sprite_manager.hpp"
#include "falcon/game/title_screen.hpp"
#include "falcon/ui/confirm_screen.hpp"
#include "falcon/world/level_info.hpp"
#include "falcon/version.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

// The sound checks of the client (--audio-devices, --audio-test).

namespace falcon {

// Diagnostic for --audio-devices: lists the outputs and switches to each in turn, then back to the
// system default, the way the Device button does. Silent: nothing is played.
int runAudioDevices() {
    using Clock = std::chrono::steady_clock;
    AudioEngine engine;
    if (!engine.available()) {
        std::printf("[audio-devices] FAILED: %s\n", engine.error().c_str());
        return 2;
    }
    std::printf("[audio-devices] opened '%s' at %d Hz\n", engine.deviceName().c_str(), engine.sampleRate());

    const auto waitFor = [](auto&& done) {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!done() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return done();
    };

    const auto listStart = Clock::now();
    const unsigned before = engine.deviceListVersion();
    engine.refreshDevices();
    if (!waitFor([&] { return engine.deviceListVersion() != before; })) {
        std::printf("[audio-devices] FAILED: no device list within 10 s\n");
        return 3;
    }
    const auto listMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - listStart).count();
    const std::vector<AudioDevice> devices = engine.devices();
    std::printf("[audio-devices] %zu output device(s), listed in %lld ms:\n", devices.size(),
                static_cast<long long>(listMs));
    for (const AudioDevice& d : devices) std::printf("    %s%s\n", d.name.c_str(), d.isDefault ? "  (default)" : "");

    std::string defaultName;
    for (const AudioDevice& d : devices) {
        if (d.isDefault) defaultName = d.name;
    }
    int failures = 0;
    std::vector<std::string> targets;
    for (const AudioDevice& d : devices) targets.push_back(d.name);
    targets.emplace_back("");  // finish back on the system default
    for (const std::string& target : targets) {
        const auto start = Clock::now();
        engine.selectDevice(target);
        const auto returnedUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
        // Done when the engine reports the wanted device and its callback is delivering buffers.
        const std::string& expected = target.empty() ? defaultName : target;
        const std::uint64_t framesBefore = engine.stats().framesRendered;
        const bool ok = waitFor([&] {
            return engine.available() && engine.deviceName() == expected &&
                   engine.stats().framesRendered > framesBefore + 480;
        });
        const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::printf("[audio-devices] select '%s': call returned in %lld us, playing '%s' at %d Hz after %lld ms  %s\n",
                    target.empty() ? "(system default)" : target.c_str(), static_cast<long long>(returnedUs),
                    engine.deviceName().c_str(), engine.sampleRate(), static_cast<long long>(totalMs),
                    ok ? "OK" : "FAILED");
        if (!ok) ++failures;
    }
    std::printf("[audio-devices] %s\n", failures == 0 ? "all switches worked" : "some switches FAILED");
    return failures == 0 ? 0 : 4;
}

// Diagnostic for --audio-test: proves decoding and the output device work with real game files.
int runAudioTest(const ResourceManager& resources) {
    const auto clickBytes = resources.readBytes(ResourceLocation("minecraft", "sounds/random/click.ogg"));
    const auto click = clickBytes ? decodeOgg(*clickBytes) : std::nullopt;
    if (!click) {
        std::printf("[audio-test] FAILED: could not decode sounds/random/click.ogg\n");
        return 1;
    }
    float clickPeak = 0.0f;
    for (const float s : click->samples) clickPeak = std::max(clickPeak, std::fabs(s));
    std::printf("[audio-test] click.ogg: %d Hz, %d ch, %.3f s, peak sample %.3f\n", click->sampleRate,
                click->channels, click->durationSeconds(), static_cast<double>(clickPeak));

    AudioEngine engine;
    if (!engine.available()) {
        std::printf("[audio-test] FAILED: %s\n", engine.error().c_str());
        return 2;
    }
    std::printf("[audio-test] device '%s' at %d Hz\n", engine.deviceName().c_str(), engine.sampleRate());

    engine.playSound(std::make_shared<PcmSound>(*click), 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const float soundPeak = engine.takePeak();
    std::printf("[audio-test] click mixed: peak %.3f (device callbacks rendered %llu frames)\n",
                static_cast<double>(soundPeak),
                static_cast<unsigned long long>(engine.stats().framesRendered));

    auto bytes = resources.readBytes(ResourceLocation("minecraft", "sounds/music/game/clark.ogg"));
    auto stream = bytes ? OggStream::open(std::move(*bytes)) : nullptr;
    if (!stream) {
        std::printf("[audio-test] FAILED: could not open music/game/clark.ogg\n");
        return 3;
    }
    std::printf("[audio-test] clark.ogg: %d Hz, %d ch, %.1f s\n", stream->sampleRate(),
                stream->channels(), stream->durationSeconds());
    engine.playMusic(std::move(stream), 0.5f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const float musicPeak = engine.takePeak();
    std::printf("[audio-test] music mixed: peak %.3f, still playing: %s\n",
                static_cast<double>(musicPeak), engine.musicPlaying() ? "yes" : "no");
    engine.stopMusic();

    // Music often fades in from silence, so also measure the loudest sample in its first 40 s.
    auto again = resources.readBytes(ResourceLocation("minecraft", "sounds/music/game/clark.ogg"));
    auto scan = again ? OggStream::open(std::move(*again)) : nullptr;
    float trackPeak = 0.0f;
    if (scan) {
        std::vector<float> block(4096 * static_cast<std::size_t>(scan->channels()));
        for (int decoded = 0; decoded < scan->sampleRate() * 40;) {
            const int got = scan->read(block.data(), 4096);
            if (got <= 0) break;
            decoded += got;
            for (int i = 0; i < got * scan->channels(); ++i) {
                trackPeak = std::max(trackPeak, std::fabs(block[static_cast<std::size_t>(i)]));
            }
        }
    }
    std::printf("[audio-test] clark.ogg loudest sample in the first 40 s: %.3f\n",
                static_cast<double>(trackPeak));
    return (soundPeak > 0.0f && trackPeak > 0.05f) ? 0 : 4;
}

}  // namespace falcon
