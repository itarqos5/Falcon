#include "apex/game/vanilla_compare.hpp"

#include "apex/assets/nbt.hpp"
#include "apex/assets/resource_manager.hpp"
#include "apex/world/block_registry.hpp"
#include "apex/world/chunk.hpp"
#include "apex/world/decoration.hpp"
#include "apex/world/level_info.hpp"
#include "apex/worldgen/overworld_generator.hpp"

#include <stb_image.h>  // (the declaration of stbi_zlib_decode_malloc)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace apex {

namespace fs = std::filesystem;

namespace {

// ---- the Anvil region files the game writes ---------------------------------------------------------------------------------

class RegionFiles {
public:
    explicit RegionFiles(fs::path dir) : dir_(std::move(dir)) {}

    // The NBT of the chunk (cx, cz); nullopt if the game did not save it.
    std::optional<NbtTag> chunk(int cx, int cz) {
        const std::vector<unsigned char>& file = region(cx >> 5, cz >> 5);
        if (file.size() < 8192) return std::nullopt;
        const std::size_t index = static_cast<std::size_t>((cx & 31) + (cz & 31) * 32) * 4;
        const std::uint32_t entry = (static_cast<std::uint32_t>(file[index]) << 24) | (static_cast<std::uint32_t>(file[index + 1]) << 16) |
                                    (static_cast<std::uint32_t>(file[index + 2]) << 8) | file[index + 3];
        const std::size_t offset = static_cast<std::size_t>(entry >> 8) * 4096;
        if (entry == 0 || offset + 5 > file.size()) return std::nullopt;
        const std::uint32_t length = (static_cast<std::uint32_t>(file[offset]) << 24) | (static_cast<std::uint32_t>(file[offset + 1]) << 16) |
                                     (static_cast<std::uint32_t>(file[offset + 2]) << 8) | file[offset + 3];
        const unsigned compression = file[offset + 4];
        if (length < 1 || offset + 4 + length > file.size()) return std::nullopt;
        const unsigned char* data = file.data() + offset + 5;
        const std::size_t size = length - 1;
        std::vector<unsigned char> raw;
        if (compression == 2) {
            int out = 0;
            char* inflated = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(data), static_cast<int>(size), &out);
            if (inflated == nullptr) return std::nullopt;
            raw.assign(reinterpret_cast<unsigned char*>(inflated), reinterpret_cast<unsigned char*>(inflated) + out);
            std::free(inflated);
        } else if (compression == 1) {
            auto un = gunzip(data, size);
            if (!un) return std::nullopt;
            raw = std::move(*un);
        } else if (compression == 3) {
            raw.assign(data, data + size);
        } else {
            std::printf("[compare] a chunk is compressed with method %u (only deflate is read: set region-file-compression=deflate)\n", compression);
            return std::nullopt;
        }
        return parseNbt(raw);
    }

private:
    const std::vector<unsigned char>& region(int rx, int rz) {
        const auto key = std::make_pair(rx, rz);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            std::vector<unsigned char> bytes;
            std::ifstream in(dir_ / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca"), std::ios::binary);
            if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            it = cache_.emplace(key, std::move(bytes)).first;
        }
        return it->second;
    }
    fs::path dir_;
    std::map<std::pair<int, int>, std::vector<unsigned char>> cache_;
};

// "minecraft:oak_stairs[facing=east,half=bottom]" for a palette entry.
std::string stateText(const NbtTag& entry) {
    if (entry.type == NbtTag::Type::String) return entry.text;  // (26.x writes a state with no properties as a plain string)
    // 26.3 writes {id: "...", properties: {...}} (the older form was {Name, Properties}); a state without properties may be a single
    // unnamed string in a compound.
    std::string out = entry.getString("id");
    if (out.empty()) out = entry.getString("Name");
    if (out.empty()) {
        for (const auto& [k, v] : entry.fields) {
            if (v.type == NbtTag::Type::String && !v.text.empty()) {
                out = v.text;
                break;
            }
        }
    }
    const NbtTag* props = entry.get("properties");
    if (props == nullptr) props = entry.get("Properties");
    if (props != nullptr && !props->fields.empty()) {
        out += '[';
        bool first = true;
        for (const auto& [k, v] : props->fields) {
            if (!first) out += ',';
            first = false;
            out += k + "=" + v.text;
        }
        out += ']';
    }
    return out;
}

// The blocks of the game's chunk: 24 sections of 4096, in Falcon's block ids (air for anything Falcon does not know).
struct VanillaChunk {
    std::array<std::array<Block, 4096>, kSectionCount> blocks{};
    std::array<bool, kSectionCount> present{};
    std::unordered_map<std::string, int> unknown;
};

bool readVanillaChunk(const NbtTag& root, const BlockRegistry& registry, VanillaChunk& out) {
    const NbtTag* sections = root.get("sections");
    if (sections == nullptr) return false;
    const Block air = commonBlocks().air;
    for (auto& s : out.blocks) s.fill(air);
    for (const NbtTag& s : sections->items) {
        const int sy = static_cast<int>(s.getInt("Y")) + 4;  // section Y -4 is the lowest
        if (sy < 0 || sy >= kSectionCount) continue;
        const NbtTag* states = s.get("block_states");
        if (states == nullptr) continue;
        const NbtTag* palette = states->get("palette");
        if (palette == nullptr || palette->items.empty()) continue;
        std::vector<Block> ids;
        for (const NbtTag& entry : palette->items) {
            const std::string text = stateText(entry);
            const auto parsed = registry.parse(text);
            if (!parsed) {
                ++out.unknown[text];
                ids.push_back(air);
            } else {
                ids.push_back(*parsed);
            }
        }
        out.present[static_cast<std::size_t>(sy)] = true;
        const NbtTag* data = states->get("data");
        if (ids.size() == 1 || data == nullptr || data->numbers.empty()) {
            out.blocks[static_cast<std::size_t>(sy)].fill(ids[0]);
            continue;
        }
        int bits = 4;
        while ((1u << bits) < ids.size()) ++bits;
        const int perLong = 64 / bits;
        const std::uint64_t mask = (1ull << bits) - 1;
        for (int i = 0; i < 4096; ++i) {
            const std::size_t word = static_cast<std::size_t>(i / perLong);
            if (word >= data->numbers.size()) break;
            const std::uint64_t value = (static_cast<std::uint64_t>(data->numbers[word]) >> ((i % perLong) * bits)) & mask;
            out.blocks[static_cast<std::size_t>(sy)][static_cast<std::size_t>(i)] = value < ids.size() ? ids[value] : air;
        }
    }
    return true;
}

struct Pair {
    std::uint64_t count = 0;
    int minY = 1 << 30, maxY = -(1 << 30);
};

struct Tally {
    std::uint64_t compared = 0, same = 0;
    std::map<std::pair<std::string, std::string>, Pair> differences;  // (the game's block, Falcon's block)
    std::map<std::string, std::uint64_t> missingInFalcon, extraInFalcon;
    void add(const BlockRegistry& registry, Block vanilla, Block falcon, int y) {
        ++compared;
        const auto name = [&](Block b) { return registry.isAir(b) ? std::string("air") : registry.type(b).name; };
        const std::string v = name(vanilla), f = name(falcon);
        if (v == f) {  // (the same block; a differing property is not counted here)
            ++same;
            return;
        }
        Pair& p = differences[{v, f}];
        ++p.count;
        p.minY = std::min(p.minY, y);
        p.maxY = std::max(p.maxY, y);
    }
};

void report(const char* title, const Tally& t) {
    std::printf("\n== %s\n", title);
    std::printf("   blocks compared: %llu   same block: %llu (%.2f %%)   different: %llu\n", static_cast<unsigned long long>(t.compared), static_cast<unsigned long long>(t.same),
                t.compared ? 100.0 * static_cast<double>(t.same) / static_cast<double>(t.compared) : 0.0, static_cast<unsigned long long>(t.compared - t.same));
    std::vector<std::pair<std::pair<std::string, std::string>, Pair>> rows(t.differences.begin(), t.differences.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
    std::printf("   the biggest differences (the game's block -> Falcon's block, blocks, heights):\n");
    for (std::size_t i = 0; i < std::min<std::size_t>(rows.size(), 28); ++i) {
        std::printf("     %-28s -> %-26s %10llu   y %d..%d\n", rows[i].first.first.c_str(), rows[i].first.second.c_str(), static_cast<unsigned long long>(rows[i].second.count),
                    rows[i].second.minY, rows[i].second.maxY);
    }
}

}  // namespace

int runVanillaCompare(const ResourceManager& resources, const fs::path& gameDataDir, const fs::path& cacheDir, const fs::path& worldDir) {
    std::string error;
    auto registry = BlockRegistry::loadFromGameData(gameDataDir, cacheDir, error);
    if (!registry) {
        std::printf("[compare] cannot read the block data: %s\n", error.c_str());
        return 1;
    }
    installBlockRegistry(registry);
    (void)resources;
    auto info = readLevelInfo(worldDir);
    if (!info) {
        std::printf("[compare] %s has no readable level.dat\n", worldDir.string().c_str());
        return 1;
    }
    if (info->seed == 0) {
        // 26.x keeps the seed in data/minecraft/world_gen_settings.dat: look for a field called "seed" anywhere in it.
        std::ifstream in(worldDir / "data" / "minecraft" / "world_gen_settings.dat", std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (const auto root = parseNbt(bytes)) {
            const auto find = [&](const auto& self, const NbtTag& tag, const std::string& path) -> bool {
                for (const auto& [name, child] : tag.fields) {
                    if (name.find("seed") != std::string::npos && child.isInteger() && child.integer != 0) {
                        std::printf("[compare] seed found at %s/%s = %lld\n", path.c_str(), name.c_str(), static_cast<long long>(child.integer));
                        info->seed = child.integer;
                        return true;
                    }
                    if (child.type == NbtTag::Type::Compound && self(self, child, path + "/" + name)) return true;
                }
                return false;
            };
            find(find, *root, "");
        }
    }
    std::printf("[compare] world %s, seed %lld\n", worldDir.string().c_str(), static_cast<long long>(info->seed));

    worldgen::GeneratorOptions options;
    if (!worldgen::loadWorldPreset(gameDataDir, worldgen::worldPresetFor(TerrainStyle::Hills), options, error)) {
        std::printf("[compare] world preset: %s\n", error.c_str());
        return 1;
    }
    auto generator = worldgen::OverworldGenerator::create(gameDataDir, *registry, info->seed, error, options);
    if (!generator) {
        std::printf("[compare] generator: %s\n", error.c_str());
        return 1;
    }
    Decorator decorator(*generator, *registry, info->seed);

    // The spawns.
    const auto ours = generator->findSpawn();
    std::printf("[compare] Falcon's spawn: %.1f %.1f %.1f\n", ours.x, ours.y, ours.z);
    {
        std::ifstream in(worldDir / "level.dat", std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (const auto root = parseNbt(bytes)) {
            if (const NbtTag* data = root->get("Data")) {
                for (const auto& [name, tag] : data->fields) {
                    if (name.find("pawn") == std::string::npos) continue;
                    std::printf("[compare] the game's level.dat %s:", name.c_str());
                    if (tag.type == NbtTag::Type::Compound) {
                        for (const auto& [k, v] : tag.fields) {
                            if (v.type == NbtTag::Type::IntArray) {
                                std::printf(" %s=[", k.c_str());
                                for (const std::int64_t n : v.numbers) std::printf("%lld ", static_cast<long long>(n));
                                std::printf("]");
                            } else if (v.isNumber()) {
                                std::printf(" %s=%lld", k.c_str(), static_cast<long long>(v.integer));
                            } else if (!v.text.empty()) {
                                std::printf(" %s=%s", k.c_str(), v.text.c_str());
                            }
                        }
                    } else if (tag.isNumber()) {
                        std::printf(" %lld", static_cast<long long>(tag.integer));
                    }
                    std::printf("\n");
                }
            }
        }
    }

    // (26.x keeps each dimension in its own folder.)
    fs::path regionDir = worldDir / "dimensions" / "minecraft" / "overworld" / "region";
    if (!fs::exists(regionDir)) regionDir = worldDir / "region";
    RegionFiles regions(regionDir);
    if (std::getenv("FALCON_DUMP_NBT") != nullptr) {  // (what the files really contain: to find fields the layout moved)
        const auto dump = [&](const auto& self, const NbtTag& tag, const std::string& name, int depth, int maxDepth) -> void {
            std::string line(static_cast<std::size_t>(depth) * 2, ' ');
            line += name + " : type " + std::to_string(static_cast<int>(tag.type));
            if (tag.isInteger()) line += " = " + std::to_string(tag.integer);
            else if (tag.type == NbtTag::Type::String) line += " = " + tag.text;
            else if (tag.type == NbtTag::Type::List) line += " [" + std::to_string(tag.items.size()) + " items]";
            else if (tag.type == NbtTag::Type::LongArray || tag.type == NbtTag::Type::IntArray || tag.type == NbtTag::Type::ByteArray) line += " [" + std::to_string(tag.numbers.size()) + " numbers]";
            std::printf("%s\n", line.c_str());
            if (depth >= maxDepth) return;
            for (const auto& [k, v] : tag.fields) self(self, v, k, depth + 1, maxDepth);
            if (!tag.items.empty()) self(self, tag.items[0], "[0]", depth + 1, maxDepth);
        };
        if (const auto nbt = regions.chunk(0, 0)) dump(dump, *nbt, "chunk(0,0)", 0, 5);
        if (const auto nbt = regions.chunk(0, 0)) {
            if (const NbtTag* sections = nbt->get("sections")) {
                for (const NbtTag& s : sections->items) {
                    const NbtTag* st = s.get("block_states");
                    const NbtTag* pal = st != nullptr ? st->get("palette") : nullptr;
                    if (pal == nullptr) continue;
                    std::printf("[dump] section Y=%d palette:", static_cast<int>(s.getInt("Y")));
                    for (const NbtTag& e : pal->items) {
                        if (e.type == NbtTag::Type::String) { std::printf(" '%s'", e.text.c_str()); continue; }
                        std::printf(" {");
                        for (const auto& [k, v] : e.fields) std::printf("%s:%s%s ", k.c_str(), v.type == NbtTag::Type::String ? v.text.c_str() : "<tag>", "");
                        std::printf("}");
                    }
                    std::printf("\n");
                }
            }
        }
        std::ifstream in(worldDir / "level.dat", std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (const auto root = parseNbt(bytes)) dump(dump, *root, "level.dat", 0, 3);
    }
    Tally base, decorated;
    int chunks = 0, missing = 0;
    // The natural surface: the highest solid block that is not a tree or a plant, per column. Caves that open to the sky change it, the
    // shape of the terrain does not, so this says whether the terrain itself is the game's.
    std::uint64_t columns = 0, sameTop = 0, within1 = 0, within3 = 0;
    std::map<int, std::uint64_t> topDelta;
    const auto naturalSolid = [&](Block b) {
        if (registry->isAir(b) || !registry->state(b).has(kStateFullCollision)) return false;
        const std::string& n = registry->type(b).name;
        return n.find("leaves") == std::string::npos && n.find("_log") == std::string::npos && n.find("_wood") == std::string::npos;
    };
    std::map<int, std::uint64_t> topDifferences;  // by section: blocks different, for a feel of where
    std::map<std::string, int> allUnknown;        // states of the game that Falcon could not read, with how often
    for (int cz = -8; cz < 8; ++cz) {
        for (int cx = -8; cx < 8; ++cx) {
            const auto nbt = regions.chunk(cx, cz);
            VanillaChunk vanilla;
            if (!nbt || !readVanillaChunk(*nbt, *registry, vanilla)) {
                ++missing;
                continue;
            }
            worldgen::ChunkExtras extras;
            std::shared_ptr<Chunk> plain = generator->generate(cx, cz, extras);
            auto withFeatures = std::make_shared<Chunk>(*plain);
            decorator.decorate(*withFeatures, cx, cz, extras);
            ++chunks;
            for (const auto& [text, n] : vanilla.unknown) allUnknown[text] += n;
            if (std::getenv("FALCON_DUMP_TOPS") != nullptr && cx == 0 && cz == 0) {
                std::printf("[compare] chunk 0,0 natural surface: Falcon top / game's top per column (rows z, columns x)\n");
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        int vTop = kMinY - 1, fTop = kMinY - 1;
                        for (int y = kMaxY - 1; y >= kMinY && (vTop < kMinY || fTop < kMinY); --y) {
                            const int sy = (y - kMinY) >> 4;
                            const int i = ((y - kMinY) & 15) * 256 + z * 16 + x;
                            if (vTop < kMinY && vanilla.present[static_cast<std::size_t>(sy)] && naturalSolid(vanilla.blocks[static_cast<std::size_t>(sy)][static_cast<std::size_t>(i)])) vTop = y;
                            if (fTop < kMinY && naturalSolid(plain->at(x, y, z))) fTop = y;
                        }
                        std::printf(" %3d/%-3d", fTop, vTop);
                    }
                    std::printf("\n");
                }
            }
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    int vTop = kMinY - 1, fTop = kMinY - 1;
                    for (int y = kMaxY - 1; y >= kMinY && (vTop < kMinY || fTop < kMinY); --y) {
                        const int sy = (y - kMinY) >> 4;
                        const int i = ((y - kMinY) & 15) * 256 + z * 16 + x;
                        if (vTop < kMinY && vanilla.present[static_cast<std::size_t>(sy)] && naturalSolid(vanilla.blocks[static_cast<std::size_t>(sy)][static_cast<std::size_t>(i)])) vTop = y;
                        if (fTop < kMinY && naturalSolid(plain->at(x, y, z))) fTop = y;
                    }
                    ++columns;
                    const int d = std::abs(vTop - fTop);
                    if (d == 0) ++sameTop;
                    if (d <= 1) ++within1;
                    if (d <= 3) ++within3;
                    ++topDelta[std::min(d, 20)];
                }
            }
            for (int sy = 0; sy < kSectionCount; ++sy) {
                if (!vanilla.present[static_cast<std::size_t>(sy)]) continue;
                for (int i = 0; i < 4096; ++i) {
                    const int x = i & 15, z = (i >> 4) & 15, y = kMinY + sy * 16 + (i >> 8);
                    const Block v = vanilla.blocks[static_cast<std::size_t>(sy)][static_cast<std::size_t>(i)];
                    base.add(*registry, v, plain->at(x, y, z), y);
                    decorated.add(*registry, v, withFeatures->at(x, y, z), y);
                }
            }
        }
    }
    std::printf("[compare] %d chunks compared (%d of the 16 x 16 around the origin were not in the region files)\n", chunks, missing);
    if (!allUnknown.empty()) {
        std::printf("[compare] block states of the game that Falcon's registry could not read (%zu kinds), counted as air:\n", allUnknown.size());
        int shown = 0;
        for (const auto& [text, n] : allUnknown) {
            if (++shown > 12) break;
            std::printf("     %s  (x%d)\n", text.c_str(), n);
        }
    }
    report("Falcon's generator alone (terrain, aquifers, surface, ore veins) against the game's finished chunks", base);
    report("Falcon's generator plus its own decoration against the game's finished chunks", decorated);
    std::printf("\n== The natural surface (highest solid block that is not a tree or a plant), %llu columns\n", static_cast<unsigned long long>(columns));
    std::printf("   same height: %llu (%.2f %%)   within 1: %.2f %%   within 3: %.2f %%\n", static_cast<unsigned long long>(sameTop), 100.0 * static_cast<double>(sameTop) / static_cast<double>(std::max<std::uint64_t>(columns, 1)),
                100.0 * static_cast<double>(within1) / static_cast<double>(std::max<std::uint64_t>(columns, 1)), 100.0 * static_cast<double>(within3) / static_cast<double>(std::max<std::uint64_t>(columns, 1)));
    std::printf("   by how many blocks they differ:");
    for (const auto& [d, n] : topDelta) std::printf("  %d%s:%llu", d, d == 20 ? "+" : "", static_cast<unsigned long long>(n));
    std::printf("\n");
    return 0;
}

}  // namespace apex
