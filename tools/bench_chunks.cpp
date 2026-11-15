// How fast chunks are made, and a hash of what is made: for speeding up terrain generation without changing a single block.
//
//   falcon_bench_chunks [--size N] [--threads T] [--seed S] [--light] [--no-caves] [--no-decorate] [--sample gen|light <file>]
//
// Makes the N x N chunks round the spawn of the seed the way the game's workers do (terrain, the caves and ores, the decoration), on T threads, then (with --light) the light of
// the ones that have all their neighbours. Prints the time per chunk and a hash of every block, biome and light value: the hash is the same for any T and must stay the same
// when something is only made faster. FALCON_PROFILE_WORLDGEN=1 adds where the time of the terrain went. It reads the game's data the way the tests do (locateAssets).
#include "falcon/assets/asset_locator.hpp"
#include "falcon/platform/sampling_profiler.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/decoration.hpp"
#include "falcon/world/light.hpp"
#include "falcon/worldgen/noise_router.hpp"
#include "falcon/worldgen/overworld_generator.hpp"
#include "falcon/worldgen/underground.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

namespace {

double millisSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Hasher {
    std::uint64_t h = 0xcbf29ce484222325ull;
    void add(std::uint64_t v) {
        h ^= v;
        h *= 0x100000001b3ull;
    }
};

// One value that depends on the chunk's place and everything in it; the sum over the chunks does not depend on the order they were made in.
std::uint64_t hashChunk(int cx, int cz, const falcon::Chunk& chunk) {
    Hasher h;
    h.add(static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32 | static_cast<std::uint32_t>(cz));
    std::vector<falcon::Block> blocks(falcon::kSectionVolume);
    for (const falcon::SectionPtr& section : chunk.sections) {
        if (!section) {
            h.add(0xFFFFFFFFull);
            continue;
        }
        section->decode(blocks.data());
        for (const falcon::Block b : blocks) h.add(static_cast<std::uint64_t>(falcon::stateId(b)));
    }
    if (chunk.biomes) {
        for (const std::uint8_t id : chunk.biomes->ids) h.add(id);
    }
    return h.h;
}

std::uint64_t hashLight(int cx, int cz, const falcon::ChunkLight& light) {
    Hasher h;
    h.add(static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32 | static_cast<std::uint32_t>(cz));
    for (int s = 0; s < falcon::kSectionCount; ++s) {
        const auto i = static_cast<std::size_t>(s);
        h.add(light.skyFill[i]);
        h.add(light.blockFill[i]);
        for (const auto* arrays : {&light.sky, &light.block}) {
            const auto& a = (*arrays)[i];
            h.add(a ? 1u : 0u);
            if (a) {
                for (const std::uint8_t byte : a->data) h.add(byte);
            }
        }
    }
    return h.h;
}

}  // namespace

int main(int argc, char** argv) {
    int size = 12;
    unsigned threads = 1;
    std::int64_t seed = 12345;
    bool withLight = false, withCaves = true, withDecoration = true;
    std::string samplePhase, sampleFile;  // --sample gen|light <file>
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--size" && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (a == "--threads" && i + 1 < argc) threads = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoll(argv[++i], nullptr, 10);
        else if (a == "--light") withLight = true;
        else if (a == "--no-caves") withCaves = false;
        else if (a == "--no-decorate") withDecoration = false;
        else if (a == "--sample" && i + 2 < argc) {
            samplePhase = argv[++i];
            sampleFile = argv[++i];
        }
        else {
            std::fputs("usage: falcon_bench_chunks [--size N] [--threads T] [--seed S] [--light] [--no-caves] [--no-decorate] [--sample gen|light <file>]\n", stderr);
            return 2;
        }
    }
    threads = std::max(1u, threads);
    if (!sampleFile.empty()) {
#ifdef _WIN32
        _putenv_s("FALCON_SAMPLE", sampleFile.c_str());
#else
        setenv("FALCON_SAMPLE", sampleFile.c_str(), 1);
#endif
    }

    const falcon::AssetSearchResult assets = falcon::locateAssets(std::nullopt);
    if (!assets.location) {
        std::fputs("the game's assets were not found (the tests look in the same places)\n", stderr);
        return 1;
    }
    const std::filesystem::path root = assets.location->root.parent_path();
    std::string error;
    const auto registry = falcon::BlockRegistry::loadFromGameData(root, std::filesystem::temp_directory_path() / "falcon_bench_chunks", error);
    if (!registry) {
        std::fprintf(stderr, "block registry: %s\n", error.c_str());
        return 1;
    }
    auto generator = falcon::worldgen::OverworldGenerator::create(root, *registry, seed, error);
    if (!generator) {
        std::fprintf(stderr, "generator: %s\n", error.c_str());
        return 1;
    }
    const falcon::worldgen::Underground underground(*registry, seed);
    if (withCaves) {
        const falcon::worldgen::OverworldGenerator* self = generator.get();
        generator->setChunkStage([&underground, self](std::vector<falcon::Block>& blocks, int cx, int cz) { underground.apply(blocks, cx, cz, *self); });
    }
    const falcon::Decorator decorator(*generator, *registry, seed);

    const auto spawn = generator->findSpawn();
    const int baseX = (static_cast<int>(std::floor(spawn.x)) >> 4) - size / 2, baseZ = (static_cast<int>(std::floor(spawn.z)) >> 4) - size / 2;
    const int count = size * size;
    std::vector<std::shared_ptr<falcon::Chunk>> chunks(static_cast<std::size_t>(count));
    std::vector<std::uint64_t> hashes(static_cast<std::size_t>(count));

    // Warm-up on one chunk elsewhere: first-use costs (tables, thread-local buffers) are not what is measured.
    {
        falcon::worldgen::ChunkExtras extras;
        (void)generator->generate(baseX + 1000, baseZ + 1000, extras);
    }

    std::atomic<int> next{0};
    if (samplePhase == "gen") falcon::profiler::start();
    const auto started = Clock::now();
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            if (samplePhase == "gen") falcon::profiler::registerThread("gen");
            for (int i = next++; i < count; i = next++) {
                const int cx = baseX + i % size, cz = baseZ + i / size;
                falcon::worldgen::ChunkExtras extras;
                std::shared_ptr<falcon::Chunk> chunk = generator->generate(cx, cz, extras);
                if (withDecoration) decorator.decorate(*chunk, cx, cz, extras);
                auto grid = std::make_shared<falcon::BiomeGrid>();
                std::copy(extras.biomes.ids.begin(), extras.biomes.ids.end(), grid->ids.begin());
                chunk->biomes = std::move(grid);
                hashes[static_cast<std::size_t>(i)] = hashChunk(cx, cz, *chunk);
                chunks[static_cast<std::size_t>(i)] = std::move(chunk);
            }
        });
    }
    for (std::thread& t : pool) t.join();
    const double wall = millisSince(started);
    if (samplePhase == "gen") falcon::profiler::finish();
    std::uint64_t total = 0;
    for (const std::uint64_t h : hashes) total += h;
    std::printf("%d chunks on %u thread%s: %.2f ms of wall time per chunk (%.1f chunks/s), %.2f ms of thread time per chunk; blocks hash %016llx\n", count, threads, threads == 1 ? "" : "s",
                wall / count, count * 1000.0 / wall, wall * threads / count, static_cast<unsigned long long>(total));

    if (withLight) {
        falcon::Level level;
        for (int i = 0; i < count; ++i) level.putChunk({baseX + i % size, baseZ + i / size}, chunks[static_cast<std::size_t>(i)]);
        std::vector<std::uint64_t> lightHashes(static_cast<std::size_t>(count), 0);
        std::atomic<int> nextLight{0};
        if (samplePhase == "light") falcon::profiler::start();
        const auto lightStart = Clock::now();
        std::vector<std::thread> lightPool;
        for (unsigned t = 0; t < threads; ++t) {
            lightPool.emplace_back([&] {
                if (samplePhase == "light") falcon::profiler::registerThread("light");
                for (int i = nextLight++; i < count; i = nextLight++) {
                    const int x = i % size, z = i / size;
                    if (x == 0 || z == 0 || x == size - 1 || z == size - 1) continue;  // needs all eight neighbours
                    const auto light = falcon::computeChunkLight(falcon::snapshotLightInput(level, baseX + x, baseZ + z));
                    lightHashes[static_cast<std::size_t>(i)] = hashLight(baseX + x, baseZ + z, *light);
                }
            });
        }
        for (std::thread& t : lightPool) t.join();
        const double lightWall = millisSince(lightStart);
        if (samplePhase == "light") falcon::profiler::finish();
        const int lit = (size - 2) * (size - 2);
        std::uint64_t lightTotal = 0;
        for (const std::uint64_t h : lightHashes) lightTotal += h;
        std::printf("light of %d chunks: %.2f ms of wall time per chunk, %.2f ms of thread time per chunk; light hash %016llx\n", lit, lightWall / lit, lightWall * threads / lit,
                    static_cast<unsigned long long>(lightTotal));
    }
    return 0;
}
