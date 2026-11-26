#include "falcon/world/decoration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <unordered_set>
#include <utility>

namespace falcon {
namespace {

constexpr int kSeaLevel = 63;

std::uint64_t mix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
std::uint64_t columnHash(std::int64_t seed, int x, int z, std::uint64_t salt) {
    return mix64(static_cast<std::uint64_t>(seed) ^ mix64(static_cast<std::uint64_t>(static_cast<std::int64_t>(x)) * 0x9E3779B97F4A7C15ULL ^
                                                          static_cast<std::uint64_t>(static_cast<std::int64_t>(z)) * 0xC2B2AE3D27D4EB4FULL ^ salt));
}
double unit(std::uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

}  // namespace

Block Decorator::named(const char* text) const { return registry_.parse(text).value_or(Block::Air); }

Decorator::Decorator(const worldgen::OverworldGenerator& generator, const BlockRegistry& registry, std::int64_t seed)
    : generator_(generator), registry_(registry), seed_(seed) {
    Blocks& b = b_;
    b.oakLog = named("oak_log[axis=y]");
    b.oakLeaves = named("oak_leaves[persistent=true]");
    b.birchLog = named("birch_log[axis=y]");
    b.birchLeaves = named("birch_leaves[persistent=true]");
    b.spruceLog = named("spruce_log[axis=y]");
    b.spruceLeaves = named("spruce_leaves[persistent=true]");
    b.acaciaLog = named("acacia_log[axis=y]");
    b.acaciaLeaves = named("acacia_leaves[persistent=true]");
    b.jungleLog = named("jungle_log[axis=y]");
    b.jungleLeaves = named("jungle_leaves[persistent=true]");
    b.cherryLog = named("cherry_log[axis=y]");
    b.cherryLeaves = named("cherry_leaves[persistent=true]");
    b.darkOakLog = named("dark_oak_log[axis=y]");
    b.darkOakLeaves = named("dark_oak_leaves[persistent=true]");
    b.shortGrass = named("short_grass");
    b.tallGrassLower = named("tall_grass[half=lower]");
    b.tallGrassUpper = named("tall_grass[half=upper]");
    b.fern = named("fern");
    b.deadBush = named("dead_bush");
    b.cactus = named("cactus");
    b.snow = named("snow[layers=1]");
    b.ice = named("ice");
    b.packedIce = named("packed_ice");
    b.snowBlock = named("snow_block");
    b.seagrass = named("seagrass");
    b.kelp = named("kelp");
    b.kelpPlant = named("kelp_plant");
    b.sugarCane = named("sugar_cane");
    b.grassBlock = named("grass_block");
    b.dirt = named("dirt");
    b.podzol = named("podzol");
    b.sand = named("sand");
    b.redSand = named("red_sand");
    b.water = named("water");
    b.moss = named("moss_block");
    b.lilyPad = named("lily_pad");
    const auto list = [&](std::initializer_list<const char*> names) {
        std::vector<Block> out;
        for (const char* n : names) {
            const Block block = named(n);
            if (!registry_.isAir(block)) out.push_back(block);
        }
        return out;
    };
    b.flowersPlains = list({"poppy", "dandelion", "azure_bluet", "oxeye_daisy", "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "cornflower"});
    b.flowersForest = list({"poppy", "dandelion", "lily_of_the_valley", "allium"});
    b.flowersMeadow = list({"dandelion", "azure_bluet", "oxeye_daisy", "cornflower", "allium", "poppy"});
    b.flowersFlower = list({"poppy", "dandelion", "allium", "azure_bluet", "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley"});
    b.flowersSwamp = list({"blue_orchid"});
    b.sunflowerLower = named("sunflower[half=lower]");
    b.sunflowerUpper = named("sunflower[half=upper]");
    b.roseBushLower = named("rose_bush[half=lower]");
    b.roseBushUpper = named("rose_bush[half=upper]");
    b.lilacLower = named("lilac[half=lower]");
    b.lilacUpper = named("lilac[half=upper]");
    b.peonyLower = named("peony[half=lower]");
    b.peonyUpper = named("peony[half=upper]");

    const worldgen::BiomeRegistry& biomes = generator_.biomes();
    profiles_.resize(biomes.size());
    for (std::size_t i = 0; i < biomes.size(); ++i) {
        profiles_[i] = profileFor(biomes.name(static_cast<worldgen::BiomeId>(i)));
        profiles_[i].snow = biomes.info(static_cast<worldgen::BiomeId>(i)).temperature < 0.15f;
    }
}

worldgen::LodTrees Decorator::treeFacts(worldgen::BiomeId biome) const {
    worldgen::LodTrees out;
    if (biome >= profiles_.size()) return out;
    const Profile& p = profiles_[biome];
    out.kind = static_cast<int>(p.trees[0]);
    if (out.kind == 0) return out;
    out.perChunk = p.treesPerChunk;
    static constexpr int kHeights[8] = {0, 6, 6, 8, 6, 11, 6, 7};  // by kind: the middle of what placeTree makes
    out.height = kHeights[out.kind];
    return out;
}

// What each biome grows. The tree counts and the patch counts follow the game's own placed features: grass comes in a
// few patches a chunk, while flowers and tall grass sit behind a rarity filter (1 in 32 chunks for plains and forest),
// so most chunks have none and the ones that do have a clump.
Decorator::Profile Decorator::profileFor(const std::string& full) const {
    std::string n = full;
    if (n.rfind("minecraft:", 0) == 0) n.erase(0, 10);
    Profile p;
    using T = Tree;
    const auto set = [&p](Plant plant, float patches) { p.patches[static_cast<std::size_t>(plant)] = patches; };
    const auto trees = [&p](float perChunk, T a, T b, T c) {
        p.treesPerChunk = perChunk;
        p.trees[0] = a;
        p.trees[1] = b;
        p.trees[2] = c;
    };
    if (contains(n, "ocean") || contains(n, "river") || contains(n, "beach") || contains(n, "shore")) {
        p.seaPlants = contains(n, "ocean") || contains(n, "river");
        return p;
    }
    if (n == "plains" || n == "sunflower_plains") {
        trees(0.05f, T::Oak, T::None, T::None);
        set(Plant::Grass, 4.0f);
        set(Plant::Flower, 0.30f);  // flower_plains: 4-15 tries behind a 1 in 32 filter
        set(Plant::TallGrass, 0.20f);
        if (n == "sunflower_plains") {
            set(Plant::Sunflower, 0.33f);  // patch_sunflower: a 1 in 3 filter
            p.flowerSet = "plains";
        }
    } else if (n == "forest") {
        trees(9.0f, T::Oak, T::Oak, T::Birch);
        set(Plant::Grass, 2.0f);
        set(Plant::Flower, 0.12f);
        p.flowerSet = "forest";
    } else if (n == "flower_forest") {
        trees(6.0f, T::Oak, T::Birch, T::Birch);
        set(Plant::Grass, 2.0f);
        set(Plant::Flower, 3.0f);  // the one biome that really is covered in flowers
        p.flowerSet = "flower";
    } else if (contains(n, "birch_forest")) {
        trees(9.0f, T::Birch, T::Birch, T::Birch);
        set(Plant::Grass, 2.0f);
        set(Plant::Flower, 0.12f);
        set(Plant::TallGrass, 0.15f);
        p.flowerSet = "forest";
    } else if (n == "dark_forest" || n == "pale_garden") {
        trees(14.0f, T::DarkOak, T::DarkOak, T::Oak);
        set(Plant::Grass, 1.0f);
        set(Plant::Flower, 0.10f);
        p.flowerSet = "forest";
    } else if (contains(n, "taiga")) {
        trees(10.0f, T::Spruce, T::Spruce, T::Spruce);
        set(Plant::Grass, 1.0f);
        set(Plant::Fern, 2.0f);
        set(Plant::Flower, 0.08f);
        p.flowerSet = "forest";
    } else if (n == "snowy_plains" || n == "ice_spikes") {
        trees(0.3f, T::Spruce, T::None, T::None);
        set(Plant::Grass, 0.3f);
    } else if (contains(n, "jungle")) {
        trees(contains(n, "sparse") ? 3.0f : 18.0f, T::Jungle, T::Jungle, T::Oak);
        set(Plant::Grass, contains(n, "sparse") ? 6.0f : 10.0f);
        set(Plant::Fern, 3.0f);
        set(Plant::TallGrass, 1.0f);
        p.flowerSet = "forest";
    } else if (contains(n, "savanna")) {
        trees(1.2f, T::Acacia, T::Acacia, T::Oak);
        set(Plant::Grass, 6.0f);
        set(Plant::TallGrass, 0.5f);
    } else if (n == "desert") {
        set(Plant::Cactus, 1.0f / 6.0f);  // patch_cactus_desert: rarity_filter 6, then 10 tries
        set(Plant::DeadBush, 2.0f);       // patch_dead_bush_2: count 2, then 4 tries
    } else if (contains(n, "badlands")) {
        trees(contains(n, "wooded") ? 3.0f : 0.0f, T::Oak, T::Oak, T::Oak);
        set(Plant::Cactus, 1.0f / 13.0f);  // patch_cactus_decorated: rarity_filter 13, then 10 tries
        set(Plant::DeadBush, 20.0f);       // patch_dead_bush_badlands: count 20, then 4 tries
    } else if (n == "swamp" || n == "mangrove_swamp") {
        trees(2.5f, T::Oak, T::Oak, T::Oak);
        set(Plant::Grass, 2.0f);
        set(Plant::Flower, 0.15f);
        p.flowerSet = "swamp";
    } else if (n == "meadow") {
        trees(0.05f, T::Oak, T::None, T::None);
        set(Plant::Grass, 4.0f);
        set(Plant::Flower, 2.0f);  // meadows are flowery
        p.flowerSet = "meadow";
    } else if (n == "cherry_grove") {
        trees(5.0f, T::Cherry, T::Cherry, T::Cherry);
        set(Plant::Grass, 3.0f);
        set(Plant::Flower, 1.0f);
        p.flowerSet = "meadow";
    } else if (n == "grove") {
        trees(5.0f, T::Spruce, T::Spruce, T::Spruce);
    } else if (contains(n, "windswept") || n == "stony_shore") {
        trees(1.5f, T::Spruce, T::Oak, T::Spruce);
        set(Plant::Grass, 1.0f);
    }
    // snowy_slopes, peaks, mushroom fields and the cave biomes grow nothing here.
    return p;
}

void Decorator::placeTree(std::vector<Edit>& edits, Tree kind, int chunkX, int chunkZ, int wx, int groundY, int wz, std::uint64_t h) const {
    const int x0 = chunkX * 16, z0 = chunkZ * 16;
    const auto put = [&](int x, int y, int z, Block block, bool onlyIfAir) {
        if (x < x0 || x >= x0 + 16 || z < z0 || z >= z0 + 16 || y < kMinY || y >= kMaxY) return;  // the neighbour chunk draws its own part
        edits.push_back({static_cast<std::uint8_t>(x - x0), static_cast<std::uint8_t>(z - z0), static_cast<std::int16_t>(y), block, onlyIfAir});
    };
    std::uint64_t state = mix64(h);
    const auto rnd = [&](int bound) {
        state = mix64(state);
        return static_cast<int>((state >> 33) % static_cast<std::uint64_t>(bound));
    };
    Block log = b_.oakLog, leaves = b_.oakLeaves;
    int height = 4 + rnd(3);
    switch (kind) {
        case Tree::Birch: log = b_.birchLog; leaves = b_.birchLeaves; height = 5 + rnd(3); break;
        case Tree::Spruce: log = b_.spruceLog; leaves = b_.spruceLeaves; height = 6 + rnd(4); break;
        case Tree::Acacia: log = b_.acaciaLog; leaves = b_.acaciaLeaves; height = 5 + rnd(3); break;
        case Tree::Jungle: log = b_.jungleLog; leaves = b_.jungleLeaves; height = 8 + rnd(6); break;
        case Tree::Cherry: log = b_.cherryLog; leaves = b_.cherryLeaves; height = 5 + rnd(2); break;
        case Tree::DarkOak: log = b_.darkOakLog; leaves = b_.darkOakLeaves; height = 6 + rnd(3); break;
        default: break;
    }
    const int top = groundY + height;  // the y of the highest log
    // A round blob of leaves: radius r around (cx, cz) at height y, corners of the wide layers taken off at random.
    const auto blob = [&](int cx, int cz, int y, int r, bool roundCorners) {
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (roundCorners && std::abs(dx) == r && std::abs(dz) == r && (r == 1 || rnd(2) == 0)) continue;
                put(wx + cx + dx, y, wz + cz + dz, leaves, true);
            }
        }
    };
    switch (kind) {
        case Tree::Spruce: {
            for (int y = groundY + 1; y <= top; ++y) put(wx, y, wz, log, false);
            // A cone: the layers widen going down, 0 1 2 1 2 3 ...
            for (int y = top + 1; y >= groundY + 3; --y) {
                const int k = top + 1 - y;
                const int r = k == 0 ? 0 : std::min(3, (k + 1) / 2);
                for (int dz = -r; dz <= r; ++dz) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > r + (r > 1 ? 1 : 0)) continue;
                        put(wx + dx, y, wz + dz, leaves, true);
                    }
                }
            }
            break;
        }
        case Tree::Acacia: {
            static constexpr int kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            const int* d = kDir[rnd(4)];
            const int bend = groundY + 3 + rnd(2);
            int cx = wx, cz = wz;
            for (int y = groundY + 1; y <= top; ++y) {
                if (y > bend) {
                    cx += d[0];
                    cz += d[1];
                    if (y > bend + 1 && y > top - 1) { /* stays */ }
                }
                put(cx, y, cz, log, false);
            }
            // A flat canopy: wide below, small on top.
            for (int dz = -3; dz <= 3; ++dz) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (std::abs(dx) + std::abs(dz) > 4 || (std::abs(dx) == 3 && std::abs(dz) == 3)) continue;
                    put(cx + dx, top, cz + dz, leaves, true);
                }
            }
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (std::abs(dx) + std::abs(dz) > 3) continue;
                    put(cx + dx, top + 1, cz + dz, leaves, true);
                }
            }
            break;
        }
        case Tree::Jungle: {
            // A jungle grows two kinds of tree (JungleTreeFeature and MegaJungleTreeFeature): a small one with a single trunk and a canopy like an oak's, and
            // a great one whose trunk is 2 x 2 and twice as tall, with vines hanging off its sides. A great one is only grown where all four of its trunk
            // columns are in this chunk, because the next chunk along would not draw its half.
            const bool mega = height >= 10 && wx + 1 < x0 + 16 && wz + 1 < z0 + 16;
            if (mega) {
                for (int y = groundY + 1; y <= top; ++y) {
                    put(wx, y, wz, log, false);
                    put(wx + 1, y, wz, log, false);
                    put(wx, y, wz + 1, log, false);
                    put(wx + 1, y, wz + 1, log, false);
                }
                // The canopy: five layers over the top of the trunk, the lowest the widest.
                for (int layer = 0; layer < 5; ++layer) {
                    const int y = top + 2 - layer;
                    const int r = layer == 0 ? 1 : layer == 1 ? 2 : 3;
                    for (int dz = -r; dz <= r; ++dz) {
                        for (int dx = -r; dx <= r; ++dx) {
                            if (std::abs(dx) == r && std::abs(dz) == r && rnd(2) == 0) continue;
                            put(wx + dx, y, wz + dz, leaves, true);
                            put(wx + 1 + dx, y, wz + dz, leaves, true);
                        }
                    }
                }
            } else {
                for (int y = groundY + 1; y <= top; ++y) put(wx, y, wz, log, false);
                blob(0, 0, top - 2, 2, true);
                blob(0, 0, top - 1, 3, true);
                blob(0, 0, top, 3, true);
                blob(0, 0, top + 1, 2, true);
            }
            // Vines (JungleTreeDecorator, VinesFeature): a hanging column down the side of the trunk, the face it is drawn on turned towards the trunk.
            // (fx, fz) steps out of the trunk; (ax, az) walks along the trunk's face, which a great tree has two columns of.
            struct Side {
                int fx, fz, ax, az;
                const char* face;
            };
            static constexpr Side kSides[4] = {{-1, 0, 0, 1, "east"}, {1, 0, 0, 1, "west"}, {0, -1, 1, 0, "south"}, {0, 1, 1, 0, "north"}};
            for (const Side& side : kSides) {
                for (int column = 0; column < (mega ? 2 : 1); ++column) {
                    if (rnd(3) != 0) continue;  // not every side of every tree has one
                    const int vx = wx + side.fx + (mega ? side.ax * column : 0);
                    const int vz = wz + side.fz + (mega ? side.az * column : 0);
                    const Block vine = named((std::string("vine[") + side.face + "=true]").c_str());
                    int y = top - rnd(3);
                    for (int length = 1 + rnd(6); length > 0 && y > groundY; --y, --length) put(vx, y, vz, vine, true);
                }
            }
            break;
        }
        case Tree::Cherry: {
            for (int y = groundY + 1; y <= top; ++y) put(wx, y, wz, log, false);
            blob(0, 0, top - 2, 2, true);
            blob(0, 0, top - 1, 3, true);
            blob(0, 0, top, 3, true);
            blob(0, 0, top + 1, 2, true);
            break;
        }
        case Tree::DarkOak: {
            for (int y = groundY + 1; y <= top; ++y) put(wx, y, wz, log, false);
            blob(0, 0, top - 2, 2, true);
            blob(0, 0, top - 1, 3, true);
            blob(0, 0, top, 3, true);
            blob(0, 0, top + 1, 2, true);
            break;
        }
        default: {  // oak and birch
            for (int y = groundY + 1; y <= top; ++y) put(wx, y, wz, log, false);
            blob(0, 0, top - 2, 2, true);
            blob(0, 0, top - 1, 2, true);
            blob(0, 0, top, 1, true);
            blob(0, 0, top + 1, 1, true);
            break;
        }
    }
}

void Decorator::decorate(Chunk& chunk, int chunkX, int chunkZ, const worldgen::ChunkExtras& extras) const {
    std::vector<Edit> edits;
    edits.reserve(512);
    const int x0 = chunkX * 16, z0 = chunkZ * 16;
    const BlockRegistry& reg = registry_;
    const int grassIndex = reg.state(b_.grassBlock).block, podzolIndex = reg.state(b_.podzol).block;
    const int sandIndex = reg.state(b_.sand).block, redSandIndex = reg.state(b_.redSand).block, mossIndex = reg.state(b_.moss).block;
    // What a dead bush may stand on besides sand (the game's supports_dry_vegetation: terracotta, and the grounds plants grow on).
    std::unordered_set<int> dryVegetationGround;
    for (const char* name : {"terracotta", "white_terracotta", "orange_terracotta", "magenta_terracotta", "light_blue_terracotta", "yellow_terracotta", "lime_terracotta", "pink_terracotta",
                             "gray_terracotta", "light_gray_terracotta", "cyan_terracotta", "purple_terracotta", "blue_terracotta", "brown_terracotta", "green_terracotta", "red_terracotta",
                             "black_terracotta", "dirt", "coarse_dirt", "rooted_dirt", "podzol", "grass_block", "mycelium", "moss_block", "mud", "farmland"}) {
        const Block block = named(name);
        if (!registry_.isAir(block)) dryVegetationGround.insert(reg.state(block).block);
    }
    std::vector<std::array<int, 2>> cactusColumns;  // (the cacti this chunk has laid down so far, by world column)

    // ---- the ground: snow, ice and sea plants, column by column (this chunk's own columns) ----
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const int wx = x0 + x, wz = z0 + z;
            const int surface = extras.heightmaps.worldSurface[static_cast<std::size_t>(x + 16 * z)] - 1;  // the highest block
            if (surface < kMinY || surface + 1 >= kMaxY) continue;
            const Block top = chunk.at(x, surface, z);
            const worldgen::BiomeId biomeId = extras.biomes.at(x >> 2, surface >> 2, z >> 2);
            const Profile& profile = profiles_[biomeId < profiles_.size() ? biomeId : 0];
            const double u = unit(columnHash(seed_, wx, wz, 0x11));
            const double v = unit(columnHash(seed_, wx, wz, 0x22));
            const StateInfo& info = reg.state(top);
            if (info.has(kStateLiquid)) {
                if (!info.has(kStateWater)) continue;
                if (profile.snow) {  // the sea freezes over in the cold
                    edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface), b_.ice, false});
                    continue;
                }
                if (profile.seaPlants) {
                    const int floorY = extras.heightmaps.oceanFloor[static_cast<std::size_t>(x + 16 * z)] - 1;
                    const int depth = surface - floorY;
                    if (floorY < kMinY || depth < 1) continue;
                    const Block floorBlock = chunk.at(x, floorY, z);
                    if (reg.state(floorBlock).has(kStateLiquid) || reg.isAir(floorBlock)) continue;
                    if (u < 0.08) {
                        edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(floorY + 1), b_.seagrass, true});
                    } else if (u < 0.105 && depth >= 4) {  // kelp: a stalk of 2..depth-1 blocks, topped by the tip
                        const int length = 2 + static_cast<int>(v * std::max(1, depth - 3));
                        for (int k = 1; k <= length && floorY + k < surface; ++k) {
                            edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(floorY + k),
                                             k == length ? b_.kelp : b_.kelpPlant, true});
                        }
                    }
                } else if (profile.flowerSet != nullptr && std::string(profile.flowerSet) == "swamp" && surface + 1 < kMaxY && u < 0.04 && surface == kSeaLevel - 1) {
                    edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface + 1), b_.lilyPad, true});
                }
                continue;
            }
            if (reg.isAir(top)) continue;
            // Snow lies on the ground in the cold biomes (the game's freeze_top_layer, which is also per column).
            if (profile.snow && info.has(kStateFullCollision) && reg.isAir(chunk.at(x, surface + 1, z))) {
                const int blockIndex = info.block;
                if (blockIndex == grassIndex || blockIndex == podzolIndex) {
                    edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface), reg.with(top, "snowy", "true"), false});
                }
                edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface + 1), b_.snow, true});
            }
        }
    }

    // ---- plants, in patches ----
    // Each patch starts somewhere in a chunk and scatters its tries around that spot, so plants grow in clumps with bare
    // ground between, as the game's random_patch features do. Patches of the 8 neighbouring chunks reach into this one, so
    // every chunk also lays down the parts that fall inside it: the borders match whatever order chunks are made in.
    struct PatchKind {
        Plant plant;
        Block block;
        Block upper;   // the top half of a two block tall plant (Air: one block tall)
        int tries;
        int spread;
        std::uint64_t salt;
    };
    const PatchKind kKinds[] = {
        {Plant::Grass, b_.shortGrass, Block::Air, 32, 7, 0x51},
        {Plant::Fern, b_.fern, Block::Air, 32, 7, 0x52},
        {Plant::TallGrass, b_.tallGrassLower, b_.tallGrassUpper, 48, 7, 0x53},
        {Plant::Flower, Block::Air, Block::Air, 48, 6, 0x54},  // the flower itself is picked per plant
        {Plant::Sunflower, b_.sunflowerLower, b_.sunflowerUpper, 48, 7, 0x55},
        {Plant::Cactus, b_.cactus, Block::Air, 10, 7, 0x56},
        {Plant::DeadBush, b_.deadBush, Block::Air, 4, 7, 0x57},
    };
    for (int dcz = -1; dcz <= 1; ++dcz) {
        for (int dcx = -1; dcx <= 1; ++dcx) {
            const int ncx = chunkX + dcx, ncz = chunkZ + dcz;
            for (const PatchKind& kind : kKinds) {
                // How many patches this chunk has of this plant: the same everywhere in the chunk, from the biome at its middle.
                const worldgen::BiomeId midBiome = dcx == 0 && dcz == 0
                                                       ? extras.biomes.at(1, (extras.heightmaps.worldSurface[8 + 16 * 8] - 1) >> 2, 1)
                                                       : generator_.biomeAt(ncx * 16 + 8, kSeaLevel, ncz * 16 + 8);
                const Profile& midProfile = profiles_[midBiome < profiles_.size() ? midBiome : 0];
                const float want = midProfile.patchesOf(kind.plant);
                if (want <= 0.0f) continue;
                const std::uint64_t chunkSeed = columnHash(seed_, ncx, ncz, kind.salt);
                int count = static_cast<int>(want);
                if (unit(mix64(chunkSeed)) < static_cast<double>(want) - count) ++count;  // a fraction is the chance of one more
                for (int patch = 0; patch < count; ++patch) {
                    std::uint64_t state = mix64(chunkSeed + static_cast<std::uint64_t>(patch) * 0x9E3779B97F4A7C15ULL);
                    const auto rnd = [&state](int bound) {
                        state = mix64(state);
                        return static_cast<int>((state >> 33) % static_cast<std::uint64_t>(bound));
                    };
                    const int originX = ncx * 16 + rnd(16);
                    const int originZ = ncz * 16 + rnd(16);
                    // Cacti and dead bushes try at a height too: the game takes the height of the ground at the patch's own spot and moves each try up or down by up to 3 (a trapezoid, like
                    // the sideways move), and the try only counts if it is in the air with something to stand on. On flat sand that is one try in four (the roll is 0), on a dune the
                    // slope decides: far fewer than a plant at the ground of every try.
                    const bool movesInHeight = kind.plant == Plant::Cactus || kind.plant == Plant::DeadBush;
                    int originHeight = 0;
                    if (movesInHeight) {
                        const bool inside = originX >= x0 && originX < x0 + 16 && originZ >= z0 && originZ < z0 + 16;
                        originHeight = inside ? extras.heightmaps.worldSurface[static_cast<std::size_t>((originX - x0) + 16 * (originZ - z0))] : generator_.surfaceHeightAt(originX, originZ) + 1;
                    }
                    for (int t = 0; t < kind.tries; ++t) {
                        // The game's trapezoid offset: two rolls added, so the middle of the patch is the thickest.
                        const int ox = (rnd(kind.spread + 1) + rnd(kind.spread + 1)) - kind.spread;
                        const int oz = (rnd(kind.spread + 1) + rnd(kind.spread + 1)) - kind.spread;
                        const int oy = movesInHeight ? rnd(4) + rnd(4) - 3 : 0;
                        const int roll = rnd(1024);
                        const int wx = originX + ox, wz = originZ + oz;
                        if (wx < x0 || wx >= x0 + 16 || wz < z0 || wz >= z0 + 16) continue;  // another chunk lays this one down
                        const int x = wx - x0, z = wz - z0;
                        const int surface = extras.heightmaps.worldSurface[static_cast<std::size_t>(x + 16 * z)] - 1;
                        if (surface < kMinY || surface + 2 >= kMaxY) continue;
                        // The biome where the plant actually lands has to grow it too (the game's biome filter).
                        const worldgen::BiomeId here = extras.biomes.at(x >> 2, surface >> 2, z >> 2);
                        const Profile& hereProfile = profiles_[here < profiles_.size() ? here : 0];
                        if (hereProfile.patchesOf(kind.plant) <= 0.0f || hereProfile.snow) continue;
                        if (movesInHeight && originHeight + oy != surface + 1) continue;  // (not in the air on the ground here)
                        const Block top = chunk.at(x, surface, z);
                        const StateInfo& topInfo = reg.state(top);
                        if (!topInfo.has(kStateFullCollision) || topInfo.has(kStateLiquid)) continue;
                        if (!reg.isAir(chunk.at(x, surface + 1, z))) continue;
                        const int blockIndex = topInfo.block;
                        const bool grassy = blockIndex == grassIndex || blockIndex == podzolIndex || blockIndex == mossIndex;
                        const bool sandy = blockIndex == sandIndex || blockIndex == redSandIndex;
                        Block lower = kind.block, upper = kind.upper;
                        if (kind.plant == Plant::Cactus) {
                            if (!sandy) continue;  // (the game's tag of what a cactus stands on: sand, red sand)
                        } else if (kind.plant == Plant::DeadBush) {
                            if (!sandy && !dryVegetationGround.count(blockIndex)) continue;  // (sand, terracotta, dirt and the grounds of plants)
                        } else if (!grassy) {
                            continue;
                        }
                        if (kind.plant == Plant::Flower) {
                            const std::string setName = hereProfile.flowerSet ? hereProfile.flowerSet : "plains";
                            const std::vector<Block>& flowers = setName == "forest" ? b_.flowersForest : setName == "meadow" ? b_.flowersMeadow
                                                                : setName == "flower" ? b_.flowersFlower : setName == "swamp" ? b_.flowersSwamp
                                                                                                                             : b_.flowersPlains;
                            if (flowers.empty()) continue;
                            if (setName == "flower" && roll < 70) {  // a flower forest also has the tall ones
                                const int which = roll % 3;
                                lower = which == 0 ? b_.lilacLower : which == 1 ? b_.roseBushLower : b_.peonyLower;
                                upper = which == 0 ? b_.lilacUpper : which == 1 ? b_.roseBushUpper : b_.peonyUpper;
                            } else {
                                lower = flowers[static_cast<std::size_t>(roll) % flowers.size()];
                                upper = Block::Air;
                            }
                        }
                        if (registry_.isAir(lower)) continue;
                        if (!registry_.isAir(upper) && !reg.isAir(chunk.at(x, surface + 2, z))) continue;
                        if (kind.plant == Plant::Cactus) {
                            // A cactus lives only where nothing solid is beside its foot (CactusBlock.canSurvive): not against a dune's slope, not next to another cactus.
                            bool crowded = false;
                            for (const auto& [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                                const int nx = x + dx, nz = z + dz;
                                if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && reg.state(chunk.at(nx, surface + 1, nz)).has(kStateFullCollision)) crowded = true;
                                for (const auto& column : cactusColumns) crowded = crowded || (column[0] == wx + dx && column[1] == wz + dz);
                            }
                            if (crowded) continue;
                            cactusColumns.push_back({wx, wz});
                            // 1 to 3 blocks tall, mostly 1 (BiasedToBottomInt: the height is 1 + a roll of a roll): 61% one, 28% two, 11% three.
                            const int inner = (roll >> 2) % 3;
                            const int height = 1 + ((roll >> 5) % (inner + 1));
                            for (int k = 1; k <= height && surface + k < kMaxY; ++k) {
                                edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface + k), lower, true});
                            }
                            continue;
                        }
                        edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface + 1), lower, true});
                        if (!registry_.isAir(upper)) {
                            edits.push_back({static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(z), static_cast<std::int16_t>(surface + 2), upper, true});
                        }
                    }
                }
            }
        }
    }

    // ---- trees: decided from their own position, so the neighbouring chunks agree on them ----
    constexpr double kCandidateCap = 0.09;  // no biome grows more than about 14 trees a chunk, so nothing above this can be a tree
    for (int wz = z0 - 3; wz < z0 + 19; ++wz) {
        for (int wx = x0 - 3; wx < x0 + 19; ++wx) {
            const double u = unit(columnHash(seed_, wx, wz, 0x33));
            if (u >= kCandidateCap) continue;
            bool beaten = false;  // the tree with the smallest number within two blocks stays, so trunks are never crowded
            for (int dz = -2; dz <= 2 && !beaten; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if ((dx != 0 || dz != 0) && unit(columnHash(seed_, wx + dx, wz + dz, 0x33)) < u) {
                        beaten = true;
                        break;
                    }
                }
            }
            if (beaten) continue;
            const bool inside = wx >= x0 && wx < x0 + 16 && wz >= z0 && wz < z0 + 16;
            int ground;
            worldgen::BiomeId biomeId;
            if (inside) {
                const int x = wx - x0, z = wz - z0;
                ground = extras.heightmaps.worldSurface[static_cast<std::size_t>(x + 16 * z)] - 1;
                biomeId = extras.biomes.at(x >> 2, ground >> 2, z >> 2);
                const Block top = chunk.at(x, ground, z);
                const StateInfo& ti = reg.state(top);
                if (ti.has(kStateLiquid) || !ti.has(kStateFullCollision)) continue;  // not on water, plants or air
            } else {
                ground = generator_.surfaceHeightAt(wx, wz);
                biomeId = generator_.biomeAt(wx, ground, wz);
            }
            if (ground <= kSeaLevel - 1 || ground >= 200) continue;
            const Profile& profile = profiles_[biomeId < profiles_.size() ? biomeId : 0];
            const double density = profile.treesPerChunk / 256.0 * 1.7;  // (some candidates are beaten by a neighbour)
            if (u >= density || profile.trees[0] == Tree::None) continue;
            const std::uint64_t h = columnHash(seed_, wx, wz, 0x44);
            const Tree kind = profile.trees[(h >> 40) % 3];
            if (kind == Tree::None) continue;
            placeTree(edits, kind, chunkX, chunkZ, wx, ground, wz, h);
        }
    }

    // ---- apply: one rebuilt section per touched section ----
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return sectionOf(a.y) < sectionOf(b.y); });
    std::size_t i = 0;
    while (i < edits.size()) {
        const int sy = sectionOf(edits[i].y);
        SectionBuilder builder;
        builder.load(chunk.sections[static_cast<std::size_t>(sy)]);
        for (; i < edits.size() && sectionOf(edits[i].y) == sy; ++i) {
            const Edit& e = edits[i];
            const int ly = (e.y - kMinY) & 15;
            if (e.onlyIfAir) {
                const StateInfo& current = reg.state(builder.get(e.x, ly, e.z));
                if (!current.has(kStateAir) && !(current.has(kStateReplaceable) && !current.has(kStateLiquid))) continue;
            }
            builder.set(e.x, ly, e.z, e.block);
        }
        chunk.sections[static_cast<std::size_t>(sy)] = builder.build();
    }
}

}  // namespace falcon
