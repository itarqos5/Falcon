#include "falcon/world/chunk.hpp"

#include <algorithm>
#include <mutex>

namespace falcon {

void SectionData::copyRow(int x0, int y, int z, int count, Block* dst) const {
    if (packing == Packing::Uniform) {
        std::fill_n(dst, count, uniform);
        return;
    }
    const int base = index(x0, y, z);
    for (int i = 0; i < count; ++i) dst[i] = atIndex(base + i);
}

void SectionData::decode(Block* dst) const {
    switch (packing) {
        case Packing::Uniform:
            std::fill_n(dst, kSectionVolume, uniform);
            break;
        case Packing::Nibble:
            for (int i = 0; i < kSectionVolume; i += 2) {
                const std::uint8_t two = data[static_cast<std::size_t>(i >> 1)];
                dst[i] = palette[two & 15];
                dst[i + 1] = palette[two >> 4];
            }
            break;
        case Packing::Byte:
            for (int i = 0; i < kSectionVolume; ++i) dst[i] = palette[data[static_cast<std::size_t>(i)]];
            break;
        case Packing::Wide:
            for (int i = 0; i < kSectionVolume; ++i) {
                const std::size_t o = static_cast<std::size_t>(i) * 2;
                dst[i] = static_cast<Block>(data[o] | (data[o + 1] << 8));
            }
            break;
    }
}

std::size_t SectionData::memoryBytes() const {
    std::size_t bytes = sizeof(SectionData) + palette.capacity() * sizeof(Block);
    switch (packing) {
        case Packing::Uniform: break;
        case Packing::Nibble: bytes += 2048; break;
        case Packing::Byte: bytes += 4096; break;
        case Packing::Wide: bytes += 8192; break;
    }
    return bytes;
}

void SectionBuilder::fillLayers(int y0, int y1, Block b) {
    for (int y = std::max(y0, 0); y < std::min(y1, kSectionSize); ++y) {
        std::fill_n(blocks_.begin() + y * 256, 256, b);
    }
}

void SectionBuilder::load(const SectionPtr& section) {
    if (!section) {
        blocks_.fill(Block::Air);
        return;
    }
    section->decode(blocks_.data());
}

SectionPtr SectionBuilder::build() const {
    const Block first = blocks_[0];
    if (std::all_of(blocks_.begin(), blocks_.end(), [first](Block b) { return b == first; })) {
        return first == Block::Air ? nullptr : uniformSection(first);
    }
    // The distinct blocks, in order of first appearance. A bitmap over all 65536 ids would be faster but this runs
    // once per non-uniform section, and the palette stops growing at 257 entries.
    auto section = std::make_shared<SectionData>();
    std::array<std::uint16_t, 65536>* lookup = nullptr;
    thread_local std::array<std::uint16_t, 65536> table;
    lookup = &table;
    std::vector<Block> palette;
    bool overflow = false;
    {
        // 0xFFFF marks "not in the palette"; the table is cleaned up again below.
        static thread_local bool initialised = false;
        if (!initialised) {
            table.fill(0xFFFF);
            initialised = true;
        }
        for (const Block b : blocks_) {
            std::uint16_t& slot = (*lookup)[stateId(b)];
            if (slot == 0xFFFF) {
                if (palette.size() >= 256) {
                    overflow = true;
                    break;
                }
                slot = static_cast<std::uint16_t>(palette.size());
                palette.push_back(b);
            }
        }
        if (overflow) {
            for (const Block b : palette) (*lookup)[stateId(b)] = 0xFFFF;
            // Rare: every block's own id is stored.
            for (const Block b : blocks_) (*lookup)[stateId(b)] = 0xFFFF;
            section->packing = Packing::Wide;
            section->data = std::make_unique<std::uint8_t[]>(8192);
            for (int i = 0; i < kSectionVolume; ++i) {
                const std::uint16_t id = stateId(blocks_[static_cast<std::size_t>(i)]);
                section->data[static_cast<std::size_t>(i) * 2] = static_cast<std::uint8_t>(id & 0xFF);
                section->data[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::uint8_t>(id >> 8);
            }
            return section;
        }
        if (palette.size() <= 16) {
            section->packing = Packing::Nibble;
            section->data = std::make_unique<std::uint8_t[]>(2048);
            for (int i = 0; i < kSectionVolume; i += 2) {
                const std::uint8_t lo = static_cast<std::uint8_t>((*lookup)[stateId(blocks_[static_cast<std::size_t>(i)])]);
                const std::uint8_t hi = static_cast<std::uint8_t>((*lookup)[stateId(blocks_[static_cast<std::size_t>(i) + 1])]);
                section->data[static_cast<std::size_t>(i >> 1)] = static_cast<std::uint8_t>(lo | (hi << 4));
            }
        } else {
            section->packing = Packing::Byte;
            section->data = std::make_unique<std::uint8_t[]>(4096);
            for (int i = 0; i < kSectionVolume; ++i) {
                section->data[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((*lookup)[stateId(blocks_[static_cast<std::size_t>(i)])]);
            }
        }
        for (const Block b : palette) (*lookup)[stateId(b)] = 0xFFFF;
    }
    section->uniform = palette.front();
    section->palette = std::move(palette);
    return section;
}

SectionPtr uniformSection(Block b) {
    if (b == Block::Air) return nullptr;
    // One shared object per block state.
    static std::mutex mutex;
    static std::unordered_map<std::uint16_t, SectionPtr> shared;
    std::lock_guard<std::mutex> lock(mutex);
    SectionPtr& slot = shared[stateId(b)];
    if (!slot) {
        auto s = std::make_shared<SectionData>();
        s->uniform = b;
        slot = s;
    }
    return slot;
}

SectionPtr sectionWithBlock(const SectionPtr& section, int x, int y, int z, Block b) {
    SectionBuilder builder;
    builder.load(section);
    builder.set(x, y, z, b);
    return builder.build();
}

Block Chunk::at(int x, int y, int z) const {
    if (y < kMinY || y >= kMaxY) return Block::Air;
    const SectionPtr& s = sections[static_cast<std::size_t>(sectionOf(y))];
    return s ? s->at(x, (y - kMinY) & 15, z) : Block::Air;
}

std::size_t Chunk::memoryBytes() const {
    std::size_t bytes = sizeof(Chunk);
    for (const SectionPtr& s : sections) {
        if (s && !s->isUniform()) bytes += s->memoryBytes();  // uniform sections are shared
    }
    return bytes;
}

Chunk* Level::chunk(ChunkPos pos) {
    const auto it = chunks_.find(pos);
    return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk* Level::chunk(ChunkPos pos) const {
    const auto it = chunks_.find(pos);
    return it == chunks_.end() ? nullptr : it->second.get();
}

bool Level::isLoaded(int blockX, int blockZ) const { return chunk({chunkOf(blockX), chunkOf(blockZ)}) != nullptr; }

Block Level::block(int x, int y, int z) const {
    const Chunk* c = chunk({chunkOf(x), chunkOf(z)});
    return c != nullptr ? c->at(x & 15, y, z & 15) : Block::Air;
}

bool Level::setBlock(int x, int y, int z, Block b) {
    if (y < kMinY || y >= kMaxY) return false;
    Chunk* c = chunk({chunkOf(x), chunkOf(z)});
    if (c == nullptr) return false;
    SectionPtr& s = c->sections[static_cast<std::size_t>(sectionOf(y))];
    s = sectionWithBlock(s, x & 15, (y - kMinY) & 15, z & 15, b);
    return true;
}

std::size_t Level::memoryBytes() const {
    std::size_t bytes = 0;
    for (const auto& [pos, c] : chunks_) bytes += c->memoryBytes();
    return bytes;
}

}  // namespace falcon
