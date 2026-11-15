// D:/Projects/Falcon/tests/worldgen tests (the others are in D:/Projects/Falcon/tests/test_worldgen_*.cpp).
#include "test_worldgen_support.hpp"

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


int main() {    std::printf("worldgen tests\n");
    testRandomSources();
    testLegacyRandom();
    testPerlin();
    testBlendedNoise();
    testJsonAndParameters();
    testSyntheticGraphs();
    testFillHelpers();

    const falcon::AssetSearchResult assets = falcon::locateAssets(std::nullopt);
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
