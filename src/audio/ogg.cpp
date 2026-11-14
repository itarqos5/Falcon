#include "apex/audio/ogg.hpp"

#include <algorithm>

// Declarations only; the implementation is compiled as C in vorbis_impl.c.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace apex {

struct OggStream::Impl {
    std::vector<unsigned char> data;  // must outlive `vorbis`
    stb_vorbis* vorbis = nullptr;
    ~Impl() {
        if (vorbis != nullptr) stb_vorbis_close(vorbis);
    }
};

std::unique_ptr<OggStream> OggStream::open(std::vector<unsigned char> bytes) {
    auto impl = std::make_unique<Impl>();
    impl->data = std::move(bytes);
    int error = 0;
    impl->vorbis = stb_vorbis_open_memory(impl->data.data(), static_cast<int>(impl->data.size()),
                                          &error, nullptr);
    if (impl->vorbis == nullptr) return nullptr;

    const stb_vorbis_info info = stb_vorbis_get_info(impl->vorbis);
    std::unique_ptr<OggStream> stream(new OggStream());
    stream->sampleRate_ = static_cast<int>(info.sample_rate);
    stream->channels_ = std::min(info.channels, 2);
    const unsigned int total = stb_vorbis_stream_length_in_samples(impl->vorbis);
    stream->durationSeconds_ =
        info.sample_rate > 0 ? static_cast<double>(total) / info.sample_rate : 0.0;
    stream->impl_ = std::move(impl);
    return stream;
}

OggStream::~OggStream() = default;

int OggStream::read(float* out, int frames) {
    if (frames <= 0) return 0;
    return stb_vorbis_get_samples_float_interleaved(impl_->vorbis, channels_, out,
                                                    frames * channels_);
}

std::optional<PcmSound> decodeOgg(const std::vector<unsigned char>& bytes) {
    auto stream = OggStream::open(bytes);
    if (!stream) return std::nullopt;

    PcmSound sound;
    sound.sampleRate = stream->sampleRate();
    sound.channels = stream->channels();
    constexpr int kChunk = 4096;
    std::vector<float> chunk(static_cast<std::size_t>(kChunk) * static_cast<std::size_t>(sound.channels));
    for (;;) {
        const int frames = stream->read(chunk.data(), kChunk);
        if (frames <= 0) break;
        sound.samples.insert(sound.samples.end(), chunk.begin(),
                             chunk.begin() + static_cast<std::ptrdiff_t>(frames) * sound.channels);
    }
    if (sound.samples.empty()) return std::nullopt;
    return sound;
}

}  // namespace apex
