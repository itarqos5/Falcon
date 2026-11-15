// D:/Projects/Falcon/tests/worldgen tests: basics.
#include "test_worldgen_support.hpp"

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
