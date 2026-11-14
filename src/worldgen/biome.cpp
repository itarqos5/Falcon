#include "apex/worldgen/biome.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace apex::worldgen {

// ===============================================================================================================
// SHA-256
namespace {

constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu,
    0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau,
    0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u,
    0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr32(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256Block(std::uint32_t h[8], const std::uint8_t* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) | (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = hh + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

}  // namespace

std::array<std::uint8_t, 32> sha256(const void* data, std::size_t size) {
    std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t done = 0;
    while (size - done >= 64) {
        sha256Block(h, bytes + done);
        done += 64;
    }
    std::uint8_t tail[128] = {};
    const std::size_t rest = size - done;
    if (rest > 0) std::memcpy(tail, bytes + done, rest);
    tail[rest] = 0x80;
    const std::size_t total = rest + 1 + 8 <= 64 ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8u;
    for (int i = 0; i < 8; ++i) tail[total - 1 - static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (8 * i));
    sha256Block(h, tail);
    if (total == 128) sha256Block(h, tail + 64);
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<std::size_t>(i) * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
}

std::int64_t obfuscateSeed(std::int64_t seed) {
    std::uint8_t bytes[8];
    const auto u = static_cast<std::uint64_t>(seed);
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(u >> (8 * i));
    const auto digest = sha256(bytes, 8);
    std::uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= static_cast<std::uint64_t>(digest[static_cast<std::size_t>(i)]) << (8 * i);
    return static_cast<std::int64_t>(r);
}

// ===============================================================================================================
// Biome registry
namespace {

std::string withNamespace(std::string_view name) {
    if (name.find(':') != std::string_view::npos) return std::string(name);
    return "minecraft:" + std::string(name);
}

// "#rrggbb" (or a plain integer) -> 0xRRGGBB
void readColor(const JsonValue* v, std::optional<std::uint32_t>& out) {
    if (!v) return;
    if (v->isString()) {
        const std::string& s = v->text;
        if (s.size() == 7 && s[0] == '#') {
            std::uint32_t c = 0;
            for (std::size_t i = 1; i < s.size(); ++i) {
                const char ch = s[i];
                std::uint32_t d;
                if (ch >= '0' && ch <= '9') d = static_cast<std::uint32_t>(ch - '0');
                else if (ch >= 'a' && ch <= 'f') d = static_cast<std::uint32_t>(ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') d = static_cast<std::uint32_t>(ch - 'A' + 10);
                else return;
                c = c * 16 + d;
            }
            out = c;
        }
    } else if (v->isNumber()) {
        std::int32_t i = 0;
        if (v->toInt(i)) out = static_cast<std::uint32_t>(i) & 0xFFFFFFu;
    }
}

bool parseBiome(const JsonValue& js, BiomeInfo& b) {
    if (!js.isObject()) return false;
    float f = 0.0f;
    if (const JsonValue* t = js.find("temperature")) {
        if (t->toFloat(f)) b.temperature = f;
    }
    if (const JsonValue* d = js.find("downfall")) {
        if (d->toFloat(f)) b.downfall = f;
    }
    if (const JsonValue* m = js.find("temperature_modifier")) {
        if (m->isString() && m->text == "frozen") b.temperatureModifier = TemperatureModifier::Frozen;
    }
    if (const JsonValue* p = js.find("has_precipitation")) b.hasPrecipitation = p->kind == JsonValue::Kind::Bool && p->boolean;
    if (const JsonValue* e = js.find("effects"); e && e->isObject()) {
        readColor(e->find("water_color"), b.waterColor);
        readColor(e->find("foliage_color"), b.foliageColor);
        readColor(e->find("dry_foliage_color"), b.dryFoliageColor);
        readColor(e->find("grass_color"), b.grassColor);
        if (const JsonValue* m = e->find("grass_color_modifier"); m && m->isString()) {
            if (m->text == "dark_forest") b.grassColorModifier = GrassColorModifier::DarkForest;
            else if (m->text == "swamp") b.grassColorModifier = GrassColorModifier::Swamp;
        }
    }
    if (const JsonValue* a = js.find("attributes"); a && a->isObject()) {
        readColor(a->find("minecraft:visual/sky_color"), b.skyColor);
        readColor(a->find("minecraft:visual/fog_color"), b.fogColor);
        readColor(a->find("minecraft:visual/water_fog_color"), b.waterFogColor);
    }
    return true;
}

}  // namespace

std::shared_ptr<const BiomeRegistry> BiomeRegistry::load(const std::filesystem::path& biomeDir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(biomeDir, ec)) {
        error = "biome directory not found: " + biomeDir.string();
        return nullptr;
    }
    std::vector<std::pair<std::string, fs::path>> files;
    for (fs::recursive_directory_iterator it(biomeDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".json") continue;
        std::string rel = fs::relative(it->path(), biomeDir, ec).generic_string();
        rel.resize(rel.size() - 5);  // ".json"
        files.emplace_back("minecraft:" + rel, it->path());
    }
    if (files.empty()) {
        error = "no biome files in " + biomeDir.string();
        return nullptr;
    }
    if (files.size() > 255) {
        error = "more than 255 biomes are not supported (chunk biome storage is one byte per cell)";
        return nullptr;
    }
    std::sort(files.begin(), files.end());
    auto reg = std::shared_ptr<BiomeRegistry>(new BiomeRegistry());
    for (const auto& [name, path] : files) {
        JsonValue js;
        if (!parseJsonFile(path, js, error)) return nullptr;
        BiomeInfo info;
        info.name = name;
        if (!parseBiome(js, info)) {
            error = path.string() + ": not a biome object";
            return nullptr;
        }
        reg->byName_.emplace_back(name, static_cast<BiomeId>(reg->biomes_.size()));
        reg->biomes_.push_back(std::move(info));
    }
    return reg;
}

BiomeId BiomeRegistry::find(std::string_view name) const {
    const std::string full = withNamespace(name);
    const auto it = std::lower_bound(byName_.begin(), byName_.end(), full, [](const auto& e, const std::string& s) { return e.first < s; });
    return (it != byName_.end() && it->first == full) ? it->second : kNoBiome;
}

// ===============================================================================================================
// Climate
namespace climate {

std::int64_t quantize(float f) {
    const float scaled = f * 10000.0f;  // the product is rounded to float, as in the game
    if (scaled != scaled) return 0;
    if (scaled >= 9.2233720368547758e18f) return std::numeric_limits<std::int64_t>::max();
    if (scaled <= -9.2233720368547758e18f) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(scaled);
}

std::int64_t fitness(const ParameterPoint& p, const TargetPoint& t) {
    const std::int64_t tv[kDims] = {t.temperature, t.humidity, t.continentalness, t.erosion, t.depth, t.weirdness, 0};
    std::int64_t sum = 0;
    for (int i = 0; i < kDims; ++i) {
        const std::int64_t d = p.space[i].distance(tv[i]);
        sum += d * d;
    }
    return sum;
}

namespace {

bool readParameter(const JsonValue* v, Parameter& out) {
    if (!v) return false;
    float a = 0.0f, b = 0.0f;
    if (v->isNumber()) {
        if (!v->toFloat(a)) return false;
        out.min = out.max = quantize(a);
        return true;
    }
    if (v->isArray() && v->items.size() == 2 && v->items[0].toFloat(a) && v->items[1].toFloat(b)) {
        out.min = quantize(a);
        out.max = quantize(b);
        return true;
    }
    return false;
}

}  // namespace

bool parseParameterList(const JsonValue& json, const BiomeRegistry& registry, ParameterList& out, std::string& error) {
    const JsonValue* biomes = json.find("biomes");
    if (!biomes || !biomes->isArray()) {
        error = "biome parameter list: \"biomes\" array missing";
        return false;
    }
    out.points.clear();
    out.biomes.clear();
    out.points.reserve(biomes->items.size());
    out.biomes.reserve(biomes->items.size());
    static constexpr const char* kKeys[kDims] = {"temperature", "humidity", "continentalness", "erosion", "depth", "weirdness", "offset"};
    for (const JsonValue& e : biomes->items) {
        const JsonValue* name = e.find("biome");
        const JsonValue* params = e.find("parameters");
        if (!name || !name->isString() || !params || !params->isObject()) {
            error = "biome parameter list: malformed entry";
            return false;
        }
        const BiomeId id = registry.find(name->text);
        if (id == kNoBiome) {
            error = "biome parameter list: unknown biome " + name->text;
            return false;
        }
        ParameterPoint pt;
        for (int i = 0; i < kDims; ++i) {
            if (!readParameter(params->find(kKeys[i]), pt.space[i])) {
                error = std::string("biome parameter list: bad \"") + kKeys[i] + "\" for " + name->text;
                return false;
            }
        }
        out.points.push_back(pt);
        out.biomes.push_back(id);
    }
    return true;
}

bool loadParameterList(const std::filesystem::path& file, const BiomeRegistry& registry, ParameterList& out, std::string& error) {
    JsonValue js;
    if (!parseJsonFile(file, js, error)) return false;
    return parseParameterList(js, registry, out, error);
}

// ---------------------------------------------------------------------------------------------------------------
// R-tree construction, exactly as Climate.RTree.build (java List.sort is a stable sort).
namespace {

struct TNode {
    std::int64_t lo[kDims];
    std::int64_t hi[kDims];
    std::vector<std::int32_t> kids;  // empty for a leaf
};

class TreeBuilder {
public:
    explicit TreeBuilder(const ParameterList& list) {
        arena_.reserve(list.points.size() * 2 + 64);
        for (const ParameterPoint& p : list.points) {
            TNode n;
            for (int i = 0; i < kDims; ++i) {
                n.lo[i] = p.space[i].min;
                n.hi[i] = p.space[i].max;
            }
            arena_.push_back(std::move(n));
        }
    }

    [[nodiscard]] std::int32_t buildAll(std::size_t count) {
        std::vector<std::int32_t> nodes(count);
        for (std::size_t i = 0; i < count; ++i) nodes[i] = static_cast<std::int32_t>(i);
        return build(std::move(nodes));
    }
    [[nodiscard]] const TNode& node(std::int32_t i) const { return arena_[static_cast<std::size_t>(i)]; }

private:
    [[nodiscard]] std::int64_t mid(std::int32_t n, int d) const {
        const TNode& t = arena_[static_cast<std::size_t>(n)];
        return (t.lo[d] + t.hi[d]) / 2;  // truncating division, as Java's long division
    }

    std::int32_t makeSubTree(std::vector<std::int32_t> kids) {
        TNode n;
        for (int d = 0; d < kDims; ++d) {
            std::int64_t mn = std::numeric_limits<std::int64_t>::max(), mx = std::numeric_limits<std::int64_t>::min();
            for (const std::int32_t k : kids) {
                const TNode& c = arena_[static_cast<std::size_t>(k)];
                mn = std::min(mn, c.lo[d]);
                mx = std::max(mx, c.hi[d]);
            }
            n.lo[d] = mn;
            n.hi[d] = mx;
        }
        n.kids = std::move(kids);
        arena_.push_back(std::move(n));
        return static_cast<std::int32_t>(arena_.size() - 1);
    }

    void sortNodes(std::vector<std::int32_t>& nodes, int dim, bool absolute) const {
        std::stable_sort(nodes.begin(), nodes.end(), [&](std::int32_t a, std::int32_t b) {
            for (int i = 0; i < kDims; ++i) {
                const int axis = (dim + i) % kDims;
                std::int64_t ka = mid(a, axis), kb = mid(b, axis);
                if (absolute) {
                    ka = ka < 0 ? -ka : ka;
                    kb = kb < 0 ? -kb : kb;
                }
                if (ka != kb) return ka < kb;
            }
            return false;
        });
    }

    std::vector<std::int32_t> bucketize(const std::vector<std::int32_t>& nodes) {
        // size = (int) Math.pow(19, Math.floor(Math.log(n - 0.01) / Math.log(19)))   (the exponent is a whole number)
        const int e = static_cast<int>(std::floor(std::log(static_cast<double>(nodes.size()) - 0.01) / std::log(static_cast<double>(RTree::kChildrenPerNode))));
        std::size_t size = 1;
        for (int i = 0; i < e; ++i) size *= static_cast<std::size_t>(RTree::kChildrenPerNode);
        std::vector<std::int32_t> out;
        std::vector<std::int32_t> cur;
        for (const std::int32_t n : nodes) {
            cur.push_back(n);
            if (cur.size() >= size) {
                out.push_back(makeSubTree(std::move(cur)));
                cur.clear();
            }
        }
        if (!cur.empty()) out.push_back(makeSubTree(std::move(cur)));
        return out;
    }

    std::int32_t build(std::vector<std::int32_t> nodes) {
        if (nodes.size() == 1) return nodes[0];
        if (nodes.size() <= static_cast<std::size_t>(RTree::kChildrenPerNode)) {
            const auto cost = [&](std::int32_t n) {
                std::int64_t s = 0;
                for (int d = 0; d < kDims; ++d) {
                    const std::int64_t m = mid(n, d);
                    s += m < 0 ? -m : m;
                }
                return s;
            };
            std::stable_sort(nodes.begin(), nodes.end(), [&](std::int32_t a, std::int32_t b) { return cost(a) < cost(b); });
            return makeSubTree(std::move(nodes));
        }
        std::int64_t minCost = std::numeric_limits<std::int64_t>::max();
        int minDim = -1;
        std::vector<std::int32_t> minBuckets;
        for (int d = 0; d < kDims; ++d) {
            sortNodes(nodes, d, false);
            std::vector<std::int32_t> buckets = bucketize(nodes);
            std::int64_t cost = 0;
            for (const std::int32_t b : buckets) {
                const TNode& t = arena_[static_cast<std::size_t>(b)];
                for (int k = 0; k < kDims; ++k) {
                    const std::int64_t span = t.hi[k] - t.lo[k];
                    cost += span < 0 ? -span : span;
                }
            }
            if (minCost > cost) {
                minCost = cost;
                minDim = d;
                minBuckets = std::move(buckets);
            }
        }
        sortNodes(minBuckets, minDim, true);
        std::vector<std::int32_t> kids;
        kids.reserve(minBuckets.size());
        for (const std::int32_t b : minBuckets) {
            std::vector<std::int32_t> children = arena_[static_cast<std::size_t>(b)].kids;
            kids.push_back(build(std::move(children)));
        }
        return makeSubTree(std::move(kids));
    }

    std::vector<TNode> arena_;
};

}  // namespace

RTree::RTree(const ParameterList& list) {
    leafCount_ = list.points.size();
    if (leafCount_ == 0) return;
    TreeBuilder builder(list);
    const std::int32_t rootArena = builder.buildAll(leafCount_);

    nodes_.resize(leafCount_);
    for (std::size_t i = 0; i < leafCount_; ++i) {
        Node& n = nodes_[i];
        const TNode& t = builder.node(static_cast<std::int32_t>(i));
        std::memcpy(n.lo, t.lo, sizeof n.lo);
        std::memcpy(n.hi, t.hi, sizeof n.hi);
        n.biome = list.biomes[i];
    }
    // Flatten: sub-trees after the leaves, each with its children contiguous in childIndex_.
    struct Flatten {
        const TreeBuilder& b;
        std::vector<Node>& nodes;
        std::vector<std::int32_t>& childIndex;
        std::size_t leaves;
        int maxDepth = 0;
        std::int32_t nextRank = 0;
        std::int32_t run(std::int32_t a, int depth) {
            maxDepth = std::max(maxDepth, depth);
            if (static_cast<std::size_t>(a) < leaves) {
                nodes[static_cast<std::size_t>(a)].minRank = nextRank++;  // depth-first visiting order of the leaves
                return a;
            }
            const TNode& t = b.node(a);
            std::vector<std::int32_t> kids;
            kids.reserve(t.kids.size());
            for (const std::int32_t k : t.kids) kids.push_back(run(k, depth + 1));
            Node n;
            std::memcpy(n.lo, t.lo, sizeof n.lo);
            std::memcpy(n.hi, t.hi, sizeof n.hi);
            n.minRank = nodes[static_cast<std::size_t>(kids.front())].minRank;  // the first child holds the lowest ranks
            n.first = static_cast<std::int32_t>(childIndex.size());
            n.count = static_cast<std::int32_t>(kids.size());
            childIndex.insert(childIndex.end(), kids.begin(), kids.end());
            nodes.push_back(n);
            return static_cast<std::int32_t>(nodes.size() - 1);
        }
    } flatten{builder, nodes_, childIndex_, leafCount_};
    root_ = flatten.run(rootArena, 0);
    depth_ = flatten.maxDepth;
}

std::int64_t RTree::distance(const Node& n, const std::int64_t* t) {
    std::int64_t sum = 0;
    for (int k = 0; k < kDims; ++k) {
        const std::int64_t d1 = t[k] - n.hi[k];
        const std::int64_t d2 = n.lo[k] - t[k];
        const std::int64_t d = d1 > 0 ? d1 : (d2 > 0 ? d2 : 0);
        sum += d * d;
    }
    return sum;
}

std::int32_t RTree::searchNode(std::int32_t node, const std::int64_t* t, std::int32_t best) const {
    const Node& n = nodes_[static_cast<std::size_t>(node)];
    if (n.count == 0) return node;
    std::int64_t bestDist = best < 0 ? std::numeric_limits<std::int64_t>::max() : distance(nodes_[static_cast<std::size_t>(best)], t);
    std::int32_t bestLeaf = best;
    const std::int32_t* kids = &childIndex_[static_cast<std::size_t>(n.first)];
    for (std::int32_t i = 0; i < n.count; ++i) {
        const std::int32_t child = kids[i];
        const std::int64_t d = distance(nodes_[static_cast<std::size_t>(child)], t);
        if (bestDist > d) {  // strictly greater: ties never replace, boxes at exactly bestDist are pruned
            const std::int32_t leaf = searchNode(child, t, bestLeaf);
            const std::int64_t leafDist = (child == leaf) ? d : distance(nodes_[static_cast<std::size_t>(leaf)], t);
            if (bestDist > leafDist) {
                bestDist = leafDist;
                bestLeaf = leaf;
            }
        }
    }
    return bestLeaf;
}

std::int32_t RTree::searchLeaf(const TargetPoint& t, RTreeCache& cache) const {
    if (root_ < 0) return -1;
    const std::int64_t tv[kDims] = {t.temperature, t.humidity, t.continentalness, t.erosion, t.depth, t.weirdness, 0};
    const std::int32_t leaf = searchNode(root_, tv, cache.lastLeaf);
    cache.lastLeaf = leaf;
    return leaf;
}

BiomeId RTree::search(const TargetPoint& t, RTreeCache& cache) const {
    const std::int32_t leaf = searchLeaf(t, cache);
    return leaf < 0 ? kNoBiome : nodes_[static_cast<std::size_t>(leaf)].biome;
}

// The minimum over (fitness, depth-first rank): a subtree box can only hold a better pair when its distance is smaller, or
// equal while it holds a leaf of a lower rank than the best so far.
void RTree::descendCanonical(std::int32_t node, const std::int64_t* t, std::int32_t& best, std::int64_t& bestDist) const {
    const Node& n = nodes_[static_cast<std::size_t>(node)];
    if (n.count == 0) {
        const std::int64_t d = distance(n, t);
        if (d < bestDist || (d == bestDist && (best < 0 || n.minRank < nodes_[static_cast<std::size_t>(best)].minRank))) {
            best = node;
            bestDist = d;
        }
        return;
    }
    const std::int32_t* kids = &childIndex_[static_cast<std::size_t>(n.first)];
    for (std::int32_t i = 0; i < n.count; ++i) {
        const Node& c = nodes_[static_cast<std::size_t>(kids[i])];
        const std::int64_t d = distance(c, t);
        if (d < bestDist || (d == bestDist && (best < 0 || c.minRank < nodes_[static_cast<std::size_t>(best)].minRank))) descendCanonical(kids[i], t, best, bestDist);
    }
}

std::int32_t RTree::searchLeafCanonical(const TargetPoint& t, RTreeCache& cache) const {
    if (root_ < 0) return -1;
    const std::int64_t tv[kDims] = {t.temperature, t.humidity, t.continentalness, t.erosion, t.depth, t.weirdness, 0};
    std::int32_t best = cache.lastLeaf;
    std::int64_t bestDist = best < 0 ? std::numeric_limits<std::int64_t>::max() : distance(nodes_[static_cast<std::size_t>(best)], tv);
    descendCanonical(root_, tv, best, bestDist);
    cache.lastLeaf = best;
    return best;
}

BiomeId RTree::searchCanonical(const TargetPoint& t, RTreeCache& cache) const { return biomeOfLeaf(searchLeafCanonical(t, cache)); }

std::int32_t RTree::bruteForce(const ParameterList& list, const TargetPoint& t) {
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    std::int32_t index = -1;
    for (std::size_t i = 0; i < list.points.size(); ++i) {
        const std::int64_t f = fitness(list.points[i], t);
        if (f < best) {
            best = f;
            index = static_cast<std::int32_t>(i);
        }
    }
    return index;
}

std::size_t RTree::memoryBytes() const { return nodes_.capacity() * sizeof(Node) + childIndex_.capacity() * sizeof(std::int32_t); }

}  // namespace climate

// ===============================================================================================================
// Climate sampler
ClimateSampler::ClimateSampler(const NoiseRouter& router) : router_(&router), evaluator_(router.graph()) {}

climate::TargetPoint ClimateSampler::sample(int qx, int qy, int qz) {
    evaluator_.setPosition(qx * 4, qy * 4, qz * 4);
    climate::TargetPoint t;
    t.temperature = climate::quantize(evaluator_.value(router_->temperature()));
    t.humidity = climate::quantize(evaluator_.value(router_->vegetation()));
    t.continentalness = climate::quantize(evaluator_.value(router_->continents()));
    t.erosion = climate::quantize(evaluator_.value(router_->erosion()));
    t.depth = climate::quantize(evaluator_.value(router_->depth()));
    t.weirdness = climate::quantize(evaluator_.value(router_->ridges()));
    return t;
}

void ClimateSampler::sampleFloats(int x, int y, int z, float out[6]) {
    evaluator_.setPosition(x, y, z);
    out[0] = evaluator_.value(router_->temperature());
    out[1] = evaluator_.value(router_->vegetation());
    out[2] = evaluator_.value(router_->continents());
    out[3] = evaluator_.value(router_->erosion());
    out[4] = evaluator_.value(router_->depth());
    out[5] = evaluator_.value(router_->ridges());
}

// ===============================================================================================================
// Biome source
namespace {
std::atomic<std::uint64_t> g_nextSourceId{1};
}

BiomeSource::Context::Context(const BiomeSource& source) : sourceId_(source.id_) {
    if (source.router_) sampler_ = std::make_unique<ClimateSampler>(*source.router_);
}

std::unique_ptr<BiomeSource> BiomeSource::create(const std::filesystem::path& parameterFile, std::shared_ptr<const BiomeRegistry> registry,
                                                 const NoiseRouter& router, std::int64_t seed, std::string& error) {
    if (!registry) {
        error = "biome source: no biome registry";
        return nullptr;
    }
    std::unique_ptr<BiomeSource> s(new BiomeSource());
    s->registry_ = std::move(registry);
    s->router_ = &router;
    if (!climate::loadParameterList(parameterFile, *s->registry_, s->list_, error)) return nullptr;
    s->tree_ = climate::RTree(s->list_);
    s->seed_ = seed;
    s->zoomSeed_ = obfuscateSeed(seed);
    s->id_ = g_nextSourceId.fetch_add(1);
    return s;
}

std::unique_ptr<BiomeSource> BiomeSource::createFixed(std::shared_ptr<const BiomeRegistry> registry, BiomeId biome, std::int64_t seed) {
    std::unique_ptr<BiomeSource> s(new BiomeSource());
    s->registry_ = std::move(registry);
    s->fixed_ = biome;
    s->seed_ = seed;
    s->zoomSeed_ = obfuscateSeed(seed);
    s->id_ = g_nextSourceId.fetch_add(1);
    return s;
}

BiomeSource::Context& BiomeSource::threadContext() const {
    thread_local std::vector<std::unique_ptr<Context>> contexts;
    for (const auto& c : contexts) {
        if (c->sourceId() == id_) return *c;
    }
    if (contexts.size() >= 4) contexts.erase(contexts.begin());
    contexts.push_back(std::make_unique<Context>(*this));
    return *contexts.back();
}

climate::TargetPoint BiomeSource::sampleClimate(Context& ctx, int qx, int qy, int qz) const {
    if (!ctx.sampler_) return {};
    return ctx.sampler_->sample(qx, qy, qz);
}

BiomeId BiomeSource::noiseBiome(Context& ctx, int qx, int qy, int qz) const {
    if (fixed_ != kNoBiome) return fixed_;
    qy = std::clamp(qy, ChunkBiomes::kMinQuartY, ChunkBiomes::kMinQuartY + ChunkBiomes::kQuartsY - 1);
    return tree_.searchCanonical(ctx.sampler_->sample(qx, qy, qz), ctx.cache_);
}

BiomeId BiomeSource::noiseBiome(int qx, int qy, int qz) const { return noiseBiome(threadContext(), qx, qy, qz); }

void BiomeSource::fillChunk(Context& ctx, int chunkX, int chunkZ, ChunkBiomes& out) const {
    if (fixed_ != kNoBiome) {
        out.ids.fill(static_cast<std::uint8_t>(fixed_));
        return;
    }
    for (int qz = 0; qz < ChunkBiomes::kQuartsXZ; ++qz) {
        for (int qx = 0; qx < ChunkBiomes::kQuartsXZ; ++qx) {
            std::uint8_t* col = &out.ids[ChunkBiomes::index(qx, 0, qz)];
            for (int i = 0; i < ChunkBiomes::kQuartsY; ++i) {
                col[i] = static_cast<std::uint8_t>(tree_.searchCanonical(ctx.sampler_->sample(chunkX * 4 + qx, ChunkBiomes::kMinQuartY + i, chunkZ * 4 + qz), ctx.cache_));
            }
        }
    }
}

// BiomeManager.getBiome, step 1: which quart cell a block position belongs to after the random fiddling.
namespace {

[[nodiscard]] inline std::uint64_t lcgNext(std::uint64_t s, std::uint64_t inc) { return s * (s * 6364136223846793005ull + 1442695040888963407ull) + inc; }

[[nodiscard]] inline double fiddle(std::uint64_t l) {
    // ((double)Math.floorMod(l >> 24, 1024) / 1024.0 - 0.5) * 0.9 ;  >> is arithmetic, floorMod(.., 1024) == & 1023
    const auto shifted = static_cast<std::int64_t>(l) >> 24;
    return (static_cast<double>(shifted & 1023) / 1024.0 - 0.5) * 0.9;
}

[[nodiscard]] inline double fiddledDistance(std::uint64_t seed, int ix, int iy, int iz, double dx, double dy, double dz) {
    const auto ex = static_cast<std::uint64_t>(static_cast<std::int64_t>(ix));
    const auto ey = static_cast<std::uint64_t>(static_cast<std::int64_t>(iy));
    const auto ez = static_cast<std::uint64_t>(static_cast<std::int64_t>(iz));
    std::uint64_t l = seed;
    l = lcgNext(l, ex);
    l = lcgNext(l, ey);
    l = lcgNext(l, ez);
    l = lcgNext(l, ex);
    l = lcgNext(l, ey);
    l = lcgNext(l, ez);
    const double f1 = fiddle(l);
    l = lcgNext(l, seed);
    const double f2 = fiddle(l);
    l = lcgNext(l, seed);
    const double f3 = fiddle(l);
    const double a = dz + f3, b = dy + f2, c = dx + f1;
    const double sa = a * a;
    const double sb = b * b;
    const double sc = c * c;
    return (sa + sb) + sc;
}

}  // namespace

BiomeSource::QuartPos BiomeSource::zoom(std::int64_t zoomSeed, int x, int y, int z) {
    const int xm = x - 2, ym = y - 2, zm = z - 2;
    const int qx = xm >> 2, qy = ym >> 2, qz = zm >> 2;
    const double fx = static_cast<double>(xm & 3) / 4.0;
    const double fy = static_cast<double>(ym & 3) / 4.0;
    const double fz = static_cast<double>(zm & 3) / 4.0;
    const auto seed = static_cast<std::uint64_t>(zoomSeed);
    int best = 0;
    double bestD = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 8; ++i) {
        const bool ax = (i & 4) == 0, ay = (i & 2) == 0, az = (i & 1) == 0;
        const int cx = ax ? qx : qx + 1, cy = ay ? qy : qy + 1, cz = az ? qz : qz + 1;
        const double dx = ax ? fx : fx - 1.0, dy = ay ? fy : fy - 1.0, dz = az ? fz : fz - 1.0;
        const double d = fiddledDistance(seed, cx, cy, cz, dx, dy, dz);
        if (bestD > d) {
            best = i;
            bestD = d;
        }
    }
    return {(best & 4) == 0 ? qx : qx + 1, (best & 2) == 0 ? qy : qy + 1, (best & 1) == 0 ? qz : qz + 1};
}

BiomeId BiomeSource::biomeAt(Context& ctx, int x, int y, int z) const {
    if (fixed_ != kNoBiome) return fixed_;
    const QuartPos q = zoom(zoomSeed_, x, y, z);
    return noiseBiome(ctx, q.x, q.y, q.z);
}

BiomeId BiomeSource::biomeAt(int x, int y, int z) const { return biomeAt(threadContext(), x, y, z); }

}  // namespace apex::worldgen
