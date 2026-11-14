#include "apex/audio/audio_engine.hpp"

// The MA_* feature switches come from CMake, shared with miniaudio_impl.cpp.
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace apex {
namespace {

constexpr std::size_t kMaxVoices = 64;
constexpr int kMusicChunkFrames = 4096;

void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioEngine*>(device->pUserData);
    engine->render(static_cast<float*>(output), static_cast<int>(frameCount));
}

// Opens (but does not start) a stereo float playback device. `id` is a raw ma_device_id, or null
// for the system default. Returns null and sets `error` on failure.
ma_device* createDevice(AudioEngine* engine, const unsigned char* id, std::string& error) {
    auto device = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0;  // the device's native rate
    config.dataCallback = dataCallback;
    config.pUserData = engine;
    ma_device_id deviceId;
    if (id != nullptr) {
        std::memcpy(&deviceId, id, sizeof(deviceId));
        config.playback.pDeviceID = &deviceId;
    }
    const ma_result opened = ma_device_init(nullptr, &config, device.get());
    if (opened != MA_SUCCESS) {
        error = std::string("could not open an audio device: ") + ma_result_description(opened);
        return nullptr;
    }
    return device.release();
}

void destroyDevice(ma_device* device) {
    if (device == nullptr) return;
    ma_device_uninit(device);  // returns once the callback has stopped
    delete device;
}

std::string nameOf(ma_device* device) {
    char name[256] = {};
    ma_device_get_name(device, ma_device_type_playback, name, sizeof(name), nullptr);
    return name;
}

}  // namespace

AudioEngine::AudioEngine(Mode mode, int offlineSampleRate) {
    if (mode == Mode::Deferred) {  // opened later by selectDevice()
        sampleRate_ = 48000;
        return;
    }
    if (mode == Mode::Offline) {
        offline_ = true;
        sampleRate_ = offlineSampleRate;
        deviceName_ = "offline";
        available_ = true;
        return;
    }

    ma_device* device = createDevice(this, nullptr, error_);
    if (device == nullptr) return;
    deviceName_ = nameOf(device);
    sampleRate_ = static_cast<int>(device->sampleRate);

    const ma_result started = ma_device_start(device);
    if (started != MA_SUCCESS) {
        error_ = std::string("could not start the audio device: ") + ma_result_description(started);
        destroyDevice(device);
        return;
    }
    device_ = device;
    available_ = true;
}

AudioEngine::~AudioEngine() {
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        shuttingDown_ = true;
        wantRefresh_ = false;
        pendingSelect_.reset();
    }
    if (worker_.joinable()) worker_.join();  // a switch in flight finishes first
    destroyDevice(device_);                   // stops the callback before we tear anything down
}

// ---- output devices ---------------------------------------------------------------------

std::string AudioEngine::deviceName() const {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    return deviceName_;
}

std::string AudioEngine::error() const {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    return error_;
}

std::vector<AudioDevice> AudioEngine::devices() const {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    return devices_;
}

void AudioEngine::setDevicesForTest(std::vector<AudioDevice> devices) {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    devices_ = std::move(devices);
    deviceIds_.assign(devices_.size(), {});
    ++deviceListVersion_;
}

void AudioEngine::refreshDevices() {
    // Also fine for an offline (muted) engine: listing outputs opens nothing and plays nothing.
    std::lock_guard<std::mutex> lock(deviceMutex_);
    wantRefresh_ = true;
    startWorkerLocked();
}

void AudioEngine::selectDevice(const std::string& name) {
    if (offline_) {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        deviceName_ = name.empty() ? "offline" : name;
        return;
    }
    std::lock_guard<std::mutex> lock(deviceMutex_);
    pendingSelect_ = name;  // a newer request replaces one that has not started yet
    startWorkerLocked();
}

void AudioEngine::startWorkerLocked() {
    if (workerRunning_ || shuttingDown_) return;
    if (worker_.joinable()) worker_.join();  // the previous run has already finished its loop
    workerRunning_ = true;
    worker_ = std::thread([this] { workerLoop(); });
}

void AudioEngine::workerLoop() {
    for (;;) {
        bool refresh = false;
        std::optional<std::string> select;
        {
            std::lock_guard<std::mutex> lock(deviceMutex_);
            if (shuttingDown_ || (!wantRefresh_ && !pendingSelect_)) {
                workerRunning_ = false;
                return;
            }
            refresh = std::exchange(wantRefresh_, false);
            select = std::move(pendingSelect_);
            pendingSelect_.reset();
        }
        if (refresh) enumerateDevices();
        if (select) switchDevice(*select);
    }
}

void AudioEngine::enumerateDevices() {
    auto context = std::make_unique<ma_context>();
    if (ma_context_init(nullptr, 0, nullptr, context.get()) != MA_SUCCESS) return;

    std::vector<AudioDevice> list;
    std::vector<std::vector<unsigned char>> ids;
    ma_device_info* playback = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* capture = nullptr;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(context.get(), &playback, &playbackCount, &capture, &captureCount) ==
        MA_SUCCESS) {
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            list.push_back({playback[i].name, playback[i].isDefault != 0});
            const auto* raw = reinterpret_cast<const unsigned char*>(&playback[i].id);
            ids.emplace_back(raw, raw + sizeof(ma_device_id));
        }
    }
    ma_context_uninit(context.get());

    std::lock_guard<std::mutex> lock(deviceMutex_);
    deviceIds_ = std::move(ids);
    if (list != devices_) {
        devices_ = std::move(list);
        ++deviceListVersion_;
    }
}

void AudioEngine::switchDevice(const std::string& name) {
    // Find the device's id; an unknown name gets one fresh look at the OS list before we give up.
    std::vector<unsigned char> id;
    const auto find = [&] {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        for (std::size_t i = 0; i < devices_.size(); ++i) {
            if (devices_[i].name == name && i < deviceIds_.size()) {
                id = deviceIds_[i];
                return true;
            }
        }
        return false;
    };
    bool specific = !name.empty();
    if (specific && !find()) {
        enumerateDevices();
        specific = find();  // still missing (unplugged): use the system default instead
    }

    // Open the new device first so a failure leaves the current one playing untouched.
    std::string error;
    ma_device* fresh = createDevice(this, specific ? id.data() : nullptr, error);
    if (fresh == nullptr && specific) fresh = createDevice(this, nullptr, error);
    if (fresh == nullptr) {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        error_ = error;
        return;
    }

    // Stop the old device before touching the mixer (its callback takes mutex_), then carry the
    // playing sounds over, rescaled if the new device runs at another sample rate.
    const int oldRate = sampleRate_.load();
    destroyDevice(std::exchange(device_, nullptr));
    const int newRate = static_cast<int>(fresh->sampleRate);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (oldRate != newRate && newRate > 0) {
            const double scale = static_cast<double>(oldRate) / newRate;
            for (Voice& voice : voices_) voice.step *= scale;
            if (music_) music_->step *= scale;
        }
        sampleRate_ = newRate;
    }

    if (ma_device_start(fresh) != MA_SUCCESS) {
        destroyDevice(fresh);
        available_ = false;
        std::lock_guard<std::mutex> lock(deviceMutex_);
        error_ = "could not start the audio device";
        return;
    }
    device_ = fresh;
    available_ = true;
    std::lock_guard<std::mutex> lock(deviceMutex_);
    deviceName_ = nameOf(fresh);
    error_.clear();
}

std::uint64_t AudioEngine::playSound(std::shared_ptr<const PcmSound> sound, float volume,
                                     float pitch) {
    if (!available_ || !sound || sound->frames() == 0 || volume <= 0.0f) return 0;
    Voice voice;
    voice.volume = volume;

    std::lock_guard<std::mutex> lock(mutex_);
    voice.step = static_cast<double>(pitch) * sound->sampleRate / sampleRate_.load();  // rate may change
    voice.sound = std::move(sound);
    voice.id = nextVoiceId_++;
    const std::uint64_t id = voice.id;
    if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());  // drop the oldest
    voices_.push_back(std::move(voice));
    ++soundsStarted_;
    return id;
}

void AudioEngine::stopSound(std::uint64_t id) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(voices_, [id](const Voice& v) { return v.id == id; });
}

bool AudioEngine::isSoundActive(std::uint64_t id) const {
    if (id == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(voices_.begin(), voices_.end(), [id](const Voice& v) { return v.id == id; });
}

void AudioEngine::playMusic(std::unique_ptr<OggStream> stream, float volume) {
    if (!available_ || !stream) return;
    auto music = std::make_unique<Music>();
    music->volume = volume;

    std::lock_guard<std::mutex> lock(mutex_);
    music->step = static_cast<double>(stream->sampleRate()) / sampleRate_.load();
    music->stream = std::move(stream);
    music_ = std::move(music);
}

void AudioEngine::setMusicVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (music_) music_->volume = volume;
}

void AudioEngine::stopMusic() {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.reset();
}

bool AudioEngine::musicPlaying() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return music_ != nullptr;
}

AudioEngine::Stats AudioEngine::stats() const {
    return {soundsStarted_.load(), framesRendered_.load()};
}

float AudioEngine::takePeak() { return peak_.exchange(0.0f); }

void AudioEngine::mixVoice(Voice& voice, float* out, int frames, bool& finished) const {
    const PcmSound& s = *voice.sound;
    const std::size_t total = s.frames();
    const bool stereo = s.channels == 2;
    finished = false;

    for (int i = 0; i < frames; ++i) {
        const auto i0 = static_cast<std::size_t>(voice.position);
        if (i0 >= total) {
            finished = true;
            return;
        }
        const std::size_t i1 = std::min(i0 + 1, total - 1);
        const auto frac = static_cast<float>(voice.position - static_cast<double>(i0));
        float left;
        float right;
        if (stereo) {
            left = s.samples[i0 * 2] + (s.samples[i1 * 2] - s.samples[i0 * 2]) * frac;
            right = s.samples[i0 * 2 + 1] + (s.samples[i1 * 2 + 1] - s.samples[i0 * 2 + 1]) * frac;
        } else {
            left = right = s.samples[i0] + (s.samples[i1] - s.samples[i0]) * frac;
        }
        out[i * 2] += left * voice.volume;
        out[i * 2 + 1] += right * voice.volume;
        voice.position += voice.step;
    }
}

void AudioEngine::refillMusic() {
    Music& m = *music_;
    const auto channels = static_cast<std::size_t>(m.stream->channels());

    // Drop frames that have been fully played, keeping the interpolation neighbour.
    const auto consumed = static_cast<std::size_t>(m.position);
    if (consumed > 0 && consumed <= m.bufferFrames) {
        m.buffer.erase(m.buffer.begin(),
                       m.buffer.begin() + static_cast<std::ptrdiff_t>(consumed * channels));
        m.bufferFrames -= consumed;
        m.position -= static_cast<double>(consumed);
    }

    while (!m.endOfStream && m.bufferFrames < static_cast<std::size_t>(kMusicChunkFrames) * 2) {
        const std::size_t old = m.buffer.size();
        m.buffer.resize(old + static_cast<std::size_t>(kMusicChunkFrames) * channels);
        const int got = m.stream->read(m.buffer.data() + old, kMusicChunkFrames);
        m.buffer.resize(old + static_cast<std::size_t>(std::max(got, 0)) * channels);
        if (got <= 0) {
            m.endOfStream = true;
        } else {
            m.bufferFrames += static_cast<std::size_t>(got);
        }
    }
}

void AudioEngine::mixMusic(float* out, int frames) {
    Music& m = *music_;
    const auto channels = static_cast<std::size_t>(m.stream->channels());
    const bool stereo = channels == 2;

    for (int i = 0; i < frames; ++i) {
        if (static_cast<std::size_t>(m.position) + 2 >= m.bufferFrames) refillMusic();
        const auto i0 = static_cast<std::size_t>(m.position);
        if (i0 >= m.bufferFrames) {  // ran out of data: the track has finished
            music_.reset();
            return;
        }
        const std::size_t i1 = std::min(i0 + 1, m.bufferFrames - 1);
        const auto frac = static_cast<float>(m.position - static_cast<double>(i0));
        const float* b = m.buffer.data();
        float left;
        float right;
        if (stereo) {
            left = b[i0 * 2] + (b[i1 * 2] - b[i0 * 2]) * frac;
            right = b[i0 * 2 + 1] + (b[i1 * 2 + 1] - b[i0 * 2 + 1]) * frac;
        } else {
            left = right = b[i0] + (b[i1] - b[i0]) * frac;
        }
        out[i * 2] += left * m.volume;
        out[i * 2 + 1] += right * m.volume;
        m.position += m.step;
    }
}

void AudioEngine::render(float* out, int frames) {
    std::fill(out, out + static_cast<std::ptrdiff_t>(frames) * 2, 0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < voices_.size();) {
            bool finished = false;
            mixVoice(voices_[i], out, frames, finished);
            if (finished) {
                voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        if (music_) mixMusic(out, frames);
    }

    float peak = 0.0f;
    for (int i = 0; i < frames * 2; ++i) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
        peak = std::max(peak, std::fabs(out[i]));
    }
    float seen = peak_.load();
    while (peak > seen && !peak_.compare_exchange_weak(seen, peak)) {
    }
    framesRendered_ += static_cast<std::uint64_t>(frames);
}

}  // namespace apex
