#include "apex/world/light.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace apex {
namespace {

// The working volume: the middle chunk and the 8 around it, 48 x 48 columns of 512 cells (384 used), column-major so a
// column is contiguous (sky light is filled and searched down columns).
constexpr int kSpan = 48;
constexpr int kColumns = kSpan * kSpan;
constexpr int kStride = 512;
constexpr std::size_t kCells = static_cast<std::size_t>(kColumns) * kStride;

struct ColumnTables {
    std::array<std::uint8_t, kColumns> x{}, z{};
    ColumnTables() {
        for (int c = 0; c < kColumns; ++c) {
            x[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(c % kSpan);
            z[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(c / kSpan);
        }
    }
};
const ColumnTables kTables;

struct Scratch {
    std::vector<std::uint8_t> opacity, sky, block;
    std::vector<std::int16_t> top;  // per column: y index of the highest cell that blocks any light, -1 if none
    std::vector<std::uint32_t> queue;
    std::vector<std::uint32_t> emitters;
    std::vector<Block> decoded;
    Scratch() : opacity(kCells), sky(kCells), block(kCells), top(kColumns), decoded(kSectionVolume) { queue.reserve(1 << 16); }
};

// Spreads light from the cells in `queue` (already holding their level in `level`). `skyMode`: the sky rule that full
// light goes straight down through clear blocks without losing a level.
void flood(std::vector<std::uint8_t>& level, const std::vector<std::uint8_t>& opacity, std::vector<std::uint32_t>& queue, bool skyMode) {
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::uint32_t idx = queue[head];
        const int l = level[idx];
        if (l <= 1) continue;
        const int y = static_cast<int>(idx & (kStride - 1));
        const std::uint32_t col = idx >> 9;
        const int cx = kTables.x[col], cz = kTables.z[col];
        const auto visit = [&](std::uint32_t n, bool down) {
            const int o = opacity[n];
            if (o >= 15) return;
            const int next = (skyMode && down && l == 15) ? 15 - o : l - std::max(1, o);
            if (next > level[n]) {
                level[n] = static_cast<std::uint8_t>(next);
                queue.push_back(n);
            }
        };
        if (y > 0) visit(idx - 1, true);
        if (y < kSectionCount * 16 - 1) visit(idx + 1, false);
        if (cx > 0) visit(idx - kStride, false);
        if (cx < kSpan - 1) visit(idx + kStride, false);
        if (cz > 0) visit(idx - kSpan * kStride, false);
        if (cz < kSpan - 1) visit(idx + kSpan * kStride, false);
    }
    queue.clear();
}

std::shared_ptr<const NibbleArray> pack(const std::vector<std::uint8_t>& level, int chunkX0, int chunkZ0, int sy, std::uint8_t& fill) {
    int lo = 15, hi = 0;
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const std::size_t base = (static_cast<std::size_t>(chunkZ0 + z) * kSpan + static_cast<std::size_t>(chunkX0 + x)) * kStride + static_cast<std::size_t>(sy) * 16;
            for (int y = 0; y < 16; ++y) {
                const int v = level[base + static_cast<std::size_t>(y)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
    }
    if (lo == hi) {
        fill = static_cast<std::uint8_t>(lo);
        return nullptr;
    }
    auto a = std::make_shared<NibbleArray>();
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const std::size_t base = (static_cast<std::size_t>(chunkZ0 + z) * kSpan + static_cast<std::size_t>(chunkX0 + x)) * kStride + static_cast<std::size_t>(sy) * 16;
            for (int y = 0; y < 16; ++y) a->set(SectionData::index(x, y, z), level[base + static_cast<std::size_t>(y)]);
        }
    }
    return a;
}

}  // namespace

std::size_t ChunkLight::memoryBytes() const {
    std::size_t bytes = sizeof(ChunkLight);
    for (int s = 0; s < kSectionCount; ++s) {
        if (sky[static_cast<std::size_t>(s)]) bytes += sizeof(NibbleArray);
        if (block[static_cast<std::size_t>(s)]) bytes += sizeof(NibbleArray);
    }
    return bytes;
}

LightInput snapshotLightInput(const Level& level, int chunkX, int chunkZ) {
    LightInput in;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* c = level.chunk({chunkX + dx, chunkZ + dz});
            if (c != nullptr) in.chunks[static_cast<std::size_t>((dx + 1) + 3 * (dz + 1))] = c->sections;
        }
    }
    return in;
}

std::shared_ptr<const ChunkLight> computeChunkLight(const LightInput& input) {
    thread_local Scratch scratch;
    Scratch& s = scratch;
    const BlockRegistry& registry = blockRegistry();
    std::fill(s.opacity.begin(), s.opacity.end(), std::uint8_t{0});
    std::fill(s.sky.begin(), s.sky.end(), std::uint8_t{0});
    std::fill(s.block.begin(), s.block.end(), std::uint8_t{0});
    s.emitters.clear();

    // 1. What blocks light, and what gives it off.
    for (int cz = 0; cz < 3; ++cz) {
        for (int cx = 0; cx < 3; ++cx) {
            const auto& sections = input.chunks[static_cast<std::size_t>(cx + 3 * cz)];
            for (int sy = 0; sy < kSectionCount; ++sy) {
                const SectionPtr& sec = sections[static_cast<std::size_t>(sy)];
                if (!sec) continue;  // air: no opacity, no emission
                const auto cellIndex = [&](int x, int y, int z) {
                    return (static_cast<std::size_t>(cz * 16 + z) * kSpan + static_cast<std::size_t>(cx * 16 + x)) * kStride + static_cast<std::size_t>(sy) * 16 + static_cast<std::size_t>(y);
                };
                if (sec->isUniform()) {
                    const StateInfo& info = registry.state(sec->uniform);
                    if (info.dampening == 0 && info.emission == 0) continue;
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            for (int y = 0; y < 16; ++y) {
                                const std::size_t i = cellIndex(x, y, z);
                                s.opacity[i] = info.dampening;
                                if (info.emission > 0) {
                                    s.block[i] = info.emission;
                                    s.emitters.push_back(static_cast<std::uint32_t>(i));
                                }
                            }
                        }
                    }
                    continue;
                }
                sec->decode(s.decoded.data());
                Block last = Block::Air;
                std::uint8_t damp = 0, emit = 0;
                bool haveLast = false;
                for (int i = 0; i < kSectionVolume; ++i) {
                    const Block b = s.decoded[static_cast<std::size_t>(i)];
                    if (!haveLast || b != last) {
                        const StateInfo& info = registry.state(b);
                        damp = info.dampening;
                        emit = info.emission;
                        last = b;
                        haveLast = true;
                    }
                    if (damp == 0 && emit == 0) continue;
                    const std::size_t at = cellIndex(i & 15, i >> 8, (i >> 4) & 15);
                    s.opacity[at] = damp;
                    if (emit > 0) {
                        s.block[at] = emit;
                        s.emitters.push_back(static_cast<std::uint32_t>(at));
                    }
                }
            }
        }
    }

    // 2. Sky light: full above the highest light-blocking cell of each column, then flooded into the shadows.
    for (int col = 0; col < kColumns; ++col) {
        const std::size_t base = static_cast<std::size_t>(col) * kStride;
        int top = kSectionCount * 16 - 1;
        while (top >= 0 && s.opacity[base + static_cast<std::size_t>(top)] == 0) --top;
        s.top[static_cast<std::size_t>(col)] = static_cast<std::int16_t>(top);
        std::memset(&s.sky[base + static_cast<std::size_t>(top + 1)], 15, static_cast<std::size_t>(kSectionCount * 16 - 1 - top));
    }
    s.queue.clear();
    for (int col = 0; col < kColumns; ++col) {
        const int cx = kTables.x[static_cast<std::size_t>(col)], cz = kTables.z[static_cast<std::size_t>(col)];
        const int top = s.top[static_cast<std::size_t>(col)];
        const std::uint32_t base = static_cast<std::uint32_t>(col) * kStride;
        if (top >= 0 && top + 1 < kSectionCount * 16 && s.opacity[base + static_cast<std::size_t>(top)] < 15) s.queue.push_back(base + static_cast<std::uint32_t>(top + 1));
        // Light spills sideways into the columns whose top is higher: from this column's lit cells next to them.
        int highest = top;
        if (cx > 0) highest = std::max<int>(highest, s.top[static_cast<std::size_t>(col - 1)]);
        if (cx < kSpan - 1) highest = std::max<int>(highest, s.top[static_cast<std::size_t>(col + 1)]);
        if (cz > 0) highest = std::max<int>(highest, s.top[static_cast<std::size_t>(col - kSpan)]);
        if (cz < kSpan - 1) highest = std::max<int>(highest, s.top[static_cast<std::size_t>(col + kSpan)]);
        for (int y = top + 1; y <= highest && y < kSectionCount * 16; ++y) s.queue.push_back(base + static_cast<std::uint32_t>(y));
    }
    flood(s.sky, s.opacity, s.queue, true);

    // 3. Block light from the emitters.
    s.queue.assign(s.emitters.begin(), s.emitters.end());
    flood(s.block, s.opacity, s.queue, false);

    // 4. The middle chunk's part.
    auto light = std::make_shared<ChunkLight>();
    for (int sy = 0; sy < kSectionCount; ++sy) {
        const std::size_t i = static_cast<std::size_t>(sy);
        light->sky[i] = pack(s.sky, 16, 16, sy, light->skyFill[i]);
        light->block[i] = pack(s.block, 16, 16, sy, light->blockFill[i]);
    }
    return light;
}

void computeCarriedLight(const Level& level, int x, int y, int z, int lightLevel, int size, std::vector<std::uint8_t>& volume) {
    const auto total = static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    volume.assign(total, 0);
    lightLevel = std::clamp(lightLevel, 0, 15);
    if (lightLevel == 0 || size < 1) return;
    const int half = size / 2;
    const int originX = x - half, originY = y - half, originZ = z - half;
    const auto indexOf = [size](int cx, int cy, int cz) { return static_cast<std::size_t>(cx + size * (cy + size * cz)); };
    // Flood out from the middle: a block loses one level (or more where the block it enters dims light). Highest level first, so
    // every block ends with the best level any path gives it.
    std::array<std::vector<std::size_t>, 16> byLevel;
    const std::size_t start = indexOf(half, half, half);
    volume[start] = static_cast<std::uint8_t>(lightLevel);
    byLevel[static_cast<std::size_t>(lightLevel)].push_back(start);
    const BlockRegistry& registry = blockRegistry();
    constexpr int kSteps[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int l = lightLevel; l >= 2; --l) {  // level 1 lights nothing further
        const std::vector<std::size_t>& cells = byLevel[static_cast<std::size_t>(l)];
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const std::size_t cell = cells[i];
            if (volume[cell] != l) continue;  // a better level got there since
            const int cx = static_cast<int>(cell % static_cast<std::size_t>(size));
            const int cy = static_cast<int>((cell / static_cast<std::size_t>(size)) % static_cast<std::size_t>(size));
            const int cz = static_cast<int>(cell / (static_cast<std::size_t>(size) * static_cast<std::size_t>(size)));
            for (const auto& step : kSteps) {
                const int nx = cx + step[0], ny = cy + step[1], nz = cz + step[2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= size || ny >= size || nz >= size) continue;
                const StateInfo& info = registry.state(level.block(originX + nx, originY + ny, originZ + nz));
                const int next = l - std::max(1, static_cast<int>(info.dampening));
                const std::size_t target = indexOf(nx, ny, nz);
                if (next > volume[target]) {
                    volume[target] = static_cast<std::uint8_t>(next);
                    byLevel[static_cast<std::size_t>(next)].push_back(target);
                }
            }
        }
    }
}

}  // namespace apex
