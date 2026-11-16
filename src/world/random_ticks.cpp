// What the random ticks do to the blocks that grow, spread and rot: grass spreading and dying, leaves that decay once their tree is gone, crops that ripen, sugar cane and cactus that
// grow, farmland that dries, saplings that become trees (the rules of the game's randomTick methods, with the numbers they use).
#include "falcon/world/block_physics.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

}  // namespace

// The largest value of a block's integer property (age 0..7 is 7); 0 when it has none.
static int maxOfProperty(const BlockType& type, const char* property) {
    for (const BlockProperty& p : type.properties) {
        if (p.name == property) return static_cast<int>(p.values.size()) - 1;
    }
    return 0;
}

void BlockPhysics::growthRandomTick(int x, int y, int z, Block b) {
    const StateInfo& st = registry_.state(b);
    const BlockType& type = registry_.type(static_cast<int>(st.block));
    const std::string& name = type.name;
    const auto sky = [&](int px, int py, int pz) {
        int s = 0, bl = 0;
        if (host_.light) host_.light(px, py, pz, s, bl);
        else s = 15;
        return std::max(s, bl);
    };
    const auto opaque = [&](Block at) {
        const StateInfo& info = registry_.state(at);
        return info.dampening >= 15 || info.has(kStateOpaqueCube);
    };
    const auto put = [&](int px, int py, int pz, Block block) {
        if (host_.setBlock) host_.setBlock(px, py, pz, block);
    };
    const auto number = [&](Block at, const char* property) {
        const std::string_view v = registry_.get(at, property);
        return v.empty() ? 0 : std::atoi(std::string(v).c_str());
    };

    if (name == "grass_block") {  // SpreadingSnowyDirtBlock: under a roof it turns to dirt; in the light it spreads to the dirt round it
        const Block above = block(x, y + 1, z);
        if (opaque(above) || (registry_.state(above).has(kStateWater) && registry_.state(above).fluidLevel == 0)) {
            if (const auto dirt = registry_.find("dirt")) put(x, y, z, *dirt);
            return;
        }
        if (sky(x, y + 1, z) < 9) return;
        for (int i = 0; i < 4; ++i) {
            const int bx = x + nextInt(3) - 1, by = y + nextInt(5) - 3, bz = z + nextInt(3) - 1;
            const Block target = block(bx, by, bz);
            if (registry_.type(static_cast<int>(registry_.state(target).block)).name != "dirt") continue;
            const Block over = block(bx, by + 1, bz);
            if (opaque(over) || registry_.state(over).has(kStateWater) || sky(bx, by + 1, bz) < 4) continue;
            if (const auto grass = registry_.find("grass_block")) put(bx, by, bz, *grass);
        }
        return;
    }
    if (endsWith(name, "_leaves")) {  // LeavesBlock.randomTick: no log within six blocks (through the leaves) and it rots away
        if (registry_.get(b, "persistent") == "true") return;
        std::vector<std::array<int, 4>> open{{x, y, z, 0}};
        std::set<std::array<int, 3>> seen{{x, y, z}};
        for (std::size_t i = 0; i < open.size() && i < 2400; ++i) {
            const auto [cx, cy, cz, d] = std::array<int, 4>(open[i]);
            static constexpr int kDir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const auto& dir : kDir) {
                const int nx = cx + dir[0], ny = cy + dir[1], nz = cz + dir[2];
                if (!seen.insert({nx, ny, nz}).second) continue;
                const Block n = block(nx, ny, nz);
                const std::string& nn = registry_.type(static_cast<int>(registry_.state(n).block)).name;
                if (endsWith(nn, "_log") || endsWith(nn, "_wood") || endsWith(nn, "_stem") || endsWith(nn, "_hyphae")) return;  // (a trunk is near: it stays)
                if (endsWith(nn, "_leaves") && d + 1 < 6) open.push_back({nx, ny, nz, d + 1});
            }
        }
        if (host_.drops) {
            for (const ItemStack& stack : host_.drops(b)) items_.spawn(stack, x + 0.5, y + 0.5, z + 0.5, 0.0, 0.1, 0.0, 10, nextFloat() * 6.2831855f);
        }
        put(x, y, z, Block::Air);
        return;
    }
    if (name == "wheat" || name == "carrots" || name == "potatoes" || name == "beetroots" || name == "nether_wart") {  // CropBlock.randomTick
        const int maxAge = maxOfProperty(type, "age");
        const int age = number(b, "age");
        if (age >= maxAge) return;
        const bool wart = name == "nether_wart";
        if (!wart && sky(x, y + 1, z) < 9) return;
        float speed = 1.0f;
        if (!wart) {
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const Block soil = block(x + dx, y - 1, z + dz);
                    float g = 0.0f;
                    if (registry_.type(static_cast<int>(registry_.state(soil).block)).name == "farmland") g = number(soil, "moisture") > 0 ? 3.0f : 1.0f;
                    if (dx != 0 || dz != 0) g /= 4.0f;
                    speed += g;
                }
            }
            const auto same = [&](int dx, int dz) { return registry_.state(block(x + dx, y, z + dz)).block == st.block; };
            const bool rows = same(-1, 0) || same(1, 0), columns = same(0, -1) || same(0, 1), diagonals = same(-1, -1) || same(1, -1) || same(-1, 1) || same(1, 1);
            if ((rows && columns) || diagonals) speed /= 2.0f;
        }
        if (nextInt(wart ? 10 : static_cast<int>(25.0f / speed) + 1) == 0) put(x, y, z, registry_.with(b, "age", std::to_string(age + 1)));
        return;
    }
    if (name == "sugar_cane" || name == "cactus") {  // grows a block at the top, up to three high, when it has ripened
        if (!registry_.isAir(block(x, y + 1, z))) return;
        int height = 1;
        while (height < 3 && registry_.type(static_cast<int>(registry_.state(block(x, y - height, z)).block)).name == name) ++height;
        if (height >= 3) return;
        const int age = number(b, "age");
        if (age >= 15) {
            put(x, y + 1, z, registry_.defaultState(static_cast<int>(st.block)));
            put(x, y, z, registry_.with(b, "age", "0"));
        } else {
            put(x, y, z, registry_.with(b, "age", std::to_string(age + 1)));
        }
        return;
    }
    if (name == "farmland") {  // FarmBlock.randomTick: it drinks from water within four blocks, and dries out into dirt without
        bool water = false;
        for (int dx = -4; dx <= 4 && !water; ++dx) {
            for (int dz = -4; dz <= 4 && !water; ++dz) {
                for (int dy = 0; dy <= 1 && !water; ++dy) water = registry_.state(block(x + dx, y + dy, z + dz)).has(kStateWater);
            }
        }
        const int moisture = number(b, "moisture");
        if (water) {
            if (moisture < 7) put(x, y, z, registry_.with(b, "moisture", "7"));
        } else if (moisture > 0) {
            put(x, y, z, registry_.with(b, "moisture", std::to_string(moisture - 1)));
        } else {
            const std::string& above = registry_.type(static_cast<int>(registry_.state(block(x, y + 1, z)).block)).name;
            if (above != "wheat" && above != "carrots" && above != "potatoes" && above != "beetroots" && above != "pumpkin_stem" && above != "melon_stem")
                if (const auto dirt = registry_.find("dirt")) put(x, y, z, *dirt);
        }
        return;
    }
    if (endsWith(name, "_sapling")) {  // SaplingBlock.randomTick: in the light, one tick in seven moves the stage on, and the second stage is a tree
        if (sky(x, y + 1, z) < 9 || nextInt(7) != 0) return;
        if (number(b, "stage") == 0) {
            put(x, y, z, registry_.with(b, "stage", "1"));
            return;
        }
        const std::string species = name.substr(0, name.size() - 8);
        const auto log = registry_.find(species + "_log"), leaves = registry_.find(species + "_leaves");
        if (!log || !leaves) return;
        const int height = 4 + nextInt(3);
        for (int k = 1; k <= height + 1; ++k) {
            if (!registry_.isAir(block(x, y + k, z)) && !endsWith(registry_.type(static_cast<int>(registry_.state(block(x, y + k, z)).block)).name, "_leaves")) return;
        }
        const Block leafBlock = registry_.with(*leaves, "persistent", "false");
        for (int dy = height - 2; dy <= height + 1; ++dy) {
            const int r = dy > height - 1 ? 1 : 2;
            for (int dx = -r; dx <= r; ++dx) {
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::abs(dx) == r && std::abs(dz) == r && (dy > height - 1 || nextInt(2) == 0)) continue;
                    if (registry_.isAir(block(x + dx, y + dy, z + dz))) put(x + dx, y + dy, z + dz, leafBlock);
                }
            }
        }
        for (int k = 0; k < height; ++k) put(x, y + k, z, *log);
        return;
    }
}

}  // namespace falcon
