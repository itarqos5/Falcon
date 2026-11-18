#include "falcon/world/collision_shapes.hpp"

#include "falcon/assets/nbt.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace falcon {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------
// VoxelShape

bool VoxelShape::filled(int ix, int iy, int iz) const {
    if (ix < 0 || iy < 0 || iz < 0 || ix >= n_[0] || iy >= n_[1] || iz >= n_[2]) return false;
    const std::size_t bit = bitBase_ + (static_cast<std::size_t>(ix) * static_cast<std::size_t>(n_[1]) + static_cast<std::size_t>(iy)) * static_cast<std::size_t>(n_[2]) +
                            static_cast<std::size_t>(iz);
    return ((bits_[bit >> 3] >> (bit & 7)) & 1) != 0;
}

// The largest grid line at or before `position` (the shape moved by `offset` blocks), -1 before the first line and
// cells(axis) past the last: VoxelShape.findIndex.
int VoxelShape::findIndex(int axis, double position, int offset) const {
    int index = -1;
    const double shift = static_cast<double>(offset);
    for (int i = 0; i <= n_[axis]; ++i) {
        if (coord(axis, i) + shift <= position) {
            index = i;
        } else {
            break;
        }
    }
    return index;
}

double VoxelShape::collide(int axis, const Aabb& m, double d, int bx, int by, int bz) const {
    if (empty()) return d;
    if (std::abs(d) < 1.0e-7) return 0.0;
    const int offset[3] = {bx, by, bz};
    const int a1 = (axis + 1) % 3;
    const int a2 = (axis + 2) % 3;
    const int lo1 = std::max(0, findIndex(a1, m.min(a1) + 1.0e-7, offset[a1]));
    const int hi1 = std::min(n_[a1], findIndex(a1, m.max(a1) - 1.0e-7, offset[a1]) + 1);
    const int lo2 = std::max(0, findIndex(a2, m.min(a2) + 1.0e-7, offset[a2]));
    const int hi2 = std::min(n_[a2], findIndex(a2, m.max(a2) - 1.0e-7, offset[a2]) + 1);
    const double shift = static_cast<double>(offset[axis]);
    int cell[3] = {0, 0, 0};
    if (d > 0.0) {
        const double movingMax = m.max(axis);
        for (int layer = findIndex(axis, movingMax - 1.0e-7, offset[axis]) + 1; layer < n_[axis]; ++layer) {
            for (int i1 = lo1; i1 < hi1; ++i1) {
                for (int i2 = lo2; i2 < hi2; ++i2) {
                    cell[axis] = layer;
                    cell[a1] = i1;
                    cell[a2] = i2;
                    if (!filled(cell[0], cell[1], cell[2])) continue;
                    const double gap = (coord(axis, layer) + shift) - movingMax;
                    if (gap >= -1.0e-7) d = std::min(d, gap);
                    return d;
                }
            }
        }
    } else {
        const double movingMin = m.min(axis);
        for (int layer = findIndex(axis, movingMin + 1.0e-7, offset[axis]) - 1; layer >= 0; --layer) {
            for (int i1 = lo1; i1 < hi1; ++i1) {
                for (int i2 = lo2; i2 < hi2; ++i2) {
                    cell[axis] = layer;
                    cell[a1] = i1;
                    cell[a2] = i2;
                    if (!filled(cell[0], cell[1], cell[2])) continue;
                    const double gap = (coord(axis, layer + 1) + shift) - movingMin;
                    if (gap <= 1.0e-7) d = std::max(d, gap);
                    return d;
                }
            }
        }
    }
    return d;
}

bool VoxelShape::intersects(const Aabb& box, int bx, int by, int bz) const {
    if (empty()) return false;
    const int offset[3] = {bx, by, bz};
    // The range of cells on each axis that overlaps the box by more than a sliver.
    int lo[3];
    int hi[3];
    for (int a = 0; a < 3; ++a) {
        lo[a] = std::max(0, findIndex(a, box.min(a) + 1.0e-7, offset[a]));
        hi[a] = std::min(n_[a], findIndex(a, box.max(a) - 1.0e-7, offset[a]) + 1);
    }
    for (int x = lo[0]; x < hi[0]; ++x) {
        for (int y = lo[1]; y < hi[1]; ++y) {
            for (int z = lo[2]; z < hi[2]; ++z) {
                if (filled(x, y, z)) return true;
            }
        }
    }
    return false;
}

void VoxelShape::boxes(std::vector<Aabb>& out) const {
    if (empty()) return;
    const std::size_t total = static_cast<std::size_t>(n_[0]) * static_cast<std::size_t>(n_[1]) * static_cast<std::size_t>(n_[2]);
    std::vector<char> used(total, 0);
    const auto at = [&](int x, int y, int z) { return (static_cast<std::size_t>(x) * static_cast<std::size_t>(n_[1]) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(n_[2]) + static_cast<std::size_t>(z); };
    for (int x = 0; x < n_[0]; ++x) {
        for (int y = 0; y < n_[1]; ++y) {
            for (int z = 0; z < n_[2]; ++z) {
                if (!filled(x, y, z) || used[at(x, y, z)]) continue;
                int z1 = z + 1;
                while (z1 < n_[2] && filled(x, y, z1) && !used[at(x, y, z1)]) ++z1;
                int y1 = y + 1;
                const auto rowFree = [&](int yy, int xx) {
                    for (int zz = z; zz < z1; ++zz) {
                        if (!filled(xx, yy, zz) || used[at(xx, yy, zz)]) return false;
                    }
                    return true;
                };
                while (y1 < n_[1] && rowFree(y1, x)) ++y1;
                int x1 = x + 1;
                const auto planeFree = [&](int xx) {
                    for (int yy = y; yy < y1; ++yy) {
                        if (!rowFree(yy, xx)) return false;
                    }
                    return true;
                };
                while (x1 < n_[0] && planeFree(x1)) ++x1;
                for (int xx = x; xx < x1; ++xx) {
                    for (int yy = y; yy < y1; ++yy) {
                        for (int zz = z; zz < z1; ++zz) used[at(xx, yy, zz)] = 1;
                    }
                }
                out.push_back({coord(0, x), coord(1, y), coord(2, z), coord(0, x1), coord(1, y1), coord(2, z1)});
            }
        }
    }
}

Aabb VoxelShape::bounds() const {
    Aabb r;
    bool any = false;
    for (int x = 0; x < n_[0]; ++x) {
        for (int y = 0; y < n_[1]; ++y) {
            for (int z = 0; z < n_[2]; ++z) {
                if (!filled(x, y, z)) continue;
                const Aabb cell{coord(0, x), coord(1, y), coord(2, z), coord(0, x + 1), coord(1, y + 1), coord(2, z + 1)};
                if (!any) {
                    r = cell;
                    any = true;
                } else {
                    r.minX = std::min(r.minX, cell.minX);
                    r.minY = std::min(r.minY, cell.minY);
                    r.minZ = std::min(r.minZ, cell.minZ);
                    r.maxX = std::max(r.maxX, cell.maxX);
                    r.maxY = std::max(r.maxY, cell.maxY);
                    r.maxZ = std::max(r.maxZ, cell.maxZ);
                }
            }
        }
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// CollisionShapes


class CollisionShapesBuilder {
public:
    using Record = CollisionShapes::Record;
    using Special = CollisionShapes::Special;

    static std::vector<Record>& shapes(CollisionShapes& c) { return c.shapes_; }
    static std::vector<float>& coords(CollisionShapes& c) { return c.coords_; }
    static std::vector<std::uint8_t>& bits(CollisionShapes& c) { return c.bits_; }
    static std::vector<std::uint16_t>& stateShape(CollisionShapes& c) { return c.stateShape_; }
    static std::vector<Special>& specials(CollisionShapes& c) { return c.specials_; }
    static void setUnit(CollisionShapes& c, std::uint16_t unit, std::uint16_t empty) {
        c.unitCube_ = unit;
        c.emptyShape_ = empty;
    }
    static void setGameData(CollisionShapes& c, bool v) { c.fromGameData_ = v; }
    static std::uint16_t unitCube(const CollisionShapes& c) { return c.unitCube_; }
    static std::uint16_t emptyShape(const CollisionShapes& c) { return c.emptyShape_; }
};

namespace {

using Builder = CollisionShapesBuilder;
constexpr std::uint8_t kEmptyFlag = 1;
constexpr std::uint8_t kUnitCubeFlag = 2;
constexpr std::uint8_t kLargeFlag = 4;

constexpr char kCacheMagic[4] = {'F', 'C', 'S', '1'};

struct Fingerprint {
    std::uint64_t size[2] = {};
    std::int64_t time[2] = {};
    std::uint32_t stateCount = 0;
    std::uint32_t blockCount = 0;
    bool operator==(const Fingerprint&) const = default;
};

std::optional<Fingerprint> fingerprintOf(const fs::path& dir, const BlockRegistry& registry) {
    Fingerprint f;
    const char* names[2] = {"cached-collision.nbt", "movement-properties.nbt"};
    for (int i = 0; i < 2; ++i) {
        std::error_code ec;
        const fs::path p = dir / names[i];
        f.size[i] = fs::file_size(p, ec);
        if (ec) return std::nullopt;
        f.time[i] = static_cast<std::int64_t>(fs::last_write_time(p, ec).time_since_epoch().count());
        if (ec) return std::nullopt;
    }
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
template <typename T>
void writeVector(std::ostream& out, const std::vector<T>& v) {
    writePod(out, static_cast<std::uint32_t>(v.size()));
    out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(T)));
}
template <typename T>
bool readVector(std::istream& in, std::vector<T>& v, std::uint32_t limit) {
    std::uint32_t n = 0;
    if (!readPod(in, n) || n > limit) return false;
    v.resize(n);
    in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(static_cast<std::size_t>(n) * sizeof(T)));
    return static_cast<bool>(in);
}

std::optional<NbtTag> readNbtFile(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.string();
        return std::nullopt;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto root = parseNbt(bytes);
    if (!root) error = path.filename().string() + " is not valid NBT";
    return root;
}

double doubleFromBits(std::int64_t bits) {
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

// Appends one shape to the table and returns its index.
std::uint16_t addShape(CollisionShapes& table, const float* xs, int nx, const float* ys, int ny, const float* zs, int nz, const std::vector<bool>& cells) {
    CollisionShapesBuilder::Record r;
    auto& coords = Builder::coords(table);
    auto& bits = Builder::bits(table);
    r.coordBase = static_cast<std::uint32_t>(coords.size());
    coords.insert(coords.end(), xs, xs + nx + 1);
    coords.insert(coords.end(), ys, ys + ny + 1);
    coords.insert(coords.end(), zs, zs + nz + 1);
    r.bitBase = static_cast<std::uint32_t>(bits.size() * 8);
    r.n[0] = static_cast<std::uint8_t>(nx);
    r.n[1] = static_cast<std::uint8_t>(ny);
    r.n[2] = static_cast<std::uint8_t>(nz);
    bits.resize(bits.size() + (cells.size() + 7) / 8, 0);
    bool any = false;
    // The filled extent decides whether the shape sticks out of its block.
    float lo[3] = {1e9f, 1e9f, 1e9f};
    float hi[3] = {-1e9f, -1e9f, -1e9f};
    for (int x = 0; x < nx; ++x) {
        for (int y = 0; y < ny; ++y) {
            for (int z = 0; z < nz; ++z) {
                const std::size_t i = (static_cast<std::size_t>(x) * static_cast<std::size_t>(ny) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(nz) + static_cast<std::size_t>(z);
                if (!cells[i]) continue;
                any = true;
                const std::size_t bit = r.bitBase + i;
                bits[bit >> 3] = static_cast<std::uint8_t>(bits[bit >> 3] | (1u << (bit & 7)));
                lo[0] = std::min(lo[0], xs[x]);
                hi[0] = std::max(hi[0], xs[x + 1]);
                lo[1] = std::min(lo[1], ys[y]);
                hi[1] = std::max(hi[1], ys[y + 1]);
                lo[2] = std::min(lo[2], zs[z]);
                hi[2] = std::max(hi[2], zs[z + 1]);
            }
        }
    }
    r.flags = 0;
    if (!any) {
        r.flags = kEmptyFlag;
    } else {
        if (nx == 1 && ny == 1 && nz == 1 && xs[0] == 0.0f && xs[1] == 1.0f && ys[0] == 0.0f && ys[1] == 1.0f && zs[0] == 0.0f && zs[1] == 1.0f) r.flags |= kUnitCubeFlag;
        for (int a = 0; a < 3; ++a) {
            if (lo[a] < 0.0f || hi[a] > 1.0f) r.flags |= kLargeFlag;
        }
    }
    auto& shapes = Builder::shapes(table);
    shapes.push_back(r);
    return static_cast<std::uint16_t>(shapes.size() - 1);
}

// Makes sure the table has an empty shape and a unit cube, and remembers where they are.
void finishTable(CollisionShapes& table) {
    auto& shapes = Builder::shapes(table);
    int empty = -1;
    int unit = -1;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (empty < 0 && (shapes[i].flags & kEmptyFlag)) empty = static_cast<int>(i);
        if (unit < 0 && (shapes[i].flags & kUnitCubeFlag)) unit = static_cast<int>(i);
    }
    const float zero[1] = {0.0f};
    const float line[2] = {0.0f, 1.0f};
    if (empty < 0) empty = addShape(table, zero, 0, zero, 0, zero, 0, {});
    if (unit < 0) unit = addShape(table, line, 1, line, 1, line, 1, {true});
    Builder::setUnit(table, static_cast<std::uint16_t>(unit), static_cast<std::uint16_t>(empty));
}

}  // namespace

VoxelShape CollisionShapes::view(std::uint16_t index) const {
    const Record& r = shapes_[index];
    VoxelShape s;
    s.coords_ = coords_.data();
    s.bits_ = bits_.data();
    s.axisBase_[0] = r.coordBase;
    s.axisBase_[1] = r.coordBase + static_cast<std::size_t>(r.n[0]) + 1;
    s.axisBase_[2] = s.axisBase_[1] + static_cast<std::size_t>(r.n[1]) + 1;
    s.bitBase_ = r.bitBase;
    for (int a = 0; a < 3; ++a) s.n_[a] = r.n[a];
    s.flags_ = r.flags;
    return s;
}

VoxelShape CollisionShapes::shape(Block b) const {
    const auto id = static_cast<std::size_t>(stateId(b));
    return view(id < stateShape_.size() ? stateShape_[id] : emptyShape_);
}

std::vector<Aabb> CollisionShapes::boxes(Block b) const {
    std::vector<Aabb> out;
    shape(b).boxes(out);
    return out;
}

const CollisionShapes::Special* CollisionShapes::special(Block b) const {
    const std::uint16_t id = stateId(b);
    auto it = std::upper_bound(specials_.begin(), specials_.end(), id, [](std::uint16_t v, const Special& s) { return v < s.firstState; });
    if (it == specials_.begin()) return nullptr;
    --it;
    return id < it->firstState + it->stateCount ? &*it : nullptr;
}

float CollisionShapes::bounceRestitution(Block b) const {
    const Special* s = special(b);
    return s != nullptr ? s->bounce : 0.0f;
}

float CollisionShapes::fallDistanceReduction(Block b) const {
    const Special* s = special(b);
    return s != nullptr ? s->reduction : 0.0f;
}

std::size_t CollisionShapes::memoryBytes() const {
    return shapes_.capacity() * sizeof(Record) + coords_.capacity() * sizeof(float) + bits_.capacity() + stateShape_.capacity() * sizeof(std::uint16_t) +
           specials_.capacity() * sizeof(Special);
}

std::shared_ptr<const CollisionShapes> CollisionShapes::makeMinimal(const BlockRegistry& registry) {
    auto table = std::make_shared<CollisionShapes>();
    finishTable(*table);
    auto& stateShape = Builder::stateShape(*table);
    stateShape.resize(registry.stateCount());
    for (std::size_t i = 0; i < stateShape.size(); ++i) {
        const bool cube = registry.state(static_cast<Block>(i)).has(kStateFullCollision);
        stateShape[i] = cube ? Builder::unitCube(*table) : Builder::emptyShape(*table);
    }
    return table;
}

std::shared_ptr<const CollisionShapes> CollisionShapes::withCubes(const CollisionShapes& base, std::size_t states) {
    auto table = std::make_shared<CollisionShapes>(base);
    auto& stateShape = Builder::stateShape(*table);
    if (stateShape.size() < states) stateShape.resize(states, Builder::unitCube(*table));
    return table;
}

std::shared_ptr<const CollisionShapes> CollisionShapes::makeFromBoxes(const BlockRegistry& registry, const std::vector<Override>& overrides) {
    auto table = std::const_pointer_cast<CollisionShapes>(makeMinimal(registry));
    auto& stateShape = Builder::stateShape(*table);
    for (const Override& o : overrides) {
        const auto id = static_cast<std::size_t>(stateId(o.block));
        if (id >= stateShape.size()) continue;
        // The grid lines are every box face; a cell is filled when its middle is inside a box.
        std::vector<float> lines[3];
        for (int a = 0; a < 3; ++a) {
            lines[a].push_back(0.0f);
            for (const Aabb& b : o.boxes) {
                lines[a].push_back(static_cast<float>(b.min(a)));
                lines[a].push_back(static_cast<float>(b.max(a)));
            }
            std::sort(lines[a].begin(), lines[a].end());
            lines[a].erase(std::unique(lines[a].begin(), lines[a].end()), lines[a].end());
            if (lines[a].size() < 2) lines[a].push_back(1.0f);
        }
        const int n[3] = {static_cast<int>(lines[0].size()) - 1, static_cast<int>(lines[1].size()) - 1, static_cast<int>(lines[2].size()) - 1};
        std::vector<bool> cells(static_cast<std::size_t>(n[0]) * static_cast<std::size_t>(n[1]) * static_cast<std::size_t>(n[2]), false);
        for (int ix = 0; ix < n[0]; ++ix) {
            for (int iy = 0; iy < n[1]; ++iy) {
                for (int iz = 0; iz < n[2]; ++iz) {
                    const double mid[3] = {(static_cast<double>(lines[0][static_cast<std::size_t>(ix)]) + static_cast<double>(lines[0][static_cast<std::size_t>(ix) + 1])) / 2.0,
                                           (static_cast<double>(lines[1][static_cast<std::size_t>(iy)]) + static_cast<double>(lines[1][static_cast<std::size_t>(iy) + 1])) / 2.0,
                                           (static_cast<double>(lines[2][static_cast<std::size_t>(iz)]) + static_cast<double>(lines[2][static_cast<std::size_t>(iz) + 1])) / 2.0};
                    for (const Aabb& b : o.boxes) {
                        if (mid[0] > b.minX && mid[0] < b.maxX && mid[1] > b.minY && mid[1] < b.maxY && mid[2] > b.minZ && mid[2] < b.maxZ) {
                            cells[(static_cast<std::size_t>(ix) * static_cast<std::size_t>(n[1]) + static_cast<std::size_t>(iy)) * static_cast<std::size_t>(n[2]) + static_cast<std::size_t>(iz)] = true;
                            break;
                        }
                    }
                }
            }
        }
        stateShape[id] = addShape(*table, lines[0].data(), n[0], lines[1].data(), n[1], lines[2].data(), n[2], cells);
        if (o.bounceRestitution != 0.0f || o.fallDistanceReduction != 0.0f) {
            Builder::specials(*table).push_back({static_cast<std::uint16_t>(id), 1, o.bounceRestitution, o.fallDistanceReduction});
        }
    }
    auto& specials = Builder::specials(*table);
    std::sort(specials.begin(), specials.end(), [](const Special& a, const Special& b) { return a.firstState < b.firstState; });
    return table;
}

std::shared_ptr<const CollisionShapes> CollisionShapes::loadFromGameData(const fs::path& dir, const fs::path& cacheDir, const BlockRegistry& registry,
                                                                         std::string& error) {
    const auto fingerprint = fingerprintOf(dir, registry);
    if (!fingerprint) {
        error = "the game's collision data (cached-collision.nbt, movement-properties.nbt) is not in " + dir.string();
        return nullptr;
    }
    const fs::path cachePath = cacheDir / "collision.cache";

    // 1. The cache, when it was made from these very files.
    {
        std::ifstream in(cachePath, std::ios::binary);
        char magic[4] = {};
        Fingerprint stored;
        if (in.read(magic, 4) && std::memcmp(magic, kCacheMagic, 4) == 0 && readPod(in, stored) && stored == *fingerprint) {
            auto table = std::make_shared<CollisionShapes>();
            std::uint16_t unit = 0;
            std::uint16_t empty = 0;
            if (readVector(in, Builder::shapes(*table), 65535) && readVector(in, Builder::coords(*table), 1u << 24) &&
                readVector(in, Builder::bits(*table), 1u << 24) && readVector(in, Builder::stateShape(*table), 65536) &&
                readVector(in, Builder::specials(*table), 65536) && readPod(in, unit) && readPod(in, empty) &&
                Builder::stateShape(*table).size() == registry.stateCount() && unit < Builder::shapes(*table).size() &&
                empty < Builder::shapes(*table).size()) {
                Builder::setUnit(*table, unit, empty);
                Builder::setGameData(*table, true);
                return table;
            }
        }
    }

    // 2. Build it from the NBT.
    auto table = std::make_shared<CollisionShapes>();
    {
        auto root = readNbtFile(dir / "cached-collision.nbt", error);
        if (!root) return nullptr;
        const NbtTag* shapeList = root->get("shapes");
        const NbtTag* cached = root->get("cached_shapes");
        if (shapeList == nullptr || shapeList->type != NbtTag::Type::List || cached == nullptr || cached->type != NbtTag::Type::IntArray) {
            error = "cached-collision.nbt has no shapes / cached_shapes";
            return nullptr;
        }
        if (cached->numbers.size() != registry.stateCount()) {
            error = "cached-collision.nbt has " + std::to_string(cached->numbers.size()) + " states but the block registry has " + std::to_string(registry.stateCount());
            return nullptr;
        }
        if (shapeList->items.size() >= 65535) {
            error = "cached-collision.nbt has too many shapes";
            return nullptr;
        }
        std::vector<std::uint16_t> indexOf;
        indexOf.reserve(shapeList->items.size());
        for (const NbtTag& s : shapeList->items) {
            const NbtTag* ax[3] = {s.get("x"), s.get("y"), s.get("z")};
            const NbtTag* occupied = s.get("occupied");
            if (!ax[0] || !ax[1] || !ax[2] || !occupied || ax[0]->numbers.empty() || ax[1]->numbers.empty() || ax[2]->numbers.empty()) {
                error = "cached-collision.nbt: a shape is missing its coordinates";
                return nullptr;
            }
            std::vector<float> lines[3];
            int n[3];
            for (int a = 0; a < 3; ++a) {
                for (const std::int64_t bits : ax[a]->numbers) lines[a].push_back(static_cast<float>(doubleFromBits(bits)));
                n[a] = static_cast<int>(lines[a].size()) - 1;
                if (n[a] > 250) {
                    error = "cached-collision.nbt: a shape has too many grid lines";
                    return nullptr;
                }
            }
            const std::size_t cellCount = static_cast<std::size_t>(n[0]) * static_cast<std::size_t>(n[1]) * static_cast<std::size_t>(n[2]);
            if (occupied->numbers.size() != cellCount) {
                error = "cached-collision.nbt: a shape's cells do not match its grid";
                return nullptr;
            }
            std::vector<bool> cells(cellCount);
            for (std::size_t i = 0; i < cellCount; ++i) cells[i] = occupied->numbers[i] != 0;
            indexOf.push_back(addShape(*table, lines[0].data(), n[0], lines[1].data(), n[1], lines[2].data(), n[2], cells));
        }
        finishTable(*table);
        auto& stateShape = Builder::stateShape(*table);
        stateShape.resize(registry.stateCount());
        for (std::size_t i = 0; i < stateShape.size(); ++i) {
            const std::int64_t v = cached->numbers[i];
            // -1 is a state whose shape depends on the world around it (a moving piston): no collision here.
            stateShape[i] = v >= 0 && static_cast<std::size_t>(v) < indexOf.size() ? indexOf[static_cast<std::size_t>(v)] : Builder::emptyShape(*table);
        }
    }
    {
        auto root = readNbtFile(dir / "movement-properties.nbt", error);
        if (!root) return nullptr;
        const NbtTag* blocks = root->get("blocks");
        if (blocks == nullptr || blocks->type != NbtTag::Type::Compound) {
            error = "movement-properties.nbt has no blocks";
            return nullptr;
        }
        auto& specials = Builder::specials(*table);
        for (const auto& [name, props] : blocks->fields) {
            const double bounce = props.getDouble("bounceRestitution");
            const double reduction = props.getDouble("fallDistanceReduction");
            if (bounce == 0.0 && reduction == 0.0) continue;
            const auto index = registry.findBlock(name);
            if (!index) continue;
            const BlockType& type = registry.type(*index);
            specials.push_back({type.firstState, type.stateCount, static_cast<float>(bounce), static_cast<float>(reduction)});
        }
        std::sort(specials.begin(), specials.end(), [](const Special& a, const Special& b) { return a.firstState < b.firstState; });
    }
    Builder::setGameData(*table, true);

    // 3. Save the cache (failing to is not an error).
    {
        std::error_code ec;
        fs::create_directories(cacheDir, ec);
        const fs::path tmp = cachePath.string() + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(kCacheMagic, 4);
            writePod(out, *fingerprint);
            writeVector(out, Builder::shapes(*table));
            writeVector(out, Builder::coords(*table));
            writeVector(out, Builder::bits(*table));
            writeVector(out, Builder::stateShape(*table));
            writeVector(out, Builder::specials(*table));
            writePod(out, Builder::unitCube(*table));
            writePod(out, Builder::emptyShape(*table));
            out.close();
            if (out) {
                fs::rename(tmp, cachePath, ec);
                if (ec) fs::remove(tmp, ec);
            }
        }
    }
    return table;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
std::shared_ptr<const CollisionShapes> g_shapes;
const BlockRegistry* g_shapesRegistry = nullptr;  // the registry a built-in table was made for
bool g_shapesInstalled = false;                   // a table somebody installed on purpose is kept as it is
}  // namespace

void installCollisionShapes(std::shared_ptr<const CollisionShapes> shapes) {
    g_shapesInstalled = shapes != nullptr;
    g_shapes = std::move(shapes);
    g_shapesRegistry = nullptr;
}

bool hasCollisionShapesFromGameData() { return g_shapes && g_shapes->isFromGameData(); }

const CollisionShapes& collisionShapes() {
    const BlockRegistry& registry = blockRegistry();
    const bool stale = g_shapes && !g_shapesInstalled && (g_shapesRegistry != &registry || g_shapes->stateCount() != registry.stateCount());
    if (!g_shapes || stale) {
        g_shapes = CollisionShapes::makeMinimal(registry);
        g_shapesRegistry = &registry;
        g_shapesInstalled = false;
    }
    return *g_shapes;
}

}  // namespace falcon
