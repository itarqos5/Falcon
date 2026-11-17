#pragma once

// The mobs of a singleplayer world: pigs and zombies, endermen and creepers, their bodies (the game's LivingEntity physics), their wits (goals: wander, look, flee, chase, strike, shoot,
// swell, teleport), their coming and going (NaturalSpawner and the despawn rules) and what they leave behind. Falcon's mobs are its own simplification of the game's brains: a mob
// walks a path it finds with A* over the blocks, not the game's PathNavigation, and a goal is a few lines, not a goal selector with priorities; the numbers (health, speed,
// damage, ranges, cooldowns, drops) are the game's.
//
// Nothing here draws: the session reads `mobs()` and gives each one to MobRenderer. Nothing here knows about the session either: what a mob needs from the world it asks of `MobsHost`.

#include "falcon/assets/nbt.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/collision_shapes.hpp"
#include "falcon/world/item_stack.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace falcon {

enum class MobKind : std::uint8_t { Passive, Neutral, Hostile };

// What sets a species apart; the AI reads these.
enum MobTrait : std::uint32_t {
    kMobBurnsInDaylight = 1u << 0,    // zombie, skeleton: sunlight sets them on fire
    kMobCreeper = 1u << 1,            // swells when near the player and blows up
    kMobArcher = 1u << 2,             // shoots arrows from a distance
    kMobTeleports = 1u << 3,          // enderman
    kMobAngryWhenLookedAt = 1u << 4,  // enderman: a look at its head makes it hostile
    kMobFlies = 1u << 5,              // blaze, ghast: no walking, no gravity
    kMobHurtByWater = 1u << 6,        // enderman
    kMobFireImmune = 1u << 7,         // blaze, ghast, zombified piglin
    kMobNeutralInDaylight = 1u << 8,  // spider: only attacks when the light is dim, or once hit
    kMobLeaps = 1u << 9,              // spider: jumps at its target
    kMobClimbs = 1u << 10,            // spider: walls are no obstacle
    kMobFireballs = 1u << 11,         // blaze (small fireballs), ghast (a big one)
    kMobNoFallDamage = 1u << 12,      // chicken
    kMobLaysEggs = 1u << 13,
    kMobAngryWhenHurt = 1u << 14,     // zombified piglin: neutral until hit, then it and the others near it come for the player
    kMobSlowFall = 1u << 15,          // chicken: flaps its wings
    kMobDragon = 1u << 16,            // the Ender Dragon: circles the island, charges and spits fire, is healed by the crystals
    kMobCrystal = 1u << 17,           // an end crystal: does not move, goes off when it is hurt
};

struct MobDrop {
    const char* item = nullptr;  // null ends the list
    int min = 0, max = 0;
    float chance = 1.0f;
};

struct MobSpecies {
    const char* id;  // "zombie"
    MobKind kind;
    float health;
    float speed;   // the movement speed attribute
    float attack;  // melee damage before the difficulty scales it (0: none)
    float follow;  // how far it notices a player, in blocks
    std::uint32_t traits;
    int xp;
    std::array<MobDrop, 4> drops;
};

[[nodiscard]] const MobSpecies* mobSpecies(const std::string& id);
[[nodiscard]] const std::vector<MobSpecies>& allMobSpecies();

struct Mob {
    int id = 0;
    const MobSpecies* species = nullptr;
    float width = 0.6f, height = 1.8f;
    double x = 0.0, y = 0.0, z = 0.0;  // the middle of the feet
    double vx = 0.0, vy = 0.0, vz = 0.0;
    double xo = 0.0, yo = 0.0, zo = 0.0;
    float yaw = 0.0f, yawo = 0.0f;  // the body, degrees (0 faces south, +z)
    float headYaw = 0.0f;           // the head against the body
    float headPitch = 0.0f;
    float health = 20.0f;
    bool onGround = false, inWater = false, inLava = false, horizontalCollision = false;
    double fallDistance = 0.0;
    int age = 0;
    int hurtTime = 0, invulnerable = 0, deathTime = 0;
    float lastHurt = 0.0f;
    float limbSpeed = 0.0f, limbPos = 0.0f, limbSpeedO = 0.0f, limbPosO = 0.0f;  // the walk animation (LivingEntity.walkAnimation)
    bool persistent = false;  // never despawns (a /summoned mob, a mob that was named)

    // goals
    std::vector<std::array<int, 3>> path;
    std::size_t pathIndex = 0;
    std::array<int, 3> pathGoal{0, 0, 0};
    int repathTimer = 0;
    int stuckTimer = 0;
    double stuckX = 0.0, stuckZ = 0.0;
    float moveSpeed = 0.0f;  // the speed modifier of the goal that moves it (0: standing)
    bool wantsJump = false;
    int wanderTimer = 0;
    int panic = 0;
    int target = 0;       // 1: the player (the only thing a mob hunts)
    int angry = 0;        // ticks of anger left (neutral mobs)
    int unseen = 0;       // ticks the target has been out of sight
    int attackCooldown = 0;
    int shootCooldown = 0;
    int burn = 0;         // ticks it will burn
    int idle = 0;         // ticks without a player near (despawning)
    int ambientTimer = 0;
    int fuse = 0;         // creeper: ticks of swelling, 0..30
    int fuseDir = -1;     // +1 swelling, -1 calming
    int teleportTimer = 0;
    int eggTimer = 0;
    float flyAngle = 0.0f;
    double flyTargetX = 0.0, flyTargetY = 0.0, flyTargetZ = 0.0;
    int flyTimer = 0;
    int burst = 0;  // blaze: fireballs left in this volley
    double stepDist = 0.0;  // how far it has walked since its last footstep sound

    [[nodiscard]] bool dead() const { return health <= 0.0f; }
    [[nodiscard]] Aabb box() const {
        const double h = static_cast<double>(width) / 2.0;
        return {x - h, y, z - h, x + h, y + static_cast<double>(height), z + h};
    }
    [[nodiscard]] double eyeY() const { return y + static_cast<double>(height) * 0.85; }
};

// A shot: an arrow (from a skeleton) or a fireball (blaze: small, ghast: large).
struct Projectile {
    enum class Kind : std::uint8_t { Arrow, SmallFireball, LargeFireball };
    Kind kind = Kind::Arrow;
    int id = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    double xo = 0.0, yo = 0.0, zo = 0.0;
    int age = 0;
    int stuck = 0;  // arrow: ticks since it hit a block (0: in flight)
    float damage = 2.0f;
    int ownerId = 0;
};

// What the world gives and takes: the session sets it up once.
struct MobsHost {
    std::function<void(const std::string& event, double x, double y, double z, float volume, float pitch, bool hostile)> sound;
    std::function<void(float amount, double fromX, double fromZ, float knockback)> hurtPlayer;
    std::function<void(double x, double y, double z, float power)> explode;
    std::function<void(ItemStack stack, double x, double y, double z)> dropItem;
    std::function<void(int points)> addExperience;
    std::function<void(int ticks)> burnPlayer;
    std::function<int(int x, int z)> surfaceY;                                  // the highest block of a column that is not air
    std::function<void(int x, int y, int z, int& sky, int& block)> light;      // the light stored at a cell (the sky light as it is by day)
};

// What a tick knows about the world and the player.
struct MobTick {
    const Level* level = nullptr;
    std::array<double, 3> player{};  // the player's feet
    double playerEyeY = 0.0;
    float playerYaw = 0.0f, playerPitch = 0.0f;
    float playerWidth = 0.6f, playerHeight = 1.8f;
    bool playerTargetable = false;   // alive, in survival or adventure
    int difficulty = 2;              // 0 peaceful .. 3 hard
    long long dayTime = 0;
    float darkSteps = 0.0f;          // how many levels of sky light the night takes away
    int dimension = 0;               // 0 overworld, 1 the Nether, 2 the End
    bool spawnMobs = true, spawnMonsters = true, mobGriefing = true, daylightBurns = true;
    int renderDistance = 12;
    std::function<bool(int chunkX, int chunkZ)> isChunkVisible;
    std::function<bool(int chunkX, int chunkZ)> isChunkRendered;
};

class Mobs {
public:
    explicit Mobs(MobsHost host, std::uint64_t seed = 0);

    // Puts a mob in the world; null for a type that is not in the table.
    Mob* spawn(const std::string& type, double x, double y, double z, bool persistent = false);
    // One game tick for every mob and shot, then the coming and going of mobs.
    void tick(const MobTick& in);

    [[nodiscard]] std::vector<Mob>& mobs() { return mobs_; }
    [[nodiscard]] const std::vector<Mob>& mobs() const { return mobs_; }
    [[nodiscard]] const std::vector<Projectile>& projectiles() const { return shots_; }
    // The nearest live mob whose box the line meets within `maxDistance`; its id, or -1.
    [[nodiscard]] int pick(const std::array<double, 3>& eye, const std::array<double, 3>& look, double maxDistance, double* distance = nullptr) const;
    // The player hits a mob (or an explosion or a fall does): damage after armor, the knockback's strength, and where it came from. True when it counted.
    bool hurt(int id, float amount, double fromX, double fromZ, float knockback, bool byPlayer);
    [[nodiscard]] Mob* find(int id);
    [[nodiscard]] int count(MobKind kind) const;
    // /kill and /gamemode peaceful: removes the mobs the test selects, returning how many.
    int remove(const std::function<bool(const Mob&)>& which);
    void clear();
    // A shot from outside (the player's bow, a dispenser): not used yet.
    void shoot(Projectile::Kind kind, double x, double y, double z, double vx, double vy, double vz, float damage, int ownerId);

    // Saving: the mobs as they are and the chunks that already had their animals.
    [[nodiscard]] NbtTag toNbt() const;
    void fromNbt(const NbtTag& tag);

private:
    friend struct MobBrain;
    [[nodiscard]] float random() { return uniform_(rng_); }
    [[nodiscard]] int randomInt(int bound) { return bound <= 0 ? 0 : static_cast<int>(random() * static_cast<float>(bound)) % bound; }

    void tickMob(Mob& m, const MobTick& in);
    void tickShots(const MobTick& in);
    void naturalSpawning(const MobTick& in);
    void populateChunks(const MobTick& in);
    void despawn(const MobTick& in);
    void die(Mob& m);
    int spawnGroup(const std::string& id, int count, int x, int y, int z, const MobTick& in, bool monster);

    MobsHost host_;
    std::vector<Mob> mobs_;
    std::vector<Projectile> shots_;
    std::set<std::pair<int, int>> populated_;  // chunks whose animals have been placed
    std::mt19937_64 rng_;
    std::uniform_real_distribution<float> uniform_{0.0f, 1.0f};
    int nextId_ = 1;
    int pathBudget_ = 0;  // paths that may still be found this tick
    long long tickCount_ = 0;
};

// A path over the blocks (Falcon's own A*, not the game's PathNavigation): the cells a mob of this size can stand on from its feet to near `goal`, the nearest it could get if the goal is
// out of reach. Empty when it is already there or nothing is walkable. `maxNodes` bounds the search.
[[nodiscard]] std::vector<std::array<int, 3>> findMobPath(const Level& level, const std::array<int, 3>& from, const std::array<int, 3>& goal, float width, float height, int maxNodes,
                                                          bool canSwim);
// Whether a cell stops a walking mob (a full block) and whether it hurts one (lava, fire, a cactus ...).
[[nodiscard]] bool mobBlocks(Block b);
[[nodiscard]] bool mobHazard(Block b);

}  // namespace falcon
