// D:/Projects/Falcon/tests/worldgen tests: router.
#include "test_worldgen_support.hpp"

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
