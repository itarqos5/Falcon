#include "falcon/world/chunk_cache.hpp"

#include <stb_image.h>  // (only the declarations: stbi_zlib_decode_malloc)

#include <algorithm>
#include <cstdlib>
#include <cstring>

// stb_image_write.h compiles its zlib compressor into image.cpp with external linkage; it is declared here to be used.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace falcon {

namespace {

constexpr unsigned char kFormat = 1;

void put16(std::vector<unsigned char>& out, unsigned v) {
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

struct Reader {
    const unsigned char* p;
    const unsigned char* end;
    bool bad = false;
    unsigned u8() {
        if (p >= end) { bad = true; return 0; }
        return *p++;
    }
    unsigned u16() {
        const unsigned lo = u8();
        return lo | (u8() << 8);
    }
    void bytes(unsigned char* dst, std::size_t n) {
        if (static_cast<std::size_t>(end - p) < n) { bad = true; return; }
        std::memcpy(dst, p, n);
        p += n;
    }
};

}  // namespace

std::vector<unsigned char> ChunkCache::compress(const Chunk& chunk) {
    // Raw form: for each section its kind (0 air, then the Packing + 1), and what it needs; then whether there are biomes, and them.
    std::vector<unsigned char> raw;
    raw.reserve(16 * 1024);
    raw.push_back(kFormat);
    for (const SectionPtr& s : chunk.sections) {
        if (!s) {
            raw.push_back(0);
            continue;
        }
        raw.push_back(static_cast<unsigned char>(static_cast<int>(s->packing) + 1));
        switch (s->packing) {
            case Packing::Uniform: put16(raw, static_cast<unsigned>(s->uniform)); break;
            case Packing::Nibble:
            case Packing::Byte: {
                put16(raw, static_cast<unsigned>(s->palette.size()));
                for (const Block b : s->palette) put16(raw, static_cast<unsigned>(b));
                const std::size_t n = s->packing == Packing::Nibble ? 2048 : 4096;
                raw.insert(raw.end(), s->data.get(), s->data.get() + n);
                break;
            }
            case Packing::Wide: raw.insert(raw.end(), s->data.get(), s->data.get() + 8192); break;
        }
    }
    raw.push_back(chunk.biomes ? 1 : 0);
    if (chunk.biomes) raw.insert(raw.end(), chunk.biomes->ids.begin(), chunk.biomes->ids.end());

    int length = 0;
    unsigned char* deflated = stbi_zlib_compress(raw.data(), static_cast<int>(raw.size()), &length, 6);
    if (deflated == nullptr) return raw;  // (cannot happen in practice) kept as it is, marked by its first byte being the format
    std::vector<unsigned char> out;
    out.reserve(static_cast<std::size_t>(length) + 5);
    out.push_back(0xD5);  // deflated
    put16(out, static_cast<unsigned>(raw.size() & 0xFFFF));
    put16(out, static_cast<unsigned>(raw.size() >> 16));
    out.insert(out.end(), deflated, deflated + length);
    std::free(deflated);  // (STBIW_FREE is free)
    return out;
}

std::shared_ptr<Chunk> ChunkCache::decompress(const std::vector<unsigned char>& bytes) {
    std::vector<unsigned char> raw;
    if (!bytes.empty() && bytes[0] == 0xD5 && bytes.size() > 5) {
        int length = 0;
        char* inflated = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(bytes.data() + 5), static_cast<int>(bytes.size() - 5), &length);
        if (inflated == nullptr) return nullptr;
        raw.assign(reinterpret_cast<unsigned char*>(inflated), reinterpret_cast<unsigned char*>(inflated) + length);
        std::free(inflated);
    } else {
        raw = bytes;
    }
    Reader r{raw.data(), raw.data() + raw.size()};
    if (r.u8() != kFormat) return nullptr;
    auto chunk = std::make_shared<Chunk>();
    for (SectionPtr& slot : chunk->sections) {
        const unsigned kind = r.u8();
        if (kind == 0) continue;
        auto s = std::make_shared<SectionData>();
        s->packing = static_cast<Packing>(kind - 1);
        switch (s->packing) {
            case Packing::Uniform: s->uniform = static_cast<Block>(r.u16()); break;
            case Packing::Nibble:
            case Packing::Byte: {
                const unsigned count = r.u16();
                s->palette.resize(count);
                for (unsigned i = 0; i < count; ++i) s->palette[i] = static_cast<Block>(r.u16());
                const std::size_t n = s->packing == Packing::Nibble ? 2048 : 4096;
                s->data = std::make_unique<std::uint8_t[]>(n);
                r.bytes(s->data.get(), n);
                break;
            }
            case Packing::Wide:
                s->data = std::make_unique<std::uint8_t[]>(8192);
                r.bytes(s->data.get(), 8192);
                break;
            default: return nullptr;
        }
        slot = std::move(s);
    }
    if (r.u8() == 1) {
        auto grid = std::make_shared<BiomeGrid>();
        r.bytes(grid->ids.data(), grid->ids.size());
        chunk->biomes = std::move(grid);
    }
    return r.bad ? nullptr : chunk;
}

void ChunkCache::hold(ChunkPos pos, std::shared_ptr<Chunk> chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry& e = entries_[pos];
    packedBytes_ -= e.packed.size();
    e.packed.clear();
    e.held = std::move(chunk);
}

void ChunkCache::pack(ChunkPos pos) {
    std::shared_ptr<Chunk> chunk;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(pos);
        if (it == entries_.end() || !it->second.held) return;  // taken back (or forgotten) before its turn came
        chunk = it->second.held;
    }
    std::vector<unsigned char> packed = compress(*chunk);  // the slow part, outside the lock
    packed.shrink_to_fit();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(pos);
    if (it == entries_.end() || it->second.held != chunk) return;  // changed meanwhile
    packedBytes_ += packed.size();
    it->second.packed = std::move(packed);
    it->second.held.reset();
}

std::shared_ptr<Chunk> ChunkCache::take(ChunkPos pos) {
    Entry e;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(pos);
        if (it == entries_.end()) return nullptr;
        e = std::move(it->second);
        packedBytes_ -= e.packed.size();
        entries_.erase(it);
    }
    if (e.held) {
        auto chunk = std::make_shared<Chunk>();
        chunk->sections = e.held->sections;
        chunk->biomes = e.held->biomes;
        return chunk;  // (no light: it is made again)
    }
    return decompress(e.packed);
}

bool ChunkCache::contains(ChunkPos pos) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.contains(pos);
}

void ChunkCache::trim(ChunkPos center) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (packedBytes_ <= budget_) return;
    std::vector<std::pair<long long, ChunkPos>> order;
    order.reserve(entries_.size());
    for (const auto& [pos, e] : entries_) {
        if (e.held) continue;  // not packed yet: leave it, it is on its way
        const long long dx = pos.x - center.x, dz = pos.z - center.z;
        order.push_back({dx * dx + dz * dz, pos});
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });  // furthest first
    for (const auto& [d2, pos] : order) {
        if (packedBytes_ <= budget_) break;
        const auto it = entries_.find(pos);
        packedBytes_ -= it->second.packed.size();
        entries_.erase(it);
    }
}

std::size_t ChunkCache::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::size_t ChunkCache::packedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packedBytes_;
}

void ChunkCache::setBudget(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    budget_ = bytes;
}

}  // namespace falcon
