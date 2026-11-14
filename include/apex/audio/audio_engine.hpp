#pragma once

#include "apex/audio/ogg.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct ma_device;  // miniaudio

namespace apex {

// One audio output the player can pick in Music & Sounds.
struct AudioDevice {
    std::string name;  // as the OS shows it; also what GameOptions::audioDevice stores
    bool isDefault = false;

    bool operator==(const AudioDevice&) const = default;
};

// Software mixer plus an output device. Sound effects are fully decoded PcmSounds played as
// independent voices (linear resampling for pitch), music is a single streamed Ogg track.
// Volumes passed in are final gains (0..1); the sound system applies categories and master.
//
// In Offline mode there is no device: tests call render() themselves.
class AudioEngine {
public:
    // Device opens the output now; Offline has no device (tests, muted runs); Deferred has none yet:
    // the first selectDevice() call opens it on the background thread, so start-up does not wait for
    // the OS audio stack (everything is a harmless no-op until it is open).
    enum class Mode { Device, Offline, Deferred };

    explicit AudioEngine(Mode mode = Mode::Device, int offlineSampleRate = 48000);
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // False when no audio device could be opened; every call is then a harmless no-op.
    [[nodiscard]] bool available() const { return available_.load(); }
    [[nodiscard]] int sampleRate() const { return sampleRate_.load(); }
    // The device that is actually playing, and the last device error (both by value: they can be
    // changed by the background thread that switches devices).
    [[nodiscard]] std::string deviceName() const;
    [[nodiscard]] std::string error() const;

    // ---- output devices -------------------------------------------------------------------
    // Asking the OS for its devices and opening one can take a moment, so both happen on a
    // background thread and none of these calls ever waits for it.
    //
    // refreshDevices() starts gathering the list; devices() returns the newest list finished so
    // far (empty until the first refresh completes) and deviceListVersion() changes whenever that
    // list does, so a screen can notice new results cheaply.
    void refreshDevices();
    [[nodiscard]] std::vector<AudioDevice> devices() const;
    [[nodiscard]] unsigned deviceListVersion() const { return deviceListVersion_.load(); }
    // Moves playback to the device called `name` ("" = the system default). Returns immediately;
    // playing sounds carry over. If the device cannot be opened the current one keeps playing; a
    // name that is no longer plugged in falls back to the system default.
    void selectDevice(const std::string& name);
    // Replaces the device list (for tests; a later refreshDevices() replaces it again).
    void setDevicesForTest(std::vector<AudioDevice> devices);

    // Starts a one-shot sound. `pitch` scales playback speed (1 = original). Returns an id for
    // stopSound()/isSoundActive(), or 0 if nothing was started.
    std::uint64_t playSound(std::shared_ptr<const PcmSound> sound, float volume, float pitch = 1.0f);
    void stopSound(std::uint64_t id);
    [[nodiscard]] bool isSoundActive(std::uint64_t id) const;

    // Starts streaming music, replacing whatever music was playing.
    void playMusic(std::unique_ptr<OggStream> stream, float volume);
    void setMusicVolume(float volume);
    void stopMusic();
    [[nodiscard]] bool musicPlaying() const;

    // Mixes `frames` stereo frames (interleaved L/R floats) into `out`.
    void render(float* out, int frames);

    struct Stats {
        std::uint64_t soundsStarted = 0;
        std::uint64_t framesRendered = 0;
    };
    [[nodiscard]] Stats stats() const;
    // Loudest sample mixed since the previous call (0 if silent); for diagnostics and tests.
    [[nodiscard]] float takePeak();

private:
    struct Voice {
        std::uint64_t id = 0;
        std::shared_ptr<const PcmSound> sound;
        double position = 0.0;  // in source frames
        double step = 1.0;      // source frames per output frame
        float volume = 1.0f;
    };
    struct Music {
        std::unique_ptr<OggStream> stream;
        std::vector<float> buffer;  // decoded, interleaved
        std::size_t bufferFrames = 0;
        double position = 0.0;
        double step = 1.0;
        float volume = 1.0f;
        bool endOfStream = false;
    };

    void mixVoice(Voice& voice, float* out, int frames, bool& finished) const;
    void mixMusic(float* out, int frames);
    void refillMusic();

    // Device worker: one background thread at a time runs queued refresh/select requests.
    void startWorkerLocked();  // deviceMutex_ must be held
    void workerLoop();
    void enumerateDevices();
    void switchDevice(const std::string& name);

    mutable std::mutex mutex_;  // the mixer: voices_, music_ and the steps that depend on the rate
    std::vector<Voice> voices_;
    std::unique_ptr<Music> music_;

    ma_device* device_ = nullptr;  // touched only by the constructor, the worker and the destructor
    bool offline_ = false;
    std::atomic<bool> available_{false};
    std::atomic<int> sampleRate_{48000};

    mutable std::mutex deviceMutex_;  // guards everything below
    std::vector<AudioDevice> devices_;
    std::vector<std::vector<unsigned char>> deviceIds_;  // raw ma_device_id per devices_ entry
    std::string deviceName_;
    std::string error_;
    bool wantRefresh_ = false;
    std::optional<std::string> pendingSelect_;
    bool workerRunning_ = false;
    bool shuttingDown_ = false;
    std::thread worker_;
    std::atomic<unsigned> deviceListVersion_{0};

    std::uint64_t nextVoiceId_ = 1;
    std::atomic<std::uint64_t> soundsStarted_{0};
    std::atomic<std::uint64_t> framesRendered_{0};
    std::atomic<float> peak_{0.0f};
};

}  // namespace apex
