#include "falcon/world/block_outlines.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace falcon {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr char kCacheMagic[4] = {'F', 'B', 'O', '1'};

struct Fingerprint {
    std::uint64_t size = 0;
    std::int64_t time = 0;
    std::uint32_t stateCount = 0;
    std::uint32_t blockCount = 0;
    bool operator==(const Fingerprint&) const = default;
};

std::optional<Fingerprint> fingerprintOf(const fs::path& dir, const BlockRegistry& registry) {
    Fingerprint f;
    std::error_code ec;
    const fs::path p = dir / "gameplay-facts.json";
    f.size = fs::file_size(p, ec);
    if (ec) return std::nullopt;
    f.time = static_cast<std::int64_t>(fs::last_write_time(p, ec).time_since_epoch().count());
    if (ec) return std::nullopt;
    f.stateCount = static_cast<std::uint32_t>(registry.stateCount());
    f.blockCount = static_cast<std::uint32_t>(registry.blocks().size());
    return f;
}

template <typename T>
void writePod(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
bool readPod(std::istream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}

// gameplay-facts.json: {"states":[{"id":0, ..., "outline":[[x0,y0,z0,x1,y1,z1], ...], "outline_edges":[[...], ...], ...}, ...]}.
// Nesting: 1 the root object, 2 the "states" array, 3 one state, 4 its "outline" array, 5 one box. Only the id and those two
// arrays are kept.
class OutlineHandler {
public:
    using number_integer_t = json::number_integer_t;
    using number_unsigned_t = json::number_unsigned_t;
    using number_float_t = json::number_float_t;
    using string_t = json::string_t;
    using binary_t = json::binary_t;

    OutlineHandler(std::vector<OutlineShape>& shapes, std::vector<std::uint16_t>& stateShape) : shapes_(shapes), stateShape_(stateShape) {
        shapes_.emplace_back();  // shape 0: nothing to aim at
        index_.emplace(keyOf(OutlineShape{}), 0);
    }

    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_integer(number_integer_t v) { return value(static_cast<double>(v)); }
    bool number_unsigned(number_unsigned_t v) { return value(static_cast<double>(v)); }
    bool number_float(number_float_t v, const string_t&) { return value(v); }
    bool string(string_t&) { return true; }
    bool binary(binary_t&) { return true; }
    bool start_object(std::size_t) {
        if (++depth_ == 3) {
            cur_ = OutlineShape{};
            curId_ = -1;
        }
        return true;
    }
    bool end_object() {
        if (depth_ == 3) endState();
        --depth_;
        return true;
    }
    bool start_array(std::size_t) {
        if (++depth_ == 5) box_.clear();
        return true;
    }
    bool end_array() {
        if (depth_ == 5 && box_.size() == 6) {
            if (key3_ == "outline") {
                cur_.boxes.push_back({box_[0], box_[1], box_[2], box_[3], box_[4], box_[5]});
            } else if (key3_ == "outline_edges") {
                cur_.edges.push_back({static_cast<float>(box_[0]), static_cast<float>(box_[1]), static_cast<float>(box_[2]),
                                      static_cast<float>(box_[3]), static_cast<float>(box_[4]), static_cast<float>(box_[5])});
            }
        }
        --depth_;
        return true;
    }
    bool key(string_t& v) {
        if (depth_ == 3) key3_ = v;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& e) {
        error = e.what();
        return false;
    }

    std::string error;
    std::size_t seen = 0;

private:
    static std::string keyOf(const OutlineShape& s) {
        std::string k(reinterpret_cast<const char*>(s.boxes.data()), s.boxes.size() * sizeof(Aabb));
        k.push_back('|');
        k.append(reinterpret_cast<const char*>(s.edges.data()), s.edges.size() * sizeof(std::array<float, 6>));
        return k;
    }
    void endState() {
        if (curId_ < 0 || static_cast<std::size_t>(curId_) >= stateShape_.size()) return;
        const std::string k = keyOf(cur_);
        auto it = index_.find(k);
        if (it == index_.end()) {
            if (shapes_.size() >= 65535) throw std::runtime_error("more than 65535 distinct outline shapes");
            it = index_.emplace(k, static_cast<std::uint16_t>(shapes_.size())).first;
            shapes_.push_back(std::move(cur_));
        }
        stateShape_[static_cast<std::size_t>(curId_)] = it->second;
        ++seen;
    }
    bool value(double v) {
        if (depth_ == 3 && key3_ == "id") curId_ = static_cast<int>(v);
        else if (depth_ == 5 && (key3_ == "outline" || key3_ == "outline_edges")) box_.push_back(v);
        return true;
    }

    std::vector<OutlineShape>& shapes_;
    std::vector<std::uint16_t>& stateShape_;
    std::unordered_map<std::string, std::uint16_t> index_;
    std::string key3_;
    int depth_ = 0;
    int curId_ = -1;
    OutlineShape cur_;
    std::vector<double> box_;
};

}  // namespace

// Lets the loader fill the private members.
class BlockOutlinesBuilder {
public:
    static std::vector<OutlineShape>& shapes(BlockOutlines& o) { return o.shapes_; }
    static std::vector<std::uint16_t>& stateShape(BlockOutlines& o) { return o.stateShape_; }
    static void markFromGameData(BlockOutlines& o) { o.fromGameData_ = true; }
};

std::vector<std::array<float, 6>> boxEdges(const std::vector<Aabb>& boxes) {
    std::vector<std::array<float, 6>> edges;
    for (const Aabb& b : boxes) {
        const float x[2] = {static_cast<float>(b.minX), static_cast<float>(b.maxX)};
        const float y[2] = {static_cast<float>(b.minY), static_cast<float>(b.maxY)};
        const float z[2] = {static_cast<float>(b.minZ), static_cast<float>(b.maxZ)};
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                edges.push_back({x[0], y[j], z[k], x[1], y[j], z[k]});  // along x
                edges.push_back({x[j], y[0], z[k], x[j], y[1], z[k]});  // along y
                edges.push_back({x[j], y[k], z[0], x[j], y[k], z[1]});  // along z
            }
        }
    }
    return edges;
}

const OutlineShape& BlockOutlines::shape(Block b) const {
    const std::size_t id = stateId(b);
    return shapes_[id < stateShape_.size() ? stateShape_[id] : 0];
}

std::size_t BlockOutlines::memoryBytes() const {
    std::size_t total = stateShape_.capacity() * sizeof(std::uint16_t) + shapes_.capacity() * sizeof(OutlineShape);
    for (const OutlineShape& s : shapes_) total += s.boxes.capacity() * sizeof(Aabb) + s.edges.capacity() * sizeof(std::array<float, 6>);
    return total;
}

std::shared_ptr<const BlockOutlines> BlockOutlines::makeFromBoxes(const BlockRegistry& registry, const std::vector<Override>& overrides) {
    auto o = std::make_shared<BlockOutlines>();
    auto& shapes = BlockOutlinesBuilder::shapes(*o);
    auto& stateShape = BlockOutlinesBuilder::stateShape(*o);
    shapes.emplace_back();  // 0: empty
    OutlineShape cube;
    cube.boxes.push_back({0, 0, 0, 1, 1, 1});
    cube.edges = boxEdges(cube.boxes);
    shapes.push_back(cube);  // 1: the full cube
    stateShape.assign(registry.stateCount(), 0);
    for (std::size_t i = 0; i < stateShape.size(); ++i) {
        const StateInfo& info = registry.state(static_cast<Block>(i));
        if (!info.has(kStateAir) && !info.has(kStateLiquid)) stateShape[i] = 1;
    }
    for (const Override& ov : overrides) {
        OutlineShape s;
        s.boxes = ov.boxes;
        s.edges = boxEdges(ov.boxes);
        shapes.push_back(std::move(s));
        stateShape[stateId(ov.block)] = static_cast<std::uint16_t>(shapes.size() - 1);
    }
    return o;
}

std::shared_ptr<const BlockOutlines> BlockOutlines::makeMinimal(const BlockRegistry& registry) { return makeFromBoxes(registry, {}); }

std::shared_ptr<const BlockOutlines> BlockOutlines::withCubes(const BlockOutlines& base, std::size_t states) {
    auto o = std::make_shared<BlockOutlines>(base);
    auto& stateShape = BlockOutlinesBuilder::stateShape(*o);
    if (stateShape.size() < states) stateShape.resize(states, 1);  // (shape 1 is the full cube)
    return o;
}

std::shared_ptr<const BlockOutlines> BlockOutlines::loadFromGameData(const fs::path& dir, const fs::path& cacheDir, const BlockRegistry& registry,
                                                                     std::string& error) {
    const auto fingerprint = fingerprintOf(dir, registry);
    if (!fingerprint) {
        error = "gameplay-facts.json is not in " + dir.string();
        return nullptr;
    }
    const fs::path cachePath = cacheDir / "outlines.cache";

    // 1. The cache, when it was made from this very file.
    {
        std::ifstream in(cachePath, std::ios::binary);
        char magic[4] = {};
        Fingerprint stored;
        std::uint32_t shapeCount = 0;
        if (in.read(magic, 4) && std::memcmp(magic, kCacheMagic, 4) == 0 && readPod(in, stored) && stored == *fingerprint && readPod(in, shapeCount) &&
            shapeCount >= 1 && shapeCount < 65536) {
            auto o = std::make_shared<BlockOutlines>();
            auto& shapes = BlockOutlinesBuilder::shapes(*o);
            shapes.resize(shapeCount);
            bool ok = true;
            for (OutlineShape& s : shapes) {
                std::uint16_t boxCount = 0, edgeCount = 0;
                ok = ok && readPod(in, boxCount) && readPod(in, edgeCount);
                if (!ok) break;
                s.boxes.resize(boxCount);
                s.edges.resize(edgeCount);
                in.read(reinterpret_cast<char*>(s.boxes.data()), static_cast<std::streamsize>(boxCount * sizeof(Aabb)));
                in.read(reinterpret_cast<char*>(s.edges.data()), static_cast<std::streamsize>(edgeCount * sizeof(std::array<float, 6>)));
                ok = ok && static_cast<bool>(in);
                if (!ok) break;
            }
            if (ok) {
                auto& stateShape = BlockOutlinesBuilder::stateShape(*o);
                stateShape.resize(fingerprint->stateCount);
                in.read(reinterpret_cast<char*>(stateShape.data()), static_cast<std::streamsize>(stateShape.size() * sizeof(std::uint16_t)));
                ok = static_cast<bool>(in);
            }
            if (ok) {
                BlockOutlinesBuilder::markFromGameData(*o);
                return o;
            }
        }
    }

    // 2. Stream the 55 MB file once.
    auto o = std::make_shared<BlockOutlines>();
    try {
        auto& shapes = BlockOutlinesBuilder::shapes(*o);
        auto& stateShape = BlockOutlinesBuilder::stateShape(*o);
        stateShape.assign(registry.stateCount(), 0);
        std::ifstream in(dir / "gameplay-facts.json");
        OutlineHandler handler(shapes, stateShape);
        if (!json::sax_parse(in, &handler)) throw std::runtime_error("gameplay-facts.json: " + handler.error);
        if (handler.seen != stateShape.size()) {
            throw std::runtime_error("gameplay-facts.json has " + std::to_string(handler.seen) + " states, expected " + std::to_string(stateShape.size()));
        }
        BlockOutlinesBuilder::markFromGameData(*o);
    } catch (const std::exception& e) {
        error = std::string("reading the block outlines failed: ") + e.what();
        return nullptr;
    }

    // 3. Save the cache (failing to is not an error).
    {
        std::error_code ec;
        fs::create_directories(cacheDir, ec);
        const fs::path tmp = cachePath.string() + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(kCacheMagic, 4);
            writePod(out, *fingerprint);
            writePod(out, static_cast<std::uint32_t>(o->shapes_.size()));
            for (const OutlineShape& s : o->shapes_) {
                writePod(out, static_cast<std::uint16_t>(s.boxes.size()));
                writePod(out, static_cast<std::uint16_t>(s.edges.size()));
                out.write(reinterpret_cast<const char*>(s.boxes.data()), static_cast<std::streamsize>(s.boxes.size() * sizeof(Aabb)));
                out.write(reinterpret_cast<const char*>(s.edges.data()), static_cast<std::streamsize>(s.edges.size() * sizeof(std::array<float, 6>)));
            }
            out.write(reinterpret_cast<const char*>(o->stateShape_.data()), static_cast<std::streamsize>(o->stateShape_.size() * sizeof(std::uint16_t)));
            out.close();
            if (out) {
                fs::rename(tmp, cachePath, ec);
                if (ec) fs::remove(tmp, ec);
            }
        }
    }
    return o;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
std::shared_ptr<const BlockOutlines> g_outlines;
const BlockRegistry* g_outlinesRegistry = nullptr;  // the registry a stand-in table was made for
bool g_outlinesInstalled = false;                   // a table somebody installed on purpose is kept as it is
}  // namespace

void installBlockOutlines(std::shared_ptr<const BlockOutlines> outlines) {
    g_outlinesInstalled = outlines != nullptr;
    g_outlines = std::move(outlines);
    g_outlinesRegistry = nullptr;
}

bool hasBlockOutlinesFromGameData() { return g_outlines && g_outlines->isFromGameData(); }

const BlockOutlines& blockOutlines() {
    const BlockRegistry& registry = blockRegistry();
    const bool stale = g_outlines && !g_outlinesInstalled && (g_outlinesRegistry != &registry || g_outlines->stateCount() != registry.stateCount());
    if (!g_outlines || stale) {
        g_outlines = BlockOutlines::makeMinimal(registry);
        g_outlinesRegistry = &registry;
        g_outlinesInstalled = false;
    }
    return *g_outlines;
}

}  // namespace falcon
