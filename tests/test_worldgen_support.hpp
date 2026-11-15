#pragma once

// Shared by the files of this test (D:/Projects/Falcon/tests/test_worldgen_*.cpp): the includes, CHECK and the helpers, and the tests that live in the other files.
// Bit-exact tests for terrain generation: the random sources, noise primitives, NormalNoise, BlendedNoise, the
// density-function engine and the chunk volume of final_density.
//
// Where the numbers come from (labels used in the comments below):
//   REAL     produced by calling the game's own classes (Minecraft 26.3) in a standalone program.
//   GT       the same, for the full density-function / noise-router stack (real NoiseChunk samplers).
//   COMPOSED real game noise objects combined by formulas derived from the bytecode (not a call of the game class
//            that does the combining). These are the weaker vectors.
// All numbers are copied into this file; the tests that need the game's worldgen JSON (noise parameters, density
// functions, noise settings) find it through locateAssets() and print a skip message when it is not there.

#include "falcon/assets/asset_locator.hpp"
#include "falcon/worldgen/chunk_fill.hpp"
#include "falcon/worldgen/density.hpp"
#include "falcon/worldgen/java_math.hpp"
#include "falcon/worldgen/json_tree.hpp"
#include "falcon/worldgen/noise.hpp"
#include "falcon/worldgen/noise_router.hpp"
#include "falcon/worldgen/random.hpp"
#include "falcon/worldgen/random_state.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

inline std::uint32_t bitsOf(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}
inline std::uint64_t bitsOf(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    return u;
}

// Compares raw bits and says which vector failed.
inline void checkBits(const std::string& what, std::uint32_t got, std::uint32_t want) {
    ++g_checks;
    if (got != want) {
        float asFloat;
        std::memcpy(&asFloat, &got, sizeof asFloat);
        std::fprintf(stderr, "MISMATCH %s: got 0x%08x (%.9g) want 0x%08x\n", what.c_str(), got, static_cast<double>(asFloat), want);
        ++g_failures;
    }
}
inline void checkBits64(const std::string& what, std::uint64_t got, std::uint64_t want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got 0x%016llx want 0x%016llx\n", what.c_str(), static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
        ++g_failures;
    }
}
inline void checkInt(const std::string& what, long long got, long long want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got %lld want %lld\n", what.c_str(), got, want);
        ++g_failures;
    }
}

// =================================================================================================================
// Random sources                                                  [REAL: real-classes-groundtruth.txt, section 1]
struct RandomVec {
    std::int64_t seed;
    std::uint64_t unmixedLo, unmixedHi, mixedLo, mixedHi;
    std::uint64_t longs[5];
    std::int32_t ints[5];
    std::int32_t bound100[5], bound256[5], range3to10[5];
    std::uint64_t doubles[5];
    std::uint32_t floats[5];
    bool bools[8];
    std::uint64_t fromHashOf[3];   // forkPositional().fromHashOf("minecraft:continentalness") nextLong x3
    std::uint64_t at123;           // forkPositional().at(1,2,3).nextLong
    bool hasExtras;
    std::uint64_t fromSeed777;     // forkPositional().fromSeed(777).nextLong
    std::uint64_t fork[2];         // fork(): child nextLong x2 (the parent then continues with longs[2])
};

const RandomVec kRandomVecs[] = {
    {0, 0x6A09E667F3BCC909ull, 0x084160217307451Eull, 0x3564B439CD1E1F16ull, 0x63CFC62A2B097592ull,
     {0x2A2CA488F66F517Eull, 0xCCBC22D72E97C372ull, 0x404E64B826F4B9F4ull, 0x1DFBE5A84FB8F31Bull, 0x1986C1CAE1D8F2C5ull},
     {-160476802, 781697906, 653572596, 1337520923, -505875771},
     {96, 18, 15, 31, 88}, {246, 46, 38, 79, 225}, {9, 4, 4, 5, 9},
     {0x3fc51652447b37a8ull, 0x3fe997845ae5d2f8ull, 0x3fd013992e09bd2eull, 0x3fbdfbe5a84fb8f0ull, 0x3fb986c1cae1d8f0ull},
     {0x3e28b290u, 0x3f4cbc22u, 0x3e809cc8u, 0x3defdf28u, 0x3dcc3608u},
     {false, false, false, true, true, true, false, true},
     {0x6EB32CDA42831099ull, 0xF7E4A592E7138F42ull, 0x6F3D4198B2BED34Full}, 0xA730510BAEF3ADA4ull,
     true, 0xB8ECEE971C554048ull, {0xB8ECEE9720513F4Full, 0x3C030AB9DED89920ull}},
    {12345, 0x6A09E667F3BCF930ull, 0x0841602173077545ull, 0x0A2C34E6CA54DD9Eull, 0xCF828DADC78BBEEBull,
     {0x8F5558A8036890FBull, 0x725FE51AD193097Eull, 0x3981933FE0893743ull, 0x1105672904B629A9ull, 0x904054108E0989D4ull},
     {57184507, -778892930, -527878333, 79047081, -1911977516},
     {1, 81, 87, 1, 55}, {3, 209, 224, 4, 142}, {3, 8, 9, 3, 6},
     {0x3fe1eaab15006d12ull, 0x3fdc97f946b464c2ull, 0x3fccc0c99ff04498ull, 0x3fb105672904b628ull, 0x3fe2080a8211c131ull},
     {0x3f0f5558u, 0x3ee4bfcau, 0x3e66064cu, 0x3d882b38u, 0x3f104054u},
     {true, false, true, true, false, false, true, false},
     {0x30C0DCEF3A650C47ull, 0x1EEA7A6F68591724ull, 0x2B9B4495043C8050ull}, 0xAECFA12907096F18ull,
     true, 0x0ADB029F403A975Cull, {0x0ADB029F385A9465ull, 0xD6AF6223E96ACF03ull}},
    {-1, 0x95F619980C4336F6ull, 0x342D93518B8DB30Bull, 0xFE766AB13AFAB784ull, 0x28B159DEFD9C7FBAull,
     {0x8796DBDFA97705D3ull, 0xF3F229BC77C85F24ull, 0xA8215B2E369F765Aull, 0xE34B3DFAFCE90551ull, 0xB1EC3CDC37A4DDFBull},
     {-1451817517, 2009620260, 916420186, -51837615, 933551611},
     {66, 46, 21, 98, 21}, {169, 119, 54, 252, 55}, {7, 6, 4, 9, 4},
     {0x3fe0f2db7bf52ee0ull, 0x3fee7e45378ef90bull, 0x3fe5042b65c6d3eeull, 0x3fec6967bf5f9d20ull, 0x3fe63d879b86f49bull},
     {0x3f0796dbu, 0x3f73f229u, 0x3f28215bu, 0x3f634b3du, 0x3f31ec3cu},
     {true, false, false, true, true, true, false, true},
     {0x94D8D7815A096496ull, 0x3206A5E721881557ull, 0x3098203494F30D94ull}, 0x40E23A453EB914E1ull,
     false, 0, {0, 0}},
};

// The tests that are in the other files.
void testRandomSources();
void testLegacyRandom();
void testPerlin();
void testBlendedNoise();
void testJsonAndParameters();
void testNormalNoise(const std::filesystem::path& worldgen);
void testRouter(const std::filesystem::path& worldgen);
