// How mobs come and go: the animals a chunk starts with, the monsters that appear where it is dark (NaturalSpawner, in a simpler form), and the ones that fade away.
#include "falcon/world/mob.hpp"

#include "falcon/render/mob_renderer.hpp"
#include "falcon/world/player.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

int fl(double v) { return static_cast<int>(std::floor(v)); }

struct Choice {
    const char* id;
    int weight;
    int minGroup, maxGroup;
};

// The SpawnerData of each dimension's monster list and of the grassland's animals (weights and group sizes as the game has them).
const std::vector<Choice>& monsters(int dimension) {
    static const std::vector<Choice> overworld = {{"zombie", 95, 4, 4}, {"skeleton", 100, 4, 4}, {"creeper", 100, 4, 4}, {"spider", 100, 4, 4}, {"enderman", 10, 1, 4}};
    static const std::vector<Choice> nether = {{"zombified_piglin", 100, 2, 4}, {"ghast", 50, 1, 1}};
    static const std::vector<Choice> end = {{"enderman", 10, 4, 4}};
    return dimension == 1 ? nether : dimension == 2 ? end : overworld;
}
const std::vector<Choice>& animals() {
    static const std::vector<Choice> list = {{"sheep", 12, 4, 4}, {"pig", 10, 4, 4}, {"chicken", 10, 4, 4}, {"cow", 8, 4, 4}};
    return list;
}

const Choice& pickChoice(const std::vector<Choice>& list, float roll01) {
    int total = 0;
    for (const Choice& c : list) total += c.weight;
    int roll = std::min(total - 1, static_cast<int>(roll01 * static_cast<float>(total)));
    for (const Choice& c : list) {
        if (roll < c.weight) return c;
        roll -= c.weight;
    }
    return list.back();
}

// A place a mob `rows` blocks tall can stand near (x, y, z): air, with a block that holds it up below, room for its body, and nothing liquid or dangerous. `y` is moved to the floor.
bool findFloor(const Level& level, int x, int& y, int z, int rows) {
    if (!level.isLoaded(x, z)) return false;
    const BlockRegistry& registry = blockRegistry();
    for (int tries = 0; tries < 12; ++tries) {
        if (mobBlocks(level.block(x, y, z))) {
            ++y;  // inside rock: climb out
            continue;
        }
        const Block below = level.block(x, y - 1, z);
        if (!mobBlocks(below)) {
            if (y <= kMinY + 1) return false;
            --y;  // in the air: down to the floor
            continue;
        }
        if (mobHazard(below)) return false;
        for (int i = 0; i < rows; ++i) {
            const Block b = level.block(x, y + i, z);
            if (mobBlocks(b) || mobHazard(b) || registry.state(b).has(kStateLiquid)) return false;
        }
        return true;
    }
    return false;
}

// Room for a mob that floats: a cube of air round it.
bool airAround(const Level& level, int x, int y, int z, int halfWidth, int rows) {
    for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
        for (int dz = -halfWidth; dz <= halfWidth; ++dz) {
            if (!level.isLoaded(x + dx, z + dz)) return false;
            for (int dy = 0; dy < rows; ++dy) {
                const Block b = level.block(x + dx, y + dy, z + dz);
                if (mobBlocks(b) || mobHazard(b) || blockRegistry().state(b).has(kStateLiquid)) return false;
            }
        }
    }
    return true;
}

}  // namespace

// Places up to `count` mobs of a species round a point, each on a spot of its own within five blocks. `monster`: the rules for the dark (not near the player, dark enough); otherwise an
// animal's (on grass, in daylight). Returns how many were placed.
int Mobs::spawnGroup(const std::string& id, int count, int x, int y, int z, const MobTick& in, bool monster) {
    const MobSpecies* species = mobSpecies(id);
    if (species == nullptr) return 0;
    float w = 0.6f, h = 1.8f;
    (void)MobRenderer::sizeOf(id, w, h);
    const int rows = std::max(2, static_cast<int>(std::ceil(static_cast<double>(h) - 1.0e-6)));
    const bool flies = (species->traits & kMobFlies) != 0;
    const Level& level = *in.level;
    int placed = 0;
    for (int i = 0; i < count * 2 && placed < count; ++i) {
        const int gx = x + (i == 0 ? 0 : randomInt(11) - 5), gz = z + (i == 0 ? 0 : randomInt(11) - 5);
        int gy = y;
        if (flies) {
            if (!airAround(level, gx, gy, gz, 2, 4)) continue;
        } else if (!findFloor(level, gx, gy, gz, rows)) {
            continue;
        }
        const double dx = gx + 0.5 - in.player[0], dy = gy - in.player[1], dz = gz + 0.5 - in.player[2];
        int sky = 0, block = 0;
        if (host_.light) host_.light(gx, gy, gz, sky, block);
        if (monster) {
            if (dx * dx + dy * dy + dz * dz < 24.0 * 24.0) continue;
            if (in.dimension == 0) {  // Monster.isDarkEnoughToSpawnMonsters: no block light at all, and the sky's under what the night leaves
                const int effective = std::max(0, sky - static_cast<int>(std::lround(in.darkSteps)));
                if (block > 0 || effective > randomInt(8)) continue;
            } else if (in.dimension == 2 && block > 7) {
                continue;
            }
        } else {
            if (sky < 9 || (!flies && blockRegistry().type(level.block(gx, gy - 1, gz)).name != "grass_block")) continue;
            if (dx * dx + dz * dz < 8.0 * 8.0) continue;
        }
        if (spawn(id, gx + 0.5, gy, gz + 0.5) != nullptr) ++placed;
    }
    return placed;
}

void Mobs::despawn(const MobTick& in) {
    for (Mob& m : mobs_) {
        if (m.persistent || m.species->kind == MobKind::Passive || m.dead()) continue;
        const double dx = m.x - in.player[0], dy = m.y - in.player[1], dz = m.z - in.player[2];
        const double distSq = dx * dx + dy * dy + dz * dz;
        // Mob.checkDespawn: beyond 128 blocks at once; beyond 32, after half a minute, one tick in 800.
        if (distSq > 128.0 * 128.0 || (distSq > 32.0 * 32.0 && m.age > 600 && m.target == 0 && randomInt(800) == 0)) {
            m.health = 0.0f;
            m.deathTime = 100;  // (gone: no drops, no sound)
        }
    }
}

void Mobs::naturalSpawning(const MobTick& in) {
    if (!in.spawnMobs || !in.spawnMonsters || in.difficulty == 0 || !in.playerTargetable) return;
    if (tickCount_ % 2 != 0) return;
    int hostile = 0;
    for (const Mob& m : mobs_) {
        if (m.species->kind != MobKind::Passive && !m.dead() && !m.persistent) ++hostile;
    }
    if (hostile >= 70) return;
    const Level& level = *in.level;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const double angle = static_cast<double>(random()) * 6.283185307179586;
        const double radius = 24.0 + static_cast<double>(random()) * 76.0;
        const int x = fl(in.player[0] + std::cos(angle) * radius), z = fl(in.player[2] + std::sin(angle) * radius);
        if (!level.isLoaded(x, z)) continue;
        int y;
        if (in.dimension == 0) {
            const int top = host_.surfaceY ? host_.surfaceY(x, z) : 80;
            y = kMinY + 1 + randomInt(std::max(2, top + 2 - kMinY));
        } else if (in.dimension == 1) {
            y = 1 + randomInt(120);
        } else {
            y = 40 + randomInt(60);
        }
        const Choice& choice = pickChoice(monsters(in.dimension), random());
        const int count = choice.minGroup + randomInt(choice.maxGroup - choice.minGroup + 1);
        if (spawnGroup(choice.id, count, x, y, z, in, true) > 0) break;  // (one pack a go)
    }
}

// The animals a chunk starts with (NaturalSpawner.spawnMobsForChunkGeneration): a chunk the player comes near for the first time has one chance in ten of a herd on its grass.
void Mobs::populateChunks(const MobTick& in) {
    if (tickCount_ % 10 != 0 || !in.spawnMobs) return;
    const Level& level = *in.level;
    const int pcx = chunkOf(fl(in.player[0])), pcz = chunkOf(fl(in.player[2]));
    for (int cx = pcx - 6; cx <= pcx + 6; ++cx) {
        for (int cz = pcz - 6; cz <= pcz + 6; ++cz) {
            if (populated_.count({cx, cz}) != 0) continue;
            // (every neighbour has to be there too, so the herd's ground and the light are final)
            bool ready = true;
            for (int ox = -1; ox <= 1 && ready; ++ox) {
                for (int oz = -1; oz <= 1 && ready; ++oz) ready = level.chunk({cx + ox, cz + oz}) != nullptr && level.chunk({cx + ox, cz + oz})->light != nullptr;
            }
            if (!ready) continue;
            populated_.emplace(cx, cz);
            if (in.dimension != 0 || random() >= 0.1f) continue;
            const int x = cx * 16 + randomInt(16), z = cz * 16 + randomInt(16);
            const int top = host_.surfaceY ? host_.surfaceY(x, z) : 80;
            const Choice& choice = pickChoice(animals(), random());
            spawnGroup(choice.id, choice.minGroup + randomInt(choice.maxGroup - choice.minGroup + 1), x, top + 2, z, in, false);
        }
    }
}

}  // namespace falcon
