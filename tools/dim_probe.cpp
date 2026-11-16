// What the Nether's and the End's generators make: a chunk's blocks counted, a slice of it drawn in letters, and the biomes. A look at the terrain without starting the game.
//
//   falcon_dim_probe [--dim nether|end|overworld] [--seed S] [--chunk X Z] [--slice Z] [--map N]
//
// --map N prints the top-down surface of N x N chunks round the chunk (one character per two blocks), for the shape of the land. It reads the game's data the way the tests do.
#include "falcon/assets/asset_locator.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/worldgen/overworld_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string dim = "nether";
    std::int64_t seed = 12345;
    int cx = 0, cz = 0, slice = 8, mapSize = 0, layer = -1000;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--dim" && i + 1 < argc) dim = argv[++i];
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoll(argv[++i], nullptr, 10);
        else if (a == "--chunk" && i + 2 < argc) {
            cx = std::atoi(argv[++i]);
            cz = std::atoi(argv[++i]);
        } else if (a == "--slice" && i + 1 < argc) slice = std::atoi(argv[++i]);
        else if (a == "--map" && i + 1 < argc) mapSize = std::atoi(argv[++i]);
        else if (a == "--layer" && i + 1 < argc) layer = std::atoi(argv[++i]);  // with --map: what is at this height instead of the highest block
        else {
            std::fputs("usage: falcon_dim_probe [--dim nether|end|overworld] [--seed S] [--chunk X Z] [--slice Z] [--map N]\n", stderr);
            return 2;
        }
    }
    const falcon::AssetSearchResult assets = falcon::locateAssets(std::nullopt);
    if (!assets.location) {
        std::fputs("the game's assets were not found\n", stderr);
        return 1;
    }
    const std::filesystem::path root = assets.location->root.parent_path();
    std::string error;
    const auto registry = falcon::BlockRegistry::loadFromGameData(root, std::filesystem::temp_directory_path() / "falcon_dim_probe", error);
    if (!registry) {
        std::fprintf(stderr, "block registry: %s\n", error.c_str());
        return 1;
    }
    falcon::installBlockRegistry(registry);
    falcon::worldgen::GeneratorOptions options;
    if (dim == "nether") {
        options.noiseSettings = "minecraft:nether";
        options.biomePreset = "minecraft:nether";
    } else if (dim == "end") {
        options.noiseSettings = "minecraft:end";
        options.biomePreset = "minecraft:the_end";
    }
    const auto started = std::chrono::steady_clock::now();
    auto generator = falcon::worldgen::OverworldGenerator::create(root, *registry, seed, error, options);
    if (!generator) {
        std::fprintf(stderr, "generator: %s\n", error.c_str());
        return 1;
    }
    std::printf("generator ready in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());

    const auto glyph = [&](falcon::Block b) -> char {
        if (b == falcon::Block::Air) return ' ';
        const std::string& name = registry->type(b).name;
        if (name == "netherrack") return '#';
        if (name == "lava") return '~';
        if (name == "bedrock") return 'B';
        if (name == "soul_sand" || name == "soul_soil") return 's';
        if (name == "gravel") return 'g';
        if (name == "basalt" || name == "blackstone") return 'b';
        if (name == "crimson_nylium") return 'c';
        if (name == "warped_nylium") return 'w';
        if (name == "end_stone") return 'e';
        if (name == "magma_block") return 'm';
        if (name == "stone") return '+';
        return '?';
    };
    falcon::worldgen::ChunkExtras lastExtras;
    const auto chunkAt = [&](int x, int z) {
        falcon::worldgen::ChunkExtras extras;
        auto made = generator->generate(x, z, extras);
        lastExtras = extras;
        return made;
    };

    const auto t0 = std::chrono::steady_clock::now();
    auto chunk = chunkAt(cx, cz);
    std::printf("chunk %d,%d made in %.1f ms\n", cx, cz, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    std::map<std::string, long> counts;
    for (int y = falcon::kMinY; y < falcon::kMaxY; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) ++counts[registry->type(chunk->at(x, y, z)).name];
        }
    }
    std::vector<std::pair<long, std::string>> sorted;
    for (const auto& [name, n] : counts) sorted.emplace_back(n, name);
    std::sort(sorted.rbegin(), sorted.rend());
    for (std::size_t i = 0; i < sorted.size() && i < 12; ++i) std::printf("  %-22s %ld\n", sorted[i].second.c_str(), sorted[i].first);

    std::printf("slice z=%d (x across, y 127 down to 0, two rows per line):\n", slice);
    for (int y = 126; y >= 0; y -= 2) {
        std::string line;
        for (int x = 0; x < 16; ++x) {
            const char a = glyph(chunk->at(x, y, slice)), b = glyph(chunk->at(x, y + 1, slice));
            line += b != ' ' ? b : a;
        }
        std::printf("%3d |%s|\n", y, line.c_str());
    }
    {
        std::map<int, int> biomes;
        for (const std::uint8_t id : lastExtras.biomes.ids) ++biomes[id];
        std::printf("biomes:");
        for (const auto& [id, n] : biomes) std::printf(" %s x%d", generator->biomes().name(static_cast<falcon::worldgen::BiomeId>(id)).c_str(), n);
        std::printf("\n");
    }
    if (mapSize > 0) {
        std::printf("top-down (highest block that is not air, per 2x2 blocks), chunks %d..%d:\n", cx - mapSize / 2, cx + mapSize / 2);
        for (int mz = cz - mapSize / 2; mz <= cz + mapSize / 2; ++mz) {
            std::vector<std::shared_ptr<falcon::Chunk>> row;
            for (int mx = cx - mapSize / 2; mx <= cx + mapSize / 2; ++mx) row.push_back(chunkAt(mx, mz));
            for (int z = 0; z < 16; z += 2) {
                std::string line;
                for (const auto& c : row) {
                    for (int x = 0; x < 16; x += 2) {
                        char ch = '.';
                        if (layer > -1000) {
                            const falcon::Block b = c->at(x, layer, z);
                            line += b == falcon::Block::Air ? ' ' : glyph(b);
                            continue;
                        }
                        for (int y = falcon::kMaxY - 1; y >= falcon::kMinY; --y) {
                            const falcon::Block b = c->at(x, y, z);
                            if (b != falcon::Block::Air) {
                                ch = glyph(b);
                                break;
                            }
                        }
                        line += ch;
                    }
                }
                std::printf("%s\n", line.c_str());
            }
        }
    }
    return 0;
}
