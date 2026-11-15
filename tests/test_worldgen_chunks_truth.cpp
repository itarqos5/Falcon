// D:/Projects/Falcon/tests/worldgen_chunks tests: truth.
#include "test_worldgen_chunks_support.hpp"

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& d) : d_(d) {}
    [[nodiscard]] std::uint32_t var() {
        std::uint32_t v = 0;
        int shift = 0;
        while (pos_ < d_.size()) {
            const std::uint8_t b = d_[pos_++];
            v |= static_cast<std::uint32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
            shift += 7;
        }
        bad_ = true;
        return 0;
    }
    [[nodiscard]] int zig() {
        const std::uint32_t v = var();
        return static_cast<int>(v >> 1) ^ -static_cast<int>(v & 1);
    }
    [[nodiscard]] std::uint8_t byte() { return pos_ < d_.size() ? d_[pos_++] : (bad_ = true, std::uint8_t{0}); }
    [[nodiscard]] std::string str() {
        const std::uint32_t n = var();
        std::string s;
        for (std::uint32_t i = 0; i < n; ++i) s += static_cast<char>(byte());
        return s;
    }
    [[nodiscard]] bool bad() const { return bad_; }

private:
    const std::vector<std::uint8_t>& d_;
    std::size_t pos_ = 0;
    bool bad_ = false;
};

bool loadTruth(const std::filesystem::path& file, Truth& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 5 || std::memcmp(data.data(), "FWGC", 4) != 0 || data[4] != 1) return false;
    std::vector<std::uint8_t> body(data.begin() + 5, data.end());
    Reader r(body);
    const std::uint32_t pal = r.var();
    for (std::uint32_t i = 0; i < pal; ++i) out.palette.push_back(r.str());
    const std::uint32_t bio = r.var();
    for (std::uint32_t i = 0; i < bio; ++i) out.biomes.push_back(r.str());
    const std::uint32_t count = r.var();
    for (std::uint32_t c = 0; c < count && !r.bad(); ++c) {
        TruthChunk ch;
        ch.cx = r.zig();
        ch.cz = r.zig();
        for (int pass = 0; pass < 2; ++pass) {
            const bool surf = pass == 1;
            if (r.byte() == 0) continue;
            std::vector<TruthColumn>& cols = surf ? ch.surf : ch.fill;
            cols.resize(256);
            for (TruthColumn& col : cols) {
                col.top = r.zig();
                if (surf) col.biome = static_cast<int>(r.var());
                const std::uint32_t runs = r.var();
                for (std::uint32_t i = 0; i < runs; ++i) {
                    const int len = static_cast<int>(r.var());
                    const int p = static_cast<int>(r.var());
                    col.runs.emplace_back(len, p);
                }
            }
            (surf ? ch.hasSurf : ch.hasFill) = true;
        }
        out.chunks.push_back(std::move(ch));
    }
    return !r.bad() && out.chunks.size() == count;
}


struct CompareStats {
    long blocks = 0;
    long mismatches = 0;         // blocks that differ in a column where no biome tie can explain it: failures
    long tieMismatches = 0;      // blocks that differ in a column that reads a climate cell with an exact fitness tie (see below)
    long tieColumns = 0;
    long tieExplained = 0;       // of those columns: equal to the dump when the tie cells are flipped to the other tied biome
    long tieUnexplained = 0;
    long heightMismatches = 0;
    long biomeMismatches = 0;
    long biomeTies = 0;
};

// An exact fitness tie between leaves of different biomes: the game's R-tree then answers with whichever leaf its thread answered
// last, which no port can reproduce (the research spec: ~0.03% of cells).
// A quart cell of the climate grid that BiomeManager's zoom reads for a column, with an exact fitness tie between two biomes.
struct TieCell {
    std::array<int, 3> key;  // quart x, y (clamped), z
    BiomeId canonical = kNoBiome, other = kNoBiome;
};

// The tie cells among the cells that the zoom picks for some y of the column at (x, z).
std::vector<TieCell> columnTieCells(const OverworldGenerator& gen, int x, int z) {
    const BiomeSource& src = gen.biomeSource();
    BiomeSource::Context ctx(src);
    std::set<std::array<int, 3>> seen;
    std::vector<TieCell> out;
    for (int y = falcon::kMinY; y <= falcon::kMaxY; ++y) {
        const BiomeSource::QuartPos q = src.zoom(x, y, z);
        const int qy = std::clamp(q.y, -16, 79);
        if (!seen.insert({q.x, qy, q.z}).second) continue;
        const climate::TargetPoint t = src.sampleClimate(ctx, q.x, qy, q.z);
        const BiomeId canonical = src.noiseBiome(ctx, q.x, qy, q.z);
        std::int64_t best = INT64_MAX;
        BiomeId other = kNoBiome;
        for (std::size_t i = 0; i < src.parameters().points.size(); ++i) {
            const std::int64_t f = climate::fitness(src.parameters().points[i], t);
            if (f < best) {
                best = f;
                other = kNoBiome;
            }
            if (f == best && src.parameters().biomes[i] != canonical && other == kNoBiome) other = src.parameters().biomes[i];
        }
        if (other != kNoBiome) out.push_back({{q.x, qy, q.z}, canonical, other});
    }
    return out;
}

bool columnHasBiomeTie(const OverworldGenerator& gen, int x, int z) { return !columnTieCells(gen, x, z).empty(); }

// The biomes of the generator, except that the given tie cells answer with the other tied biome. If the game's thread had
// answered that way, the column would look like the dump.
class TieFlipLookup final : public BiomeLookup {
public:
    TieFlipLookup(const BiomeSource& src, BiomeSource::Context& ctx, std::map<std::array<int, 3>, BiomeId>& cache, const std::vector<TieCell>& flipped) : src_(src), ctx_(ctx), cache_(cache) {
        for (const TieCell& c : flipped) flips_[c.key] = c.other;
    }
    BiomeId biomeAt(int x, int y, int z) override {
        const BiomeSource::QuartPos q = src_.zoom(x, y, z);
        const int qy = std::clamp(q.y, -16, 79);
        const std::array<int, 3> key{q.x, qy, q.z};
        const auto flip = flips_.find(key);
        if (flip != flips_.end()) return flip->second;
        const auto [it, inserted] = cache_.try_emplace(key, kNoBiome);
        if (inserted) it->second = src_.noiseBiome(ctx_, q.x, qy, q.z);
        return it->second;
    }

private:
    const BiomeSource& src_;
    BiomeSource::Context& ctx_;
    std::map<std::array<int, 3>, BiomeId>& cache_;
    std::map<std::array<int, 3>, BiomeId> flips_;
};

// The biomes that tie for the minimum fitness in a quart cell, other than the one the generator answers with.
std::vector<BiomeId> tiedAlternatives(const BiomeSource& src, BiomeSource::Context& ctx, const std::array<int, 3>& key, BiomeId canonical) {
    const climate::TargetPoint t = src.sampleClimate(ctx, key[0], key[1], key[2]);
    std::int64_t best = INT64_MAX;
    std::vector<BiomeId> out;
    for (std::size_t i = 0; i < src.parameters().points.size(); ++i) {
        const std::int64_t f = climate::fitness(src.parameters().points[i], t);
        if (f < best) {
            best = f;
            out.clear();
        }
        const BiomeId b = src.parameters().biomes[i];
        if (f == best && b != canonical && std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
    }
    return out;
}

// Why does a column differ from the dump? Only the game's R-tree can answer a tie between two biomes either way. This builds
// the surface of the chunk again with the zoom's quart cells for the differing blocks answering one of the other tied biomes,
// cell by cell: the column must then equal the dump. A difference that no tie answer repairs is a real bug.
bool explainColumnWithTieFlips(const OverworldGenerator& gen, const TruthChunk& tc, const std::vector<TruthColumn>& cols, const std::vector<falcon::Block>& pal, int column,
                               const DenseChunk& fill, const std::vector<float>& surfaceLevel, BiomeSource::Context& ctx, std::map<std::array<int, 3>, BiomeId>& cache) {
    const BiomeSource& src = gen.biomeSource();
    const int x = tc.cx * 16 + (column & 15), z = tc.cz * 16 + (column >> 4);
    const TruthColumn& col = cols[static_cast<std::size_t>(column)];
    const auto build = [&](const std::vector<TieCell>& flips, DenseChunk& d) {
        d = fill;
        TieFlipLookup lookup(src, ctx, cache, flips);
        SurfaceTarget target;
        target.chunkX = tc.cx;
        target.chunkZ = tc.cz;
        target.blocks = d.blocks.data();
        target.worldSurface = d.worldSurface.data();
        target.chunkSurfaceLevel = surfaceLevel.data();
        target.biomes = &lookup;
        MaterialSystem::Workspace ws;
        gen.material().buildSurface(ws, target);
    };
    const auto differingY = [&](const DenseChunk& d) {
        std::vector<int> ys;
        const falcon::Block* got = &d.blocks[static_cast<std::size_t>(falcon::kWorldHeight) * static_cast<std::size_t>(column)];
        int y = 319;
        for (const auto& [len, p] : col.runs) {
            for (int i = 0; i < len; ++i, --y) {
                if (got[y - falcon::kMinY] != pal[static_cast<std::size_t>(p)]) ys.push_back(y);
            }
        }
        return ys;
    };
    DenseChunk d;
    build({}, d);
    const std::vector<int> bad = differingY(d);
    if (bad.empty()) return true;
    std::map<std::array<int, 3>, std::vector<int>> byCell;  // the quart cells the differing blocks read
    for (const int y : bad) {
        const BiomeSource::QuartPos q = src.zoom(x, y, z);
        byCell[{q.x, std::clamp(q.y, -16, 79), q.z}].push_back(y);
    }
    std::vector<TieCell> accepted;
    for (const auto& [key, ys] : byCell) {
        const BiomeId canonical = src.noiseBiome(ctx, key[0], key[1], key[2]);
        bool repaired = false;
        for (const BiomeId alt : tiedAlternatives(src, ctx, key, canonical)) {
            std::vector<TieCell> trial = accepted;
            trial.push_back({key, canonical, alt});
            build(trial, d);
            const std::vector<int> still = differingY(d);
            if (std::none_of(ys.begin(), ys.end(), [&](int y) { return std::find(still.begin(), still.end(), y) != still.end(); })) {
                accepted = trial;
                repaired = true;
                break;
            }
        }
        if (!repaired) return false;
    }
    build(accepted, d);
    return differingY(d).empty();
}

long explainWithTieFlip(const OverworldGenerator& gen, const TruthChunk& tc, const std::vector<TruthColumn>& cols, const std::vector<falcon::Block>& pal, const std::vector<int>& columns) {
    DenseChunk fill;
    gen.generateDense(tc.cx, tc.cz, OverworldGenerator::Stage::Fill, fill);
    std::vector<float> surfaceLevel(256);
    gen.router().chunkSurfaceLevelVolume(tc.cx, tc.cz, surfaceLevel.data());
    BiomeSource::Context ctx(gen.biomeSource());
    std::map<std::array<int, 3>, BiomeId> cache;
    long explained = 0;
    for (const int c : columns) {
        if (explainColumnWithTieFlips(gen, tc, cols, pal, c, fill, surfaceLevel, ctx, cache)) ++explained;
        else std::fprintf(stderr, "  SURF chunk %d,%d column x=%d z=%d differs from the dump and no tie answer repairs it\n", tc.cx, tc.cz, tc.cx * 16 + (c & 15), tc.cz * 16 + (c >> 4));
    }
    return explained;
}


// Compares one stage's dense output with the dump. Prints the first few mismatches of a chunk.
void compareStage(const OverworldGenerator& gen, const falcon::BlockRegistry& registry, const std::vector<falcon::Block>& pal, const char* stage, const TruthChunk& tc,
                  const std::vector<TruthColumn>& cols, const DenseChunk& dense, CompareStats& st, const std::vector<std::string>* biomeNames) {
    int shown = 0;
    long chunkMismatches = 0;
    const int minY = falcon::kMinY;
    std::vector<int> excused;
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const TruthColumn& col = cols[static_cast<std::size_t>(x + 16 * z)];
            const falcon::Block* got = &dense.blocks[static_cast<std::size_t>(falcon::kWorldHeight) * static_cast<std::size_t>(x + 16 * z)];
            int y = 319;
            long columnMismatches = 0;
            int firstBadY = 0;
            std::vector<std::string> lines;
            for (const auto& [len, p] : col.runs) {
                for (int i = 0; i < len; ++i, --y) {
                    ++st.blocks;
                    const falcon::Block want = pal[static_cast<std::size_t>(p)];
                    const falcon::Block have = got[y - minY];
                    if (want != have) {
                        if (columnMismatches++ == 0) firstBadY = y;
                        if (lines.size() < 3) {
                            char buf[256];
                            std::snprintf(buf, sizeof buf, "  %s chunk %d,%d x=%d y=%d z=%d: want %s got %s\n", stage, tc.cx, tc.cz, tc.cx * 16 + x, y, tc.cz * 16 + z,
                                          registry.describe(want).c_str(), registry.describe(have).c_str());
                            lines.emplace_back(buf);
                        }
                    }
                }
            }
            if (columnMismatches > 0) {
                if (biomeNames && columnHasBiomeTie(gen, tc.cx * 16 + x, tc.cz * 16 + z)) {  // only the surface stage reads biomes
                    st.tieMismatches += columnMismatches;
                    ++st.tieColumns;
                    excused.push_back(x + 16 * z);  // judged below: a tie answer must repair it
                } else {
                    chunkMismatches += columnMismatches;
                    st.mismatches += columnMismatches;
                    std::fprintf(stderr, "  %s chunk %d,%d column x=%d z=%d: %ld blocks differ (first at y=%d), no biome tie in this column\n", stage, tc.cx, tc.cz, tc.cx * 16 + x,
                                 tc.cz * 16 + z, columnMismatches, firstBadY);
                    for (const std::string& l : lines) std::fputs(l.c_str(), stderr);
                }
            }
            const int top = dense.worldSurface[static_cast<std::size_t>(x + 16 * z)] - 1;
            if (top != col.top) {
                ++st.heightMismatches;
                if (shown++ < 6) std::fprintf(stderr, "  %s chunk %d,%d x=%d z=%d: heightmap top want %d got %d\n", stage, tc.cx, tc.cz, tc.cx * 16 + x, tc.cz * 16 + z, col.top, top);
            }
            if (biomeNames) {
                const std::string& want = (*biomeNames)[static_cast<std::size_t>(col.biome)];
                const BiomeId have = gen.biomeAt(tc.cx * 16 + x, col.top + 1, tc.cz * 16 + z);
                if (gen.biomes().name(have) != want) {
                    // An exact fitness tie between two leaves of the parameter list is nondeterministic in the game itself.
                    const BiomeSource& src = gen.biomeSource();
                    BiomeSource::Context ctx(src);
                    const BiomeSource::QuartPos q = src.zoom(tc.cx * 16 + x, col.top + 1, tc.cz * 16 + z);
                    const int qy = std::clamp(q.y, -16, 79);
                    const climate::TargetPoint t = src.sampleClimate(ctx, q.x, qy, q.z);
                    std::int64_t bestAll = INT64_MAX, bestWant = INT64_MAX;
                    const BiomeId wantId = gen.biomes().find(want);
                    for (std::size_t i = 0; i < src.parameters().points.size(); ++i) {
                        const std::int64_t f = climate::fitness(src.parameters().points[i], t);
                        bestAll = std::min(bestAll, f);
                        if (src.parameters().biomes[i] == wantId) bestWant = std::min(bestWant, f);
                    }
                    if (bestAll == bestWant) {
                        ++st.biomeTies;
                    } else {
                        ++st.biomeMismatches;
                        if (shown++ < 6) {
                            std::fprintf(stderr, "  %s chunk %d,%d x=%d z=%d: biome at y=%d want %s got %s\n", stage, tc.cx, tc.cz, tc.cx * 16 + x, tc.cz * 16 + z, col.top + 1, want.c_str(),
                                         gen.biomes().name(have).c_str());
                        }
                    }
                }
            }
        }
    }
    if (chunkMismatches > 0) std::fprintf(stderr, "  %s chunk %d,%d: %ld block mismatches\n", stage, tc.cx, tc.cz, chunkMismatches);
    if (!excused.empty() && biomeNames) {
        const long explained = explainWithTieFlip(gen, tc, cols, pal, excused);
        st.tieExplained += explained;
        st.tieUnexplained += static_cast<long>(excused.size()) - explained;
    }
}

void testChunks(const OverworldGenerator& gen, const falcon::BlockRegistry& registry, const Truth& truth) {
    std::vector<falcon::Block> pal;
    for (const std::string& name : truth.palette) {
        const auto b = registry.parse(name);
        if (!b) {
            std::fprintf(stderr, "block registry cannot parse %s\n", name.c_str());
            ++g_failures;
            return;
        }
        pal.push_back(*b);
    }
    CompareStats fillStats, surfStats;
    DenseChunk dense;
    double fillMs = 0.0, surfMs = 0.0;
    int fillChunks = 0, surfChunks = 0;
    for (const TruthChunk& tc : truth.chunks) {
        if (tc.hasFill) {
            const auto t0 = std::chrono::steady_clock::now();
            gen.generateDense(tc.cx, tc.cz, OverworldGenerator::Stage::Fill, dense);
            fillMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++fillChunks;
            compareStage(gen, registry, pal, "FILL", tc, tc.fill, dense, fillStats, nullptr);
        }
        if (tc.hasSurf) {
            const auto t0 = std::chrono::steady_clock::now();
            gen.generateDense(tc.cx, tc.cz, OverworldGenerator::Stage::Surface, dense);
            surfMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++surfChunks;
            compareStage(gen, registry, pal, "SURF", tc, tc.surf, dense, surfStats, &truth.biomes);
        }
    }
    std::printf("  fill:    %d chunks, %ld blocks compared, %ld mismatches, %ld heightmap mismatches (%.1f ms per chunk)\n", fillChunks, fillStats.blocks, fillStats.mismatches,
                fillStats.heightMismatches, fillMs / std::max(1, fillChunks));
    std::printf("  surface: %d chunks, %ld blocks compared, %ld mismatches, %ld heightmap mismatches, %ld biome mismatches (+%ld exact-tie biome differences) (%.1f ms per chunk)\n",
                surfChunks, surfStats.blocks, surfStats.mismatches, surfStats.heightMismatches, surfStats.biomeMismatches, surfStats.biomeTies, surfMs / std::max(1, surfChunks));
    std::printf("  surface: %ld further block differences in %ld columns that read a climate cell with an exact fitness tie (the game's answer there depends on its thread's previous query);\n"
                "           with the tie cells answering the other way, %ld of those columns equal the dump (%ld do not)\n",
                surfStats.tieMismatches, surfStats.tieColumns, surfStats.tieExplained, surfStats.tieUnexplained);
    checkInt("surface: columns with a biome tie that the other tie answer does not explain", surfStats.tieUnexplained, 0);
    checkInt("fill: chunk dumps compared", fillChunks, 39);
    checkInt("surface: chunk dumps compared", surfChunks, 39);
    checkInt("fill: block mismatches", fillStats.mismatches, 0);
    checkInt("fill: WORLD_SURFACE_WG height mismatches", fillStats.heightMismatches, 0);
    checkInt("surface: block mismatches", surfStats.mismatches, 0);
    checkInt("surface: WORLD_SURFACE_WG height mismatches", surfStats.heightMismatches, 0);
    checkInt("surface: biome mismatches (excluding exact fitness ties)", surfStats.biomeMismatches, 0);
}
