#pragma once

// Shared by the files of this test (D:/Projects/Falcon/tests/test_worldgen_chunks_*.cpp): the includes, CHECK and the helpers, and the tests that live in the other files.
// Bit-exact tests for the second half of terrain generation: biomes (SHA-256 seed, climate quantisation, the R-tree,
// the BiomeManager zoom), the aquifer, ore veins, bedrock/deepslate, clay bands, the surface rules and whole chunks.
//
// Ground truth comes from the game's own classes (Minecraft 26.3), run in a standalone harness for seed 12345:
//   biome_vectors.inc / zoom_vectors.inc   climate vectors and zoomed biomes (text, generated from the harness dumps)
//   chunks_seed12345.bin                   40 whole chunks (fill and surface, every block) in a compact run-length format
// Everything is embedded or lives next to this file; the tests that need the game's JSON data find it through
// locateAssets() and print a skip message when it is not there.

#include "falcon/assets/asset_locator.hpp"
#include "falcon/platform/memory.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/worldgen/aquifer.hpp"
#include "falcon/worldgen/biome.hpp"
#include "falcon/worldgen/material.hpp"
#include "falcon/worldgen/overworld_generator.hpp"
#include "falcon/worldgen/underground.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef FALCON_TEST_DATA_DIR
#define FALCON_TEST_DATA_DIR "tests/data"
#endif

using namespace falcon::worldgen;


inline int g_failures = 0;
inline long g_checks = 0;

#define CHECK(expr)                                                                          \
    do {                                                                                     \
        ++g_checks;                                                                          \
        if (!(expr)) {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (false)

inline void checkInt(const std::string& what, long long got, long long want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got %lld want %lld\n", what.c_str(), got, want);
        ++g_failures;
    }
}
inline void checkStr(const std::string& what, const std::string& got, const std::string& want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got %s want %s\n", what.c_str(), got.c_str(), want.c_str());
        ++g_failures;
    }
}

inline std::string hex(const std::array<std::uint8_t, 32>& d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : d) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

// =================================================================================================================
struct GameData {
    std::filesystem::path root;       // the game data directory (parent of the assets root)
    std::filesystem::path worldgen;   // <root>/data/minecraft/worldgen
};

// =================================================================================================================
// Whole-chunk ground truth (chunks_seed12345.bin: converted from the harness' FILL / SURF run-length dumps)
struct TruthColumn {
    int top = 0;     // WORLD_SURFACE_WG height (y of the highest non-air block)
    int biome = 0;   // index into Truth::biomes (SURF only)
    std::vector<std::pair<int, int>> runs;  // (length, palette index), from y = 319 downward
};

struct TruthChunk {
    int cx = 0, cz = 0;
    bool hasFill = false, hasSurf = false;
    std::vector<TruthColumn> fill, surf;  // 256 each, index x + 16 * z
};

struct Truth {
    std::vector<std::string> palette, biomes;
    std::vector<TruthChunk> chunks;
};

inline std::filesystem::path testDataPath(const char* name) { return std::filesystem::path(FALCON_TEST_DATA_DIR) / name; }

// The tests that are in the other files.
void testSeedHash();
void testClimateBasics();
void testBiomes(const GameData& gd);
bool loadTruth(const std::filesystem::path& file, Truth& out);
void testChunks(const OverworldGenerator& gen, const falcon::BlockRegistry& registry, const Truth& truth);
void testPrimitives(const OverworldGenerator& gen, const falcon::BlockRegistry& registry);
void testTerrainChecksum(const OverworldGenerator& gen);
void testChunkObject(const OverworldGenerator& gen);
void testThreads(const OverworldGenerator& gen);
void testPresetsAndSpawn(const GameData& gd, const falcon::BlockRegistry& registry, const OverworldGenerator& gen);
void testErrorsAndFacts(const GameData& gd, const falcon::BlockRegistry& registry, const OverworldGenerator& gen);
