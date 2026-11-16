// GameSession: buckets (BucketItem.use: an empty one scoops up a source of water or lava, a full one puts a source down; milk from a cow) and what water and lava do to each other when they
// touch (LiquidBlock.shouldSpreadLiquid: lava next to water turns to obsidian if it is a source and to cobblestone if it flows; lava that runs down onto water makes stone).
#include "falcon/game/game_session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace falcon {
namespace {

int fl(double v) { return static_cast<int>(std::floor(v)); }

}  // namespace

// The sound a fluid makes when it fizzes (LiquidBlock.fizz): lava hitting water, water hitting the Nether's heat.
void GameSession::fizzAt(int x, int y, int z) {
    const std::array<double, 3> at = {x + 0.5, y + 0.5, z + 0.5};
    playWorldSound("block.lava.extinguish", SoundSource::Blocks, 0.5f, 2.6f + (randomFloat() - randomFloat()) * 0.8f, &at);
}

// What a change of a liquid block sets off: water in the Nether turns to steam at once; lava next to water becomes obsidian or cobblestone; water placed next to lava does the same to the lava.
void GameSession::fluidReact(int x, int y, int z) {
    if (remote_) return;
    const Level& level = renderer_.level();
    const Block here = level.block(x, y, z);
    const StateInfo& info = registry_->state(here);
    if (!info.has(kStateLiquid)) return;
    const bool lava = info.has(kStateLava), water = info.has(kStateWater);
    const auto obsidian = registry_->find("obsidian"), cobblestone = registry_->find("cobblestone");
    if (!obsidian || !cobblestone) return;
    if (water && dimension_ == 1) {  // (gameplay/water_evaporates)
        setBlock(x, y, z, Block::Air);
        fizzAt(x, y, z);
        return;
    }
    static constexpr int kSides[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};  // (not from below)
    const auto isWater = [&](int px, int py, int pz) {
        const StateInfo& s = registry_->state(level.block(px, py, pz));
        return s.has(kStateWater) && s.has(kStateLiquid);
    };
    if (lava) {
        for (const auto& d : kSides) {
            if (isWater(x + d[0], y + d[1], z + d[2])) {
                setBlock(x, y, z, info.fluidLevel == 0 ? *obsidian : *cobblestone);
                fizzAt(x, y, z);
                return;
            }
        }
    } else if (water) {
        for (const auto& d : kSides) {  // the lava round the water that has this water above it or beside it
            const int lx = x - d[0], ly = y - d[1], lz = z - d[2];
            const StateInfo& s = registry_->state(level.block(lx, ly, lz));
            if (s.has(kStateLava) && s.has(kStateLiquid)) {
                setBlock(lx, ly, lz, s.fluidLevel == 0 ? *obsidian : *cobblestone);
                fizzAt(lx, ly, lz);
            }
        }
    }
}

// A bucket in use: what the ray from the eyes meets. An empty bucket stops at a source of fluid (and at any block); a full one passes through fluid and stops at a block.
bool GameSession::useBucket(const std::array<double, 3>& eye, const std::array<double, 3>& look) {
    const std::string name = inventory_.selectedItem().type().name;
    const bool empty = name == "bucket";
    const bool lavaBucket = name == "lava_bucket";
    const Level& level = renderer_.level();
    // March along the look until something is hit: the cell and the one before it (which says which face).
    constexpr double kReach = 5.0;
    std::array<int, 3> hit{0, 0, 0}, before{fl(eye[0]), fl(eye[1]), fl(eye[2])};
    bool found = false;
    bool fluidHit = false;
    std::array<int, 3> previous = before;
    for (double t = 0.0; t <= kReach && !found; t += 0.02) {
        const std::array<int, 3> cell{fl(eye[0] + look[0] * t), fl(eye[1] + look[1] * t), fl(eye[2] + look[2] * t)};
        if (cell == previous) continue;
        const Block b = level.block(cell[0], cell[1], cell[2]);
        if (cell[1] < kMinY || cell[1] >= kMaxY) break;
        const StateInfo& info = registry_->state(b);
        if (registry_->isAir(b)) {
            previous = cell;
            continue;
        }
        if (info.has(kStateLiquid)) {
            if (empty && info.fluidLevel == 0 && (info.has(kStateWater) || info.has(kStateLava))) {  // a source: the bucket takes it
                hit = cell;
                before = previous;
                found = fluidHit = true;
                break;
            }
            previous = cell;  // (flowing fluid is no obstacle to the ray)
            continue;
        }
        hit = cell;
        before = previous;
        found = true;
    }
    if (!found) return false;
    const Block target = level.block(hit[0], hit[1], hit[2]);
    const StateInfo& targetInfo = registry_->state(target);
    const auto fullItem = [&](bool lava) { return itemRegistry().find(lava ? "lava_bucket" : "water_bucket"); };
    const auto giveBack = [&](Item item) {  // the bucket is used up and what it turned into comes into the hand (or, if more were held, into the inventory)
        if (creative()) return;
        ItemStack& held = inventory_.selectedItem();
        if (held.count <= 1) {
            held = ItemStack(item, 1);
        } else {
            held.shrink(1);
            ItemStack made(item, 1);
            if (!inventory_.add(made)) dropFromPlayer(made);
        }
    };
    const std::array<double, 3> where = {hit[0] + 0.5, hit[1] + 0.5, hit[2] + 0.5};
    if (empty) {
        if (fluidHit) {  // BucketPickup.pickupBlock: the source is gone and the bucket is full
            const bool lava = targetInfo.has(kStateLava);
            const auto filled = fullItem(lava);
            if (!filled) return false;
            setBlock(hit[0], hit[1], hit[2], Block::Air);
            giveBack(*filled);
            playWorldSound(lava ? "item.bucket.fill_lava" : "item.bucket.fill", SoundSource::Blocks, 1.0f, 1.0f, &where);
            swingArm();
            return true;
        }
        if (registry_->get(target, "waterlogged") == "true") {  // a waterlogged block gives up its water
            setBlock(hit[0], hit[1], hit[2], registry_->with(target, "waterlogged", "false"));
            if (const auto filled = fullItem(false)) giveBack(*filled);
            playWorldSound("item.bucket.fill", SoundSource::Blocks, 1.0f, 1.0f, &where);
            swingArm();
            return true;
        }
        // a cow gives milk to an empty bucket (Cow.mobInteract)
        if (mobSim_) {
            const int mob = mobSim_->pick(eye, look, 3.0);
            if (const Mob* cow = mob >= 0 ? mobSim_->find(mob) : nullptr; cow != nullptr && std::string(cow->species->id) == "cow") {
                if (const auto milk = itemRegistry().find("milk_bucket")) {
                    giveBack(*milk);
                    playWorldSound("entity.cow.milk", SoundSource::Neutral, 1.0f, 1.0f, &where);
                    swingArm();
                    return true;
                }
            }
        }
        return false;
    }
    // A full bucket: a source goes where the clicked block says (inside it if it can hold the fluid or is washed away, else beside it, on the face that was hit).
    const bool replaceable = targetInfo.has(kStateReplaceable) && !targetInfo.has(kStateLiquid);
    const bool waterloggable = !lavaBucket && registry_->get(target, "waterlogged") == "false";
    std::array<int, 3> put = replaceable ? hit : before;
    if (waterloggable) put = hit;
    const Block atPut = level.block(put[0], put[1], put[2]);
    const StateInfo& putInfo = registry_->state(atPut);
    if (put[1] < kMinY || put[1] >= kMaxY) return false;
    if (!waterloggable && !registry_->isAir(atPut) && !putInfo.has(kStateReplaceable) && !putInfo.has(kStateLiquid)) return false;  // (a fluid is replaced by the other one: a source of water by lava, and the two set)
    if (putInfo.has(kStateLiquid) && putInfo.fluidLevel == 0 && ((lavaBucket && putInfo.has(kStateLava)) || (!lavaBucket && putInfo.has(kStateWater)))) return false;  // (it is full there already)
    const auto bucket = itemRegistry().find("bucket");
    if (!bucket) return false;
    const std::array<double, 3> at = {put[0] + 0.5, put[1] + 0.5, put[2] + 0.5};
    if (!lavaBucket && dimension_ == 1) {  // the heat of the Nether: the water boils away, and the bucket is empty
        giveBack(*bucket);
        playWorldSound("block.fire.extinguish", SoundSource::Blocks, 0.5f, 2.6f + (randomFloat() - randomFloat()) * 0.8f, &at);
        swingArm();
        return true;
    }
    if (replaceable && !registry_->isAir(atPut)) breakBlockAt(put[0], put[1], put[2], !creative());  // (a plant washed away drops what it gives)
    if (waterloggable) {
        setBlock(put[0], put[1], put[2], registry_->with(atPut, "waterlogged", "true"));
    } else {
        const Block fluid = registry_->with(lavaBucket ? commonBlocks().lava : commonBlocks().water, "level", "0");
        setBlock(put[0], put[1], put[2], fluid);
    }
    giveBack(*bucket);
    playWorldSound(lavaBucket ? "item.bucket.empty_lava" : "item.bucket.empty", SoundSource::Blocks, 1.0f, 1.0f, &at);
    swingArm();
    return true;
}

}  // namespace falcon
