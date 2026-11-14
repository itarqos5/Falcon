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

#include "apex/assets/asset_locator.hpp"
#include "apex/worldgen/chunk_fill.hpp"
#include "apex/worldgen/density.hpp"
#include "apex/worldgen/java_math.hpp"
#include "apex/worldgen/json_tree.hpp"
#include "apex/worldgen/noise.hpp"
#include "apex/worldgen/noise_router.hpp"
#include "apex/worldgen/random.hpp"
#include "apex/worldgen/random_state.hpp"

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

using namespace apex::worldgen;

namespace {

int g_failures = 0;
long g_checks = 0;

#define CHECK(expr)                                                                          \
    do {                                                                                     \
        ++g_checks;                                                                          \
        if (!(expr)) {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (false)

std::uint32_t bitsOf(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}
std::uint64_t bitsOf(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    return u;
}

// Compares raw bits and says which vector failed.
void checkBits(const std::string& what, std::uint32_t got, std::uint32_t want) {
    ++g_checks;
    if (got != want) {
        float asFloat;
        std::memcpy(&asFloat, &got, sizeof asFloat);
        std::fprintf(stderr, "MISMATCH %s: got 0x%08x (%.9g) want 0x%08x\n", what.c_str(), got, static_cast<double>(asFloat), want);
        ++g_failures;
    }
}
void checkBits64(const std::string& what, std::uint64_t got, std::uint64_t want) {
    ++g_checks;
    if (got != want) {
        std::fprintf(stderr, "MISMATCH %s: got 0x%016llx want 0x%016llx\n", what.c_str(), static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
        ++g_failures;
    }
}
void checkInt(const std::string& what, long long got, long long want) {
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

void testRandomSources() {
    for (const RandomVec& v : kRandomVecs) {
        const std::string tag = "random seed " + std::to_string(v.seed) + ": ";
        const Seed128 un = upgradeSeedTo128bitUnmixed(v.seed);
        const Seed128 mx = upgradeSeedTo128bit(v.seed);
        checkBits64(tag + "unmixed lo", un.lo, v.unmixedLo);
        checkBits64(tag + "unmixed hi", un.hi, v.unmixedHi);
        checkBits64(tag + "mixed lo", mx.lo, v.mixedLo);
        checkBits64(tag + "mixed hi", mx.hi, v.mixedHi);
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkBits64(tag + "nextLong " + std::to_string(i), static_cast<std::uint64_t>(r.nextLong()), v.longs[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkInt(tag + "nextInt " + std::to_string(i), r.nextInt(), v.ints[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkInt(tag + "nextInt(100) " + std::to_string(i), r.nextInt(100), v.bound100[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkInt(tag + "nextInt(256) " + std::to_string(i), r.nextInt(256), v.bound256[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkInt(tag + "nextInt(3,10) " + std::to_string(i), r.nextInt(3, 10), v.range3to10[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkBits64(tag + "nextDouble " + std::to_string(i), bitsOf(r.nextDouble()), v.doubles[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 5; ++i) checkBits(tag + "nextFloat " + std::to_string(i), bitsOf(r.nextFloat()), v.floats[i]);
        }
        {
            XoroshiroRandomSource r(v.seed);
            for (int i = 0; i < 8; ++i) checkInt(tag + "nextBoolean " + std::to_string(i), r.nextBoolean() ? 1 : 0, v.bools[i] ? 1 : 0);
        }
        {
            // forkPositional() = the first two nextLong() values; by value and through the interface
            XoroshiroRandomSource r(v.seed);
            const XoroshiroPositionalRandomFactory f = r.forkPositionalValue();
            checkBits64(tag + "factory lo", f.lo(), v.longs[0]);
            checkBits64(tag + "factory hi", f.hi(), v.longs[1]);
            checkBits64(tag + "parent continues", static_cast<std::uint64_t>(r.nextLong()), v.longs[2]);
            XoroshiroRandomSource h = f.makeFromHashOf("minecraft:continentalness");
            for (int i = 0; i < 3; ++i) checkBits64(tag + "fromHashOf " + std::to_string(i), static_cast<std::uint64_t>(h.nextLong()), v.fromHashOf[i]);
            checkBits64(tag + "at(1,2,3)", static_cast<std::uint64_t>(f.makeAt(1, 2, 3).nextLong()), v.at123);

            XoroshiroRandomSource r2(v.seed);
            const std::unique_ptr<PositionalRandomFactory> pf = r2.forkPositional();
            checkBits64(tag + "interface fromHashOf", static_cast<std::uint64_t>(pf->fromHashOf("minecraft:continentalness")->nextLong()), v.fromHashOf[0]);
            checkBits64(tag + "interface at(1,2,3)", static_cast<std::uint64_t>(pf->at(1, 2, 3)->nextLong()), v.at123);
            if (v.hasExtras) {
                checkBits64(tag + "fromSeed(777)", static_cast<std::uint64_t>(f.makeFromSeed(777).nextLong()), v.fromSeed777);
                XoroshiroRandomSource r3(v.seed);
                const std::unique_ptr<RandomSource> child = r3.fork();
                checkBits64(tag + "fork child 0", static_cast<std::uint64_t>(child->nextLong()), v.fork[0]);
                checkBits64(tag + "fork child 1", static_cast<std::uint64_t>(child->nextLong()), v.fork[1]);
                checkBits64(tag + "fork parent continues", static_cast<std::uint64_t>(r3.nextLong()), v.longs[2]);
            }
        }
    }

    // Other primitives [REAL]
    checkBits64("mixStafford13(0)", mixStafford13(0), 0x0ull);
    checkBits64("mixStafford13(1)", mixStafford13(1), 0x5692161D100B05E5ull);
    checkBits64("mixStafford13(-1)", mixStafford13(~0ull), 0xB4D055FCF2CBBD7Bull);
    struct HashVec {
        const char* text;
        std::uint64_t lo, hi;
    };
    const HashVec hashes[] = {
        {"minecraft:continentalness", 0x83886C9D0AE3A662ull, 0xAFA638A61B42E8ADull},
        {"octave_-9", 0x082FE255F8BE6631ull, 0x4E96119E22DEDC81ull},
        {"minecraft:terrain", 0x1EE555222EF96F14ull, 0xE2BEDFDBEBE43D33ull},
        {"minecraft:aquifer", 0x7BB15CC403C6ACE6ull, 0x0BDD56BC9D232691ull},
        {"minecraft:ore", 0x9B88124DE600116Dull, 0x2AE68055AA4A7761ull},
        {"a", 0x0CC175B9C0F1B6A8ull, 0x31C399E269772661ull},
        {"", 0xD41D8CD98F00B204ull, 0xE9800998ECF8427Eull},
    };
    for (const HashVec& h : hashes) {
        const Seed128 s = seedFromHashOf(h.text);
        checkBits64(std::string("seedFromHashOf lo '") + h.text + "'", s.lo, h.lo);
        checkBits64(std::string("seedFromHashOf hi '") + h.text + "'", s.hi, h.hi);
    }
    // MD5 against the RFC 1321 test suite (the 80-character one needs a second block)
    {
        struct Md5Vec {
            const char* text;
            const char* hex;
        };
        const Md5Vec md5Vecs[] = {
            {"", "d41d8cd98f00b204e9800998ecf8427e"},
            {"abc", "900150983cd24fb0d6963f7d28e17f72"},
            {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
            {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
            {"12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a"},
        };
        for (const Md5Vec& m : md5Vecs) {
            const auto d = md5(m.text, std::strlen(m.text));
            std::string hex;
            for (const std::uint8_t b : d) {
                char buf[3];
                std::snprintf(buf, sizeof buf, "%02x", b);
                hex += buf;
            }
            CHECK(hex == m.hex);
        }
    }
    checkBits64("Mth.getSeed(1,2,3)", static_cast<std::uint64_t>(mthGetSeed(1, 2, 3)), 0xFFFFE15FA16D39F8ull);
    checkBits64("Mth.getSeed(0,0,0)", static_cast<std::uint64_t>(mthGetSeed(0, 0, 0)), 0x0ull);
    checkBits64("Mth.getSeed(-5,70,12345)", static_cast<std::uint64_t>(mthGetSeed(-5, 70, 12345)), 0xFFFFA5234C729F5Cull);
    checkBits64("Mth.getSeed(100000,-64,-100000)", static_cast<std::uint64_t>(mthGetSeed(100000, -64, -100000)), 0xFFFFF5F9CABBAB16ull);
    {
        Xoroshiro128PlusPlus zero(0, 0);  // the all-zero state is replaced by golden/silver
        checkBits64("zero-state nextLong 0", zero.nextLong(), 0x5E7A5FC809868C97ull);
        checkBits64("zero-state nextLong 1", zero.nextLong(), 0x493593F747100CAFull);
        Xoroshiro128PlusPlus one(1, 2);
        checkBits64("Xoroshiro(1,2) 0", one.nextLong(), 0x0000000000060001ull);
        checkBits64("Xoroshiro(1,2) 1", one.nextLong(), 0x000260C000660007ull);
        checkBits64("Xoroshiro(1,2) 2", one.nextLong(), 0x180ACC04718606D3ull);
        XoroshiroRandomSource reseeded(5);
        reseeded.setSeed(12345);
        checkBits64("setSeed", static_cast<std::uint64_t>(reseeded.nextLong()), 0x8F5558A8036890FBull);
    }
    CHECK(javaStringHashCode("") == 0 && javaStringHashCode("a") == 97 && javaStringHashCode("ab") == 31 * 97 + 98);
}

// Legacy (LCG) source [REAL: section 1b], seed 12345, one sequential stream
void testLegacyRandom() {
    LegacyRandomSource r(12345);
    checkInt("legacy nextInt 0", r.nextInt(), 1553932502);
    checkInt("legacy nextInt 1", r.nextInt(), -2090749135);
    checkInt("legacy nextInt 2", r.nextInt(), -287790814);
    checkInt("legacy nextInt(100)", r.nextInt(100), 28);
    checkInt("legacy nextInt(64)", r.nextInt(64), 53);
    checkInt("legacy nextLong", r.nextLong(), 694943615613659775ll);
    checkBits64("legacy nextDouble", bitsOf(r.nextDouble()), 0x3fbfea404f12d260ull);
    checkBits("legacy nextFloat", bitsOf(r.nextFloat()), 0x3d10bc60u);
    CHECK(!r.nextBoolean());
    LegacyRandomSource f(12345);
    const std::unique_ptr<PositionalRandomFactory> fp = f.forkPositional();  // seed = first nextLong
    checkInt("legacy fromHashOf(terrain)", fp->fromHashOf("minecraft:terrain")->nextLong(), -5895624637380920488ll);
    LegacyRandomSource g(12345);
    checkInt("legacy first nextLong is the factory seed", g.nextLong(), 6674089274190705457ll);
}

// =================================================================================================================
// Single-octave Perlin noise from Xoroshiro(12345)                [REAL: section 2]
void testPerlin() {
    XoroshiroRandomSource r(12345);
    const PerlinNoise p(r);
    checkBits64("perlin offsetX", bitsOf(p.offsetX()), 0x4061eaab15006d12ull);
    checkBits64("perlin offsetY", bitsOf(p.offsetY()), 0x405c97f946b464c2ull);
    checkBits64("perlin offsetZ", bitsOf(p.offsetZ()), 0x404cc0c99ff04498ull);
    const int head[16] = {4, 142, 177, 55, 130, 78, 251, 190, 237, 248, 118, 218, 250, 10, 31, 38};
    const int tail[16] = {94, 57, 59, 18, 191, 54, 80, 183, 108, 137, 216, 151, 29, 123, 181, 45};
    for (int i = 0; i < 16; ++i) {
        checkInt("perlin perm " + std::to_string(i), p.permutation(i), head[i]);
        checkInt("perlin perm " + std::to_string(240 + i), p.permutation(240 + i), tail[i]);
    }
    struct V {
        double x, y, z;
        std::uint32_t bits;
    };
    const V perlin[] = {
        {0.0, 0.0, 0.0, 0xbe8c6816u},           {0.5, 0.5, 0.5, 0x3e163696u},
        {100.5, 0.0, -250.25, 0xbe860e5eu},     {-1234.567, 0.0, 8910.11, 0x3dcdc600u},
        {3.7, -2.2, 1.9, 0xbe019c5bu},          {16777215.5, 3.0, -16777216.25, 0xbc8ae29eu},
        {40000000.1, 7.3, 123456789.987, 0x3e1b3554u},
    };
    for (const V& v : perlin) checkBits("perlin get " + std::to_string(v.x), bitsOf(p.get(v.x, v.y, v.z)), v.bits);
    checkBits("perlin 2D get", bitsOf(p.get(100.5, -250.25)), 0xbe860e5eu);

    XoroshiroRandomSource r2(12345);
    const SmearedPerlinNoise s(r2, 0.5);
    const V smeared[] = {
        {0.0, 0.0, 0.0, 0xbe8c6816u},           {0.5, 0.5, 0.5, 0x3e0c594du},
        {100.5, 0.0, -250.25, 0xbe860e5eu},     {-1234.567, 0.0, 8910.11, 0x3dcdc600u},
        {3.7, -2.2, 1.9, 0xbe019c5bu},          {16777215.5, 3.0, -16777216.25, 0xbc8ae29eu},
        {40000000.1, 7.3, 123456789.987, 0x3be04300u},
    };
    for (const V& v : smeared) checkBits("smeared get " + std::to_string(v.x), bitsOf(s.get(v.x, v.y, v.z)), v.bits);
}

// =================================================================================================================
// BlendedNoise (0.25, 0.125, 80, 160, 8)  ----  COMPOSED, not REAL: the real SmearedPerlinNoise / NoiseStack objects
// combined with the clamp/lerp formula derived from BlendedNoise's bytecode (the BlendedNoise class itself was not
// invoked). Block coordinates.
void testBlendedNoise() {
    struct V {
        int x, y, z;
        std::uint32_t bits;
    };
    struct SeedVecs {
        std::int64_t seed;
        V v[7];
    };
    const SeedVecs vecs[] = {
        {12345, {{0, 0, 0, 0xbe00af48u}, {100, 64, -250, 0xbe68e044u}, {-1234, -20, 8910, 0x3d472228u}, {37, 200, -3, 0xbde7c1d7u},
                 {16, 80, 16, 0xbdfaf757u}, {-8, 120, 1000, 0xbc567640u}, {5000, 10, -7000, 0xbd08001au}}},
        {0, {{0, 0, 0, 0x3cb8f6bau}, {100, 64, -250, 0xbd558f38u}, {-1234, -20, 8910, 0xbdbc851du}, {37, 200, -3, 0xbe3a3170u},
             {16, 80, 16, 0xbe0654eau}, {-8, 120, 1000, 0x3d9ed5b0u}, {5000, 10, -7000, 0xbe4d8814u}}},
    };
    for (const SeedVecs& sv : vecs) {
        XoroshiroRandomSource root(sv.seed);
        const XoroshiroPositionalRandomFactory f = root.forkPositionalValue();
        XoroshiroRandomSource rnd = f.makeFromHashOf("minecraft:terrain");
        const BlendedNoise bn(BlendedNoiseParams{}, rnd);  // defaults are the Overworld's parameters
        for (const V& v : sv.v) {
            checkBits("COMPOSED blended seed " + std::to_string(sv.seed) + " (" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + ")",
                      bitsOf(bn.sample(v.x, v.y, v.z)), v.bits);
        }
    }
}

// =================================================================================================================
// JSON numbers keep their text; noise parameter files
void testJsonAndParameters() {
    JsonValue j;
    std::string err;
    CHECK(parseJson(R"({"a": 0.1171875, "b": 1.0E-5, "c": -0.50375, "d": [1, 2.5], "e": "s", "f": true, "g": null,
                        "h": 1.00000005960464477539062501})", j, err));
    float f = 0.0f;
    double d = 0.0;
    CHECK(j.find("a") && j.find("a")->toFloat(f) && bitsOf(f) == 0x3df00000u);  // 0.1171875 = 15 / 128
    CHECK(j.find("b") && j.find("b")->toDouble(d) && bitsOf(d) == bitsOf(1.0E-5));
    CHECK(j.find("c") && j.find("c")->toFloat(f) && f == -0.50375f);
    CHECK(j.find("d") && j.find("d")->items.size() == 2 && j.find("d")->items[1].toDouble(d) && d == 2.5);
    CHECK(j.find("e") && j.find("e")->text == "s" && j.find("f") && j.find("f")->boolean && j.find("g") && j.find("g")->kind == JsonValue::Kind::Null);
    // Just above the midpoint of 1.0f and the next float: Float.parseFloat rounds up, (float)Double.parseDouble would not.
    CHECK(j.find("h") && j.find("h")->toFloat(f) && bitsOf(f) == 0x3f800001u);
    CHECK(!parseJson("{\"a\": ", j, err));

    NoiseParameters p;
    JsonValue np;
    CHECK(parseJson(R"({"base_octave":-9,"octave_count":9,"base_amplitude":0.8880832896205223,"amplitude_modifiers":[1.0,1.0,2.0,2.0,2.0,1.0,1.0,1.0,1.0]})", np, err));
    CHECK(parseNoiseParameters(np, p, err) && p.baseOctave == -9 && p.octaveCount == 9 && p.amplitudeModifiers.size() == 9 &&
          p.normalize == NoiseNormalize::Enabled && bitsOf(p.baseAmplitude) == bitsOf(0.8880832896205223));
    CHECK(parseJson(R"({"base_octave":-3,"base_amplitude":1.0,"octave_count":3,"amplitude_modifiers":[1.0]})", np, err));
    CHECK(!parseNoiseParameters(np, p, err));  // modifiers must match octave_count
    CHECK(parseJson(R"({"base_octave":99})", np, err));
    CHECK(!parseNoiseParameters(np, p, err));
    CHECK(parseJson(R"({"base_octave":-8,"normalize":"legacy"})", np, err) && parseNoiseParameters(np, p, err) && p.normalize == NoiseNormalize::Legacy && p.octaveCount == 1);
}

// =================================================================================================================
// NormalNoise from the game's JSON files                         [REAL: real-classes-groundtruth.txt, sections 3/4]
// Per noise and world seed: Noise.get at P1 = (0,0,0), P2 = (100.5,0,-250.25), P3 = (-1234.567,0,8910.11),
// P4 = (37,-20,-3), as raw float bits. Noise = NormalNoise.create(root.fromHashOf("minecraft:<name>")).
struct NoiseVec {
    const char* name;
    std::uint32_t bits[4];
};
struct OctaveVec {
    int octave;
    std::uint64_t frequency, amplitude;  // doubles
    std::uint32_t floatAmplitude;        // (float)(normalizationFactor * amplitude)
};
struct DerivedVec {
    const char* name;
    std::uint64_t normalizationFactor;
    std::vector<OctaveVec> octaves;
};

const NoiseVec kNoise_12345[] = {
    {"aquifer_barrier", {0x3c4779a0u, 0xbd351164u, 0xbd586268u, 0xbea77703u}},
    {"aquifer_fluid_level_floodedness", {0x3d2d76f0u, 0xbe9aaa7bu, 0x3dd2b06cu, 0x3c9230e8u}},
    {"aquifer_fluid_level_spread", {0xbc31c4eau, 0xbc9ed408u, 0xbe04e908u, 0xbe5e4a17u}},
    {"aquifer_lava", {0x3efefc5fu, 0x3da7df5bu, 0xbe0e2648u, 0x3d812ad0u}},
    {"badlands_pillar", {0xbe14823bu, 0xbe0ad003u, 0x3d53f56eu, 0xbe855b73u}},
    {"badlands_pillar_roof", {0xbee05626u, 0x3de15be6u, 0x3e86c1e7u, 0xbefde410u}},
    {"badlands_surface", {0x3c4d37bcu, 0xbeb4c7c8u, 0x3cc873cau, 0x3c86e349u}},
    {"calcite", {0xbebfd6e5u, 0x3eb7b388u, 0x3e117857u, 0xbea9530cu}},
    {"cave_cheese", {0x3f1cfe2du, 0xbe2c914cu, 0x3ec5e578u, 0x3ea6fc3bu}},
    {"cave_entrance", {0x3cf44bdeu, 0xbe0d8d84u, 0xbe2dd714u, 0x3c80e424u}},
    {"cave_layer", {0xbf35b4afu, 0xbe599fc6u, 0xbc98eeccu, 0xbf5be5e6u}},
    {"clay_bands_offset", {0xbdd0fed4u, 0xbe5bcc36u, 0xbd9ebb8cu, 0x3caa9e64u}},
    {"continentalness", {0xbece23a6u, 0x3ebaaefbu, 0x3d6037f8u, 0xbdcb9af0u}},
    {"continentalness_large", {0xbf057508u, 0x3d9bd5bbu, 0xbf0f50c3u, 0xbef9d1c9u}},
    {"erosion", {0xbf3c0cafu, 0xbde3065du, 0x3ecbc3c2u, 0xbf25c266u}},
    {"erosion_large", {0x3d84d062u, 0xbdae0f50u, 0xbe1ee88eu, 0x3c68d166u}},
    {"gravel", {0xbead62d3u, 0x3c9093b3u, 0xbee7058eu, 0xbd47e8beu}},
    {"gravel_layer", {0x3ef25b04u, 0x3e1a60b1u, 0xbe687efbu, 0x3dc89c08u}},
    {"ice", {0xbeadc54eu, 0x3e651c3fu, 0xbe6ecb6au, 0x3eefa415u}},
    {"iceberg_pillar", {0xbe432835u, 0xbe12b880u, 0x3ef4aac7u, 0xbcb6577cu}},
    {"iceberg_pillar_roof", {0x3e3b5844u, 0xbe6033c3u, 0x3f1c7bfbu, 0xbd963aa6u}},
    {"iceberg_surface", {0x3eb86644u, 0x3c95c304u, 0xbe325930u, 0x3e6cb173u}},
    {"jagged", {0x3eebf5c5u, 0x3ee90a7eu, 0x3f18cfa4u, 0x3ee9c660u}},
    {"nether_state_selector", {0xbd7a7d1cu, 0x3df0833au, 0x3b9ab3c0u, 0x3e2d8d1eu}},
    {"nether_wart", {0x3f3aad4du, 0x3f033975u, 0xbe8f9646u, 0x3e8686a7u}},
    {"netherrack", {0x3cf5a921u, 0x3ece98c4u, 0x3cb0a966u, 0x3ebb0039u}},
    {"noodle", {0x3f22323fu, 0xbf30eb3bu, 0xbe9775eau, 0x3f056f13u}},
    {"noodle_ridge_a", {0xbef83300u, 0xbe8aeaafu, 0x3d36f798u, 0xbe636e30u}},
    {"noodle_ridge_b", {0x3d5fd61cu, 0xbe8446b6u, 0xbe6d6d1eu, 0x3e4e67c3u}},
    {"noodle_thickness", {0x3e851f7eu, 0xbeb1bbb8u, 0x3e50cf18u, 0x3e834652u}},
    {"offset", {0xbf0982fdu, 0x3e4c7738u, 0xbefa653du, 0xbede449bu}},
    {"ore_gap", {0x3ef2c106u, 0xbf113fafu, 0xbdc8f2a9u, 0x3f060618u}},
    {"ore_vein_a", {0xbd8d5c72u, 0xbe621a58u, 0x3e81c1c0u, 0xbe705787u}},
    {"ore_vein_b", {0x3e1e11b7u, 0x3e82be1au, 0x3ef8cebbu, 0xbdabcba4u}},
    {"ore_veininess", {0xbeebde00u, 0x3ee7b3bau, 0xbe039554u, 0xbf1c77a8u}},
    {"packed_ice", {0x3e0d9898u, 0xbeb94bebu, 0x3de948fdu, 0xbe6bfc1cu}},
    {"patch", {0x3ea46e40u, 0xbe2218f7u, 0x3d22366cu, 0xbd4a123bu}},
    {"pillar", {0x3e490be1u, 0xbeeb836au, 0xbe666576u, 0xbe0ba849u}},
    {"pillar_rareness", {0x3e22d23bu, 0x3e02ead9u, 0x3d7f9ab6u, 0x3e8765c8u}},
    {"pillar_thickness", {0x3e98fc05u, 0x3eda7beau, 0xbef5463eu, 0x3eb7ecc7u}},
    {"powder_snow", {0xbc6253e2u, 0x3f3e37d9u, 0x3da833d0u, 0xbdfb3daeu}},
    {"ridge", {0x3e33a71au, 0x3d573586u, 0x3d1ca3d8u, 0x3d9fa654u}},
    {"small_patch", {0x3e5a2018u, 0xbfc48b40u, 0xbdaf3e30u, 0x3f09a692u}},
    {"soul_sand_layer", {0x3ed307b4u, 0x3eec3d7bu, 0xbe95d3e1u, 0x3e367237u}},
    {"spaghetti_2d", {0xbf24b8f5u, 0x3e8b0dafu, 0x3e771f8cu, 0xbec17d7cu}},
    {"spaghetti_2d_elevation", {0x3e97eed0u, 0x3f02ba34u, 0xbc493ec0u, 0x3ef04eb4u}},
    {"spaghetti_2d_modulator", {0x3e1b356fu, 0x3e377c89u, 0xbd04e356u, 0x3e186203u}},
    {"spaghetti_2d_thickness", {0xbee38920u, 0xbe91e345u, 0x3d97a4dcu, 0xbedbeaffu}},
    {"spaghetti_3d_1", {0x3b6939b7u, 0x3ecf1a7bu, 0xbe007c2cu, 0xbe868fb1u}},
    {"spaghetti_3d_2", {0x3bf2e390u, 0xbd834d44u, 0x3e915344u, 0xbdb9c9bau}},
    {"spaghetti_3d_rarity", {0xbdd3f3b1u, 0xbe3678c0u, 0x3de1f1ffu, 0xbdf00a7au}},
    {"spaghetti_3d_thickness", {0xbd630c56u, 0xbeab0a50u, 0xbdbf3c65u, 0x3dac2ab5u}},
    {"spaghetti_roughness", {0xbeefe38au, 0xbe783b1eu, 0xbe2f08f8u, 0xbd7e8e00u}},
    {"spaghetti_roughness_modulator", {0x3d93637cu, 0xbf7a91abu, 0x3f20f874u, 0x3d564906u}},
    {"sulfur_cave_gradient", {0xbd9464ecu, 0x3c88c165u, 0xbf1505d0u, 0x3da349acu}},
    {"surface", {0x3df36c66u, 0x3f08268fu, 0xbec2a7efu, 0x3ed0497cu}},
    {"surface_secondary", {0xbe00b95eu, 0x3d835c67u, 0x3c44cf42u, 0x3e6aaac0u}},
    {"surface_swamp", {0x3e8c6230u, 0xbc762e20u, 0xbe835612u, 0x3e127812u}},
    {"temperature", {0xbcca9ff2u, 0xbe957dfcu, 0x3f44325bu, 0xbdb982c1u}},
    {"temperature_large", {0xbf56290du, 0xbf7cfdbeu, 0xbe93af08u, 0xbf5b268cu}},
    {"vegetation", {0x3f2e24afu, 0xbe858b35u, 0xbeb4002au, 0x3f156e3cu}},
    {"vegetation_large", {0x3e320cb2u, 0x3e19cd89u, 0xbe89125bu, 0x3db51c06u}},
};
const NoiseVec kNoise_0[] = {
    {"aquifer_barrier", {0xbf0a3eedu, 0x3f21921eu, 0xbea9da7cu, 0x3ce711ccu}},
    {"aquifer_fluid_level_floodedness", {0xbef12151u, 0xbf416287u, 0x3f05b547u, 0xbf4ec358u}},
    {"aquifer_fluid_level_spread", {0xbd6a934eu, 0x3dda6aa0u, 0x3e2336dcu, 0xbe9ef0f3u}},
    {"aquifer_lava", {0xbe282d7au, 0x3e1bcc14u, 0x3eb08e4cu, 0xbe6bbfd4u}},
    {"badlands_pillar", {0x3ef414aeu, 0x3e8a6aa0u, 0xbf65447du, 0xbc2acd34u}},
    {"badlands_pillar_roof", {0xbdc4b270u, 0xbf1bff6au, 0xbeee6416u, 0xbe2b45fbu}},
    {"badlands_surface", {0xbdd18287u, 0xbe3ed290u, 0xbe999526u, 0x3d427984u}},
    {"calcite", {0xbe776e32u, 0xbebb1d81u, 0xbef4db86u, 0xbe05f4cfu}},
    {"cave_cheese", {0xbd0efc69u, 0x3e7f660bu, 0xbe501347u, 0xbdde009fu}},
    {"cave_entrance", {0x3d1be31cu, 0xbe480718u, 0x3e0eea58u, 0x3e4d6c35u}},
    {"cave_layer", {0xbee2e2e5u, 0x3e3feaceu, 0xbdf5afa0u, 0xbec3a09fu}},
    {"clay_bands_offset", {0x3dd5a344u, 0x3ea6e85eu, 0x3c52e891u, 0xbc16c9b0u}},
    {"continentalness", {0xbcddb989u, 0x3dc8d58au, 0xbdad5abcu, 0xbb44af7au}},
    {"continentalness_large", {0xbf20792du, 0xbe6eb313u, 0xbe359524u, 0xbf191033u}},
    {"erosion", {0xbdd7da5eu, 0x3e8c0624u, 0xbe98091bu, 0xbd9084b1u}},
    {"erosion_large", {0x3c54582du, 0xbca22f29u, 0xbdb51e1au, 0x3d6968e5u}},
    {"gravel", {0xbe19c182u, 0x3db6cc2cu, 0xbe4adb55u, 0xbeeda5d5u}},
    {"gravel_layer", {0x3e310b1eu, 0xbef1ae67u, 0xbd62ce8du, 0x3edef6dcu}},
    {"ice", {0xbd85e710u, 0x3ebd6c05u, 0x3de37236u, 0x3db3308bu}},
    {"iceberg_pillar", {0x3ef3c3fau, 0xbf1848c2u, 0x3e8781e4u, 0xbe313567u}},
    {"iceberg_pillar_roof", {0xbd23c5b8u, 0xbed2e2d3u, 0xbe447546u, 0x3e5b0cd7u}},
    {"iceberg_surface", {0x3eea6e94u, 0xbf433227u, 0x3ea42a39u, 0xbe49184eu}},
    {"jagged", {0xbe477a99u, 0xbe51dde1u, 0xbe967bfau, 0xbe490232u}},
    {"nether_state_selector", {0xbedab651u, 0x3d3f3c58u, 0x3e280918u, 0x3dd55e56u}},
    {"nether_wart", {0xbdfdd1e2u, 0x3daa4700u, 0xbd5ff764u, 0xbf4c97f1u}},
    {"netherrack", {0xbe30b68cu, 0x3df6f902u, 0xbd34db6eu, 0xbe24d852u}},
    {"noodle", {0xbee9d666u, 0x3d022c10u, 0x3ee64912u, 0xbf029c76u}},
    {"noodle_ridge_a", {0xbe6e7de4u, 0xbe4b36cau, 0x3e6eab04u, 0xbdce81ccu}},
    {"noodle_ridge_b", {0xbe86a3f9u, 0xbe40c59cu, 0x3e8a1cb1u, 0xbe1d22b6u}},
    {"noodle_thickness", {0xbf0606c6u, 0xbe166fa0u, 0xbcd05f8au, 0xbf35c83au}},
    {"offset", {0xbf0b2876u, 0x3d688edeu, 0x3e25f046u, 0x3e095ad4u}},
    {"ore_gap", {0x3ee9cda0u, 0x3e415635u, 0xbe86cf52u, 0xbda23d38u}},
    {"ore_vein_a", {0xbf26d1a4u, 0xbd00296au, 0x3e719e21u, 0xbedb4f0cu}},
    {"ore_vein_b", {0xbec5e9c1u, 0x3ef038b7u, 0x3e5ea651u, 0xbe678703u}},
    {"ore_veininess", {0xbd880f08u, 0x3eb201fcu, 0x3f1c4764u, 0xbdae71ccu}},
    {"packed_ice", {0xbe2002bdu, 0x3e6345ccu, 0x3d5c0ee0u, 0x3e33357cu}},
    {"patch", {0x3d8b00a1u, 0xbe6494a6u, 0xbe1494ddu, 0xbe1a9df0u}},
    {"pillar", {0xbeb6140cu, 0x3e1ec0d8u, 0xbee4ea14u, 0xbc97c8d4u}},
    {"pillar_rareness", {0xbef1e35au, 0xbea8150eu, 0xbf3158eau, 0xbeca7ef9u}},
    {"pillar_thickness", {0xbe429265u, 0xbebd7b14u, 0xbf0c2b7fu, 0xbd921df0u}},
    {"powder_snow", {0xbda08cd4u, 0xbe19a15au, 0x3ed87d00u, 0xbd848486u}},
    {"ridge", {0x3cbdfd0fu, 0x3e1e28b8u, 0x3ddc0c76u, 0x3e6d5a58u}},
    {"small_patch", {0xbf15dd58u, 0x40097ccfu, 0x3f1d30adu, 0x3db146e8u}},
    {"soul_sand_layer", {0xbebebf2bu, 0x3cc44f43u, 0xbe78e6f1u, 0xbeb26a6fu}},
    {"spaghetti_2d", {0x3f2cdbbfu, 0x3e520e36u, 0xbcd727d8u, 0x3eb60b64u}},
    {"spaghetti_2d_elevation", {0xbdb01e50u, 0xbea2b3c2u, 0x3dc5e0a8u, 0xbe5e14d5u}},
    {"spaghetti_2d_modulator", {0x3e1eea7cu, 0x3e8b8d3fu, 0x3e5344c0u, 0x3e1a37d8u}},
    {"spaghetti_2d_thickness", {0xbf07545au, 0xbec079bau, 0x3e8a1330u, 0xbf0a78feu}},
    {"spaghetti_3d_1", {0xbe2b93b2u, 0xbeabbfa8u, 0xbdddcb98u, 0xbee3a481u}},
    {"spaghetti_3d_2", {0x3f0fd730u, 0x3e222146u, 0xbd8d0b3au, 0x3eaa15bcu}},
    {"spaghetti_3d_rarity", {0xbe01ad33u, 0xbc88a604u, 0x3de059c0u, 0xbde2fe26u}},
    {"spaghetti_3d_thickness", {0x3f25db38u, 0xbe223fabu, 0x3efe3f96u, 0x3f17bfd9u}},
    {"spaghetti_roughness", {0x3dbb3d00u, 0x3e30baceu, 0x3e40e7e0u, 0xbeba6e5cu}},
    {"spaghetti_roughness_modulator", {0x3e05e0acu, 0x3e376c4du, 0xbd1788d3u, 0xbd7959e0u}},
    {"sulfur_cave_gradient", {0x3e44e6cfu, 0x3c167588u, 0xbe790426u, 0x3bc38b70u}},
    {"surface", {0x3f49c66du, 0xbede5deau, 0xbf2ce84bu, 0xbe69ca82u}},
    {"surface_secondary", {0xbdb9b76cu, 0x3e11889fu, 0x3e9ab2afu, 0xbe84a4acu}},
    {"surface_swamp", {0xbf02c662u, 0x3ead5e70u, 0x3dfd9ff2u, 0x3f25f6d6u}},
    {"temperature", {0x3dfa8994u, 0x3e8b3f11u, 0x3cf7d8aeu, 0x3e299c66u}},
    {"temperature_large", {0x3e78b31bu, 0x3e92de84u, 0x3ea4f460u, 0x3e70ac21u}},
    {"vegetation", {0xbc132888u, 0xbe8e3c4au, 0xbea5d004u, 0xbd40ebacu}},
    {"vegetation_large", {0xbe2c1e79u, 0xbc81c2c6u, 0x3ef55b22u, 0xbe1e9589u}},
};
const NoiseVec kNoise_m1[] = {
    {"aquifer_barrier", {0xbd6bf0d8u, 0xbd8a9bc5u, 0x3e838171u, 0x3db5acd6u}},
    {"aquifer_fluid_level_floodedness", {0x3d808633u, 0x3e2f6b79u, 0xbd5b1f80u, 0xbe62d08bu}},
    {"aquifer_fluid_level_spread", {0x3ee80808u, 0x3ddf0058u, 0x3e615473u, 0xbec0191cu}},
    {"aquifer_lava", {0xbd5edbb1u, 0x3e21b902u, 0xbe618a51u, 0x3e8557f4u}},
    {"badlands_pillar", {0xbc4bffdcu, 0x3d141363u, 0x3f091162u, 0x3e4b67aau}},
    {"badlands_pillar_roof", {0x3e4b384eu, 0xbe7fd6c3u, 0x3d8b692bu, 0x3e05a712u}},
    {"badlands_surface", {0xbf0ebb44u, 0xbef307f7u, 0x3e48bd5au, 0xbd9198f8u}},
    {"calcite", {0xbc1805c2u, 0x3e2b5504u, 0x3ed3034fu, 0x3ceb80b9u}},
    {"cave_cheese", {0x3ed15108u, 0x3e98404cu, 0xbdbf772eu, 0x3d882768u}},
    {"cave_entrance", {0x3d7aeeecu, 0x3e80e68du, 0xbd8795c2u, 0x3d6e3372u}},
    {"cave_layer", {0xbc5128f0u, 0x3e9708f6u, 0x3e0257deu, 0xbd4a95deu}},
    {"clay_bands_offset", {0xbec836e4u, 0x3d2e238au, 0x3e3d47a5u, 0xbe7cb779u}},
    {"continentalness", {0xbf1c9ff9u, 0xbf2f7eceu, 0xbcde65b9u, 0xbf3340f7u}},
    {"continentalness_large", {0x3e243987u, 0x3e354912u, 0x3e2d73c5u, 0x3e1e9bf9u}},
    {"erosion", {0x3e81135au, 0x3ec693cdu, 0xbdc91c16u, 0x3e76fab3u}},
    {"erosion_large", {0x3e16a1dfu, 0x3e1fabd8u, 0x3e36928au, 0x3e5d9cecu}},
    {"gravel", {0x3e1d0dfcu, 0x3e3ee36eu, 0x3e995f1cu, 0x3db872a6u}},
    {"gravel_layer", {0xbda90f88u, 0xbf347ad2u, 0xbc30ae04u, 0x3d97867au}},
    {"ice", {0x3e8bae94u, 0x3ebd3c7du, 0xbe98a5f7u, 0x3ec3f5a5u}},
    {"iceberg_pillar", {0xbdf6db66u, 0xbe9d3516u, 0xbea5766au, 0xbeb3a3d6u}},
    {"iceberg_pillar_roof", {0xbd9a2004u, 0x3e662604u, 0xbe5585f7u, 0xbe5cfc5fu}},
    {"iceberg_surface", {0x3ddbc0b8u, 0x3f0ebae2u, 0x3eae9472u, 0x3de4f35bu}},
    {"jagged", {0x3ed74ed1u, 0x3edd90eeu, 0x3e951ba0u, 0x3ed6e547u}},
    {"nether_state_selector", {0x3dad9a99u, 0x3f362158u, 0xbec9f227u, 0x3f00ac99u}},
    {"nether_wart", {0xbee7c8dbu, 0x3ea7489du, 0x3d9a1e7cu, 0xbe3348cdu}},
    {"netherrack", {0x3edf2aedu, 0xbe0ed381u, 0xbe27ffa2u, 0x3ed17ca7u}},
    {"noodle", {0x3ec39b05u, 0x3e5c3f17u, 0xbe212a38u, 0x3e8b1786u}},
    {"noodle_ridge_a", {0x3ebcb46au, 0x3efa6d25u, 0xbe8e4dcbu, 0xbd2de127u}},
    {"noodle_ridge_b", {0x3ee4ea34u, 0xbe9d0803u, 0x3e7420a2u, 0x3ea10306u}},
    {"noodle_thickness", {0x3d636100u, 0xbd5f16b1u, 0xbe01cdf2u, 0x3b1c9500u}},
    {"offset", {0x3e96c284u, 0x3e9533acu, 0xbe4d99a2u, 0x3d1c9beau}},
    {"ore_gap", {0x3d42a218u, 0x3e2d1e36u, 0x3e96c566u, 0x3d7fcf54u}},
    {"ore_vein_a", {0x3cfdd968u, 0xbf5157ffu, 0xbe129539u, 0x3d94bed4u}},
    {"ore_vein_b", {0xbe24462du, 0xbe910bd8u, 0xbe7f1a7eu, 0xbde83438u}},
    {"ore_veininess", {0xbe58b4a6u, 0xbed85618u, 0x3e1ad61cu, 0x3d1f2479u}},
    {"packed_ice", {0xbdf57ba7u, 0x3dceba6bu, 0xbe8a921fu, 0x3d596dffu}},
    {"patch", {0xbe93aff7u, 0xbddc77f5u, 0xbee281d4u, 0xba208ae8u}},
    {"pillar", {0xbe4d08d7u, 0x3e6f3174u, 0x3dd33742u, 0xbe3d1af6u}},
    {"pillar_rareness", {0x3e826adbu, 0x3e9c233cu, 0xbe83f222u, 0x3e623bc1u}},
    {"pillar_thickness", {0x3eb95b1bu, 0xbee7150du, 0xbe34d8f7u, 0x3e8c37afu}},
    {"powder_snow", {0x3e0d1908u, 0xbe4a259cu, 0x3d2194f1u, 0xbf02d37eu}},
    {"ridge", {0xbd8aa91eu, 0x3e898de5u, 0xbec82f90u, 0xbea43ab6u}},
    {"small_patch", {0x3e8e358cu, 0x3f8644e0u, 0x3e18df26u, 0xbf16fcc6u}},
    {"soul_sand_layer", {0x3eeb2d5du, 0xbf014a83u, 0x3ecfba9eu, 0x3dd3e3ecu}},
    {"spaghetti_2d", {0xbe0e91b6u, 0x3e68d558u, 0xbdc6e348u, 0xbe1cd2eeu}},
    {"spaghetti_2d_elevation", {0xbdc7165eu, 0xbe45a40cu, 0x3ed33a1fu, 0x3e07f8b4u}},
    {"spaghetti_2d_modulator", {0x3d496447u, 0x3df55bdbu, 0xbf756546u, 0x3d9683d9u}},
    {"spaghetti_2d_thickness", {0x3e70a210u, 0x3ea523cau, 0x3e2f4131u, 0x3e64d22du}},
    {"spaghetti_3d_1", {0xbebcea11u, 0x3ec8e4d8u, 0x3dd9580eu, 0x3b59ea40u}},
    {"spaghetti_3d_2", {0xbdd436a3u, 0xbe6e0b42u, 0x3e610ee8u, 0x3c4833c0u}},
    {"spaghetti_3d_rarity", {0x3c513654u, 0x3d8d0ff9u, 0xbed509c4u, 0x3c20eab0u}},
    {"spaghetti_3d_thickness", {0xbdfa6197u, 0xbd984b72u, 0x3c4d0960u, 0xbd7b8a00u}},
    {"spaghetti_roughness", {0x3ebd404eu, 0xbe84377bu, 0xbf785c70u, 0x3e737ea0u}},
    {"spaghetti_roughness_modulator", {0x3e18604eu, 0xbf6f5cccu, 0xbebdd73bu, 0x3d4bf85au}},
    {"sulfur_cave_gradient", {0x3c5f5c28u, 0xbd6cbe0du, 0xbc0292f8u, 0xbe7fbf58u}},
    {"surface", {0xbd6fe394u, 0xbf0ee425u, 0xbdcf76d0u, 0x3eb63238u}},
    {"surface_secondary", {0x3d240d90u, 0xbc282924u, 0x3dea6158u, 0x3e3bf0b6u}},
    {"surface_swamp", {0xbf109e5bu, 0xbe16369du, 0xbc290da0u, 0x3e3c247au}},
    {"temperature", {0x3e250e45u, 0x3d33630au, 0x3f09e480u, 0x3d48ccdeu}},
    {"temperature_large", {0x3ec52f1cu, 0x3ed0cbfcu, 0x3e656f5du, 0x3ece8975u}},
    {"vegetation", {0xbe105d07u, 0xbe4979d1u, 0x3ed5e115u, 0xbdbafe6bu}},
    {"vegetation_large", {0xbdd3232fu, 0xbe84a486u, 0x3e7b15cdu, 0xbe001ffau}},
};
const NoiseVec kNoise_8675309123456789[] = {
    {"aquifer_barrier", {0x3dc8647du, 0x3d5dfcb2u, 0x3e6ba5dau, 0x3e5a149eu}},
    {"aquifer_fluid_level_floodedness", {0x3dfc7a9au, 0xbddb931cu, 0xbf4e5842u, 0x3e1b719du}},
    {"aquifer_fluid_level_spread", {0xbe281c5fu, 0x3e23b01bu, 0x3d423a60u, 0x3e35b480u}},
    {"aquifer_lava", {0x3ee24543u, 0x3c8a1d08u, 0xbd5cbc4cu, 0x3e2fc0eeu}},
    {"badlands_pillar", {0x3e5106a8u, 0x3e7ec2b7u, 0xbe38624bu, 0xbe3055acu}},
    {"badlands_pillar_roof", {0x3dd28ebbu, 0xbd71a7c0u, 0x3dba9b2au, 0x3e3c039fu}},
    {"badlands_surface", {0xbe8dae7bu, 0xbed7c655u, 0x3d588160u, 0xbb86b006u}},
    {"calcite", {0xbdefefefu, 0xbdc422c1u, 0x3c191949u, 0xbd5e1eb4u}},
    {"cave_cheese", {0x3e2ee662u, 0x3afba57cu, 0xbeeaa24bu, 0x3f12ec36u}},
    {"cave_entrance", {0xbd71a156u, 0xbdfe9fe4u, 0xbe045adau, 0xbdc2e0e3u}},
    {"cave_layer", {0x3eb3a4d1u, 0xbb10a180u, 0xbf174ffcu, 0x3de03e01u}},
    {"clay_bands_offset", {0xbee71e50u, 0x3f0038fau, 0x3dfff9a2u, 0xbe4d9e44u}},
    {"continentalness", {0x3c920081u, 0xbe1200bau, 0xbe5da6e3u, 0x3e9eebf1u}},
    {"continentalness_large", {0xbf1d4cafu, 0xbe6ced63u, 0x3d5e3560u, 0xbf14805bu}},
    {"erosion", {0xbcb74470u, 0x3d8bf0b8u, 0x3ebd2040u, 0xbd94f28fu}},
    {"erosion_large", {0xbdd4ec44u, 0xbcd825a2u, 0x3ee82212u, 0xbdb307e5u}},
    {"gravel", {0x3d5a51e9u, 0x3ee9e1bfu, 0x3dd83798u, 0x3e9c5e8eu}},
    {"gravel_layer", {0x3f0598c8u, 0xbec54558u, 0x3d590d77u, 0x3e4be218u}},
    {"ice", {0xbe92928eu, 0x3ed8b68bu, 0x3edbaa93u, 0x3dbc3e5bu}},
    {"iceberg_pillar", {0x3d18af0au, 0xbe5b369du, 0x3e4fc7d2u, 0x3dfbbb84u}},
    {"iceberg_pillar_roof", {0xbdeeb9c0u, 0x3ec1126cu, 0xbe0a26f0u, 0xbed6a6c2u}},
    {"iceberg_surface", {0xbe3d49c8u, 0xbe7987d6u, 0xbe22b022u, 0xbd2e7659u}},
    {"jagged", {0xbe73065du, 0xbe74faebu, 0xbe1771f3u, 0xbe735755u}},
    {"nether_state_selector", {0x3e462272u, 0x3ed50ea4u, 0xbe63a5eau, 0xbe6443e7u}},
    {"nether_wart", {0xbe97fe6du, 0xbf079170u, 0xbe3ed640u, 0x3ec08ef0u}},
    {"netherrack", {0x3ec4df88u, 0x3ea974e1u, 0x3ed21d1bu, 0x3e690048u}},
    {"noodle", {0xbe062a8bu, 0xbe8685deu, 0xbf020b9du, 0xbe3bc510u}},
    {"noodle_ridge_a", {0xbf032c66u, 0x3e95b2b0u, 0x3e9bd1d4u, 0xbf11dff4u}},
    {"noodle_ridge_b", {0x3de7d190u, 0xbe6e710eu, 0x3ee019a5u, 0x3de42cb0u}},
    {"noodle_thickness", {0x3ea6117cu, 0x3ebb2762u, 0xbe23733cu, 0x3e8f6f80u}},
    {"offset", {0x3e872febu, 0xbe366284u, 0x3e46ba84u, 0xbd56fc50u}},
    {"ore_gap", {0xbf2a2152u, 0xbf61072eu, 0x3ddb87eau, 0x3dac3da5u}},
    {"ore_vein_a", {0x3ebbe91cu, 0x3d9c44f6u, 0x3e882bdfu, 0xbdf1c87fu}},
    {"ore_vein_b", {0x3e284f67u, 0xbeab505bu, 0xbdf9a9e6u, 0x3e581ba4u}},
    {"ore_veininess", {0x3e7984b6u, 0xbe5402c6u, 0xbe14a5adu, 0x3df1b3d4u}},
    {"packed_ice", {0xbd62ee6bu, 0x3d83f666u, 0x3e0f17a6u, 0x3e7b58b6u}},
    {"patch", {0x3e8c2090u, 0xbe972d55u, 0x3eddaa2au, 0x3ea47bc4u}},
    {"pillar", {0xbf02b461u, 0xbeb476a8u, 0x3e216a87u, 0xbeb09899u}},
    {"pillar_rareness", {0xbe0cf3fcu, 0xbe33dc34u, 0xbe6b9fdeu, 0xbcf23410u}},
    {"pillar_thickness", {0xbe9848ccu, 0xbd660ad4u, 0x3e0e122cu, 0xbe4da212u}},
    {"powder_snow", {0xbdfd5238u, 0xbe1a0dfbu, 0x3e044053u, 0xbd1e00aau}},
    {"ridge", {0x3f3c8a4eu, 0x3dcdd5e4u, 0x3cfcfe9eu, 0x3ea924fdu}},
    {"small_patch", {0xbef30f7cu, 0xbf9fec9bu, 0x3f1fbcfeu, 0xbf85dfd6u}},
    {"soul_sand_layer", {0x3e2b238du, 0xbe30c8abu, 0xbd74aea4u, 0x3e986c49u}},
    {"spaghetti_2d", {0x3e9a77dbu, 0x3e3fe584u, 0xbd82ec4du, 0x3d71a4c8u}},
    {"spaghetti_2d_elevation", {0xbd8a0944u, 0x3e700dd3u, 0x3e310568u, 0xbda34082u}},
    {"spaghetti_2d_modulator", {0x3ed7eaa8u, 0x3f034dc0u, 0xbf25b756u, 0x3edf361fu}},
    {"spaghetti_2d_thickness", {0x3f142bbeu, 0x3f170402u, 0x3de3d4cfu, 0x3f162450u}},
    {"spaghetti_3d_1", {0x3f07007au, 0xbd1e7260u, 0xbe92c00eu, 0x3ea385d2u}},
    {"spaghetti_3d_2", {0xbe2b0755u, 0xbef3730au, 0xbec2fe86u, 0xbec92e6bu}},
    {"spaghetti_3d_rarity", {0xbd0dd61cu, 0xbd774aa8u, 0x3e13e36du, 0xbcbee2c8u}},
    {"spaghetti_3d_thickness", {0x3dc74f85u, 0x3e3e62a1u, 0xbe8de4f7u, 0x3df24f91u}},
    {"spaghetti_roughness", {0x3d970bd0u, 0xbf075aebu, 0x3e2565c8u, 0xbec23de8u}},
    {"spaghetti_roughness_modulator", {0x3d887eb6u, 0xbdce9d0cu, 0xbf5a0da9u, 0xbe8788fcu}},
    {"sulfur_cave_gradient", {0x3f09c448u, 0xbd1985b1u, 0x3f3725d7u, 0xbd1f40b5u}},
    {"surface", {0x3cb9772eu, 0x3d40ed5fu, 0xbe8b185fu, 0xbdd13f67u}},
    {"surface_secondary", {0xbdcd7657u, 0xbd394dcbu, 0x3ec8bdf5u, 0xbd898fb1u}},
    {"surface_swamp", {0xbe4788cfu, 0x3e7dc1ddu, 0xbee38b5du, 0x3e9d9d62u}},
    {"temperature", {0x3e4f3c55u, 0x3f0cb641u, 0x3e7f392eu, 0x3e7bb46eu}},
    {"temperature_large", {0x3e3e9bbeu, 0x3e1ebee1u, 0xbed66943u, 0x3e4d0b0fu}},
    {"vegetation", {0x3e4f7357u, 0x3e0955d6u, 0xbe3c1617u, 0x3dd9ec24u}},
    {"vegetation_large", {0x3dbe6ccau, 0xbe9c8178u, 0xbed73e57u, 0x3dc5f2e8u}},
};

// Derived parameters of every noise file [REAL: noise-derived-table.txt]: normalizationFactor, per octave
// (index, frequency, amplitude, float amplitude).
const DerivedVec kDerived[] = {
    {"aquifer_barrier", 0x3febe96eb0ed53aeull, {
        {-3, 0x3fc0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"aquifer_fluid_level_floodedness", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"aquifer_fluid_level_spread", 0x3febe96eb0ed53aeull, {
        {-5, 0x3fa0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"aquifer_lava", 0x3febe96eb0ed53aeull, {
        {-1, 0x3fe0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"badlands_pillar", 0x3ff6b4bb713aca51ull, {
        {-2, 0x3fd0000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-1, 0x3fe0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {0, 0x3ff0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {1, 0x4000000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"badlands_pillar_roof", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"badlands_surface", 0x3ff5516a04816e8dull, {
        {-6, 0x3f90000000000000ull, 0x3fe127b919be8d44ull, 0x3f36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fd127b919be8d44ull, 0x3eb6db6eu},
        {-4, 0x3fb0000000000000ull, 0x3fc127b919be8d44ull, 0x3e36db6eu},
    }},
    {"calcite", 0x3ff6b4bb713aca51ull, {
        {-9, 0x3f60000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-8, 0x3f70000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-7, 0x3f80000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-6, 0x3f90000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"cave_cheese", 0x3ffc596be1d802b7ull, {
        {-8, 0x3f70000000000000ull, 0x3fcacefb4d73f6fbull, 0x3ebe0030u},
        {-7, 0x3f80000000000000ull, 0x3fcacefb4d73f6fbull, 0x3ebe0030u},
        {-6, 0x3f90000000000000ull, 0x3fcacefb4d73f6fbull, 0x3ebe0030u},
        {-5, 0x3fa0000000000000ull, 0x3faacefb4d73f6fbull, 0x3dbe0030u},
        {-4, 0x3fb0000000000000ull, 0x3faacefb4d73f6fbull, 0x3dbe0030u},
        {-3, 0x3fc0000000000000ull, 0x3f8acefb4d73f6fbull, 0x3cbe0030u},
        {-1, 0x3fe0000000000000ull, 0x3f7acefb4d73f6fbull, 0x3c3e0030u},
    }},
    {"cave_entrance", 0x3ff787145c3ad350ull, {
        {-7, 0x3f80000000000000ull, 0x3fc8ded46e1ddd1bull, 0x3e924925u},
        {-6, 0x3f90000000000000ull, 0x3fbf168989a55461ull, 0x3e36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fbf168989a55461ull, 0x3e36db6eu},
    }},
    {"cave_layer", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"clay_bands_offset", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"continentalness", 0x3ffb064520309512ull, {
        {-9, 0x3f60000000000000ull, 0x3fdc796a5aced1d1ull, 0x3f406030u},
        {-8, 0x3f70000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-7, 0x3f80000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-6, 0x3f90000000000000ull, 0x3fbc796a5aced1d1ull, 0x3e406030u},
        {-5, 0x3fa0000000000000ull, 0x3fac796a5aced1d1ull, 0x3dc06030u},
        {-4, 0x3fb0000000000000ull, 0x3f8c796a5aced1d1ull, 0x3cc06030u},
        {-3, 0x3fc0000000000000ull, 0x3f7c796a5aced1d1ull, 0x3c406030u},
        {-2, 0x3fd0000000000000ull, 0x3f6c796a5aced1d1ull, 0x3bc06030u},
        {-1, 0x3fe0000000000000ull, 0x3f5c796a5aced1d1ull, 0x3b406030u},
    }},
    {"continentalness_large", 0x3ffb064520309512ull, {
        {-11, 0x3f40000000000000ull, 0x3fdc796a5aced1d1ull, 0x3f406030u},
        {-10, 0x3f50000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-9, 0x3f60000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-8, 0x3f70000000000000ull, 0x3fbc796a5aced1d1ull, 0x3e406030u},
        {-7, 0x3f80000000000000ull, 0x3fac796a5aced1d1ull, 0x3dc06030u},
        {-6, 0x3f90000000000000ull, 0x3f8c796a5aced1d1ull, 0x3cc06030u},
        {-5, 0x3fa0000000000000ull, 0x3f7c796a5aced1d1ull, 0x3c406030u},
        {-4, 0x3fb0000000000000ull, 0x3f6c796a5aced1d1ull, 0x3bc06030u},
        {-3, 0x3fc0000000000000ull, 0x3f5c796a5aced1d1ull, 0x3b406030u},
    }},
    {"erosion", 0x3ff4e6d2bdd08b4eull, {
        {-9, 0x3f60000000000000ull, 0x3fe18f4343d3c75aull, 0x3f378336u},
        {-8, 0x3f70000000000000ull, 0x3fd18f4343d3c75aull, 0x3eb78336u},
        {-6, 0x3f90000000000000ull, 0x3fb18f4343d3c75aull, 0x3db78336u},
        {-5, 0x3fa0000000000000ull, 0x3fa18f4343d3c75aull, 0x3d378336u},
    }},
    {"erosion_large", 0x3ff4e6d2bdd08b4eull, {
        {-11, 0x3f40000000000000ull, 0x3fe18f4343d3c75aull, 0x3f378336u},
        {-10, 0x3f50000000000000ull, 0x3fd18f4343d3c75aull, 0x3eb78336u},
        {-8, 0x3f70000000000000ull, 0x3fb18f4343d3c75aull, 0x3db78336u},
        {-7, 0x3f80000000000000ull, 0x3fa18f4343d3c75aull, 0x3d378336u},
    }},
    {"gravel", 0x3ff6b4bb713aca51ull, {
        {-8, 0x3f70000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-7, 0x3f80000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-6, 0x3f90000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"gravel_layer", 0x3ff6b4e4c698bd40ull, {
        {-8, 0x3f70000000000000ull, 0x3fe0f1c87cef6c04ull, 0x3f406030u},
        {-7, 0x3f80000000000000ull, 0x3fd0f1c87cef6c04ull, 0x3ec06030u},
        {-6, 0x3f90000000000000ull, 0x3fc0f1c87cef6c04ull, 0x3e406030u},
        {-5, 0x3fa0000000000000ull, 0x3fb0f1c87cef6c04ull, 0x3dc06030u},
        {0, 0x3ff0000000000000ull, 0x3efceb26612b6304ull, 0x3824291fu},
    }},
    {"ice", 0x3ff6b4bb713aca51ull, {
        {-4, 0x3fb0000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-3, 0x3fc0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-2, 0x3fd0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-1, 0x3fe0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"iceberg_pillar", 0x3ff6b4bb713aca51ull, {
        {-6, 0x3f90000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-4, 0x3fb0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-3, 0x3fc0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"iceberg_pillar_roof", 0x3febe96eb0ed53aeull, {
        {-3, 0x3fc0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"iceberg_surface", 0x3ff5516a04816e8dull, {
        {-6, 0x3f90000000000000ull, 0x3fe127b919be8d44ull, 0x3f36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fd127b919be8d44ull, 0x3eb6db6eu},
        {-4, 0x3fb0000000000000ull, 0x3fc127b919be8d44ull, 0x3e36db6eu},
    }},
    {"jagged", 0x3ff82c081361c4cfull, {
        {-16, 0x3ef0000000000000ull, 0x3fe09cfc11a88280ull, 0x3f48c992u},
        {-15, 0x3f00000000000000ull, 0x3fd09cfc11a88280ull, 0x3ec8c992u},
        {-14, 0x3f10000000000000ull, 0x3fc09cfc11a88280ull, 0x3e48c992u},
        {-13, 0x3f20000000000000ull, 0x3fb09cfc11a88280ull, 0x3dc8c992u},
        {-12, 0x3f30000000000000ull, 0x3fa09cfc11a88280ull, 0x3d48c992u},
        {-11, 0x3f40000000000000ull, 0x3f909cfc11a88280ull, 0x3cc8c992u},
        {-10, 0x3f50000000000000ull, 0x3f809cfc11a88280ull, 0x3c48c992u},
        {-9, 0x3f60000000000000ull, 0x3f709cfc11a88280ull, 0x3bc8c992u},
        {-8, 0x3f70000000000000ull, 0x3f609cfc11a88280ull, 0x3b48c992u},
        {-7, 0x3f80000000000000ull, 0x3f509cfc11a88280ull, 0x3ac8c992u},
        {-6, 0x3f90000000000000ull, 0x3f409cfc11a88280ull, 0x3a48c992u},
        {-5, 0x3fa0000000000000ull, 0x3f309cfc11a88280ull, 0x39c8c992u},
        {-4, 0x3fb0000000000000ull, 0x3f209cfc11a88280ull, 0x3948c992u},
        {-3, 0x3fc0000000000000ull, 0x3f109cfc11a88280ull, 0x38c8c992u},
        {-2, 0x3fd0000000000000ull, 0x3f009cfc11a88280ull, 0x3848c992u},
        {-1, 0x3fe0000000000000ull, 0x3ef09cfc11a88280ull, 0x37c8c992u},
    }},
    {"nether_state_selector", 0x3febe96eb0ed53aeull, {
        {-4, 0x3fb0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"nether_wart", 0x3feedb7640935fb8ull, {
        {-3, 0x3fc0000000000000ull, 0x3fe79927313cbe2full, 0x3f360b61u},
        {0, 0x3ff0000000000000ull, 0x3fb53d09ac5044c4ull, 0x3da3d70au},
    }},
    {"netherrack", 0x3fed1aeaee8429aaull, {
        {-3, 0x3fc0000000000000ull, 0x3fe904d465f0c0eaull, 0x3f360b61u},
        {0, 0x3ff0000000000000ull, 0x3fa183617a8eed70ull, 0x3cfedcbbu},
    }},
    {"noodle", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"noodle_ridge_a", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"noodle_ridge_b", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"noodle_thickness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"offset", 0x3ff5516a04816e8dull, {
        {-3, 0x3fc0000000000000ull, 0x3fe002f106f61d72ull, 0x3f2aaaabu},
        {-2, 0x3fd0000000000000ull, 0x3fd002f106f61d72ull, 0x3eaaaaabu},
        {-1, 0x3fe0000000000000ull, 0x3fc002f106f61d72ull, 0x3e2aaaabu},
    }},
    {"ore_gap", 0x3febe96eb0ed53aeull, {
        {-5, 0x3fa0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"ore_vein_a", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"ore_vein_b", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"ore_veininess", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"packed_ice", 0x3ff6b4bb713aca51ull, {
        {-7, 0x3f80000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-6, 0x3f90000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-5, 0x3fa0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-4, 0x3fb0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"patch", 0x3febec68b63370e7ull, {
        {-5, 0x3fa0000000000000ull, 0x3fea9c1d1481e4dbull, 0x3f39c278u},
        {0, 0x3ff0000000000000ull, 0x3f36b500ebf3f67eull, 0x399e83d0u},
    }},
    {"pillar", 0x3ff2b94d40666c6aull, {
        {-7, 0x3f80000000000000ull, 0x3fe441639d5be97cull, 0x3f3da12fu},
        {-6, 0x3f90000000000000ull, 0x3fd441639d5be97cull, 0x3ebda12fu},
    }},
    {"pillar_rareness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"pillar_thickness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"powder_snow", 0x3ff6b4bb713aca51ull, {
        {-6, 0x3f90000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-4, 0x3fb0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-3, 0x3fc0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"ridge", 0x3ff5dd5edbee6c3dull, {
        {-7, 0x3f80000000000000ull, 0x3fddbc49ffb096e1ull, 0x3f228a29u},
        {-6, 0x3f90000000000000ull, 0x3fddbc49ffb096e1ull, 0x3f228a29u},
        {-5, 0x3fa0000000000000ull, 0x3fbdbc49ffb096e1ull, 0x3e228a29u},
    }},
    {"small_patch", 0x3febe96eb0ed53b0ull, {
        {-3, 0x3fc0000000000000ull, 0x4006ede8c7ae9c5eull, 0x40200000u},
    }},
    {"soul_sand_layer", 0x3ff6b4e4c698bd40ull, {
        {-8, 0x3f70000000000000ull, 0x3fe0f1c87cef6c04ull, 0x3f406030u},
        {-7, 0x3f80000000000000ull, 0x3fd0f1c87cef6c04ull, 0x3ec06030u},
        {-6, 0x3f90000000000000ull, 0x3fc0f1c87cef6c04ull, 0x3e406030u},
        {-5, 0x3fa0000000000000ull, 0x3fb0f1c87cef6c04ull, 0x3dc06030u},
        {0, 0x3ff0000000000000ull, 0x3efceb26612b6304ull, 0x3824291fu},
    }},
    {"spaghetti_2d", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_2d_elevation", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_2d_modulator", 0x3febe96eb0ed53aeull, {
        {-11, 0x3f40000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_2d_thickness", 0x3febe96eb0ed53aeull, {
        {-11, 0x3f40000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_1", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_2", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_rarity", 0x3febe96eb0ed53aeull, {
        {-11, 0x3f40000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_thickness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_roughness", 0x3febe96eb0ed53aeull, {
        {-5, 0x3fa0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_roughness_modulator", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"sulfur_cave_gradient", 0x3ff0ec8e05c3aa98ull, {
        {-5, 0x3fa0000000000000ull, 0x3fe59bf1b99f3397ull, 0x3f36db6eu},
        {-3, 0x3fc0000000000000ull, 0x3fc59bf1b99f3397ull, 0x3e36db6eu},
    }},
    {"surface", 0x3ff5516a04816e8dull, {
        {-6, 0x3f90000000000000ull, 0x3fe127b919be8d44ull, 0x3f36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fd127b919be8d44ull, 0x3eb6db6eu},
        {-4, 0x3fb0000000000000ull, 0x3fc127b919be8d44ull, 0x3e36db6eu},
    }},
    {"surface_secondary", 0x3ff428970e00bc6full, {
        {-6, 0x3f90000000000000ull, 0x3fe20faeb1c49dd5ull, 0x3f360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fd20faeb1c49dd5ull, 0x3eb60b61u},
        {-3, 0x3fc0000000000000ull, 0x3fb20faeb1c49dd5ull, 0x3db60b61u},
    }},
    {"surface_swamp", 0x3febe96eb0ed53aeull, {
        {-2, 0x3fd0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"temperature", 0x3ff00f74db28a272ull, {
        {-10, 0x3f50000000000000ull, 0x3fee5c93158b2016ull, 0x3f73cf3du},
        {-8, 0x3f70000000000000ull, 0x3fc43db763b21564ull, 0x3e228a29u},
    }},
    {"temperature_large", 0x3ff00f74db28a272ull, {
        {-12, 0x3f30000000000000ull, 0x3fee5c93158b2016ull, 0x3f73cf3du},
        {-10, 0x3f50000000000000ull, 0x3fc43db763b21564ull, 0x3e228a29u},
    }},
    {"vegetation", 0x3ff2b94d40666c6aull, {
        {-8, 0x3f70000000000000ull, 0x3fdedd8b9a73acebull, 0x3f107acfu},
        {-7, 0x3f80000000000000ull, 0x3fcedd8b9a73acebull, 0x3e907acfu},
    }},
    {"vegetation_large", 0x3ff2b94d40666c6aull, {
        {-10, 0x3f50000000000000ull, 0x3fdedd8b9a73acebull, 0x3f107acfu},
        {-9, 0x3f60000000000000ull, 0x3fcedd8b9a73acebull, 0x3e907acfu},
    }},
};

struct SeedNoise {
    std::int64_t seed;
    const NoiseVec* vecs;
    std::size_t count;
};

#define SEED_NOISE(seed, table) SeedNoise{seed, table, sizeof(table) / sizeof(table[0])}

void testNormalNoise(const std::filesystem::path& worldgen) {
    const struct {
        double x, y, z;
    } points[4] = {{0.0, 0.0, 0.0}, {100.5, 0.0, -250.25}, {-1234.567, 0.0, 8910.11}, {37.0, -20.0, -3.0}};
    const SeedNoise seeds[] = {SEED_NOISE(12345, kNoise_12345), SEED_NOISE(0, kNoise_0), SEED_NOISE(-1, kNoise_m1),
                               SEED_NOISE(8675309123456789ll, kNoise_8675309123456789)};
    for (const SeedNoise& sn : seeds) {
        const RandomState rs(sn.seed, worldgen);
        for (std::size_t i = 0; i < sn.count; ++i) {
            std::string err;
            const NormalNoise* n = rs.noise(sn.vecs[i].name, err);
            if (!n) {
                std::fprintf(stderr, "noise %s: %s\n", sn.vecs[i].name, err.c_str());
                CHECK(n != nullptr);
                continue;
            }
            for (int p = 0; p < 4; ++p) {
                checkBits(std::string("noise seed ") + std::to_string(sn.seed) + " " + sn.vecs[i].name + " P" + std::to_string(p + 1),
                          bitsOf(n->get(points[p].x, points[p].y, points[p].z)), sn.vecs[i].bits[p]);
            }
        }
    }

    // All 62 noise files exist and instantiate; the derived parameters match the real objects.
    const RandomState rs(12345, worldgen);
    // 62 overworld/cave/surface noises + noise/nether/temperature and vegetation (the game builds those two differently)
    CHECK(rs.noiseKeys().size() == 64);
    std::string err;
    CHECK(rs.createAllNoises(err));
    if (!err.empty()) std::fprintf(stderr, "createAllNoises: %s\n", err.c_str());
    for (const DerivedVec& dv : kDerived) {
        const NormalNoise* n = rs.noise(dv.name, err);
        CHECK(n != nullptr);
        if (!n) continue;
        checkBits64(std::string("normalizationFactor ") + dv.name, bitsOf(n->normalizationFactor()), dv.normalizationFactor);
        checkInt(std::string("octave count ") + dv.name, static_cast<long long>(n->octaves().size()), static_cast<long long>(dv.octaves.size()));
        checkInt(std::string("layer count ") + dv.name, static_cast<long long>(n->layerCount()), static_cast<long long>(dv.octaves.size() * 2));
        if (n->octaves().size() != dv.octaves.size() || n->layerCount() != dv.octaves.size() * 2) continue;
        for (std::size_t o = 0; o < dv.octaves.size(); ++o) {
            const std::string t = std::string(dv.name) + " octave " + std::to_string(dv.octaves[o].octave);
            checkInt(t + " index", n->octaves()[o].octave, dv.octaves[o].octave);
            checkBits64(t + " frequency", bitsOf(n->octaves()[o].frequency), dv.octaves[o].frequency);
            checkBits64(t + " amplitude", bitsOf(n->octaves()[o].amplitude), dv.octaves[o].amplitude);
            checkBits(t + " float amplitude (first)", bitsOf(n->layerAmplitude(2 * o)), dv.octaves[o].floatAmplitude);
            checkBits(t + " float amplitude (second)", bitsOf(n->layerAmplitude(2 * o + 1)), dv.octaves[o].floatAmplitude);
            double freq;
            std::memcpy(&freq, &dv.octaves[o].frequency, sizeof freq);
            checkBits64(t + " second layer frequency", bitsOf(n->layerFrequency(2 * o + 1)), bitsOf(freq * 1.0181268882175227));
        }
    }
    // The first octave of continentalness for seed 12345 [REAL: section 4]: offsets and permutation heads of both layers
    {
        const NormalNoise* n = rs.noise("minecraft:continentalness", err);
        if (n) {
            checkBits64("continentalness first octave_-9 offsetX", bitsOf(n->layerNoise(0).offsetX()), 0x4061c8eb3025e24full);
            checkBits64("continentalness first octave_-9 offsetY", bitsOf(n->layerNoise(0).offsetY()), 0x401d30ea9a0af2e0ull);
            checkBits64("continentalness first octave_-9 offsetZ", bitsOf(n->layerNoise(0).offsetZ()), 0x404b85183d8ee5dcull);
            const int perm1[8] = {14, 158, 73, 15, 250, 129, 33, 152};
            const int perm2[8] = {140, 210, 9, 42, 126, 234, 225, 254};
            for (int i = 0; i < 8; ++i) {
                checkInt("continentalness first perm " + std::to_string(i), n->layerNoise(0).permutation(i), perm1[i]);
                checkInt("continentalness second perm " + std::to_string(i), n->layerNoise(1).permutation(i), perm2[i]);
            }
            checkBits64("continentalness second octave_-9 offsetX", bitsOf(n->layerNoise(1).offsetX()), 0x4069187448aba073ull);
        }
    }
    // Named factories: root.fromHashOf(id).forkPositional()
    {
        const XoroshiroPositionalRandomFactory aq = rs.randomFactory("minecraft:aquifer");
        XoroshiroRandomSource direct = rs.root().makeFromHashOf("minecraft:aquifer");
        const XoroshiroPositionalRandomFactory expect = direct.forkPositionalValue();
        CHECK(aq.lo() == expect.lo() && aq.hi() == expect.hi());
    }
}

// =================================================================================================================
// Router probes, seed 12345                                      [GT: real NoiseChunk samplers, basic12345.out]
// point path: temperature, vegetation, continents, erosion, ridges, depth, chunk_surface_level, final_density
struct Probe {
    int x, y, z;
    std::uint32_t temperature, vegetation, continents, erosion, ridges, depth, chunkSurface, finalDensity;
};
const Probe kProbes[] = {
    {0, 64, 0, 0xbcb0d800u, 0x3f2c113au, 0xbecc59f1u, 0xbf3aba16u, 0x3e20aea7u, 0xbdfd70a8u, 0x42000000u, 0xbe403440u},
    {0, 0, 0, 0xbcb0d800u, 0x3f2c113au, 0xbecc59f1u, 0xbf3aba16u, 0x3e20aea7u, 0x3ec0a3d6u, 0x42000000u, 0xbcbd089au},
    {100, 70, -200, 0xbd588e89u, 0x3f3fd7efu, 0x3da17b98u, 0xbf20a8d6u, 0x3e95588au, 0x3e6dc68au, 0x42ae0000u, 0x3d2b5f22u},
    {-1000, 64, 777, 0x3e681f7au, 0x3e705916u, 0x3eed97a4u, 0xbf084602u, 0x3eba6b47u, 0x3ede8f6cu, 0x42d48000u, 0x3d9fa317u},
    {5, 200, 5, 0xbcc7ca3cu, 0x3f2df35au, 0xbece1967u, 0xbf3bf1c5u, 0x3e2fd7aeu, 0xbf97d70au, 0x42000000u, 0xbeeaaaabu},
    {16, -60, 3, 0xbce7663au, 0x3f2fa328u, 0xbec92e46u, 0xbf3bcf1eu, 0x3e7ca1a7u, 0x3f5851ebu, 0x42000000u, 0x3d15a95eu},
    {3, 1, 7, 0xbcc887d4u, 0x3f2e0d33u, 0xbecf7a23u, 0xbf3c471cu, 0x3e27fc62u, 0x3ebca3d6u, 0x42000000u, 0xb9a4c700u},
};

// Intermediate functions at corner-aligned points (point path) [GT: inter1.out]
struct InterPoint {
    int x, y, z;
    std::vector<std::pair<const char*, std::uint32_t>> functions;
};
const InterPoint kInter[] = {
    {0, 64, 0, {
        {"shift_x", 0xc00982fdu},
        {"shift_z", 0xc00982fdu},
        {"overworld/continents", 0xbecc59f1u},
        {"overworld/erosion", 0xbf3aba16u},
        {"overworld/ridges", 0x3e20aea7u},
        {"overworld/ridges_folded", 0xbf077d02u},
        {"overworld/offset", 0xbf1fae15u},
        {"overworld/factor", 0x407ccccdu},
        {"overworld/jaggedness", 0x0u},
        {"overworld/base_3d_noise", 0xbdd6d10cu},
        {"overworld/sloped_cheese", 0xbf17fcf4u},
        {"overworld/caves/entrances", 0x3e39f192u},
        {"overworld/caves/pillars", 0xbd2c29e3u},
        {"overworld/caves/spaghetti_2d", 0x3f800000u},
        {"overworld/caves/spaghetti_roughness_function", 0x3c67c9d8u},
        {"overworld/caves/noodle", 0x3e0561f3u},
        {"overworld/preliminary_surface_level", 0x42000000u},
        {"overworld/depth", 0xbdfd70a8u},
    }},
    {100, 72, -200, {
        {"shift_x", 0xbda8c8a3u},
        {"shift_z", 0x3feb926au},
        {"overworld/continents", 0x3da17b98u},
        {"overworld/erosion", 0xbf20a8d6u},
        {"overworld/ridges", 0x3e95588au},
        {"overworld/ridges_folded", 0xbdffd98cu},
        {"overworld/offset", 0xbe623976u},
        {"overworld/factor", 0x4096147bu},
        {"overworld/jaggedness", 0x0u},
        {"overworld/base_3d_noise", 0x3da2a284u},
        {"overworld/sloped_cheese", 0x40848e9du},
        {"overworld/caves/entrances", 0x3e04cf5eu},
        {"overworld/caves/pillars", 0xbf0e39d8u},
        {"overworld/caves/spaghetti_2d", 0x3f800000u},
        {"overworld/caves/spaghetti_roughness_function", 0x3ba57c20u},
        {"overworld/caves/noodle", 0x42800000u},
        {"overworld/preliminary_surface_level", 0x42b00000u},
        {"overworld/depth", 0x3e5dc68au},
    }},
    {-1000, 64, 776, {
        {"shift_x", 0xbe26b166u},
        {"shift_z", 0x3f654b96u},
        {"overworld/continents", 0x3eed1ccau},
        {"overworld/erosion", 0xbf08441bu},
        {"overworld/ridges", 0x3eb9d6feu},
        {"overworld/ridges_folded", 0x3db613e4u},
        {"overworld/offset", 0xbd89437cu},
        {"overworld/factor", 0x404a9c93u},
        {"overworld/jaggedness", 0x0u},
        {"overworld/base_3d_noise", 0x3dac5699u},
        {"overworld/sloped_cheese", 0x40b22520u},
        {"overworld/caves/entrances", 0x3e7e079cu},
        {"overworld/caves/pillars", 0xbd91138au},
        {"overworld/caves/spaghetti_2d", 0x3f800000u},
        {"overworld/caves/spaghetti_roughness_function", 0xbaa4ced4u},
        {"overworld/caves/noodle", 0x42800000u},
        {"overworld/preliminary_surface_level", 0x42d00000u},
        {"overworld/depth", 0x3eddaf21u},
    }},
    {1232, -24, -4320, {
        {"shift_x", 0x3e235744u},
        {"shift_z", 0xbf4fd14eu},
        {"overworld/continents", 0xbeca6a96u},
        {"overworld/erosion", 0x3f06df05u},
        {"overworld/ridges", 0x3e93e7aeu},
        {"overworld/ridges_folded", 0xbe0891eeu},
        {"overworld/offset", 0xbf1fae15u},
        {"overworld/factor", 0x407ccccdu},
        {"overworld/jaggedness", 0x0u},
        {"overworld/base_3d_noise", 0x3eb39b84u},
        {"overworld/sloped_cheese", 0x411420f4u},
        {"overworld/caves/entrances", 0x3e61ecd0u},
        {"overworld/caves/pillars", 0xbe03f6eau},
        {"overworld/caves/spaghetti_2d", 0x3f800000u},
        {"overworld/caves/spaghetti_roughness_function", 0x3c002998u},
        {"overworld/caves/noodle", 0x42800000u},
        {"overworld/preliminary_surface_level", 0x42000000u},
        {"overworld/depth", 0x3f1051ebu},
    }},
};

// One block column: climate (at y = 64), depth at four heights, the preliminary surface level, final_density from the
// chunk VOLUME at ten heights, and the runs of solid stone top to bottom (from the real doFill including the aquifer;
// water and cave air are both "not stone" here).                                  [GT: vec1.out / seed B run]
struct YBits {
    int y;
    std::uint32_t bits;
};
struct StoneRun {
    int top, bottom;
};
struct ColumnVec {
    int x, z;
    std::uint32_t climate[5];  // temperature, vegetation, continents, erosion, ridges
    YBits depth[4];
    std::uint32_t preliminarySurface;
    YBits density[10];
    std::vector<StoneRun> stone;
};
const ColumnVec kColumns12345[] = {
    {0, 0, {0xbcb0d800u, 0x3f2c113au, 0xbecc59f1u, 0xbf3aba16u, 0x3e20aea7u}, {{-64, 0x3f6051ebu}, {0, 0x3ec0a3d6u}, {64, 0xbdfd70a8u}, {200, 0xbf97d70au}}, 0x42000000u,
     {{-64, 0x3d19872au}, {-33, 0x3d659becu}, {0, 0xbcbd089au}, {31, 0x3d65a409u}, {62, 0xbe2d28aau}, {63, 0xbe36b2d2u}, {64, 0xbe403440u}, {100, 0xbee4e04bu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{48, 8}, {-3, -5}, {-11, -64}}},
    {100, -200, {0xbd588e89u, 0x3f3fd7efu, 0x3da17b98u, 0xbf20a8d6u, 0x3e95588au}, {{-64, 0x3fa3b8d1u}, {0, 0x3f4771a2u}, {64, 0x3e8ee345u}, {200, 0xbf488e5eu}}, 0x42b00000u,
     {{-64, 0x3d19872au}, {-33, 0x3daaf714u}, {0, 0x3defa101u}, {31, 0x3d53876eu}, {62, 0x3d3a0c69u}, {63, 0x3d34eb35u}, {64, 0x3d2fc9dfu}, {100, 0x3dfd517eu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{105, 26}, {14, -64}}},
    {-1000, 777, {0x3e681f7au, 0x3e705916u, 0x3eed97a4u, 0xbf084602u, 0x3eba6b47u}, {{-64, 0x3fb7a3dbu}, {0, 0x3f6f47b6u}, {64, 0x3ede8f6cu}, {200, 0xbf20b84au}}, 0x42d00000u,
     {{-64, 0x3d19872au}, {-33, 0x3d90de6cu}, {0, 0x3c88df53u}, {31, 0x3d026697u}, {62, 0x3d9cefdfu}, {63, 0x3d9e4980u}, {64, 0x3d9fa317u}, {100, 0x3cf6721bu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{127, 98}, {94, -13}, {-21, -64}}},
    {1234, -4321, {0xbdad2c40u, 0x3e2dc01fu, 0xbeca6348u, 0x3f06f188u, 0x3e9525f6u}, {{-64, 0x3f6051ebu}, {0, 0x3ec0a3d6u}, {64, 0xbdfd70a8u}, {200, 0xbf97d70au}}, 0x42000000u,
     {{-64, 0x3d19872au}, {-33, 0x3d6d27e6u}, {0, 0x3d4409adu}, {31, 0x3d18fd80u}, {62, 0xbe03d955u}, {63, 0xbe0ccbe5u}, {64, 0xbe15b8b9u}, {100, 0xbee9904fu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{50, -64}}},
    {-8, 8, {0xbcb81b4eu, 0x3f2d14cdu, 0xbed28504u, 0xbf3d1947u, 0x3dd47860u}, {{-64, 0x3f6051ebu}, {0, 0x3ec0a3d6u}, {64, 0xbdfd70a8u}, {200, 0xbf97d70au}}, 0x42000000u,
     {{-64, 0x3d19872au}, {-33, 0x3da0ca09u}, {0, 0xbbdc4167u}, {31, 0x3d08dec3u}, {62, 0xbe174161u}, {63, 0xbe288de8u}, {64, 0xbe39c04bu}, {100, 0xbee8b76au}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{47, 15}, {-2, -64}}},
};
const ColumnVec kColumnsB[] = {
    {0, 0, {0x3e4ef0cdu, 0x3e49f23du, 0x3cc76e15u, 0xbcf13a85u, 0x3f377e44u}, {{-64, 0x3f93445du}, {0, 0x3f2688bau}, {64, 0x3e1a22eau}, {200, 0xbf697746u}}, 0x42900000u,
     {{-64, 0x3d19872au}, {-33, 0x3dde532fu}, {0, 0x3d922dadu}, {31, 0x3db1a6e5u}, {62, 0x3d88b6f3u}, {63, 0x3d873671u}, {64, 0x3d85b5e6u}, {100, 0xbe1b8febu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{87, -64}}},
    {100, -200, {0x3e8927b1u, 0x3e665936u, 0xbe054308u, 0x3d9d894fu, 0x3dec2248u}, {{-64, 0x3f7a3631u}, {0, 0x3ef46c62u}, {64, 0xbcb939e0u}, {200, 0xbf8ae4e8u}}, 0x42400000u,
     {{-64, 0x3d19872au}, {-33, 0x3e06ecf1u}, {0, 0x3dd6d76du}, {31, 0xba35b7feu}, {62, 0x3d47d3d4u}, {63, 0x3bb240bdu}, {64, 0xbd1b591eu}, {100, 0xbeeaaaabu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{63, 32}, {23, 22}, {12, -64}}},
    {-1000, 777, {0xbd080cb4u, 0x3d2a7c7cu, 0xbe4a4efdu, 0xbdb32c4cu, 0xbe54382au}, {{-64, 0x3f6051ebu}, {0, 0x3ec0a3d6u}, {64, 0xbdfd70a8u}, {200, 0xbf97d70au}}, 0x42000000u,
     {{-64, 0x3d19872au}, {-33, 0x3d116ddcu}, {0, 0x3c094657u}, {31, 0x3d3dea9au}, {62, 0xbde72325u}, {63, 0xbdf9a71au}, {64, 0xbe061020u}, {100, 0xbeeaaaabu}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{48, -10}, {-15, -64}}},
    {1234, -4321, {0xbeb407d3u, 0xbe5b8213u, 0x3e9d9bd2u, 0x3ec55bbcu, 0x3e08988fu}, {{-64, 0x3f800620u}, {0, 0x3f000c40u}, {64, 0x39440000u}, {200, 0xbf87f9e0u}}, 0x42600000u,
     {{-64, 0x3d19872au}, {-33, 0x3c9ca7d8u}, {0, 0x3ddb8814u}, {31, 0x3d151f5fu}, {62, 0x3c83ae91u}, {63, 0x3bf36099u}, {64, 0xbaa009cfu}, {100, 0xbeccddf9u}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{63, -64}}},
    {-8, 8, {0x3e4d53d2u, 0x3e4ed1e7u, 0x3c722cc5u, 0xbd0e6da1u, 0x3f37bd5cu}, {{-64, 0x3f91f414u}, {0, 0x3f23e828u}, {64, 0x3e0fa0a0u}, {200, 0xbf6c17d8u}}, 0x42900000u,
     {{-64, 0x3d19872au}, {-33, 0x3d99c1e1u}, {0, 0x3df61c80u}, {31, 0x3d578a6du}, {62, 0x3d5f2048u}, {63, 0x3d6028a0u}, {64, 0x3d6130f6u}, {100, 0xbe334d14u}, {200, 0xbeeaaaabu}, {319, 0xbcccc1e0u}},
     {{87, -64}}},
};

struct ChunkSum {
    int cx, cz;
    std::uint64_t fnv;
    int positive;
    std::uint32_t maxBits;
    int stoneIfNoWater;  // stone count of the real fill when it placed no water in this chunk (else -1); air = 98304 - stone
};
const ChunkSum kChunks12345[] = {
    {0, 0, 0xdef072582385a22cull, 26161, 0x3e949553u, -1},
    {5, -9, 0x9b5b7717a29b0fcaull, 45645, 0x3e198ef1u, 45645},
    {-13, 27, 0xa3849c79f64bfad8ull, 30693, 0x3ee90966u, -1},
    {100, 100, 0x8323ea32bf1f5762ull, 31869, 0x3ee7f117u, -1},
};
const ChunkSum kChunksB[] = {
    {0, 0, 0x9d34da9d53e8f4a2ull, 39025, 0x3ed88201u, 39025},
    {5, -9, 0xcf1b34cb4d38afa1ull, 27769, 0x3e1a5503u, 27769},
    {-13, 27, 0xeb197cbda1c0c9d7ull, 35073, 0x3ed3c4d6u, -1},
};
constexpr std::uint32_t kMinDensityBits = 0xbeeaaaabu;  // -0.45833334 in every chunk

// Climate noise functions at five blocks, seed 12345                [COMPOSED: real noise objects + spec formulas]
// shiftA = offset.get(x*0.25, y*0.0, z*0.25) * 4f, shiftB = offset.get(z*0.25, x*0.25, 0) * 4f, value =
// noise.get(x*0.25 + (double)shiftA, y*0.0, z*0.25 + (double)shiftB)
struct ClimateVec {
    int x, y, z;
    std::uint32_t shiftA, shiftB, continentalness, erosion, ridge, temperature, vegetation;
};
const ClimateVec kClimate[] = {
    {0, 64, 0, 0xc00982fdu, 0xc00982fdu, 0xbecc59f1u, 0xbf3aba16u, 0x3e20aea7u, 0xbcb0d800u, 0x3f2c113au},
    {100, 64, -250, 0x3e90cd5fu, 0x3f873ba3u, 0x3e325329u, 0xbf1754f1u, 0x3dbdbe03u, 0xbd4ded50u, 0x3f432dcbu},
    {-1234, 70, 8910, 0xbea1a517u, 0x3f3431a3u, 0xbe02c79du, 0xbe649d0bu, 0x3ef4a9c7u, 0x3eb08c33u, 0xbd20a142u},
    {37, -20, -3, 0xbf2ee946u, 0xc03a7008u, 0xbeb3364fu, 0xbf397ad6u, 0x3ebe5de7u, 0xbd10f412u, 0x3f316799u},
    {16000, 120, -32000, 0x3eb35491u, 0x4000d014u, 0x3f49f38du, 0x3e89380cu, 0xbe364c71u, 0xbbde52a6u, 0x3cc4f530u},
};

std::vector<std::string> extraFunctionRefs() {
    std::vector<std::string> refs;
    for (const auto& f : kInter[0].functions) refs.push_back(std::string("minecraft:") + f.first);
    return refs;
}

// Stone runs (top to bottom) of one column of a block volume from "density > 0 is stone".
std::vector<StoneRun> stoneRunsOf(const std::vector<float>& volume, int localX, int localZ) {
    const std::vector<std::uint8_t> blocks = stoneAirFromDensity(volume.data(), volume.size());
    std::vector<StoneRun> out;
    for (const ColumnRun& r : columnRuns(blocks.data(), localX, localZ, -64, 384)) {
        if (r.block == static_cast<std::uint8_t>(FillBlock::Stone)) out.push_back({r.topY, r.bottomY});
    }
    return out;
}

// Cells of the real fills above that are stone only because of the aquifer (density <= 0 there): column -> y ranges.
std::vector<StoneRun> aquiferOnlyStone(std::int64_t seed, int x, int z) {
    if (seed == 12345 && x == 0 && z == 0) return {{-3, -5}};
    if (seed == 12345 && x == -8 && z == 8) return {{-2, -2}};
    if (seed == 8675309123456789ll && x == 100 && z == -200) return {{23, 22}};
    return {};
}

void testColumns(const NoiseRouter& router, std::int64_t seed, const ColumnVec* cols, std::size_t count) {
    std::map<std::pair<int, int>, std::vector<float>> chunks;
    for (std::size_t i = 0; i < count; ++i) {
        const ColumnVec& c = cols[i];
        const std::string tag = "seed " + std::to_string(seed) + " column (" + std::to_string(c.x) + "," + std::to_string(c.z) + ") ";
        const int cx = floorDiv(c.x, 16), cz = floorDiv(c.z, 16);
        auto it = chunks.find({cx, cz});
        if (it == chunks.end()) it = chunks.emplace(std::make_pair(cx, cz), router.finalDensityVolume(cx, cz)).first;
        const std::vector<float>& vol = it->second;
        const int lx = c.x - cx * 16, lz = c.z - cz * 16;
        const DensityFunction* fns[5] = {&router.temperature(), &router.vegetation(), &router.continents(), &router.erosion(), &router.ridges()};
        const char* names[5] = {"temperature", "vegetation", "continents", "erosion", "ridges"};
        for (int k = 0; k < 5; ++k) checkBits(tag + names[k], bitsOf(fns[k]->compute(c.x, 64, c.z)), c.climate[k]);
        for (const YBits& d : c.depth) checkBits(tag + "depth@y" + std::to_string(d.y), bitsOf(router.depth().compute(c.x, d.y, c.z)), d.bits);
        checkBits(tag + "preliminary surface level", bitsOf(router.aquiferSurfaceLevel().compute(c.x, 0, c.z)), c.preliminarySurface);
        for (const YBits& d : c.density) {
            checkBits(tag + "final_density volume y" + std::to_string(d.y), bitsOf(vol[static_cast<std::size_t>(d.y + 64) + 384u * (static_cast<std::size_t>(lx) + 16u * static_cast<std::size_t>(lz))]), d.bits);
        }
        // Stone/air from "density > 0 is stone" against the real fill. The real fill also runs the aquifer, which turns
        // some cells with density <= 0 (cave walls under water/lava) into stone: those cells are listed in
        // aquiferOnlyStone and removed from the real column before comparing. Every other cell must agree exactly.
        const std::vector<StoneRun> runs = stoneRunsOf(vol, lx, lz);
        std::vector<char> ours(384, 0), real(384, 0);
        for (const StoneRun& r : runs) for (int y = r.bottom; y <= r.top; ++y) ours[static_cast<std::size_t>(y + 64)] = 1;
        for (const StoneRun& r : c.stone) for (int y = r.bottom; y <= r.top; ++y) real[static_cast<std::size_t>(y + 64)] = 1;
        for (const StoneRun& r : aquiferOnlyStone(seed, c.x, c.z)) {
            for (int y = r.bottom; y <= r.top; ++y) {
                real[static_cast<std::size_t>(y + 64)] = 0;
                CHECK(vol[static_cast<std::size_t>(y + 64) + 384u * (static_cast<std::size_t>(lx) + 16u * static_cast<std::size_t>(lz))] <= 0.0f);
            }
        }
        int differing = 0;
        for (std::size_t y = 0; y < 384; ++y) differing += ours[y] != real[y] ? 1 : 0;
        checkInt(tag + "stone/air cells that differ from the real fill (aquifer cells removed)", differing, 0);
    }
}

void testChunkSums(const NoiseRouter& router, std::int64_t seed, const ChunkSum* sums, std::size_t count) {
    std::vector<float> vol(router.chunkVolumeSize());
    CHECK(vol.size() == 98304);
    for (std::size_t i = 0; i < count; ++i) {
        const ChunkSum& s = sums[i];
        const std::string tag = "seed " + std::to_string(seed) + " chunk (" + std::to_string(s.cx) + "," + std::to_string(s.cz) + ") ";
        router.finalDensityVolume(s.cx, s.cz, vol.data());
        checkBits64(tag + "fnv1a64 of 98304 density values", densityChecksum(vol.data(), vol.size()), s.fnv);
        int positive = 0;
        float mn = vol[0], mx = vol[0];
        for (const float v : vol) {
            positive += v > 0.0f ? 1 : 0;
            mn = jmin(mn, v);
            mx = jmax(mx, v);
        }
        checkInt(tag + "density > 0 count", positive, s.positive);
        checkBits(tag + "max", bitsOf(mx), s.maxBits);
        checkBits(tag + "min", bitsOf(mn), kMinDensityBits);
        if (s.stoneIfNoWater >= 0) {
            const std::vector<std::uint8_t> blocks = stoneAirFromDensity(vol.data(), vol.size());
            const long long stone = std::count(blocks.begin(), blocks.end(), static_cast<std::uint8_t>(FillBlock::Stone));
            checkInt(tag + "stone blocks", stone, s.stoneIfNoWater);
            checkInt(tag + "air blocks", static_cast<long long>(blocks.size()) - stone, 98304 - s.stoneIfNoWater);
        }
    }
}

void testRouter(const std::filesystem::path& worldgen) {
    std::string err;
    const std::vector<std::string> extras = extraFunctionRefs();
    const auto router = NoiseRouter::create(worldgen, 12345, err, "minecraft:overworld", extras);
    if (!router) {
        std::fprintf(stderr, "NoiseRouter::create failed: %s\n", err.c_str());
        CHECK(router != nullptr);
        return;
    }
    CHECK(router->settings().minY == -64 && router->settings().height == 384 && router->settings().seaLevel == 63);
    CHECK(router->settings().defaultBlock == "minecraft:stone" && router->settings().defaultFluid == "minecraft:water");
    CHECK(router->hasAquifers() && router->aquiferSurfaceLevel().valid() && router->aquiferExclusion().valid());
    CHECK(!router->function("minecraft:not_loaded").valid() && router->function("minecraft:overworld/offset").valid());

    // y dependence: the climate functions and the preliminary surface level are 2D, depth and final_density are not
    CHECK(!router->temperature().dependsOnY() && !router->continents().dependsOnY() && !router->aquiferSurfaceLevel().dependsOnY());
    CHECK(router->depth().dependsOnY() && router->finalDensity().dependsOnY());

    for (const Probe& p : kProbes) {
        const std::string tag = "probe (" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + ") ";
        checkBits(tag + "temperature", bitsOf(router->temperature().compute(p.x, p.y, p.z)), p.temperature);
        checkBits(tag + "vegetation", bitsOf(router->vegetation().compute(p.x, p.y, p.z)), p.vegetation);
        checkBits(tag + "continents", bitsOf(router->continents().compute(p.x, p.y, p.z)), p.continents);
        checkBits(tag + "erosion", bitsOf(router->erosion().compute(p.x, p.y, p.z)), p.erosion);
        checkBits(tag + "ridges", bitsOf(router->ridges().compute(p.x, p.y, p.z)), p.ridges);
        checkBits(tag + "depth", bitsOf(router->depth().compute(p.x, p.y, p.z)), p.depth);
        checkBits(tag + "chunk_surface_level (point)", bitsOf(router->chunkSurfaceLevel().compute(p.x, p.y, p.z)), p.chunkSurface);
        checkBits(tag + "final_density (point)", bitsOf(router->finalDensity().compute(p.x, p.y, p.z)), p.finalDensity);
    }
    // an evaluator shared between functions at one position gives the same values
    {
        DensityEvaluator ev(router->graph());
        for (const Probe& p : kProbes) {
            ev.setPosition(p.x, p.y, p.z);
            checkBits("shared evaluator continents", bitsOf(ev.value(router->continents())), p.continents);
            checkBits("shared evaluator temperature", bitsOf(ev.value(router->temperature())), p.temperature);
            checkBits("shared evaluator final_density", bitsOf(ev.value(router->finalDensity())), p.finalDensity);
            checkBits("shared evaluator depth", bitsOf(ev.value(router->depth())), p.depth);
        }
    }

    for (const InterPoint& ip : kInter) {
        for (const auto& f : ip.functions) {
            const DensityFunction fn = router->function(std::string("minecraft:") + f.first);
            checkBits(std::string("intermediate ") + f.first + " at (" + std::to_string(ip.x) + "," + std::to_string(ip.y) + "," + std::to_string(ip.z) + ")",
                      bitsOf(fn.compute(ip.x, ip.y, ip.z)), f.second);
        }
    }

    for (const ClimateVec& c : kClimate) {
        const std::string tag = "COMPOSED climate (" + std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.z) + ") ";
        checkBits(tag + "shift_x", bitsOf(router->function("minecraft:shift_x").compute(c.x, c.y, c.z)), c.shiftA);
        checkBits(tag + "shift_z", bitsOf(router->function("minecraft:shift_z").compute(c.x, c.y, c.z)), c.shiftB);
        checkBits(tag + "continentalness", bitsOf(router->continents().compute(c.x, c.y, c.z)), c.continentalness);
        checkBits(tag + "erosion", bitsOf(router->erosion().compute(c.x, c.y, c.z)), c.erosion);
        checkBits(tag + "ridge", bitsOf(router->ridges().compute(c.x, c.y, c.z)), c.ridge);
        checkBits(tag + "temperature", bitsOf(router->temperature().compute(c.x, c.y, c.z)), c.temperature);
        checkBits(tag + "vegetation", bitsOf(router->vegetation().compute(c.x, c.y, c.z)), c.vegetation);
    }

    testChunkSums(*router, 12345, kChunks12345, sizeof(kChunks12345) / sizeof(kChunks12345[0]));
    testColumns(*router, 12345, kColumns12345, sizeof(kColumns12345) / sizeof(kColumns12345[0]));

    // chunk_surface_level volume of chunk (5,-9), diagonal x = z = 0..15                 [GT: ref3.out]
    {
        std::vector<float> csl(256);
        router->chunkSurfaceLevelVolume(5, -9, csl.data());
        const float diag[16] = {112.0f, 111.0625f, 110.25f, 109.5625f, 109.0f, 108.5625f, 108.25f, 108.0625f,
                                108.0f, 108.0625f, 108.25f, 108.5625f, 109.0f, 109.5625f, 110.25f, 111.0625f};
        for (int i = 0; i < 16; ++i) checkBits("chunk_surface_level diagonal " + std::to_string(i), bitsOf(csl[static_cast<std::size_t>(i + 16 * i)]), bitsOf(diag[i]));
        // the volume is the bilinear fill between the corner blocks of the preliminary surface level: the chunk's own corner is exact
        checkBits("chunk_surface_level (0,0) equals the surface level function at the chunk corner", bitsOf(csl[0]),
                  bitsOf(router->aquiferSurfaceLevel().compute(5 * 16, 0, -9 * 16)));
    }

    // Volume semantics that must not depend on the shape of the requested volume (the game's cell interpolation is
    // relative to the cell grid): 1 x 384 x 1 column volumes and a cell-aligned sub-box equal the chunk volume.   [GT: 0/4608]
    {
        const std::vector<float> chunk = router->finalDensityVolume(5, -9);
        std::vector<float> col(384);
        int mismatches = 0;
        const std::pair<int, int> columns[] = {{0, 0}, {15, 15}, {3, 9}, {8, 1}, {14, 6}, {1, 13}};
        for (const std::pair<int, int>& xz : columns) {
            DensityVolume v;
            v.sizeX = 1;
            v.sizeY = 384;
            v.sizeZ = 1;
            v.minX = 5 * 16 + xz.first;
            v.minY = -64;
            v.minZ = -9 * 16 + xz.second;
            router->finalDensity().fillVolume(v, col.data());
            for (int y = 0; y < 384; ++y) {
                const float want = chunk[static_cast<std::size_t>(y) + 384u * (static_cast<std::size_t>(xz.first) + 16u * static_cast<std::size_t>(xz.second))];
                mismatches += bitsOf(col[static_cast<std::size_t>(y)]) != bitsOf(want) ? 1 : 0;
            }
        }
        checkInt("column volumes vs chunk volume (bit mismatches)", mismatches, 0);

        DensityVolume box;
        box.sizeX = 7;
        box.sizeY = 100;
        box.sizeZ = 11;
        box.minX = 5 * 16 + 3;
        box.minY = -64 + 16;
        box.minZ = -9 * 16 + 2;
        std::vector<float> sub(box.size());
        router->finalDensity().fillVolume(box, sub.data());
        int boxMismatches = 0;
        for (int k = 0; k < box.sizeZ; ++k) {
            for (int i = 0; i < box.sizeX; ++i) {
                for (int j = 0; j < box.sizeY; ++j) {
                    const float want = chunk[static_cast<std::size_t>(j + 16) + 384u * (static_cast<std::size_t>(i + 3) + 16u * static_cast<std::size_t>(k + 2))];
                    boxMismatches += bitsOf(sub[box.index(i, j, k)]) != bitsOf(want) ? 1 : 0;
                }
            }
        }
        checkInt("sub-box volume vs chunk volume (bit mismatches)", boxMismatches, 0);
    }

    // Many threads share one router: same checksums, no interference.
    {
        std::atomic<int> bad{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&router, &bad, t] {
                std::vector<float> vol(router->chunkVolumeSize());
                for (int rep = 0; rep < 3; ++rep) {
                    const ChunkSum& s = kChunks12345[(t + rep) % 4];
                    router->finalDensityVolume(s.cx, s.cz, vol.data());
                    if (densityChecksum(vol.data(), vol.size()) != s.fnv) ++bad;
                    const float* scratch = router->finalDensityVolumeScratch(s.cx, s.cz);
                    if (densityChecksum(scratch, vol.size()) != s.fnv) ++bad;
                    if (bitsOf(router->finalDensity().compute(0, 64, 0)) != 0xbe403440u) ++bad;
                }
            });
        }
        for (std::thread& th : threads) th.join();
        checkInt("multi-threaded evaluation failures", bad.load(), 0);
    }

    // Timing (printed, not asserted): single thread, release build.
    {
        std::vector<float> vol(router->chunkVolumeSize());
        router->finalDensityVolume(0, 0, vol.data());  // warm-up
        const int reps = 20;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) router->finalDensityVolume(i % 7, (i / 7) % 5, vol.data());
        const auto t1 = std::chrono::steady_clock::now();
        std::printf("  finalDensityVolume: %.2f ms per chunk (single thread, %d chunks)\n", std::chrono::duration<double, std::milli>(t1 - t0).count() / reps, reps);
    }

    // Another world seed, compared with the real game's numbers
    const auto routerB = NoiseRouter::create(worldgen, 8675309123456789ll, err);
    if (!routerB) {
        std::fprintf(stderr, "NoiseRouter::create (seed B) failed: %s\n", err.c_str());
        CHECK(routerB != nullptr);
        return;
    }
    testChunkSums(*routerB, 8675309123456789ll, kChunksB, sizeof(kChunksB) / sizeof(kChunksB[0]));
    testColumns(*routerB, 8675309123456789ll, kColumnsB, sizeof(kColumnsB) / sizeof(kColumnsB[0]));
}

// =================================================================================================================
// The density engine itself on small hand-made graphs (no game data): node semantics, the game's compile-time
// rewrites, volume interpolation. The expected values are computed with the same float operations written out here.
struct Synth {
    std::shared_ptr<RandomState> state;
    std::unique_ptr<DensityGraph> graph;
    std::vector<DensityFunction> fns;
};

bool buildSynth(const std::vector<std::string>& definitions, Synth& s) {
    s.state = std::make_shared<RandomState>(1, std::filesystem::path());
    DensityGraphBuilder b(s.state);
    std::vector<int> ids;
    for (const std::string& def : definitions) {
        JsonValue j;
        std::string err;
        if (!parseJson(def, j, err)) {
            std::fprintf(stderr, "synthetic JSON: %s\n", err.c_str());
            return false;
        }
        const int id = b.addJson(j);
        if (id < 0) {
            std::fprintf(stderr, "synthetic graph: %s\n  in %s\n", b.error().c_str(), def.c_str());
            return false;
        }
        ids.push_back(id);
    }
    s.graph = b.build();
    if (!s.graph) return false;
    for (const int id : ids) s.fns.push_back(s.graph->function(id));
    return true;
}

void testSyntheticGraphs() {
    // minecraft:y: a gradient whose value is exactly (float)y
    const std::string Y = R"({"type":"minecraft:gradient","axis":"y","from_coordinate":-4064,"to_coordinate":4062,"from_value":-4064.0,"to_value":4062.0})";
    const std::string X = R"({"type":"minecraft:gradient","axis":"x","from_coordinate":-4064,"to_coordinate":4062,"from_value":-4064.0,"to_value":4062.0})";
    const std::string Z = R"({"type":"minecraft:gradient","axis":"z","from_coordinate":-4064,"to_coordinate":4062,"from_value":-4064.0,"to_value":4062.0})";
    const auto un = [&](const char* type, const std::string& in) { return std::string("{\"type\":\"minecraft:") + type + "\",\"input\":" + in + "}"; };
    const auto bin = [&](const char* type, const std::string& l, const std::string& r) {
        return std::string("{\"type\":\"minecraft:") + type + "\",\"left\":" + l + ",\"right\":" + r + "}";
    };
    const std::vector<std::string> defs = {
        Y,                                                                                  // 0
        bin("div", Y, "3.0"),                                                               // 1  x * (1f / 3f), NOT x / 3f
        bin("sub", Y, "5.0"),                                                               // 2  x + (-5f)
        bin("sub", "5.0", Y),                                                               // 3  5f - x
        bin("div", "7.0", Y),                                                               // 4  7f / x
        bin("mul", Y, Y),                                                                   // 5  pointwise: left == 0 -> 0
        R"({"type":"minecraft:clamp","input":)" + Y + R"(,"min":-2.5,"max":3.5})",         // 6
        un("abs", Y), un("square", Y), un("cube", Y), un("half_negative", Y), un("quarter_negative", Y), un("squeeze", Y),  // 7..12
        R"({"type":"minecraft:lerp","alpha":1.0,"first":7.0,"second":)" + Y + "}",          // 13 alpha == 1 -> second exactly
        R"({"type":"minecraft:lerp","alpha":0.0,"first":7.0,"second":)" + Y + "}",          // 14 alpha == 0 -> first
        R"({"type":"minecraft:lerp","alpha":0.25,"first":4.0,"second":)" + Y + "}",         // 15 first + alpha * (second - first)
        R"({"type":"minecraft:range_choice","input":)" + Y + R"(,"min_inclusive":0.0,"max_exclusive":10.0,"when_in_range":1.5,"when_out_of_range":-2.5})",  // 16
        R"({"type":"minecraft:interval_select","input":)" + Y + R"(,"thresholds":[-1.0,5.0],"functions":[-10.0,-20.0,-30.0]})",                          // 17
        R"({"type":"minecraft:spline","spline":{"coordinate":)" + Y +
            R"(,"points":[{"location":-10.0,"value":1.0,"derivative":0.0},{"location":0.0,"value":3.0,"derivative":0.5},{"location":10.0,"value":2.0,"derivative":-0.25}]}})",  // 18
        R"({"type":"minecraft:find_top_surface","density":{"type":"minecraft:gradient","axis":"y","from_coordinate":0,"to_coordinate":100,"from_value":1.0,"to_value":-1.0},"upper_bound":96.0,"lower_bound":0,"cell_height":8})",  // 19
        R"({"type":"minecraft:find_top_surface","density":{"type":"minecraft:gradient","axis":"y","from_coordinate":0,"to_coordinate":100,"from_value":-1.0,"to_value":-2.0},"upper_bound":96.0,"lower_bound":8,"cell_height":8})",  // 20
        R"({"type":"minecraft:cache","input":{"type":"minecraft:blend_density","input":)" + Y + "}}",                                                           // 21
        bin("add", "{\"type\":\"minecraft:blend_alpha\"}", "{\"type\":\"minecraft:blend_offset\"}"),                                                           // 22  1 + 0
        bin("add", "{\"type\":\"minecraft:beardifier\"}", Y),                                                                                                   // 23  0.0f + y
        un("negate", Y),                                                                                                                                        // 24
        R"({"type":"minecraft:constant","argument":2.5})",                                                                                                      // 25
        bin("min", Y, "3.0"), bin("max", "3.0", Y),                                                                                                             // 26, 27
        R"({"type":"minecraft:interpolated","cell_size_xz":4,"cell_size_y":8,"input":)" + bin("add", bin("add", X, bin("mul", Z, "3.0")), bin("mul", Y, "2.0")) + "}",  // 28  x + 3z + 2y
    };
    Synth s;
    if (!buildSynth(defs, s)) {
        CHECK(false);
        return;
    }
    const int ys[] = {-12, -5, -1, 0, 1, 3, 4, 5, 7, 9, 10, 11, 100};
    const auto f = [&](int idx, int y) { return s.fns[static_cast<std::size_t>(idx)].compute(2, y, -3); };
    int reciprocalDiffers = 0;
    for (const int y : ys) {
        const std::string t = "synthetic y=" + std::to_string(y) + " ";
        const float fy = static_cast<float>(y);
        checkBits(t + "gradient", bitsOf(f(0, y)), bitsOf(fy));
        checkBits(t + "div by constant is x * (1/c)", bitsOf(f(1, y)), bitsOf(fy * (1.0f / 3.0f)));
        reciprocalDiffers += bitsOf(fy * (1.0f / 3.0f)) != bitsOf(fy / 3.0f) ? 1 : 0;
        checkBits(t + "sub constant right", bitsOf(f(2, y)), bitsOf(fy + (-5.0f)));
        checkBits(t + "sub constant left", bitsOf(f(3, y)), bitsOf(5.0f - fy));
        if (y != 0) checkBits(t + "div constant left", bitsOf(f(4, y)), bitsOf(7.0f / fy));
        checkBits(t + "mul", bitsOf(f(5, y)), bitsOf(fy == 0.0f ? 0.0f : fy * fy));
        checkBits(t + "clamp", bitsOf(f(6, y)), bitsOf(clampf(fy, -2.5f, 3.5f)));
        checkBits(t + "abs", bitsOf(f(7, y)), bitsOf(std::fabs(fy)));
        checkBits(t + "square", bitsOf(f(8, y)), bitsOf(fy * fy));
        checkBits(t + "cube", bitsOf(f(9, y)), bitsOf(fy * fy * fy));
        checkBits(t + "half_negative", bitsOf(f(10, y)), bitsOf(fy > 0.0f ? fy : fy * 0.5f));
        checkBits(t + "quarter_negative", bitsOf(f(11, y)), bitsOf(fy > 0.0f ? fy : fy * 0.25f));
        const float c = clampf(fy, -1.0f, 1.0f);
        checkBits(t + "squeeze", bitsOf(f(12, y)), bitsOf(c / 2.0f - (c * c * c) / 24.0f));
        checkBits(t + "lerp alpha 1", bitsOf(f(13, y)), bitsOf(fy));
        checkBits(t + "lerp alpha 0", bitsOf(f(14, y)), bitsOf(7.0f));
        checkBits(t + "lerp alpha 0.25", bitsOf(f(15, y)), bitsOf(4.0f + 0.25f * (fy - 4.0f)));
        checkBits(t + "range_choice", bitsOf(f(16, y)), bitsOf((fy >= 0.0f && fy < 10.0f) ? 1.5f : -2.5f));
        checkBits(t + "interval_select", bitsOf(f(17, y)), bitsOf(fy < -1.0f ? -10.0f : fy < 5.0f ? -20.0f : -30.0f));
        checkBits(t + "find_top_surface (independent of y)", bitsOf(f(19, y)), bitsOf(48.0f));
        checkBits(t + "find_top_surface without a surface = lower bound", bitsOf(f(20, y)), bitsOf(8.0f));
        checkBits(t + "cache / blend_density are transparent", bitsOf(f(21, y)), bitsOf(fy));
        checkBits(t + "blend_alpha + blend_offset", bitsOf(f(22, y)), bitsOf(1.0f));
        checkBits(t + "beardifier is 0 (+ x)", bitsOf(f(23, y)), bitsOf(0.0f + fy));
        checkBits(t + "negate", bitsOf(f(24, y)), bitsOf(-fy));
        checkBits(t + "constant", bitsOf(f(25, y)), bitsOf(2.5f));
        checkBits(t + "min with constant", bitsOf(f(26, y)), bitsOf(jmin(fy, 3.0f)));
        checkBits(t + "max with constant", bitsOf(f(27, y)), bitsOf(jmax(3.0f, fy)));
        // spline: Hermite between the knots, linear extension outside (derivative 0 = flat)
        {
            const float locs[3] = {-10.0f, 0.0f, 10.0f}, vals[3] = {1.0f, 3.0f, 2.0f}, ders[3] = {0.0f, 0.5f, -0.25f};
            float want;
            int i = 0;
            while (i < 3 && !(fy < locs[i])) ++i;
            i -= 1;
            if (i < 0) {
                want = ders[0] == 0.0f ? vals[0] : vals[0] + ders[0] * (fy - locs[0]);
            } else if (i == 2) {
                want = ders[2] == 0.0f ? vals[2] : vals[2] + ders[2] * (fy - locs[2]);
            } else {
                const float x0 = locs[i], x1 = locs[i + 1];
                const float tt = (fy - x0) / (x1 - x0);
                const float a = ders[i] * (x1 - x0) - (vals[i + 1] - vals[i]);
                const float b = (-ders[i + 1]) * (x1 - x0) + (vals[i + 1] - vals[i]);
                want = lerpf(tt, vals[i], vals[i + 1]) + (tt * (1.0f - tt)) * lerpf(tt, a, b);
            }
            checkBits(t + "spline", bitsOf(f(18, y)), bitsOf(want));
        }
    }
    CHECK(reciprocalDiffers > 0);  // the x * (1/c) rewrite is observable: x / c would give other bits for some y
    CHECK(s.fns[0].dependsOnY() && !s.fns[19].dependsOnY() && s.fns[25].axes() == 0u);

    // the point path of an `interpolated` node: lerp between the cell corners; exact for a linear input
    for (const int y : {-9, 0, 5, 13}) {
        for (const int x : {-7, 0, 3, 6}) {
            const float want = static_cast<float>(x) + 3.0f * static_cast<float>(-3) + 2.0f * static_cast<float>(y);
            checkBits("interpolated point path (linear input)", bitsOf(s.fns[28].compute(x, y, -3)), bitsOf(want));
        }
    }

    // volume path: a linear input is reproduced exactly by the corner grid + cell fill, for any box (unaligned in
    // every axis, partial cells at both ends), including the first partial y cell
    struct Box {
        int sx, sy, sz, minX, minY, minZ;
    };
    const Box boxes[] = {{11, 20, 9, -5, 5, 3}, {16, 384, 16, 32, -64, -48}, {1, 384, 1, 7, -64, 7}, {5, 3, 5, 0, 0, 0}, {13, 17, 6, -30, -101, 41}};
    for (const Box& b : boxes) {
        DensityVolume v;
        v.sizeX = b.sx;
        v.sizeY = b.sy;
        v.sizeZ = b.sz;
        v.minX = b.minX;
        v.minY = b.minY;
        v.minZ = b.minZ;
        std::vector<float> out(v.size(), -12345.0f);
        s.fns[28].fillVolume(v, out.data());
        int bad = 0;
        for (int k = 0; k < v.sizeZ; ++k) {
            for (int i = 0; i < v.sizeX; ++i) {
                for (int j = 0; j < v.sizeY; ++j) {
                    const float want = static_cast<float>(v.blockX(i) + 3 * v.blockZ(k) + 2 * v.blockY(j));
                    bad += bitsOf(out[v.index(i, j, k)]) != bitsOf(want) ? 1 : 0;
                }
            }
        }
        checkInt("interpolated volume (linear input) bad values in box " + std::to_string(b.sx) + "x" + std::to_string(b.sy) + "x" + std::to_string(b.sz), bad, 0);
    }
    // a corner-aligned volume with the cell step: the input is sampled as it is (no interpolation)
    {
        DensityVolume v;
        v.sizeX = 3;
        v.sizeY = 4;
        v.sizeZ = 2;
        v.minX = -8;
        v.minY = -16;
        v.minZ = 8;
        v.stepX = 4;
        v.stepY = 8;
        v.stepZ = 4;
        std::vector<float> out(v.size());
        s.fns[28].fillVolume(v, out.data());
        int bad = 0;
        for (int k = 0; k < v.sizeZ; ++k) {
            for (int i = 0; i < v.sizeX; ++i) {
                for (int j = 0; j < v.sizeY; ++j) bad += out[v.index(i, j, k)] != static_cast<float>(v.blockX(i) + 3 * v.blockZ(k) + 2 * v.blockY(j)) ? 1 : 0;
            }
        }
        checkInt("interpolated over a corner volume: bad values", bad, 0);
    }
    // plain functions over a volume = the point values
    {
        DensityVolume v;
        v.sizeX = 3;
        v.sizeY = 9;
        v.sizeZ = 2;
        v.minX = 1;
        v.minY = -4;
        v.minZ = 5;
        std::vector<float> out(v.size());
        for (const int idx : {1, 12, 16, 17, 18, 19, 23}) {
            s.fns[static_cast<std::size_t>(idx)].fillVolume(v, out.data());
            int bad = 0;
            for (int k = 0; k < v.sizeZ; ++k) {
                for (int i = 0; i < v.sizeX; ++i) {
                    for (int j = 0; j < v.sizeY; ++j) {
                        bad += bitsOf(out[v.index(i, j, k)]) != bitsOf(s.fns[static_cast<std::size_t>(idx)].compute(v.blockX(i), v.blockY(j), v.blockZ(k))) ? 1 : 0;
                    }
                }
            }
            checkInt("volume vs point for synthetic function " + std::to_string(idx), bad, 0);
        }
    }
    // an interpolated node under other nodes: elementwise combination of columns (add / min / range_choice / spline over interpolated)
    {
        const std::string I = defs[28];
        const std::vector<std::string> d2 = {
            bin("add", bin("min", I, "100.0"), "0.0"),
            R"({"type":"minecraft:range_choice","input":)" + I + R"(,"min_inclusive":-10.0,"max_exclusive":10.0,"when_in_range":)" + I + R"(,"when_out_of_range":-1.0})",
            R"({"type":"minecraft:spline","spline":{"coordinate":)" + I + R"(,"points":[{"location":-50.0,"value":0.0,"derivative":0.0},{"location":50.0,"value":10.0,"derivative":0.0}]}})",
            R"({"type":"minecraft:interval_select","input":)" + I + R"(,"thresholds":[0.0],"functions":[-1.0,)" + I + "]}",
            R"({"type":"minecraft:lerp","alpha":0.5,"first":)" + I + R"(,"second":2.0})",
        };
        Synth s2;
        if (!buildSynth(d2, s2)) {
            CHECK(false);
            return;
        }
        DensityVolume v;
        v.sizeX = 6;
        v.sizeY = 24;
        v.sizeZ = 5;
        v.minX = -3;
        v.minY = -8;
        v.minZ = 2;
        std::vector<float> out(v.size());
        for (std::size_t idx = 0; idx < d2.size(); ++idx) {
            s2.fns[idx].fillVolume(v, out.data());
            int bad = 0;
            for (int k = 0; k < v.sizeZ; ++k) {
                for (int i = 0; i < v.sizeX; ++i) {
                    for (int j = 0; j < v.sizeY; ++j) {
                        const float lin = static_cast<float>(v.blockX(i) + 3 * v.blockZ(k) + 2 * v.blockY(j));
                        float want = 0.0f;
                        if (idx == 0) want = jmin(lin, 100.0f) + 0.0f;
                        if (idx == 1) want = (lin >= -10.0f && lin < 10.0f) ? lin : -1.0f;
                        if (idx == 2) {
                            const float t = (lin - (-50.0f)) / (50.0f - (-50.0f));
                            const float a = 0.0f * 100.0f - 10.0f, b = (-0.0f) * 100.0f + 10.0f;
                            want = lin < -50.0f ? 0.0f : lin >= 50.0f ? 10.0f : lerpf(t, 0.0f, 10.0f) + (t * (1.0f - t)) * lerpf(t, a, b);
                        }
                        if (idx == 3) want = lin < 0.0f ? -1.0f : lin;
                        if (idx == 4) want = lerpf(0.5f, lin, 2.0f);
                        bad += bitsOf(out[v.index(i, j, k)]) != bitsOf(want) ? 1 : 0;
                    }
                }
            }
            checkInt("volume of functions above interpolated, case " + std::to_string(idx), bad, 0);
        }
    }
    // errors are reported, not thrown
    {
        Synth bad;
        JsonValue j;
        std::string err;
        CHECK(parseJson(R"({"type":"minecraft:no_such_type"})", j, err));
        bad.state = std::make_shared<RandomState>(1, std::filesystem::path());
        DensityGraphBuilder b(bad.state);
        CHECK(b.addJson(j) < 0 && !b.error().empty() && !b.build());
    }
}

// The doFill helpers: loop order (z outermost, then x, then y from the top down), run-length summary, checksum.
void testFillHelpers() {
    // a 16 x 4 x 16 volume (height 4) where each value encodes its own position
    std::vector<float> vol(16u * 4u * 16u);
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 0; y < 4; ++y) vol[static_cast<std::size_t>(y) + 4u * (static_cast<std::size_t>(x) + 16u * static_cast<std::size_t>(z))] = static_cast<float>(x + 16 * z) + 0.25f * static_cast<float>(y);
        }
    }
    long visited = 0;
    bool orderOk = true;
    int prevZ = -1, prevX = -1, prevY = 4;
    forEachFillCell(vol.data(), -2, 3, -64, 4, [&](int worldX, int y, int worldZ, int localX, int localZ, int yIndex, float d) {
        ++visited;
        orderOk = orderOk && worldX == -2 * 16 + localX && worldZ == 3 * 16 + localZ && y == -64 + yIndex &&
                  d == static_cast<float>(localX + 16 * localZ) + 0.25f * static_cast<float>(yIndex);
        if (localZ == prevZ && localX == prevX) orderOk = orderOk && yIndex == prevY - 1;   // y descends within a column
        else if (localZ == prevZ) orderOk = orderOk && localX == prevX + 1 && yIndex == 3 && prevY == 0;  // x ascends within a z row
        else orderOk = orderOk && localZ == prevZ + 1 && localX == 0 && yIndex == 3;       // z is the slowest
        prevZ = localZ;
        prevX = localX;
        prevY = yIndex;
    });
    CHECK(visited == 16 * 4 * 16 && orderOk);

    const float col[6] = {-1.0f, 0.5f, 0.5f, -0.25f, 0.0f, 2.0f};  // bottom to top, height 6, minY = -3; column (0,0) only
    std::vector<float> one(16u * 6u * 16u, -1.0f);
    for (int i = 0; i < 6; ++i) one[static_cast<std::size_t>(i)] = col[i];
    const std::vector<std::uint8_t> blocks = stoneAirFromDensity(one.data(), one.size());
    CHECK(blocks[0] == 0 && blocks[1] == 1 && blocks[4] == 0 && blocks[5] == 1);  // density == 0 is air (> 0 is stone)
    const std::vector<ColumnRun> runs = columnRuns(blocks.data(), 0, 0, -3, 6);
    CHECK(runs.size() == 4 && runs[0].topY == 2 && runs[0].bottomY == 2 && runs[0].block == 1 && runs[1].topY == 1 && runs[1].bottomY == 0 && runs[1].block == 0 &&
          runs[2].topY == -1 && runs[2].bottomY == -2 && runs[2].block == 1 && runs[3].topY == -3 && runs[3].bottomY == -3 && runs[3].block == 0);
    const float two[2] = {1.0f, -0.0f};
    CHECK(densityChecksum(two, 2) == ((0xcbf29ce484222325ull ^ 0x3f800000ull) * 0x100000001b3ull ^ 0x80000000ull) * 0x100000001b3ull);
}

}  // namespace

int main() {    std::printf("worldgen tests\n");
    testRandomSources();
    testLegacyRandom();
    testPerlin();
    testBlendedNoise();
    testJsonAndParameters();
    testSyntheticGraphs();
    testFillHelpers();

    const apex::AssetSearchResult assets = apex::locateAssets(std::nullopt);
    if (!assets.location) {
        std::printf("  (tests that need the game's worldgen data skipped: game assets not found)\n");
    } else {
        const std::filesystem::path worldgen = worldgenDirFromAssetsRoot(assets.location->root);
        if (!std::filesystem::exists(worldgen / "noise_settings" / "overworld.json")) {
            std::printf("  (tests that need the game's worldgen data skipped: %s has no noise_settings/overworld.json)\n", worldgen.string().c_str());
        } else {
            testNormalNoise(worldgen);
            testRouter(worldgen);
        }
    }
    std::printf("%ld checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
