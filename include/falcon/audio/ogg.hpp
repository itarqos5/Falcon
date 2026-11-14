#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace falcon {

// Fully decoded sound: interleaved float samples in [-1, 1], one or two channels.
struct PcmSound {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;

    [[nodiscard]] std::size_t frames() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
    [[nodiscard]] double durationSeconds() const {
        return sampleRate > 0 ? static_cast<double>(frames()) / sampleRate : 0.0;
    }
};

// Decodes a whole Ogg Vorbis file (used for short sound effects). Files with more than two
// channels keep their first two.
[[nodiscard]] std::optional<PcmSound> decodeOgg(const std::vector<unsigned char>& bytes);

// Incremental Ogg Vorbis decoder over an in-memory file (used for music, which is streamed
// instead of decoded up front).
class OggStream {
public:
    // Takes ownership of the file bytes; nullptr if they are not valid Ogg Vorbis.
    [[nodiscard]] static std::unique_ptr<OggStream> open(std::vector<unsigned char> bytes);
    ~OggStream();
    OggStream(const OggStream&) = delete;
    OggStream& operator=(const OggStream&) = delete;

    [[nodiscard]] int sampleRate() const { return sampleRate_; }
    [[nodiscard]] int channels() const { return channels_; }
    [[nodiscard]] double durationSeconds() const { return durationSeconds_; }

    // Decodes up to `frames` interleaved frames into `out` (which must hold frames*channels
    // floats). Returns how many were produced; 0 means the end of the stream.
    int read(float* out, int frames);

private:
    OggStream() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    int sampleRate_ = 0;
    int channels_ = 0;
    double durationSeconds_ = 0.0;
};

}  // namespace falcon
