#include "falcon/world/mob.hpp"

#include "falcon/render/mob_renderer.hpp"
#include "falcon/world/player.hpp"

#include <algorithm>
#include <cmath>

namespace falcon {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kDegrees = 57.29577951308232f;

int fl(double v) { return static_cast<int>(std::floor(v)); }

// The mobs Falcon knows. Health, speed, damage, follow range and drops are the game's.
const std::vector<MobSpecies>& makeSpecies() {
    static const std::vector<MobSpecies> table = {
        {"pig", MobKind::Passive, 10.0f, 0.25f, 0.0f, 16.0f, 0, 1, {{{"porkchop", 1, 3, 1.0f}}}},
        {"cow", MobKind::Passive, 10.0f, 0.2f, 0.0f, 16.0f, 0, 1, {{{"beef", 1, 3, 1.0f}, {"leather", 0, 2, 1.0f}}}},
        {"sheep", MobKind::Passive, 8.0f, 0.23f, 0.0f, 16.0f, 0, 1, {{{"mutton", 1, 2, 1.0f}, {"white_wool", 1, 1, 1.0f}}}},
        {"chicken", MobKind::Passive, 4.0f, 0.25f, 0.0f, 16.0f, kMobNoFallDamage | kMobLaysEggs | kMobSlowFall, 1, {{{"chicken", 1, 1, 1.0f}, {"feather", 0, 2, 1.0f}}}},
        {"zombie", MobKind::Hostile, 20.0f, 0.23f, 3.0f, 35.0f, kMobBurnsInDaylight, 5, {{{"rotten_flesh", 0, 2, 1.0f}, {"iron_ingot", 1, 1, 0.025f}}}},
        {"husk", MobKind::Hostile, 20.0f, 0.23f, 3.0f, 35.0f, 0, 5, {{{"rotten_flesh", 0, 2, 1.0f}, {"iron_ingot", 1, 1, 0.025f}}}},
        {"skeleton", MobKind::Hostile, 20.0f, 0.25f, 2.0f, 16.0f, kMobBurnsInDaylight | kMobArcher, 5, {{{"arrow", 0, 2, 1.0f}, {"bone", 0, 2, 1.0f}}}},
        {"creeper", MobKind::Hostile, 20.0f, 0.25f, 0.0f, 16.0f, kMobCreeper, 5, {{{"gunpowder", 0, 2, 1.0f}}}},
        {"spider", MobKind::Hostile, 16.0f, 0.3f, 2.0f, 16.0f, kMobNeutralInDaylight | kMobLeaps | kMobClimbs, 5, {{{"string", 0, 2, 1.0f}, {"spider_eye", 1, 1, 0.33f}}}},
        {"enderman", MobKind::Neutral, 40.0f, 0.3f, 7.0f, 64.0f, kMobTeleports | kMobAngryWhenLookedAt | kMobHurtByWater, 5, {{{"ender_pearl", 0, 1, 1.0f}}}},
        {"zombified_piglin", MobKind::Neutral, 20.0f, 0.23f, 5.0f, 35.0f, kMobAngryWhenHurt | kMobFireImmune, 5, {{{"rotten_flesh", 0, 1, 1.0f}, {"gold_nugget", 0, 1, 1.0f}}}},
        {"blaze", MobKind::Hostile, 20.0f, 0.23f, 6.0f, 48.0f, kMobFlies | kMobFireballs | kMobFireImmune | kMobNoFallDamage, 10, {{{"blaze_rod", 0, 1, 1.0f}}}},
        {"ender_dragon", MobKind::Hostile, 200.0f, 0.4f, 10.0f, 150.0f, kMobFlies | kMobFireImmune | kMobNoFallDamage | kMobDragon, 12000, {{{"dragon_egg", 0, 0, 1.0f}}}},
        {"end_crystal", MobKind::Passive, 1.0f, 0.0f, 0.0f, 0.0f, kMobCrystal | kMobFireImmune | kMobNoFallDamage, 0, {{}}},
        {"ghast", MobKind::Hostile, 10.0f, 0.1f, 0.0f, 100.0f, kMobFlies | kMobFireballs | kMobFireImmune | kMobNoFallDamage, 5, {{{"ghast_tear", 0, 1, 1.0f}, {"gunpowder", 0, 2, 1.0f}}}},
    };
    return table;
}

float scaleForDifficulty(float damage, int difficulty) {  // LivingEntity.hurtServer: what a mob's hit does on each difficulty
    if (difficulty <= 0) return 0.0f;
    if (difficulty == 1) return std::min(damage / 2.0f + 1.0f, damage);
    if (difficulty == 3) return damage * 3.0f / 2.0f;
    return damage;
}

float wrapDegrees(float d) {
    d = std::fmod(d, 360.0f);
    if (d >= 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

float yawTo(double dx, double dz) { return static_cast<float>(std::atan2(-dx, dz)) * kDegrees; }  // 0 faces south (+z), 90 west

bool isDay(long long dayTime) {  // timeline gameplay/monsters_burn: from 23460 round to 12542
    const long long t = ((dayTime % 24000) + 24000) % 24000;
    return t >= 23460 || t < 12542;
}

bool lineClear(const Level& level, double x0, double y0, double z0, double x1, double y1, double z1) {
    const double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int steps = std::max(1, static_cast<int>(length / 0.4));
    for (int i = 1; i < steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        if (mobBlocks(level.block(fl(x0 + dx * t), fl(y0 + dy * t), fl(z0 + dz * t)))) return false;
    }
    return true;
}

// Entity.move with a step up of 0.6 (maxUpStep): the way it can go, and where it ended.
void moveBody(const Level& level, Mob& m, double dx, double dy, double dz, bool step) {
    const Aabb box = m.box();
    std::array<double, 3> actual = collideAabb(level, box, dx, dy, dz);
    const bool hitX = std::abs(dx - actual[0]) > 1.0e-9, hitZ = std::abs(dz - actual[2]) > 1.0e-9;
    const bool groundedAfter = m.onGround || (dy < 0.0 && std::abs(dy - actual[1]) > 1.0e-9);
    if (step && groundedAfter && (hitX || hitZ)) {
        const double up = collideAabb(level, box, 0.0, 0.6, 0.0)[1];
        if (up > 0.0) {
            const auto over = collideAabb(level, box.moved(0.0, up, 0.0), dx, 0.0, dz);
            if (over[0] * over[0] + over[2] * over[2] > actual[0] * actual[0] + actual[2] * actual[2]) {
                const auto down = collideAabb(level, box.moved(over[0], up, over[2]), 0.0, -up, 0.0);
                actual = {over[0], up + down[1], over[2]};
            }
        }
    }
    m.x += actual[0];
    m.y += actual[1];
    m.z += actual[2];
    m.horizontalCollision = hitX || hitZ;
    const bool hitY = std::abs(dy - actual[1]) > 1.0e-9;
    m.onGround = hitY && dy < 0.0;
    if (hitX) m.vx = 0.0;
    if (hitZ) m.vz = 0.0;
    if (hitY) m.vy = 0.0;
    // Falling: the distance adds up in the air and is settled on landing.
    if (m.onGround) {
        // (settled by the caller, which knows the species)
    } else if (actual[1] < 0.0) {
        m.fallDistance -= actual[1];
    }
}

}  // namespace

const MobSpecies* mobSpecies(const std::string& id) {
    for (const MobSpecies& s : makeSpecies()) {
        if (id == s.id) return &s;
    }
    return nullptr;
}
const std::vector<MobSpecies>& allMobSpecies() { return makeSpecies(); }

// ---------------------------------------------------------------------------------------------------------------

Mobs::Mobs(MobsHost host, std::uint64_t seed) : host_(std::move(host)), rng_(seed ^ 0x6d6f62734d6f6273ull) {}

Mob* Mobs::spawn(const std::string& type, double x, double y, double z, bool persistent) {
    const MobSpecies* species = mobSpecies(type);
    if (species == nullptr) return nullptr;
    Mob m;
    m.id = nextId_++;
    m.species = species;
    float w = 0.6f, h = 1.8f;
    if (MobRenderer::sizeOf(type, w, h)) {
        m.width = w;
        m.height = h;
    }
    m.x = m.xo = x;
    m.y = m.yo = y;
    m.z = m.zo = z;
    m.yaw = m.yawo = random() * 360.0f - 180.0f;
    m.health = species->health;
    m.persistent = persistent;
    m.ambientTimer = -randomInt(80);
    m.eggTimer = 6000 + randomInt(6000);
    m.flyAngle = random() * 6.28318f;
    mobs_.push_back(std::move(m));
    return &mobs_.back();
}

Mob* Mobs::find(int id) {
    for (Mob& m : mobs_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

int Mobs::count(MobKind kind) const {
    int n = 0;
    for (const Mob& m : mobs_) {
        if (m.species->kind == kind && !m.dead()) ++n;
    }
    return n;
}

int Mobs::remove(const std::function<bool(const Mob&)>& which) {
    const std::size_t before = mobs_.size();
    mobs_.erase(std::remove_if(mobs_.begin(), mobs_.end(), which), mobs_.end());
    return static_cast<int>(before - mobs_.size());
}

void Mobs::clear() {
    mobs_.clear();
    shots_.clear();
}

int Mobs::pick(const std::array<double, 3>& eye, const std::array<double, 3>& look, double maxDistance, double* distance) const {
    int found = -1;
    double best = maxDistance;
    for (const Mob& m : mobs_) {
        if (m.dead()) continue;
        const Aabb b = m.box();
        const double lo[3] = {b.minX, b.minY, b.minZ}, hi[3] = {b.maxX, b.maxY, b.maxZ};
        double tNear = 0.0, tFar = best;
        bool hit = true;
        for (int axis = 0; axis < 3 && hit; ++axis) {
            const std::size_t a = static_cast<std::size_t>(axis);
            if (std::abs(look[a]) < 1.0e-9) {
                if (eye[a] < lo[axis] || eye[a] > hi[axis]) hit = false;
                continue;
            }
            double t0 = (lo[axis] - eye[a]) / look[a], t1 = (hi[axis] - eye[a]) / look[a];
            if (t0 > t1) std::swap(t0, t1);
            tNear = std::max(tNear, t0);
            tFar = std::min(tFar, t1);
            if (tNear > tFar) hit = false;
        }
        if (hit && tNear < best) {
            best = tNear;
            found = m.id;
        }
    }
    if (found >= 0 && distance != nullptr) *distance = best;
    return found;
}

// ---------------------------------------------------------------------------------------------------------------
// Hurting and dying

bool Mobs::hurt(int id, float amount, double fromX, double fromZ, float knockback, bool byPlayer) {
    Mob* m = find(id);
    if (m == nullptr || m->dead() || amount <= 0.0f) return false;
    float applied = amount;
    if (m->invulnerable > 10) {  // LivingEntity.hurtServer: in the half second after a hit only a stronger one counts, for the difference
        if (amount <= m->lastHurt) return false;
        applied = amount - m->lastHurt;
        m->lastHurt = amount;
    } else {
        m->lastHurt = amount;
        m->invulnerable = 20;
        m->hurtTime = 10;
    }
    m->health -= applied;
    // LivingEntity.knockback: pushed away from where the blow came from.
    if (knockback > 0.0f) {
        double dx = fromX - m->x, dz = fromZ - m->z;
        const double length = std::sqrt(dx * dx + dz * dz);
        if (length > 1.0e-4) {
            dx /= length;
            dz /= length;
            m->vx = m->vx / 2.0 - dx * static_cast<double>(knockback);
            m->vz = m->vz / 2.0 - dz * static_cast<double>(knockback);
            if (m->onGround) m->vy = std::min(0.4, m->vy / 2.0 + static_cast<double>(knockback));
        }
    }
    const bool hostile = m->species->kind != MobKind::Passive;
    const std::string base = std::string("entity.") + m->species->id + ".";
    if (host_.sound && !m->dead()) host_.sound(base + "hurt", m->x, m->y + 0.5, m->z, 1.0f, 1.0f + (random() - random()) * 0.2f, hostile);
    // What the blow makes it do.
    if (m->species->kind == MobKind::Passive) {
        m->panic = 100;
        m->path.clear();
    } else {
        m->target = 1;
        if ((m->species->traits & (kMobAngryWhenHurt | kMobAngryWhenLookedAt | kMobNeutralInDaylight)) != 0 && byPlayer) m->angry = 600 + randomInt(400);
        if ((m->species->traits & kMobAngryWhenHurt) != 0) {  // the others near it join in
            for (Mob& other : mobs_) {
                if (&other != m && other.species == m->species && std::abs(other.x - m->x) < 16.0 && std::abs(other.z - m->z) < 16.0 && std::abs(other.y - m->y) < 10.0) {
                    other.angry = 600 + randomInt(400);
                    other.target = 1;
                }
            }
        }
    }
    if (m->dead()) {
        if (byPlayer) m->lastHurt = -1.0f;  // (marks who did it, for the drops and the experience)
        die(*m);
    }
    return true;
}

void Mobs::die(Mob& m) {
    m.health = 0.0f;
    m.deathTime = 0;
    m.path.clear();
    if ((m.species->traits & kMobCrystal) != 0) {  // an end crystal blows up (EndCrystal.hurt: an explosion of power 6)
        if (host_.explode) host_.explode(m.x, m.y + 0.5, m.z, 6.0f);
        m.deathTime = 100;
        return;
    }
    const bool byPlayer = m.lastHurt < 0.0f;
    const bool hostile = m.species->kind != MobKind::Passive;
    if (host_.sound) host_.sound(std::string("entity.") + m.species->id + ".death", m.x, m.y + 0.5, m.z, 1.0f, 1.0f + (random() - random()) * 0.2f, hostile);
    if (host_.dropItem) {
        for (const MobDrop& drop : m.species->drops) {
            if (drop.item == nullptr) break;
            if (random() >= drop.chance) continue;
            const int n = drop.min + (drop.max > drop.min ? randomInt(drop.max - drop.min + 1) : 0);
            const auto item = itemRegistry().find(drop.item);
            if (n <= 0 || !item) continue;
            host_.dropItem(ItemStack(*item, n), m.x, m.y + 0.3, m.z);
        }
    }
    if (byPlayer && host_.addExperience) host_.addExperience(m.species->xp > 1 ? m.species->xp : 1 + randomInt(3));
}

// ---------------------------------------------------------------------------------------------------------------
// The brain

struct MobBrain {
    Mobs& mobs;
    Mob& m;
    const MobTick& in;
    const Level& level;
    double dx, dy, dz, distSq;  // to the player's feet

    MobBrain(Mobs& mobs_, Mob& m_, const MobTick& in_) : mobs(mobs_), m(m_), in(in_), level(*in_.level) {
        dx = in.player[0] - m.x;
        dy = in.player[1] - m.y;
        dz = in.player[2] - m.z;
        distSq = dx * dx + dy * dy + dz * dz;
    }
    [[nodiscard]] const MobSpecies& sp() const { return *m.species; }
    [[nodiscard]] bool has(std::uint32_t trait) const { return (sp().traits & trait) != 0; }

    [[nodiscard]] bool canSeePlayer() const {
        return lineClear(level, m.x, m.eyeY(), m.z, in.player[0], in.playerEyeY, in.player[2]);
    }

    void stop() {
        m.moveSpeed = 0.0f;
        m.path.clear();
    }

    // Walks towards a cell with a path, found again now and then (a path takes some time to make: only a few are made each tick).
    void walkTo(double tx, double ty, double tz, float speed) {
        const std::array<int, 3> goal{fl(tx), fl(ty), fl(tz)};
        const double goalMoved = std::abs(goal[0] - m.pathGoal[0]) + std::abs(goal[1] - m.pathGoal[1]) + std::abs(goal[2] - m.pathGoal[2]);
        const bool done = m.pathIndex >= m.path.size();
        if (--m.repathTimer <= 0 || (done && goalMoved > 0) || goalMoved > 3) {
            if (mobs.pathBudget_ > 0) {
                --mobs.pathBudget_;
                m.path = findMobPath(level, {fl(m.x), fl(m.y + 0.05), fl(m.z)}, goal, m.width, m.height, 260, false);
                m.pathIndex = 0;
                m.pathGoal = goal;
                m.repathTimer = 10 + mobs.randomInt(10);
            } else {
                m.repathTimer = 1;
            }
        }
        follow(speed);
        // Nothing left of the path but the goal is close: straight at it.
        if (m.pathIndex >= m.path.size() && std::abs(tx - m.x) + std::abs(tz - m.z) < 3.0) {
            const double ddx = tx - m.x, ddz = tz - m.z;
            if (ddx * ddx + ddz * ddz > 0.04) {
                m.moveSpeed = speed;
                turnBody(yawTo(ddx, ddz), 90.0f);
                if (m.horizontalCollision && m.onGround) m.wantsJump = true;
            }
        }
    }

    void follow(float speed) {
        while (m.pathIndex < m.path.size()) {
            const auto& node = m.path[m.pathIndex];
            const double nx = node[0] + 0.5, nz = node[2] + 0.5;
            const double ddx = nx - m.x, ddz = nz - m.z;
            if (ddx * ddx + ddz * ddz < 0.25 && std::abs(node[1] - m.y) < 1.1) {
                ++m.pathIndex;
                continue;
            }
            m.moveSpeed = speed;
            turnBody(yawTo(ddx, ddz), 90.0f);
            if ((node[1] > m.y + 0.5 && m.onGround) || (m.horizontalCollision && m.onGround)) m.wantsJump = true;
            return;
        }
    }

    void turnBody(float target, float maxStep) {
        const float d = std::clamp(wrapDegrees(target - m.yaw), -maxStep, maxStep);
        m.yaw += d;
    }

    // The head follows what the mob looks at (the player, or straight ahead), at most 75 degrees off the body.
    void lookAt(double tx, double ty, double tz) {
        const double ddx = tx - m.x, ddz = tz - m.z, ddy = ty - m.eyeY();
        const float wantYaw = std::clamp(wrapDegrees(yawTo(ddx, ddz) - m.yaw), -75.0f, 75.0f);
        const float wantPitch = std::clamp(static_cast<float>(-std::atan2(ddy, std::sqrt(ddx * ddx + ddz * ddz))) * kDegrees, -40.0f, 40.0f);
        m.headYaw += std::clamp(wantYaw - m.headYaw, -30.0f, 30.0f);
        m.headPitch += std::clamp(wantPitch - m.headPitch, -20.0f, 20.0f);
    }
    void lookAhead() {
        m.headYaw *= 0.8f;
        m.headPitch *= 0.8f;
    }

    // ---- passive ----
    void passive() {
        if (m.panic > 0) {
            --m.panic;
            if (m.pathIndex >= m.path.size()) {  // away: a point five to seven blocks off in a random direction
                const double a = static_cast<double>(mobs.random()) * 2.0 * kPi, r = 5.0 + static_cast<double>(mobs.random()) * 3.0;
                walkTo(m.x + std::cos(a) * r, m.y, m.z + std::sin(a) * r, 1.4f);
            } else {
                follow(1.4f);
            }
            lookAhead();
            return;
        }
        if (m.pathIndex < m.path.size()) {
            follow(1.0f);
            if (++m.stuckTimer > 100) stop();
        } else {
            m.stuckTimer = 0;
            // RandomStrollGoal: now and then one random place within ten blocks.
            if (mobs.randomInt(120) == 0 && mobs.pathBudget_ > 0) {
                const int tx = fl(m.x) + mobs.randomInt(21) - 10, tz = fl(m.z) + mobs.randomInt(21) - 10;
                if (level.isLoaded(tx, tz)) {
                    --mobs.pathBudget_;
                    m.path = findMobPath(level, {fl(m.x), fl(m.y + 0.05), fl(m.z)}, {tx, fl(m.y), tz}, m.width, m.height, 200, false);
                    m.pathIndex = 0;
                    m.pathGoal = {tx, fl(m.y), tz};
                }
            }
        }
        if (distSq < 36.0 && in.playerTargetable && mobs.randomInt(50) < 3) m.flyTimer = 40;  // LookAtPlayerGoal: it looks for two seconds
        if (m.flyTimer > 0) {
            --m.flyTimer;
            lookAt(in.player[0], in.playerEyeY, in.player[2]);
        } else {
            lookAhead();
        }
        if (has(kMobLaysEggs) && --m.eggTimer <= 0) {
            m.eggTimer = 6000 + mobs.randomInt(6000);
            if (mobs.host_.sound) mobs.host_.sound("entity.chicken.egg", m.x, m.y, m.z, 1.0f, 1.0f + (mobs.random() - mobs.random()) * 0.2f, false);
            const auto egg = itemRegistry().find("egg");
            if (egg && mobs.host_.dropItem) mobs.host_.dropItem(ItemStack(*egg, 1), m.x, m.y + 0.1, m.z);
        }
    }

    // ---- who it hunts ----
    bool wantsPlayer() {
        if (!in.playerTargetable || in.difficulty == 0) return false;
        const float follow = sp().follow;
        if (distSq > static_cast<double>(follow) * static_cast<double>(follow)) return false;
        if (m.angry > 0) return true;
        if (has(kMobAngryWhenLookedAt)) {  // EnderMan.isLookingAtMe: the player's view is on its head and it can be seen
            const double ddx = m.x - in.player[0], ddy = m.eyeY() - in.playerEyeY, ddz = m.z - in.player[2];
            const double length = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
            if (length < 1.0e-6) return false;
            const double yawRad = static_cast<double>(in.playerYaw) / static_cast<double>(kDegrees), pitchRad = static_cast<double>(in.playerPitch) / static_cast<double>(kDegrees);
            const double lx = -std::sin(yawRad) * std::cos(pitchRad), ly = -std::sin(pitchRad), lz = std::cos(yawRad) * std::cos(pitchRad);
            const double dot = (lx * ddx + ly * ddy + lz * ddz) / length;
            if (dot > 1.0 - 0.025 / length && canSeePlayer()) {
                if (mobs.host_.sound) mobs.host_.sound("entity.enderman.stare", m.x, m.eyeY(), m.z, 2.5f, 1.0f, true);
                m.angry = 600;
                return true;
            }
            return false;
        }
        if (has(kMobAngryWhenHurt)) return false;
        if (has(kMobNeutralInDaylight)) {  // a spider only hunts where it is dim (or once it was hit)
            int sky = 0, block = 0;
            if (mobs.host_.light) mobs.host_.light(fl(m.x), fl(m.y + 0.5), fl(m.z), sky, block);
            const float brightness = static_cast<float>(std::max(block, static_cast<int>(std::max(0.0f, static_cast<float>(sky) - in.darkSteps)))) / 15.0f;
            if (brightness > 0.5f && mobs.randomInt(100) != 0) return false;
        }
        return m.target != 0 || canSeePlayer();
    }

    // ---- hostile and neutral ----
    void hostile() {
        const bool wants = wantsPlayer();
        if (wants) {
            m.target = 1;
            m.unseen = 0;
        } else if (m.target != 0) {
            if (distSq > static_cast<double>(sp().follow) * static_cast<double>(sp().follow) * 1.2 || !in.playerTargetable || ++m.unseen > 60) m.target = 0;
        }
        if (m.angry > 0) --m.angry;
        if (m.target == 0) {
            if (has(kMobCreeper)) m.fuseDir = -1;
            wander();
            return;
        }
        lookAt(in.player[0], in.playerEyeY, in.player[2]);
        const double dist = std::sqrt(distSq);
        if (has(kMobCreeper)) return creeper(dist);
        if (has(kMobArcher)) return archer(dist);
        if (has(kMobFireballs)) return;  // (the flying brains steer themselves)
        // Melee: along a path to the player, and a blow when it is near enough (MeleeAttackGoal).
        walkTo(in.player[0], in.player[1], in.player[2], 1.0f);
        if (m.attackCooldown > 0) --m.attackCooldown;
        const double reach = static_cast<double>(m.width) * 2.0;
        if (distSq <= reach * reach + static_cast<double>(in.playerWidth) && std::abs(dy) < 1.6 && m.attackCooldown <= 0) {
            m.attackCooldown = 20;
            if (mobs.host_.hurtPlayer) mobs.host_.hurtPlayer(scaleForDifficulty(sp().attack, in.difficulty), m.x, m.z, 0.4f);
        }
        if (has(kMobLeaps) && m.onGround && dist > 2.0 && dist < 4.0 && std::abs(dy) < 1.5 && mobs.randomInt(10) == 0) {  // LeapAtTargetGoal
            m.vx += dx / dist * 0.4 + m.vx * 0.2;
            m.vz += dz / dist * 0.4 + m.vz * 0.2;
            m.vy = 0.4;
        }
        if (has(kMobTeleports)) enderman(dist);
    }

    void wander() {
        if (m.pathIndex < m.path.size()) {
            follow(0.8f);
            if (++m.stuckTimer > 100) stop();
        } else {
            m.stuckTimer = 0;
            if (mobs.randomInt(120) == 0 && mobs.pathBudget_ > 0) {  // (RandomStrollGoal, at 0.8)
                const int tx = fl(m.x) + mobs.randomInt(21) - 10, tz = fl(m.z) + mobs.randomInt(21) - 10;
                if (level.isLoaded(tx, tz)) {
                    --mobs.pathBudget_;
                    m.path = findMobPath(level, {fl(m.x), fl(m.y + 0.05), fl(m.z)}, {tx, fl(m.y), tz}, m.width, m.height, 200, false);
                    m.pathIndex = 0;
                }
            }
        }
        lookAhead();
    }

    void creeper(double dist) {
        // Swell.Goal: within three blocks (or already swelling and within seven, in sight) it swells, otherwise it calms.
        const bool swell = dist < 3.0 || (m.fuse > 0 && dist < 7.0 && canSeePlayer());
        const int before = m.fuseDir;
        m.fuseDir = swell ? 1 : -1;
        if (swell) {
            m.moveSpeed = 0.0f;
            m.path.clear();
            if (before < 0 && mobs.host_.sound) mobs.host_.sound("entity.creeper.primed", m.x, m.y, m.z, 1.0f, 0.5f, true);
        } else {
            walkTo(in.player[0], in.player[1], in.player[2], 1.0f);
        }
    }

    void archer(double dist) {
        const bool sees = canSeePlayer();
        if (dist > 14.0 || !sees) {
            walkTo(in.player[0], in.player[1], in.player[2], 1.0f);
        } else {
            m.moveSpeed = 0.0f;
            m.path.clear();
            turnBody(yawTo(dx, dz), 30.0f);
            if (dist < 5.0 && m.onGround) {  // too close: back off
                m.moveSpeed = 0.6f;
                m.yaw = yawTo(-dx, -dz);
            }
        }
        if (m.shootCooldown > 0) --m.shootCooldown;
        if (sees && dist < 15.0 && m.shootCooldown <= 0) {
            m.shootCooldown = in.difficulty >= 3 ? 20 : 40;
            const double ax = in.player[0] - m.x, az = in.player[2] - m.z;
            const double ay = (in.playerEyeY - 0.2) - (m.eyeY() - 0.1);
            const double horizontal = std::sqrt(ax * ax + az * az);
            const double inaccuracy = static_cast<double>(14 - in.difficulty * 4);
            const double dirY = ay + horizontal * 0.2;
            const double len = std::sqrt(ax * ax + dirY * dirY + az * az);
            const auto gauss = [&] { return static_cast<double>(mobs.random() + mobs.random() + mobs.random() - 1.5f) * 1.15; };
            const double speed = 1.6;
            mobs.shoot(Projectile::Kind::Arrow, m.x, m.eyeY() - 0.1, m.z, (ax / len + gauss() * 0.0075 * inaccuracy) * speed, (dirY / len + gauss() * 0.0075 * inaccuracy) * speed,
                       (az / len + gauss() * 0.0075 * inaccuracy) * speed, 2.0f, m.id);
            if (mobs.host_.sound) mobs.host_.sound("entity.skeleton.shoot", m.x, m.eyeY(), m.z, 1.0f, 1.0f / (mobs.random() * 0.4f + 0.8f), true);
        }
    }

    void enderman(double dist) {
        // EndermanLookForPlayerGoal.tick: stared at from close by, it steps away; far off, it appears beside the player.
        if (++m.teleportTimer >= 30 && dist > 16.0 && wantsLookTeleport()) {
            m.teleportTimer = 0;
            teleportNear(in.player[0], in.player[1], in.player[2]);
        } else if (dist < 4.0 && m.teleportTimer > 40 && mobs.randomInt(30) == 0) {
            m.teleportTimer = 0;
            teleportRandom();
        }
    }
    bool wantsLookTeleport() { return mobs.randomInt(3) == 0; }

    bool teleportTo(double x, double y, double z) {
        // EnderMan.teleport: down to the ground, in a place with room and no water.
        int by = fl(y);
        const int bx = fl(x), bz = fl(z);
        if (!level.isLoaded(bx, bz)) return false;
        while (by > kMinY && !mobBlocks(level.block(bx, by - 1, bz))) --by;
        const BlockRegistry& registry = blockRegistry();
        for (int i = 0; i < 3; ++i) {
            if (mobBlocks(level.block(bx, by + i, bz)) || registry.state(level.block(bx, by + i, bz)).has(kStateLiquid)) return false;
        }
        if (registry.state(level.block(bx, by - 1, bz)).has(kStateLiquid)) return false;
        const double ox = m.x, oy = m.y, oz = m.z;
        m.x = bx + 0.5;
        m.y = by;
        m.z = bz + 0.5;
        m.path.clear();
        m.vx = m.vz = 0.0;
        m.vy = 0.0;
        if (mobs.host_.sound) {
            mobs.host_.sound("entity.enderman.teleport", ox, oy, oz, 1.0f, 1.0f, true);
            mobs.host_.sound("entity.enderman.teleport", m.x, m.y, m.z, 1.0f, 1.0f, true);
        }
        return true;
    }
    bool teleportRandom() {
        for (int tries = 0; tries < 6; ++tries) {
            if (teleportTo(m.x + (static_cast<double>(mobs.random()) - 0.5) * 64.0, m.y + static_cast<double>(mobs.randomInt(64)) - 32.0, m.z + (static_cast<double>(mobs.random()) - 0.5) * 64.0)) return true;
        }
        return false;
    }
    bool teleportNear(double px, double py, double pz) {
        for (int tries = 0; tries < 6; ++tries) {
            const double a = static_cast<double>(mobs.random()) * 2.0 * kPi, r = 3.0 + static_cast<double>(mobs.random()) * 3.0;
            if (teleportTo(px + std::cos(a) * r, py + 1.0, pz + std::sin(a) * r)) return true;
        }
        return false;
    }

    // ---- the Ender Dragon (a few of the game's phases: the holding pattern, the charge at the player, the strafe with fireballs) ----
    void dragon() {
        const bool hasPlayer = in.playerTargetable && in.dimension == 2;
        m.target = hasPlayer ? 1 : 0;
        ++m.flyTimer;
        if (m.attackCooldown > 0) --m.attackCooldown;
        if (m.shootCooldown > 0) --m.shootCooldown;
        double tx = 0.0, ty = 78.0, tz = 0.0, speed = 1.0;
        if (m.burst == 1 && hasPlayer) {  // charge: straight at the player, and a blow when it gets there
            tx = in.player[0];
            ty = in.player[1] + 1.0;
            tz = in.player[2];
            speed = 1.5;
            if (distSq < 64.0 && m.attackCooldown <= 0) {
                m.attackCooldown = 40;
                if (mobs.host_.hurtPlayer) mobs.host_.hurtPlayer(scaleForDifficulty(10.0f, in.difficulty), m.x, m.z, 1.6f);
                if (mobs.host_.sound) mobs.host_.sound("entity.ender_dragon.growl", m.x, m.y, m.z, 5.0f, 0.8f + mobs.random() * 0.3f, true);
            }
            if (distSq < 16.0 || m.flyTimer > 140) {
                m.burst = 0;
                m.flyTimer = 0;
            }
        } else {  // the holding pattern: a ring of fifty blocks round the island, rising and falling
            m.flyAngle += 0.018f;
            const double a = static_cast<double>(m.flyAngle);
            tx = std::cos(a) * 48.0;
            tz = std::sin(a) * 48.0;
            ty = 80.0 + std::sin(a * 3.0) * 8.0;
            speed = 1.1;
            if (m.burst == 2 && hasPlayer) {  // the strafe: fire at the player as it goes round
                if (m.shootCooldown <= 0 && distSq < 100.0 * 100.0) {
                    m.shootCooldown = 40;
                    const double vx = in.player[0] - m.x, vy = in.player[1] + 1.0 - (m.y - 2.0), vz = in.player[2] - m.z;
                    const double l = std::sqrt(vx * vx + vy * vy + vz * vz);
                    mobs.shoot(Projectile::Kind::LargeFireball, m.x, m.y - 2.0, m.z, vx / l * 1.0, vy / l * 1.0, vz / l * 1.0, 6.0f, m.id);
                    if (mobs.host_.sound) mobs.host_.sound("entity.ender_dragon.shoot", m.x, m.y, m.z, 5.0f, 1.0f, true);
                }
                if (m.flyTimer > 300) {
                    m.burst = 0;
                    m.flyTimer = 0;
                }
            } else if (m.flyTimer > 360 && hasPlayer) {  // after a while, one or the other
                m.burst = mobs.randomInt(2) == 0 ? 1 : 2;
                m.flyTimer = 0;
            }
        }
        const double ddx = tx - m.x, ddy = ty - m.y, ddz = tz - m.z;
        const double len = std::max(1.0e-6, std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz));
        m.vx += (ddx / len * speed - m.vx) * 0.05;
        m.vy += (ddy / len * speed - m.vy) * 0.05;
        m.vz += (ddz / len * speed - m.vz) * 0.05;
        const float desired = yawTo(m.vx, m.vz);
        turnBody(desired, 6.0f);
        const double horizontal = std::sqrt(m.vx * m.vx + m.vz * m.vz);
        m.headPitch = std::clamp(static_cast<float>(-std::atan2(m.vy, std::max(horizontal, 0.05))) * kDegrees, -30.0f, 30.0f);
        if (m.age % 10 == 0 && m.health < sp().health) {  // an end crystal within 32 blocks heals it (EnderDragon.checkCrystals)
            for (const Mob& other : mobs.mobs_) {
                if ((other.species->traits & kMobCrystal) != 0 && !other.dead() && std::abs(other.x - m.x) < 32.0 && std::abs(other.y - m.y) < 40.0 && std::abs(other.z - m.z) < 32.0) {
                    m.health = std::min(sp().health, m.health + 1.0f);
                    break;
                }
            }
        }
        if (m.age % 200 == 0 && mobs.host_.sound) mobs.host_.sound("entity.ender_dragon.ambient", m.x, m.y, m.z, 5.0f, 1.0f, true);
    }

    // ---- the ones that fly ----
    void flyer() {
        const bool ghast = std::string(sp().id) == "ghast";
        const bool hunting = m.target != 0 || (in.playerTargetable && in.difficulty > 0 && distSq < static_cast<double>(sp().follow) * static_cast<double>(sp().follow) && canSeePlayer());
        if (hunting && in.playerTargetable && in.difficulty > 0) m.target = 1;
        else m.target = 0;
        // Where it drifts: a point round the player (or round itself), changed now and then (a blaze hovers about three blocks above the player's head height).
        if (--m.flyTimer <= 0 || (m.flyTargetX - m.x) * (m.flyTargetX - m.x) + (m.flyTargetZ - m.z) * (m.flyTargetZ - m.z) < 4.0) {
            m.flyTimer = 60 + mobs.randomInt(60);
            const double cx = m.target != 0 ? in.player[0] : m.x, cy = m.target != 0 ? in.player[1] : m.y, cz = m.target != 0 ? in.player[2] : m.z;
            const double a = static_cast<double>(mobs.random()) * 2.0 * kPi, r = ghast ? 12.0 + static_cast<double>(mobs.random()) * 8.0 : 4.0 + static_cast<double>(mobs.random()) * 4.0;
            m.flyTargetX = cx + std::cos(a) * r;
            m.flyTargetZ = cz + std::sin(a) * r;
            m.flyTargetY = cy + (ghast ? 6.0 + static_cast<double>(mobs.random()) * 6.0 : 1.0 + static_cast<double>(mobs.random()) * 3.0);
        }
        double tx = m.flyTargetX - m.x, ty = m.flyTargetY - m.y, tz = m.flyTargetZ - m.z;
        const double length = std::max(1.0e-6, std::sqrt(tx * tx + ty * ty + tz * tz));
        const double accel = ghast ? 0.05 : 0.04;
        m.vx += tx / length * accel;
        m.vy += ty / length * accel * 0.7;
        m.vz += tz / length * accel;
        const double speed = std::sqrt(m.vx * m.vx + m.vy * m.vy + m.vz * m.vz);
        const double limit = ghast ? 0.25 : 0.2;
        if (speed > limit) {
            m.vx *= limit / speed;
            m.vy *= limit / speed;
            m.vz *= limit / speed;
        }
        if (m.target != 0) {
            turnBody(yawTo(dx, dz), 20.0f);
            lookAt(in.player[0], in.playerEyeY, in.player[2]);
        } else if (speed > 0.02) {
            turnBody(yawTo(m.vx, m.vz), 10.0f);
        }
        if (m.target == 0 || !canSeePlayer()) {
            m.shootCooldown = std::max(m.shootCooldown, ghast ? -40 : 0);
            return;
        }
        const double dist = std::sqrt(distSq);
        if (ghast) {  // a long look, then a fireball: it charges for a second, shoots, and rests two
            if (dist > 64.0) return;
            ++m.shootCooldown;
            if (m.shootCooldown == 10 && mobs.host_.sound) mobs.host_.sound("entity.ghast.warn", m.x, m.y, m.z, 10.0f, 1.0f, true);
            if (m.shootCooldown >= 20) {
                m.shootCooldown = -40;
                const double vx = in.player[0] - m.x, vy = in.player[1] + 1.0 - (m.y + static_cast<double>(m.height) / 2.0), vz = in.player[2] - m.z;
                const double l = std::sqrt(vx * vx + vy * vy + vz * vz);
                mobs.shoot(Projectile::Kind::LargeFireball, m.x, m.y + static_cast<double>(m.height) / 2.0, m.z, vx / l * 1.0, vy / l * 1.0, vz / l * 1.0, 6.0f, m.id);
                if (mobs.host_.sound) mobs.host_.sound("entity.ghast.shoot", m.x, m.y, m.z, 10.0f, 1.0f, true);
            }
        } else {  // blaze: volleys of three small fireballs, a few seconds apart
            if (dist > 40.0) return;
            if (m.attackCooldown > 0) --m.attackCooldown;
            if (m.burst == 0 && m.attackCooldown <= 0) {
                m.burst = 3;
                m.attackCooldown = 100;
                m.shootCooldown = 0;
            }
            if (m.burst > 0 && --m.shootCooldown <= 0) {
                m.shootCooldown = 6;
                --m.burst;
                const double vx = in.player[0] - m.x, vy = in.player[1] + 0.5 - (m.y + static_cast<double>(m.height) / 2.0), vz = in.player[2] - m.z;
                const double l = std::sqrt(vx * vx + vy * vy + vz * vz);
                mobs.shoot(Projectile::Kind::SmallFireball, m.x, m.y + static_cast<double>(m.height) * 0.6, m.z, vx / l * 0.9, vy / l * 0.9, vz / l * 0.9, 5.0f, m.id);
                if (mobs.host_.sound) mobs.host_.sound("entity.blaze.shoot", m.x, m.y, m.z, 2.0f, (mobs.random() - mobs.random()) * 0.2f + 1.0f, true);
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// One tick of one mob

void Mobs::tickMob(Mob& m, const MobTick& in) {
    const Level& level = *in.level;
    m.xo = m.x;
    m.yo = m.y;
    m.zo = m.z;
    m.yawo = m.yaw;
    ++m.age;
    if (m.hurtTime > 0) --m.hurtTime;
    if (m.invulnerable > 0) --m.invulnerable;
    const MobSpecies& sp = *m.species;
    const bool flies = (sp.traits & kMobFlies) != 0;
    if ((sp.traits & kMobCrystal) != 0) return;  // (an end crystal hangs where it is until something hurts it)

    if (m.dead()) {  // the fall to its side: it lies there for a second, then it is gone
        ++m.deathTime;
        m.moveSpeed = 0.0f;
    } else {
        MobBrain brain(*this, m, in);
        m.moveSpeed = 0.0f;
        m.wantsJump = false;
        if (sp.kind == MobKind::Passive) brain.passive();
        else if ((sp.traits & kMobDragon) != 0) brain.dragon();
        else if (flies) brain.flyer();
        else brain.hostile();
        // Creeper: the fuse swells or calms; at 30 it goes off.
        if ((sp.traits & kMobCreeper) != 0) {
            m.fuse = std::clamp(m.fuse + m.fuseDir, 0, 30);
            if (m.fuse >= 30) {
                const float power = 3.0f;
                const double ex = m.x, ey = m.y + static_cast<double>(m.height) / 2.0, ez = m.z;
                m.health = 0.0f;
                m.deathTime = 100;  // (gone at once)
                if (host_.explode && in.mobGriefing) host_.explode(ex, ey, ez, power);
                else if (host_.hurtPlayer) {
                    const double dist = std::sqrt((in.player[0] - ex) * (in.player[0] - ex) + (in.player[1] - ey) * (in.player[1] - ey) + (in.player[2] - ez) * (in.player[2] - ez));
                    if (dist < 6.0) host_.hurtPlayer(static_cast<float>((1.0 - dist / 6.0) * 14.0), ex, ez, 0.4f);
                }
                for (Mob& other : mobs_) {  // the others near it feel it too (Explosion.hurtEntities)
                    if (&other == &m || other.dead()) continue;
                    const double dist = std::sqrt((other.x - ex) * (other.x - ex) + (other.y + static_cast<double>(other.height) / 2.0 - ey) * (other.y + static_cast<double>(other.height) / 2.0 - ey) + (other.z - ez) * (other.z - ez));
                    if (dist >= 6.0) continue;
                    const float impact = static_cast<float>(1.0 - dist / 6.0);
                    other.invulnerable = 0;
                    hurt(other.id, (impact * impact + impact) / 2.0f * 7.0f * 6.0f + 1.0f, ex, ez, impact * 1.5f, false);
                }
                return;
            }
        }
    }

    // ---- the world's effects ----
    const BlockRegistry& registry = blockRegistry();
    const Block atFeet = level.block(fl(m.x), fl(m.y + 0.1), fl(m.z));
    const Block atBody = level.block(fl(m.x), fl(m.y + static_cast<double>(m.height) * 0.5), fl(m.z));
    m.inWater = registry.state(atFeet).has(kStateWater);
    m.inLava = registry.state(atFeet).has(kStateLava) || registry.state(atBody).has(kStateLava);

    if (!m.dead()) {
        const bool fireImmune = (sp.traits & kMobFireImmune) != 0;
        if (m.inLava && !fireImmune) {
            if (m.age % 10 == 0) hurt(m.id, 4.0f, m.x, m.z, 0.0f, false);
            m.burn = 300;
        }
        if (m.burn > 0) {
            --m.burn;
            if (m.inWater) m.burn = 0;
            if (m.burn % 20 == 0 && !fireImmune) {
                m.invulnerable = 0;
                hurt(m.id, 1.0f, m.x, m.z, 0.0f, false);
            }
        }
        if ((sp.traits & kMobBurnsInDaylight) != 0 && in.daylightBurns && isDay(in.dayTime) && !m.inWater && host_.light) {
            int sky = 0, block = 0;
            host_.light(fl(m.x), fl(m.y + static_cast<double>(m.height) - 0.1), fl(m.z), sky, block);
            if (sky >= 15) m.burn = std::max(m.burn, 160);  // (sunlight on its head: Zombie.isSunBurnTick)
        }
        if ((sp.traits & kMobHurtByWater) != 0 && m.inWater && m.age % 10 == 0) {
            hurt(m.id, 1.0f, m.x, m.z, 0.0f, false);
            MobBrain brain(*this, m, in);
            (void)brain.teleportRandom();
        }
        if (m.y < static_cast<double>(kMinY) - 64.0) {
            m.invulnerable = 0;
            hurt(m.id, 4.0f, m.x, m.z, 0.0f, false);
        }
        // Ambient sounds: LivingEntity ambient timer (one in a thousand a tick, once the counter is up).
        if (randomInt(1000) < m.ambientTimer++) {
            m.ambientTimer = -80;
            if (host_.sound) host_.sound(std::string("entity.") + sp.id + ".ambient", m.x, m.y + 0.5, m.z, 1.0f, 1.0f + (random() - random()) * 0.2f, sp.kind != MobKind::Passive);
        }
    }

    // ---- motion ----
    if ((sp.traits & kMobDragon) != 0) {  // (it flies through the blocks it meets; the dying one hangs there)
        if (m.dead()) m.vx = m.vy = m.vz = 0.0;
        m.x += m.vx;
        m.y += m.vy;
        m.z += m.vz;
    } else if (flies) {
        moveBody(level, m, m.vx, m.vy, m.vz, false);
        m.vx *= 0.91;
        m.vy *= 0.91;
        m.vz *= 0.91;
        if (m.dead()) m.vy -= 0.04;
    } else {
        const double yawRad = static_cast<double>(m.yaw) / static_cast<double>(kDegrees);
        const double fx = -std::sin(yawRad), fz = std::cos(yawRad);
        if (m.wantsJump && m.inWater) {
            m.vy += 0.04;  // FloatGoal / JumpControl: it paddles up
        } else if (m.wantsJump && m.onGround) {
            m.vy = 0.42 * static_cast<double>(registry.state(level.block(fl(m.x), fl(m.y - 0.5), fl(m.z))).jumpFactor);
        }
        if ((sp.traits & kMobClimbs) != 0 && m.horizontalCollision && m.target != 0) m.vy = std::max(m.vy, 0.2);  // a spider climbs a wall
        const float speed = sp.speed * m.moveSpeed;
        if (m.inWater) {
            if (m.moveSpeed > 0.0f) { m.vx += fx * 0.02 * static_cast<double>(m.moveSpeed); m.vz += fz * 0.02 * static_cast<double>(m.moveSpeed); }
            if (m.moveSpeed > 0.0f && m.horizontalCollision) m.vy += 0.04;
            moveBody(level, m, m.vx, m.vy, m.vz, true);
            m.vx *= 0.8;
            m.vz *= 0.8;
            m.vy = m.vy * 0.8 - 0.02;
            m.fallDistance = 0.0;
        } else if (m.inLava) {
            if (m.moveSpeed > 0.0f) { m.vx += fx * 0.02 * static_cast<double>(m.moveSpeed); m.vz += fz * 0.02 * static_cast<double>(m.moveSpeed); }
            moveBody(level, m, m.vx, m.vy, m.vz, true);
            m.vx *= 0.5;
            m.vz *= 0.5;
            m.vy = m.vy * 0.5 - 0.02;
            m.fallDistance = 0.0;
        } else {
            const float blockFriction = m.onGround ? registry.state(level.block(fl(m.x), fl(m.y - 0.500001), fl(m.z))).friction : 1.0f;
            const double friction = static_cast<double>(blockFriction) * 0.91;
            const double accel = m.onGround ? static_cast<double>(speed) * (0.21600002 / (friction * friction * friction)) : 0.02 * static_cast<double>(m.moveSpeed);
            m.vx += fx * accel;
            m.vz += fz * accel;
            const bool wasGround = m.onGround;
            moveBody(level, m, m.vx, m.vy, m.vz, true);
            if (m.onGround && !wasGround) {  // landed: the fall is paid for
                const double fall = m.fallDistance;
                if (fall > 3.0 && (sp.traits & kMobNoFallDamage) == 0 && !m.dead()) {
                    m.invulnerable = 0;
                    hurt(m.id, static_cast<float>(std::ceil(fall - 3.0)), m.x, m.z, 0.0f, false);
                }
            }
            if (m.onGround) m.fallDistance = 0.0;
            m.vy = (m.vy - 0.08) * 0.98;
            if ((sp.traits & kMobSlowFall) != 0 && m.vy < -0.1) m.vy = -0.1;  // a chicken flaps
            m.vx *= friction;
            m.vz *= friction;
        }
        // Stuck on the way (a wall the path did not know): think again.
        if (m.moveSpeed > 0.0f && !m.path.empty() && m.age % 20 == 0) {
            const double moved = std::abs(m.x - m.stuckX) + std::abs(m.z - m.stuckZ);
            if (moved < 0.2) {
                m.path.clear();
                m.repathTimer = 0;
            }
            m.stuckX = m.x;
            m.stuckZ = m.z;
        }
    }

    // ---- looks ----
    // The body turns to face where it is going; the head's turn pulls the body round when it is beyond 75 degrees (kept by lookAt).
    const double travelled = std::sqrt((m.x - m.xo) * (m.x - m.xo) + (m.z - m.zo) * (m.z - m.zo));
    m.limbSpeedO = m.limbSpeed;
    m.limbPosO = m.limbPos;
    m.limbSpeed += (std::min(static_cast<float>(travelled) * 4.0f, 1.0f) - m.limbSpeed) * 0.4f;
    m.limbPos += m.limbSpeed;
    // Footsteps: every 1.7 blocks or so (Entity.walkDist and nextStep).
    if (!m.dead() && m.onGround && !m.inWater && travelled > 0.0) {
        m.stepDist += travelled;
        if (m.stepDist > 1.7) {
            m.stepDist = 0.0;
            if (host_.sound) host_.sound(std::string("entity.") + sp.id + ".step", m.x, m.y, m.z, 0.15f, 1.0f, sp.kind != MobKind::Passive);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Shots

void Mobs::shoot(Projectile::Kind kind, double x, double y, double z, double vx, double vy, double vz, float damage, int ownerId) {
    Projectile p;
    p.kind = kind;
    p.id = nextId_++;
    p.x = p.xo = x;
    p.y = p.yo = y;
    p.z = p.zo = z;
    p.vx = vx;
    p.vy = vy;
    p.vz = vz;
    p.damage = damage;
    p.ownerId = ownerId;
    shots_.push_back(p);
}

void Mobs::tickShots(const MobTick& in) {
    const Level& level = *in.level;
    for (Projectile& p : shots_) {
        const int pcx = chunkOf(fl(p.x));
        const int pcz = chunkOf(fl(p.z));
        if (in.isChunkRendered && !in.isChunkRendered(pcx, pcz)) {
            p.age = 10000;
            continue;
        }
        p.xo = p.x;
        p.yo = p.y;
        p.zo = p.z;
        ++p.age;
        if (p.kind == Projectile::Kind::Arrow && p.stuck > 0) {
            ++p.stuck;
            continue;
        }
        // Two half steps, so a fast arrow does not jump through a wall.
        bool hitBlock = false;
        for (int step = 0; step < 2 && !hitBlock; ++step) {
            p.x += p.vx / 2.0;
            p.y += p.vy / 2.0;
            p.z += p.vz / 2.0;
            if (mobBlocks(level.block(fl(p.x), fl(p.y), fl(p.z)))) {
                hitBlock = true;
                p.x -= p.vx / 2.0;
                p.y -= p.vy / 2.0;
                p.z -= p.vz / 2.0;
            }
        }
        // The player: the arrow's point within the box (a little larger than it).
        if (in.playerTargetable) {
            const double hw = static_cast<double>(in.playerWidth) / 2.0 + 0.2;
            if (std::abs(p.x - in.player[0]) < hw && std::abs(p.z - in.player[2]) < hw && p.y > in.player[1] - 0.2 && p.y < in.player[1] + static_cast<double>(in.playerHeight) + 0.2) {
                if (p.kind == Projectile::Kind::Arrow) {
                    const double speed = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
                    const float amount = static_cast<float>(std::ceil(speed * static_cast<double>(p.damage)));
                    if (host_.hurtPlayer) host_.hurtPlayer(scaleForDifficulty(amount, in.difficulty), p.x - p.vx, p.z - p.vz, 0.1f);
                    p.age = 100000;  // (gone)
                    if (host_.sound) host_.sound("entity.arrow.hit_player", in.player[0], in.player[1], in.player[2], 1.0f, 1.0f, true);
                } else if (p.kind == Projectile::Kind::SmallFireball) {
                    if (host_.hurtPlayer) host_.hurtPlayer(scaleForDifficulty(p.damage, in.difficulty), p.x - p.vx, p.z - p.vz, 0.2f);
                    if (host_.burnPlayer) host_.burnPlayer(100);
                    p.age = 100000;
                } else {
                    hitBlock = true;  // a ghast's fireball goes off on the player
                }
                continue;
            }
        }
        if (hitBlock) {
            if (p.kind == Projectile::Kind::Arrow) {
                p.stuck = 1;
                p.vx = p.vy = p.vz = 0.0;
                if (host_.sound) host_.sound("entity.arrow.hit", p.x, p.y, p.z, 1.0f, 1.2f / (random() * 0.2f + 0.9f), true);
            } else if (p.kind == Projectile::Kind::LargeFireball) {
                if (host_.explode) host_.explode(p.x, p.y, p.z, 1.0f);
                p.age = 100000;
            } else {
                p.age = 100000;  // (a small fireball sets fire to what it hits: no fire is lit yet)
            }
            continue;
        }
        if (p.kind == Projectile::Kind::Arrow) {
            const bool wet = blockRegistry().state(level.block(fl(p.x), fl(p.y), fl(p.z))).has(kStateWater);
            const double drag = wet ? 0.6 : 0.99;
            p.vx *= drag;
            p.vy = p.vy * drag - 0.05;
            p.vz *= drag;
        }
    }
    shots_.erase(std::remove_if(shots_.begin(), shots_.end(),
                                [](const Projectile& p) { return p.age > 600 || (p.kind == Projectile::Kind::Arrow && p.stuck > 1200); }),
                 shots_.end());
}

// ---------------------------------------------------------------------------------------------------------------

void Mobs::tick(const MobTick& in) {
    if (in.level == nullptr) return;
    ++tickCount_;
    pathBudget_ = 3;
    for (Mob& m : mobs_) {
        const int mcx = chunkOf(fl(m.x));
        const int mcz = chunkOf(fl(m.z));
        if (in.isChunkRendered && !in.isChunkRendered(mcx, mcz)) continue;
        if (!in.level->isLoaded(fl(m.x), fl(m.z))) continue;  // (frozen where nothing is loaded)
        const double dx = m.x - in.player[0], dz = m.z - in.player[2];
        const double maxRenderDist = in.renderDistance > 0 ? static_cast<double>(in.renderDistance) * 16.0 : 128.0;
        if (dx * dx + dz * dz > maxRenderDist * maxRenderDist && m.species->kind != MobKind::Passive) continue;
        tickMob(m, in);
    }
    tickShots(in);
    despawn(in);
    mobs_.erase(std::remove_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.dead() && m.deathTime >= ((m.species->traits & kMobDragon) != 0 ? 200 : 20); }), mobs_.end());
    if (in.difficulty == 0) {  // peaceful: the hostile ones are gone
        mobs_.erase(std::remove_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.species->kind == MobKind::Hostile; }), mobs_.end());
    }
    naturalSpawning(in);
    populateChunks(in);
}

// ---------------------------------------------------------------------------------------------------------------
// Saving

NbtTag Mobs::toNbt() const {
    NbtTag root = NbtTag::compound();
    std::vector<NbtTag> list;
    const auto dbl = [](double v) {
        NbtTag t;
        t.type = NbtTag::Type::Double;
        t.real = v;
        return t;
    };
    const auto flt = [](double v) {
        NbtTag t;
        t.type = NbtTag::Type::Float;
        t.real = v;
        return t;
    };
    for (const Mob& m : mobs_) {
        if (m.dead()) continue;
        NbtTag t = NbtTag::compound();
        t.put("id", NbtTag::ofString(m.species->id));
        t.put("x", dbl(m.x)).put("y", dbl(m.y)).put("z", dbl(m.z));
        t.put("yaw", flt(static_cast<double>(m.yaw)));
        t.put("health", flt(static_cast<double>(m.health)));
        t.put("persistent", NbtTag::ofBool(m.persistent));
        t.put("burn", NbtTag::ofInteger(NbtTag::Type::Int, m.burn));
        t.put("angry", NbtTag::ofInteger(NbtTag::Type::Int, m.angry));
        list.push_back(std::move(t));
    }
    if (!list.empty()) root.put("mobs", NbtTag::ofList(std::move(list)));
    NbtTag chunks;
    chunks.type = NbtTag::Type::IntArray;
    for (const auto& c : populated_) {
        chunks.numbers.push_back(c.first);
        chunks.numbers.push_back(c.second);
    }
    root.put("populated", std::move(chunks));
    return root;
}

void Mobs::fromNbt(const NbtTag& tag) {
    mobs_.clear();
    populated_.clear();
    if (const NbtTag* list = tag.get("mobs"); list != nullptr) {
        for (const NbtTag& t : list->items) {
            Mob* m = spawn(t.getString("id"), t.getDouble("x"), t.getDouble("y"), t.getDouble("z"), t.getBool("persistent"));
            if (m == nullptr) continue;
            m->yaw = m->yawo = static_cast<float>(t.getDouble("yaw"));
            m->health = std::min(static_cast<float>(t.getDouble("health", static_cast<double>(m->health))), m->species->health);
            m->burn = static_cast<int>(t.getInt("burn"));
            m->angry = static_cast<int>(t.getInt("angry"));
        }
    }
    if (const NbtTag* chunks = tag.get("populated"); chunks != nullptr) {
        for (std::size_t i = 0; i + 1 < chunks->numbers.size(); i += 2) populated_.emplace(static_cast<int>(chunks->numbers[i]), static_cast<int>(chunks->numbers[i + 1]));
    }
}

}  // namespace falcon
