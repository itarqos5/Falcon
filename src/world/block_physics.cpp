#include "falcon/world/block_physics.hpp"

#include "falcon/world/player.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <cmath>
#include <numbers>

namespace falcon {

namespace {

constexpr int kDifficulty = 2;  // Normal: FireBlock adds difficulty * 7 to a block's ignite odds
constexpr int kRandomTickChunks = 4;

int blockFloor(double v) { return static_cast<int>(std::floor(v)); }

}  // namespace

BlockPhysics::BlockPhysics(const Level& level, std::shared_ptr<const BlockBehaviours> behaviours, ItemEntities& items, Host host, std::uint32_t seed)
    : level_(level), registry_(blockRegistry()), behaviours_(std::move(behaviours)), items_(items), host_(std::move(host)), rng_(seed) {
    if (const auto fire = registry_.findBlock("fire")) fireBlock_ = *fire;
    if (const auto soul = registry_.findBlock("soul_fire")) soulFireBlock_ = *soul;
}

// ---------------------------------------------------------------------------------------------------------------
// Ticking

void BlockPhysics::tick() {
    ++time_;
    while (!due_.empty() && due_.begin()->first <= time_) {
        const std::array<int, 3> pos = due_.begin()->second;
        due_.erase(due_.begin());
        queued_.erase(pos);
        runScheduled(pos[0], pos[1], pos[2]);
    }
    randomTicks();
    for (FallingBlock& e : falling_) tickFalling(e);
    falling_.erase(std::remove_if(falling_.begin(), falling_.end(), [](const FallingBlock& e) { return e.id < 0; }), falling_.end());
    // (an explosion may add more TNT while this runs: go by index)
    for (std::size_t i = 0; i < tnt_.size(); ++i) tickTnt(tnt_[i]);
    tnt_.erase(std::remove_if(tnt_.begin(), tnt_.end(), [](const PrimedTnt& e) { return e.id < 0; }), tnt_.end());
}

void BlockPhysics::schedule(int x, int y, int z, int delay) {
    const std::array<int, 3> pos = {x, y, z};
    if (!queued_.insert(pos).second) return;
    due_.emplace(time_ + std::max(delay, 1), pos);
}

void BlockPhysics::runScheduled(int x, int y, int z) {
    if (!level_.isLoaded(x, z)) return;
    const int index = registry_.state(block(x, y, z)).block;
    if (behaviours_->isFallable(index)) {
        tryFall(x, y, z);
    } else if (behaviours_->isSpreadingFire(index)) {
        fireTick(x, y, z);
    }
}

// FallingBlock.tick / LavaFluid.randomTick: the game picks a few blocks at random in every section of the loaded chunks near the player each tick; here the
// only thing that needs it is lava starting fires, so those are what this looks for.
void BlockPhysics::randomTicks() {
    if (!host_.player) return;
    const PhysicsPlayer player = host_.player();
    const int cx = chunkOf(blockFloor((player.box.minX + player.box.maxX) * 0.5));
    const int cz = chunkOf(blockFloor((player.box.minZ + player.box.maxZ) * 0.5));
    for (int dz = -kRandomTickChunks; dz <= kRandomTickChunks; ++dz) {
        for (int dx = -kRandomTickChunks; dx <= kRandomTickChunks; ++dx) {
            const Chunk* chunk = level_.chunk({cx + dx, cz + dz});
            if (chunk == nullptr) continue;
            for (int s = 0; s < kSectionCount; ++s) {
                const SectionPtr& section = chunk->sections[static_cast<std::size_t>(s)];
                if (!section) continue;
                if (section->isUniform() && !registry_.state(section->uniform).has(kStateLava)) continue;
                for (int i = 0; i < randomTickSpeed_; ++i) {
                    const int lx = nextInt(16), ly = nextInt(16), lz = nextInt(16);
                    const Block sampled = section->at(lx, ly, lz);
                    const StateInfo& info = registry_.state(sampled);
                    if (info.has(kStateLava)) lavaRandomTick((cx + dx) * 16 + lx, kMinY + s * 16 + ly, (cz + dz) * 16 + lz);
                    else if (info.has(kStateRandomTicks)) growthRandomTick((cx + dx) * 16 + lx, kMinY + s * 16 + ly, (cz + dz) * 16 + lz, sampled);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Blocks that fall

bool BlockPhysics::isFree(Block b) const {
    const StateInfo& s = registry_.state(b);
    return s.has(kStateAir) || behaviours_->isFire(s.block) || s.has(kStateLiquid) || s.has(kStateReplaceable);  // FallingBlock.isFree
}

bool BlockPhysics::touchesWater(int x, int y, int z, const Reader& at) const {
    static constexpr int kSides[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};  // (not from below)
    for (const auto& d : kSides) {
        if (registry_.state(at(x + d[0], y + d[1], z + d[2])).has(kStateWater)) return true;
    }
    return false;
}

void BlockPhysics::spawnFalling(Block b, double x, double y, double z) {
    FallingBlock e;
    e.block = b;
    e.x = e.xo = x;
    e.y = e.yo = y;
    e.z = e.zo = z;
    e.id = nextId_++;
    falling_.push_back(e);
}

// FallingBlock.tick: when the block below is free the block becomes a falling entity (and the cell it leaves is air).
void BlockPhysics::tryFall(int x, int y, int z) {
    const Block b = block(x, y, z);
    if (!behaviours_->isFallable(registry_.state(b).block) || y < kMinY) return;
    if (!isFree(block(x, y - 1, z))) return;
    host_.setBlock(x, y, z, Block::Air);
    spawnFalling(b, x + 0.5, y, z + 0.5);
}

void BlockPhysics::tickFalling(FallingBlock& e) {
    if (e.id < 0) return;
    if (registry_.isAir(e.block) || !level_.isLoaded(blockFloor(e.x), blockFloor(e.z))) {
        if (registry_.isAir(e.block)) e.id = -1;
        return;
    }
    ++e.time;
    e.xo = e.x;
    e.yo = e.y;
    e.zo = e.z;
    e.vy -= 0.04;  // gravity
    const std::array<double, 3> actual = collideAabb(level_, e.box(), e.vx, e.vy, e.vz);
    e.x += actual[0];
    e.y += actual[1];
    e.z += actual[2];
    if (std::abs(e.vx - actual[0]) > 1.0E-5) e.vx = 0.0;
    if (std::abs(e.vz - actual[2]) > 1.0E-5) e.vz = 0.0;
    const bool vertical = std::abs(e.vy - actual[1]) > 1.0E-5;
    e.onGround = vertical && e.vy < 0.0;
    if (vertical) e.vy = 0.0;
    if (!e.onGround && actual[1] < 0.0) e.fallDistance -= actual[1];  // (the step that lands does not count: Entity.checkFallDamage)
    const int bx = blockFloor(e.x), by = blockFloor(e.y + 1.0E-6), bz = blockFloor(e.z);
    const bool powder = behaviours_->isConcretePowder(registry_.state(e.block).block);
    const bool inWater = registry_.state(block(bx, by, bz)).has(kStateWater);
    if (!e.onGround && !(powder && inWater)) {
        if ((e.time > 100 && (by <= kMinY || by > kMaxY)) || e.time > 600) {  // lost: it drops as an item
            const Item item = itemRegistry().itemOfBlock(registry_.state(e.block).block);
            if (item != Item::Air) items_.spawn(ItemStack(item, 1), e.x, e.y, e.z, 0.0, 0.0, 0.0, 10, nextFloat() * 6.2831855f);
            e.id = -1;
        }
    } else {
        landFalling(e);
    }
    if (e.id >= 0) {
        e.vx *= 0.98;
        e.vy *= 0.98;
        e.vz *= 0.98;
    }
}

// FallingBlockEntity landing: an anvil hurts what is under it and may chip; then the block goes back into the world if its cell is free, or drops as an item.
void BlockPhysics::landFalling(FallingBlock& e) {
    const int bx = blockFloor(e.x), by = blockFloor(e.y + 1.0E-6), bz = blockFloor(e.z);
    const int index = registry_.state(e.block).block;
    const bool powder = behaviours_->isConcretePowder(index);
    const bool anvil = behaviours_->isAnvil(index);
    const bool inWater = registry_.state(block(bx, by, bz)).has(kStateWater);
    if (anvil) {
        const int i = static_cast<int>(std::ceil(e.fallDistance - 1.0));
        if (i >= 0) {
            const float damage = std::min(std::floor(static_cast<float>(i) * 2.0f), 40.0f);  // Fallable: fallDamagePerDistance 2, max 40
            if (damage > 0.0f && host_.player) {
                const PhysicsPlayer player = host_.player();
                if (player.hurtable && e.box().intersects(player.box) && host_.hurtPlayer) host_.hurtPlayer(damage);
            }
            if (damage > 0.0f && nextFloat() < 0.05f + static_cast<float>(i) * 0.05f) {  // AnvilBlock.damage: anvil, chipped, damaged, gone
                const std::string& name = registry_.type(index).name;
                const char* next = name == "anvil" ? "chipped_anvil" : name == "chipped_anvil" ? "damaged_anvil" : nullptr;
                if (next == nullptr) {
                    e.cancelDrop = true;
                } else if (const auto damaged = registry_.find(next)) {
                    const std::string_view facing = registry_.get(e.block, "facing");
                    e.block = facing.empty() ? *damaged : registry_.with(*damaged, "facing", facing);
                }
            }
        }
    }
    e.vx *= 0.7;
    e.vy *= -0.5;
    e.vz *= 0.7;
    const Block there = block(bx, by, bz);
    const bool canBeReplaced = registry_.isAir(there) || registry_.state(there).has(kStateReplaceable);
    const bool freeBelow = isFree(block(bx, by - 1, bz)) && (!powder || !inWater);
    if (e.cancelDrop) {
        if (host_.sound) host_.sound("block.anvil.destroy", 1.0f, 0.9f + nextFloat() * 0.1f, e.x, e.y, e.z);
        e.id = -1;
    } else if (canBeReplaced && !freeBelow) {
        Block placed = e.block;
        if (powder && inWater) {
            if (const int concrete = behaviours_->concreteOf(index); concrete >= 0) placed = registry_.defaultState(concrete);
        }
        if (host_.setBlock) host_.setBlock(bx, by, bz, placed);
        if (anvil && host_.sound) host_.sound("block.anvil.land", 0.3f, nextFloat() * 0.1f + 0.9f, bx + 0.5, by + 0.5, bz + 0.5);
        e.id = -1;
    } else {
        const Item item = itemRegistry().itemOfBlock(index);
        if (item != Item::Air) items_.spawn(ItemStack(item, 1), e.x, e.y, e.z, 0.0, 0.1, 0.0, 10, nextFloat() * 6.2831855f);
        e.id = -1;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Placing and neighbours

void BlockPhysics::blockPlaced(int x, int y, int z) {
    const Block b = block(x, y, z);
    const int index = registry_.state(b).block;
    if (behaviours_->isFallable(index)) {
        schedule(x, y, z, 2);
        if (behaviours_->isConcretePowder(index) && touchesWater(x, y, z, [this](int a, int c, int d) { return block(a, c, d); })) {
            if (const int concrete = behaviours_->concreteOf(index); concrete >= 0 && host_.setBlock) host_.setBlock(x, y, z, registry_.defaultState(concrete));
        }
    } else if (behaviours_->isSpreadingFire(index)) {
        schedule(x, y, z, 30 + nextInt(10));
    }
}

Block BlockPhysics::afterNeighborChange(Block b, int x, int y, int z, const Reader& at) {
    const int index = registry_.state(b).block;
    if (behaviours_->isFallable(index)) {
        schedule(x, y, z, 2);
        if (behaviours_->isConcretePowder(index) && touchesWater(x, y, z, at)) {
            if (const int concrete = behaviours_->concreteOf(index); concrete >= 0) return registry_.defaultState(concrete);
        }
        return b;
    }
    if (behaviours_->isFire(index)) {
        if (!fireCanSurvive(b, x, y, z, at)) return Block::Air;
        if (behaviours_->isSpreadingFire(index)) {
            const std::string_view age = registry_.get(b, "age");
            return fireWithAge(x, y, z, std::atoi(std::string(age).c_str()), at);  // the flames on its sides follow what burns round it
        }
    }
    return b;
}

// ---------------------------------------------------------------------------------------------------------------
// Fire

bool BlockPhysics::canBurn(Block b) const {
    if (registry_.get(b, "waterlogged") == "true") return false;
    return behaviours_->igniteOdds(registry_.state(b).block) > 0;
}

bool BlockPhysics::validFireLocation(int x, int y, int z, const Reader& at) const {
    static constexpr int kDir[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (const auto& d : kDir) {
        if (canBurn(at(x + d[0], y + d[1], z + d[2]))) return true;
    }
    return false;
}

bool BlockPhysics::fireCanSurvive(Block fire, int x, int y, int z, const Reader& at) const {
    const Block below = at(x, y - 1, z);
    const StateInfo& under = registry_.state(below);
    if (registry_.state(fire).block == soulFireBlock_) {  // soul fire stands on soul sand or soul soil only
        const std::string& name = registry_.type(under.block).name;
        return name == "soul_sand" || name == "soul_soil";
    }
    return under.has(kStateFullCollision) || under.has(kStateOpaqueCube) || validFireLocation(x, y, z, at);  // FireBlock.canSurvive
}

// FireBlock.getStateForPlacement: the flames on a side show where something burns, unless the fire stands on a floor.
Block BlockPhysics::fireWithAge(int x, int y, int z, int age, const Reader& at) const {
    if (fireBlock_ < 0) return Block::Air;
    Block fire = registry_.with(registry_.defaultState(fireBlock_), "age", std::to_string(std::clamp(age, 0, 15)));
    const Block below = at(x, y - 1, z);
    const StateInfo& under = registry_.state(below);
    if (canBurn(below) || under.has(kStateFullCollision) || under.has(kStateOpaqueCube)) return fire;
    static constexpr struct {
        const char* name;
        int dx, dy, dz;
    } kSides[5] = {{"north", 0, 0, -1}, {"east", 1, 0, 0}, {"south", 0, 0, 1}, {"west", -1, 0, 0}, {"up", 0, 1, 0}};
    for (const auto& side : kSides) fire = registry_.with(fire, side.name, canBurn(at(x + side.dx, y + side.dy, z + side.dz)) ? "true" : "false");
    return fire;
}

Block BlockPhysics::fireStateAt(int x, int y, int z) const {  // BaseFireBlock.getState
    const Reader at = [this](int a, int b, int c) { return block(a, b, c); };
    const std::string& below = registry_.type(block(x, y - 1, z)).name;
    if (soulFireBlock_ >= 0 && (below == "soul_sand" || below == "soul_soil")) return registry_.defaultState(soulFireBlock_);
    return fireWithAge(x, y, z, 0, at);
}

bool BlockPhysics::canPlaceFireAt(int x, int y, int z) const {  // BaseFireBlock.canBePlacedAt
    if (!registry_.isAir(block(x, y, z)) || y < kMinY || y >= kMaxY) return false;
    const Reader at = [this](int a, int b, int c) { return block(a, b, c); };
    return fireCanSurvive(fireStateAt(x, y, z), x, y, z, at);
}

int BlockPhysics::igniteOddsAround(int x, int y, int z) const {  // FireBlock.getIgniteOdds(level, pos): only an empty cell can catch
    if (!registry_.isAir(block(x, y, z))) return 0;
    static constexpr int kDir[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    int best = 0;
    for (const auto& d : kDir) {
        const Block n = block(x + d[0], y + d[1], z + d[2]);
        if (registry_.get(n, "waterlogged") == "true") continue;
        best = std::max(best, behaviours_->igniteOdds(registry_.state(n).block));
    }
    return best;
}

// FireBlock.checkBurnOut: a block next to the fire may catch (becoming fire) or burn away; TNT that does goes off.
void BlockPhysics::checkBurnOut(int x, int y, int z, int chance, int age) {
    const Block b = block(x, y, z);
    const int index = registry_.state(b).block;
    if (registry_.get(b, "waterlogged") == "true") return;
    const int burn = behaviours_->burnOdds(index);
    if (burn <= 0 || nextInt(chance) >= burn) return;
    if (nextInt(age + 10) < 5) {
        const int newAge = std::min(age + nextInt(5) / 4, 15);
        host_.setBlock(x, y, z, fireWithAge(x, y, z, newAge, [this](int a, int c, int d) { return block(a, c, d); }));
    } else {
        host_.setBlock(x, y, z, Block::Air);
    }
    if (behaviours_->isTnt(index)) primeTnt(x, y, z);
}

// FireBlock.tick (minus rain, which Falcon has not got, and the damp biomes).
void BlockPhysics::fireTick(int x, int y, int z) {
    const Reader at = [this](int a, int b, int c) { return block(a, b, c); };
    Block state = block(x, y, z);
    schedule(x, y, z, 30 + nextInt(10));
    if (fireSpreadRadius_ == 0) return;  // the game rule fire_spread_radius_around_player: no fire ticks at all
    if (fireSpreadRadius_ > 0 && host_.player) {  // ... and none far from the player
        const PhysicsPlayer player = host_.player();
        const double dx = x + 0.5 - (player.box.minX + player.box.maxX) * 0.5, dz = z + 0.5 - (player.box.minZ + player.box.maxZ) * 0.5;
        if (dx * dx + dz * dz > static_cast<double>(fireSpreadRadius_) * fireSpreadRadius_) return;
    }
    if (!fireCanSurvive(state, x, y, z, at)) {
        host_.setBlock(x, y, z, Block::Air);
        return;
    }
    const Block below = block(x, y - 1, z);
    const std::string& belowName = registry_.type(below).name;
    const bool infiniburn = belowName == "netherrack" || belowName == "magma_block";  // #minecraft:infiniburn_overworld
    const int age = std::atoi(std::string(registry_.get(state, "age")).c_str());
    const int newAge = std::min(15, age + nextInt(3) / 2);
    if (age != newAge) {
        state = registry_.with(state, "age", std::to_string(newAge));
        host_.setBlock(x, y, z, state);
    }
    if (!infiniburn) {
        if (!validFireLocation(x, y, z, at)) {
            const StateInfo& under = registry_.state(below);
            if (!(under.has(kStateFullCollision) || under.has(kStateOpaqueCube)) || age > 3) host_.setBlock(x, y, z, Block::Air);
            return;
        }
        if (age == 15 && nextInt(4) == 0 && !canBurn(below)) {
            host_.setBlock(x, y, z, Block::Air);
            return;
        }
    }
    checkBurnOut(x + 1, y, z, 300, age);
    checkBurnOut(x - 1, y, z, 300, age);
    checkBurnOut(x, y - 1, z, 250, age);
    checkBurnOut(x, y + 1, z, 250, age);
    checkBurnOut(x, y, z - 1, 300, age);
    checkBurnOut(x, y, z + 1, 300, age);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 4; ++dy) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                int rate = 100;
                if (dy > 1) rate += (dy - 1) * 100;
                const int mx = x + dx, my = y + dy, mz = z + dz;
                const int ignite = igniteOddsAround(mx, my, mz);
                if (ignite <= 0) continue;
                const int odds = (ignite + 40 + kDifficulty * 7) / (age + 30);
                if (odds > 0 && nextInt(rate) <= odds) {
                    const int fireAge = std::min(15, age + nextInt(5) / 4);
                    host_.setBlock(mx, my, mz, fireWithAge(mx, my, mz, fireAge, at));
                }
            }
        }
    }
}

// LavaFluid.randomTick: lava sets fire to the air above it where something near burns, and to flammable blocks next to it.
void BlockPhysics::lavaRandomTick(int x, int y, int z) {
    const auto flammable = [this](int a, int b, int c) { return registry_.state(block(a, b, c)).has(kStateIgnitedByLava); };
    const int i = nextInt(3);
    if (i > 0) {
        int px = x, py = y, pz = z;
        for (int j = 0; j < i; ++j) {
            px += nextInt(3) - 1;
            py += 1;
            pz += nextInt(3) - 1;
            if (!level_.isLoaded(px, pz)) return;
            const Block b = block(px, py, pz);
            if (registry_.isAir(b)) {
                static constexpr int kDir[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
                bool near = false;
                for (const auto& d : kDir) near = near || flammable(px + d[0], py + d[1], pz + d[2]);
                if (near) {
                    host_.setBlock(px, py, pz, fireStateAt(px, py, pz));
                    return;
                }
            } else if (registry_.state(b).has(kStateFullCollision) || registry_.state(b).has(kStateOpaqueCube)) {
                return;
            }
        }
    } else {
        for (int k = 0; k < 3; ++k) {
            const int px = x + nextInt(3) - 1, pz = z + nextInt(3) - 1;
            if (!level_.isLoaded(px, pz)) return;
            if (registry_.isAir(block(px, y + 1, pz)) && flammable(px, y, pz)) host_.setBlock(px, y + 1, pz, fireStateAt(px, y + 1, pz));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// TNT and explosions

void BlockPhysics::primeTnt(int x, int y, int z, int fuse) { spawnTnt(x + 0.5, y, z + 0.5, fuse); }

int BlockPhysics::killTnt(const std::function<bool(const PrimedTnt&)>& which) {
    int n = 0;
    for (PrimedTnt& e : tnt_) {
        if (e.id >= 0 && which(e)) {
            e.id = -1;
            ++n;
        }
    }
    tnt_.erase(std::remove_if(tnt_.begin(), tnt_.end(), [](const PrimedTnt& e) { return e.id < 0; }), tnt_.end());
    return n;
}

int BlockPhysics::killFalling(const std::function<bool(const FallingBlock&)>& which) {
    int n = 0;
    for (FallingBlock& e : falling_) {
        if (e.id >= 0 && which(e)) {
            e.id = -1;
            ++n;
        }
    }
    falling_.erase(std::remove_if(falling_.begin(), falling_.end(), [](const FallingBlock& e) { return e.id < 0; }), falling_.end());
    return n;
}

void BlockPhysics::spawnTnt(double x, double y, double z, int fuse) {
    if (!tntExplodes_) return;
    PrimedTnt e;
    const double angle = static_cast<double>(nextFloat()) * std::numbers::pi * 2.0;
    e.x = e.xo = x;
    e.y = e.yo = y;
    e.z = e.zo = z;
    e.vx = -std::sin(angle) * 0.02;
    e.vy = 0.2;
    e.vz = -std::cos(angle) * 0.02;
    e.fuse = fuse;
    e.id = nextId_++;
    tnt_.push_back(e);
    if (host_.sound) host_.sound("entity.tnt.primed", 1.0f, 1.0f, e.x, e.y, e.z);
}

void BlockPhysics::tickTnt(PrimedTnt& e) {
    if (e.id < 0) return;
    e.xo = e.x;
    e.yo = e.y;
    e.zo = e.z;
    e.vy -= 0.04;
    const std::array<double, 3> actual = collideAabb(level_, e.box(), e.vx, e.vy, e.vz);
    e.x += actual[0];
    e.y += actual[1];
    e.z += actual[2];
    if (std::abs(e.vx - actual[0]) > 1.0E-5) e.vx = 0.0;
    if (std::abs(e.vz - actual[2]) > 1.0E-5) e.vz = 0.0;
    const bool vertical = std::abs(e.vy - actual[1]) > 1.0E-5;
    e.onGround = vertical && e.vy < 0.0;
    if (vertical) e.vy = 0.0;
    e.vx *= 0.98;
    e.vy *= 0.98;
    e.vz *= 0.98;
    if (e.onGround) {
        e.vx *= 0.7;
        e.vy *= -0.5;
        e.vz *= 0.7;
    }
    if (--e.fuse <= 0) {
        const double x = e.x, y = e.y + 0.98 * 0.0625, z = e.z;
        e.id = -1;
        explode(x, y, z, 4.0f);
    }
}

// How much of a body an explosion can see (Explosion.getSeenPercent): rays from a grid of points in the box to the centre, the share that reach it unblocked.
float BlockPhysics::seenPercent(double cx, double cy, double cz, const Aabb& box) const {
    const double sx = box.maxX - box.minX, sy = box.maxY - box.minY, sz = box.maxZ - box.minZ;
    const double dx = 1.0 / (sx * 2.0 + 1.0), dy = 1.0 / (sy * 2.0 + 1.0), dz = 1.0 / (sz * 2.0 + 1.0);
    const double ox = (1.0 - std::floor(1.0 / dx) * dx) / 2.0, oz = (1.0 - std::floor(1.0 / dz) * dz) / 2.0;
    if (dx < 0.0 || dy < 0.0 || dz < 0.0) return 0.0f;
    int seen = 0, total = 0;
    for (double k = 0.0; k <= 1.0; k += dx) {
        for (double l = 0.0; l <= 1.0; l += dy) {
            for (double m = 0.0; m <= 1.0; m += dz) {
                const double px = box.minX + sx * k + ox, py = box.minY + sy * l, pz = box.minZ + sz * m + oz;
                const double vx = cx - px, vy = cy - py, vz = cz - pz;
                const double length = std::sqrt(vx * vx + vy * vy + vz * vz);
                bool clear = true;
                const int steps = std::max(1, static_cast<int>(std::ceil(length / 0.25)));
                for (int s = 1; s < steps && clear; ++s) {
                    const double t = static_cast<double>(s) / steps;
                    const double qx = px + vx * t, qy = py + vy * t, qz = pz + vz * t;
                    clear = !aabbCollides(level_, Aabb{qx - 0.02, qy - 0.02, qz - 0.02, qx + 0.02, qy + 0.02, qz + 0.02});
                }
                if (clear) ++seen;
                ++total;
            }
        }
    }
    return total == 0 ? 0.0f : static_cast<float>(seen) / static_cast<float>(total);
}

// ServerExplosion: rays leave the centre through a 16 x 16 x 16 grid, losing strength to the blocks they pass; every block they still reach with strength left is
// destroyed (and drops what its loot table says: TNT does not thin its drops out). TNT among them is set off with a short fuse. What stands near is hurt and
// thrown, the closer the more, the less so behind cover.
void BlockPhysics::explode(double cx, double cy, double cz, float radius) {
    std::set<std::array<int, 3>> destroyed;
    for (int j = 0; j < 16; ++j) {
        for (int k = 0; k < 16; ++k) {
            for (int l = 0; l < 16; ++l) {
                if (j != 0 && j != 15 && k != 0 && k != 15 && l != 0 && l != 15) continue;
                double dx = j / 15.0 * 2.0 - 1.0, dy = k / 15.0 * 2.0 - 1.0, dz = l / 15.0 * 2.0 - 1.0;
                const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= length;
                dy /= length;
                dz /= length;
                float power = radius * (0.7f + nextFloat() * 0.6f);
                double px = cx, py = cy, pz = cz;
                for (; power > 0.0f; power -= 0.22500001f) {
                    const int bx = blockFloor(px), by = blockFloor(py), bz = blockFloor(pz);
                    if (by < kMinY || by >= kMaxY) break;
                    const Block b = block(bx, by, bz);
                    if (!registry_.isAir(b)) {
                        power -= (registry_.state(b).explosionResistance + 0.3f) * 0.3f;
                        if (power > 0.0f) destroyed.insert({bx, by, bz});
                    }
                    px += dx * 0.3;
                    py += dy * 0.3;
                    pz += dz * 0.3;
                }
            }
        }
    }
    const float diameter = radius * 2.0f;
    const auto affect = [&](double ex, double ey, double ez, double eyeY, float seen, double& outDx, double& outDy, double& outDz, float& impact) {
        const double dist = std::sqrt((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy) + (ez - cz) * (ez - cz)) / diameter;
        if (dist > 1.0) return false;
        double dx = ex - cx, dy = eyeY - cy, dz = ez - cz;
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length == 0.0) return false;
        outDx = dx / length;
        outDy = dy / length;
        outDz = dz / length;
        impact = static_cast<float>((1.0 - dist) * seen);
        return true;
    };
    // The player.
    if (host_.player) {
        const PhysicsPlayer p = host_.player();
        double dx, dy, dz;
        float impact;
        const double px = (p.box.minX + p.box.maxX) * 0.5, pz = (p.box.minZ + p.box.maxZ) * 0.5;
        if (p.hurtable && affect(px, p.box.minY, pz, p.eyeY, seenPercent(cx, cy, cz, p.box), dx, dy, dz, impact) && impact > 0.0f) {
            if (host_.hurtPlayer) host_.hurtPlayer(static_cast<float>(static_cast<int>((impact * impact + impact) / 2.0f * 7.0f * diameter + 1.0f)));
            if (host_.pushPlayer) host_.pushPlayer(dx * impact, dy * impact, dz * impact);
        }
    }
    // Dropped items (the bigger bangs destroy them) and other TNT (thrown about).
    for (ItemEntity& item : items_.items()) {
        double dx, dy, dz;
        float impact;
        if (!affect(item.x, item.y, item.z, item.y + 0.125, 1.0f, dx, dy, dz, impact)) continue;
        if (static_cast<int>((impact * impact + impact) / 2.0f * 7.0f * diameter + 1.0f) >= 5) {
            item.stack = ItemStack();
        } else {
            item.vx += dx * impact;
            item.vy += dy * impact;
            item.vz += dz * impact;
        }
    }
    for (PrimedTnt& other : tnt_) {
        double dx, dy, dz;
        float impact;
        if (other.id < 0 || !affect(other.x, other.y, other.z, other.y, 1.0f, dx, dy, dz, impact)) continue;
        other.vx += dx * impact;
        other.vy += dy * impact;
        other.vz += dz * impact;
    }
    // The blocks, in random order.
    std::vector<std::array<int, 3>> order(destroyed.begin(), destroyed.end());
    std::shuffle(order.begin(), order.end(), rng_);
    if (host_.beginBatch) host_.beginBatch();
    for (const std::array<int, 3>& pos : order) {
        const Block b = block(pos[0], pos[1], pos[2]);
        if (registry_.isAir(b)) continue;
        const int index = registry_.state(b).block;
        if (behaviours_->isTnt(index)) {
            primeTnt(pos[0], pos[1], pos[2], nextInt(20) + 10);  // TntBlock.onExplosionHit: a short fuse (80 / 8 .. 80 / 4 + 80 / 8)
        } else if (host_.drops) {
            for (const ItemStack& drop : host_.drops(b)) {
                const auto jitter = [this] { return static_cast<double>(nextFloat()) * 0.5 - 0.25; };
                items_.spawn(drop, pos[0] + 0.5 + jitter(), pos[1] + 0.5 + jitter() - 0.125, pos[2] + 0.5 + jitter(), static_cast<double>(nextFloat()) * 0.2 - 0.1, 0.2,
                             static_cast<double>(nextFloat()) * 0.2 - 0.1, 10, nextFloat() * 6.2831855f);
            }
        }
        if (host_.setBlock) host_.setBlock(pos[0], pos[1], pos[2], Block::Air);
    }
    if (host_.endBatch) host_.endBatch();
    if (host_.sound) host_.sound("entity.generic.explode", 4.0f, (1.0f + (nextFloat() - nextFloat()) * 0.2f) * 0.7f, cx, cy, cz);
}

}  // namespace falcon
