// Rules of the world on the game's own data: how long blocks take to dig and what they drop, blocks that fall, burn and explode, redstone, and the command tree.
// (Needs the game's exported data; without it the tests say so and pass.)
#include "falcon/assets/asset_locator.hpp"
#include "falcon/game/command_tree.hpp"
#include "falcon/game/mining.hpp"
#include "falcon/world/block_behaviours.hpp"
#include "falcon/world/block_physics.hpp"
#include "falcon/world/block_registry.hpp"
#include "falcon/world/block_tags.hpp"
#include "falcon/world/item_registry.hpp"
#include "falcon/world/loot.hpp"
#include "falcon/world/redstone.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {
int g_failures = 0;
}

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

using namespace falcon;

// The game's command tree: what parses, what is suggested, what the usage says.
void test_commands_real(const std::filesystem::path& dataDir, const BlockRegistry& blocks) {
    std::string error;
    const auto tree = CommandTree::loadFromGameData(dataDir, error);
    CHECK(tree != nullptr);
    if (!tree) {
        std::printf("  commands failed: %s\n", error.c_str());
        return;
    }
    std::vector<std::string> itemNames, blockNames;
    for (const ItemType& t : itemRegistry().items()) itemNames.push_back("minecraft:" + t.name);
    for (const BlockType& t : blocks.blocks()) blockNames.push_back("minecraft:" + t.name);
    CommandContext cheats;
    cheats.playerName = "Steve";
    cheats.cheats = true;
    cheats.names = [&](const std::string& registry) -> const std::vector<std::string>& {
        static const std::vector<std::string> none;
        return registry == "minecraft:item" ? itemNames : registry == "minecraft:block" ? blockNames : none;
    };
    CommandContext plain = cheats;
    plain.cheats = false;
    const auto parses = [&](const char* text) { return parseCommand(*tree, cheats, text).complete; };
    CHECK(parses("give @s diamond 2") && parses("give Steve minecraft:diamond_sword") && parses("gamemode creative") && parses("kill") && parses("seed"));
    CHECK(parses("tp @s ~ ~ ~") && parses("tp ~ 70 ~") && parses("teleport @s @a") && parses("setblock ~ ~-1 ~ oak_stairs[facing=east]"));
    CHECK(parses("fill 0 0 0 1 1 1 stone replace dirt") && parses("fill ~ ~ ~ ~5 ~5 ~5 glass hollow") && parses("execute as @a run say hi") && parses("gamerule random_tick_speed 0"));
    CHECK(parses("summon tnt ~ ~ ~ {fuse:20}") && parses("xp add @s 5 levels") && parses("say hello  there"));
    CHECK(!parses("give @s") && !parses("give") && !parses("gamemode") && !parses("seed ") && !parses("nonsense 1 2"));
    const CommandParse badMode = parseCommand(*tree, cheats, "gamemode foo");
    CHECK(!badMode.complete && badMode.error == "Invalid game mode 'foo'" && badMode.errorAt == 9);
    const CommandParse badItem = parseCommand(*tree, cheats, "give @s notanitem");
    CHECK(!badItem.complete && badItem.error == "Unknown item 'minecraft:notanitem'");
    const CommandParse badCount = parseCommand(*tree, cheats, "give @s stone abc");
    CHECK(!badCount.complete && badCount.error == "Expected integer");
    CHECK(parseCommand(*tree, cheats, "nonsense").error == "command.unknown.command");
    CHECK(parseCommand(*tree, cheats, "give @s").error == "command.unknown.command" && parseCommand(*tree, cheats, "give @s").errorAt == 7);
    // Without cheats only the commands anyone may use are there.
    CHECK(!parseCommand(*tree, plain, "give @s stone").complete && parseCommand(*tree, plain, "help").complete);
    // The parts of a command, for the colours and for running it.
    const CommandParse give = parseCommand(*tree, cheats, "give @s stone 5");
    CHECK(give.parts.size() == 4 && give.parts[0].node->name == "give" && !give.parts[0].node->isArgument() && give.parts[1].node->name == "targets" && give.parts[3].node->name == "count");
    CHECK(give.parts[3].start == 14 && give.parts[3].end == 15);
    // Suggestions: the words of a node, the values an argument knows, the rest of a name.
    const auto has = [](const CommandSuggestions& s, const char* what) { return std::find(s.list.begin(), s.list.end(), what) != s.list.end(); };
    const CommandSuggestions root = suggestCommands(*tree, cheats, "");
    CHECK(has(root, "give") && has(root, "gamemode") && has(root, "tp") && has(root, "help") && root.start == 0);
    CHECK(!has(suggestCommands(*tree, plain, ""), "give") && has(suggestCommands(*tree, plain, ""), "help"));
    CHECK(suggestCommands(*tree, cheats, "gamem").list == std::vector<std::string>{"gamemode"} && suggestCommands(*tree, cheats, "gamem").start == 0);
    const CommandSuggestions modes = suggestCommands(*tree, cheats, "gamemode c");
    CHECK(modes.list == std::vector<std::string>{"creative"} && modes.start == 9);
    const CommandSuggestions diamonds = suggestCommands(*tree, cheats, "give @s dia");
    CHECK(has(diamonds, "minecraft:diamond") && has(diamonds, "minecraft:diamond_pickaxe") && !has(diamonds, "minecraft:stone") && diamonds.start == 8);
    CHECK(has(suggestCommands(*tree, cheats, "give @s pick"), "minecraft:iron_pickaxe"));  // (the start of any word of the name)
    CHECK(has(suggestCommands(*tree, cheats, "setblock ~ ~ "), "~") && has(suggestCommands(*tree, cheats, "setblock ~ ~ ~ gla"), "minecraft:glass"));
    const CommandSuggestions tp = suggestCommands(*tree, cheats, "tp @s ");
    CHECK(has(tp, "@a") && has(tp, "~") && has(tp, "Steve"));  // an entity to go to, or a position
    CHECK(has(suggestCommands(*tree, cheats, "fill ~ ~ ~ ~ ~ ~ stone "), "replace") && has(suggestCommands(*tree, cheats, "execute "), "as"));
    CHECK(has(suggestCommands(*tree, cheats, "gamerule random"), "random_tick_speed") && has(suggestCommands(*tree, cheats, "gamerule keep_inventory "), "true"));
    // Usage: what could come next.
    const auto usage = commandUsage(*tree, cheats, "give @s diamond ");
    CHECK(usage.size() == 1 && usage[0] == "<count>");
    CHECK(commandUsage(*tree, cheats, "give ")[0] == "<item> [<count>]" || commandUsage(*tree, cheats, "give @s ")[0] == "<item> [<count>]");
    const CommandNode* giveNode = tree->resolve({"give"});
    CHECK(giveNode != nullptr && commandUsageLine(*tree, *giveNode, true) == "/give <targets> <item> [<count>]");
}

// Blocks that fall, burn and explode, with the game's own tables: which blocks do what, and a small world in which they do it.
void test_block_physics_real(const std::filesystem::path& dataDir, const BlockRegistry& blocks) {
    std::string error;
    const auto behaviours = BlockBehaviours::loadFromGameData(dataDir, blocks, error);
    CHECK(behaviours != nullptr);
    if (!behaviours) {
        std::printf("  block behaviours failed: %s\n", error.c_str());
        return;
    }
    const auto index = [&](const char* name) { return blocks.findBlock(name).value_or(-1); };
    CHECK(behaviours->isFallable(index("sand")) && behaviours->isFallable(index("gravel")) && behaviours->isFallable(index("anvil")) &&
          behaviours->isFallable(index("white_concrete_powder")) && !behaviours->isFallable(index("stone")));
    CHECK(behaviours->isAnvil(index("chipped_anvil")) && behaviours->isTnt(index("tnt")) && behaviours->isFire(index("soul_fire")) && behaviours->isSpreadingFire(index("fire")));
    CHECK(!behaviours->isSpreadingFire(index("soul_fire")) && behaviours->concreteOf(index("white_concrete_powder")) == index("white_concrete"));
    CHECK(behaviours->igniteOdds(index("oak_planks")) == 5 && behaviours->burnOdds(index("oak_planks")) == 20);
    CHECK(behaviours->igniteOdds(index("tnt")) == 15 && behaviours->burnOdds(index("tnt")) == 100 && behaviours->burnOdds(index("oak_leaves")) == 60);
    CHECK(behaviours->igniteOdds(index("stone")) == 0);

    // A small world: one chunk, a stone floor at y = 60.
    Level level;
    level.putChunk({0, 0}, std::make_shared<Chunk>());
    const Block stone = blocks.find("stone").value(), air = Block::Air;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) level.setBlock(x, 60, z, stone);
    }
    ItemEntities items;
    BlockPhysics* self = nullptr;
    BlockPhysics::Host host;
    int sounds = 0;
    host.setBlock = [&](int x, int y, int z, Block b) {  // (what GameSession::setBlock does: the block, then its neighbours are told)
        if (!level.setBlock(x, y, z, b)) return false;
        self->blockPlaced(x, y, z);
        static constexpr int kDir[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        const BlockPhysics::Reader at = [&](int bx, int by, int bz) { return level.block(bx, by, bz); };
        for (const auto& d : kDir) {
            const Block n = level.block(x + d[0], y + d[1], z + d[2]);
            const Block now = self->afterNeighborChange(n, x + d[0], y + d[1], z + d[2], at);
            if (now != n) level.setBlock(x + d[0], y + d[1], z + d[2], now);
        }
        return true;
    };
    host.sound = [&](const std::string&, float, float, double, double, double) { ++sounds; };
    host.player = [] { return PhysicsPlayer{Aabb{8, 70, 8, 8.6, 71.8, 8.6}, 71.6, false}; };  // (near the fires: the game rule fire_spread_radius_around_player is 128)
    BlockPhysics physics(level, behaviours, items, host, 1234);
    self = &physics;

    // Sand over a hole falls and lands on the floor; the block above it follows; gravel on the floor stays.
    const Block sand = blocks.find("sand").value(), gravel = blocks.find("gravel").value();
    host.setBlock(4, 70, 4, sand);
    host.setBlock(4, 71, 4, gravel);
    host.setBlock(8, 61, 8, gravel);
    for (int i = 0; i < 120; ++i) physics.tick();
    CHECK(level.block(4, 61, 4) == sand && level.block(4, 62, 4) == gravel);  // the lower one lands first, the one above follows it down
    CHECK(level.block(4, 70, 4) == air && level.block(4, 71, 4) == air && level.block(8, 61, 8) == gravel);
    CHECK(physics.fallingBlocks().empty());

    // Concrete powder that lands in water sets.
    const Block water = blocks.find("water").value(), powder = blocks.find("white_concrete_powder").value();
    host.setBlock(10, 61, 10, water);
    host.setBlock(10, 66, 10, powder);
    for (int i = 0; i < 80; ++i) physics.tick();
    CHECK(level.block(10, 61, 10) == blocks.find("white_concrete").value() || level.block(10, 62, 10) == blocks.find("white_concrete").value());

    // TNT: a block of dirt round the middle is gone (stone would swallow the rays), and the TNT beside it is set off.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 61; y <= 70; ++y) level.setBlock(x, y, z, air);
        }
    }
    const Block dirt = blocks.find("dirt").value();
    for (int x = 5; x <= 11; ++x) {
        for (int z = 5; z <= 11; ++z) {
            for (int y = 61; y <= 66; ++y) level.setBlock(x, y, z, dirt);
        }
    }
    const Block tnt = blocks.find("tnt").value();
    level.setBlock(13, 61, 8, tnt);
    physics.explode(8.5, 63.5, 8.5, 4.0f);
    CHECK(level.block(8, 63, 8) == air && level.block(8, 64, 8) == air);   // the middle
    CHECK(level.block(8, 60, 8) == stone);  // the floor under it holds (stone: resistance 6, the rays run out)
    int dirtLeft = 0;
    for (int x = 5; x <= 11; ++x) {
        for (int z = 5; z <= 11; ++z) {
            for (int y = 61; y <= 66; ++y) dirtLeft += level.block(x, y, z) == dirt ? 1 : 0;
        }
    }
    CHECK(dirtLeft > 0 && dirtLeft < 7 * 7 * 6 - 60);  // a crater, not the whole lot
    CHECK(sounds > 0);
    // The TNT block was reached by the blast only if the rays got that far; a second explosion right beside it surely does.
    physics.explode(12.5, 61.5, 8.5, 4.0f);
    CHECK(level.block(13, 61, 8) == air && physics.primedTnt().size() == 1 && physics.primedTnt()[0].fuse >= 10 && physics.primedTnt()[0].fuse < 30);
    for (int i = 0; i < 40; ++i) physics.tick();
    CHECK(physics.primedTnt().empty());  // it went off

    // Fire: a flame next to a plank burns on for a while and, with leaves round it, they go (statistically); it cannot stand on nothing.
    const Block planks = blocks.find("oak_planks").value();
    level.setBlock(2, 61, 2, planks);
    level.setBlock(2, 62, 2, planks);
    CHECK(physics.canPlaceFireAt(3, 61, 2) && physics.canPlaceFireAt(2, 63, 2) && !physics.canPlaceFireAt(2, 62, 2) && !physics.canPlaceFireAt(14, 80, 14));
    // On a floor the flames stand on it; hanging off the side of a plank in the air they show on that side.
    CHECK(blocks.get(physics.fireStateAt(3, 61, 2), "west") == "false");
    level.setBlock(2, 70, 2, planks);
    const Block sideFire = physics.fireStateAt(3, 70, 2);
    CHECK(physics.canPlaceFireAt(3, 70, 2) && blocks.type(sideFire).name == "fire" && blocks.get(sideFire, "west") == "true" && blocks.get(sideFire, "up") == "false");
    level.setBlock(2, 70, 2, air);
    for (int attempt = 0; attempt < 12; ++attempt) {  // (a fire lives about a minute and burns what is next to it by chance: light it again until the wood goes)
        if (blocks.isAir(level.block(3, 61, 2))) host.setBlock(3, 61, 2, physics.fireStateAt(3, 61, 2));
        for (int i = 0; i < 400; ++i) physics.tick();
    }
    int planksLeft = (level.block(2, 61, 2) == planks ? 1 : 0) + (level.block(2, 62, 2) == planks ? 1 : 0);
    CHECK(planksLeft < 2);  // wood next to a fire for minutes does not survive

    // A fire with nothing to stand on or burn goes out when its floor goes.
    level.setBlock(12, 61, 12, stone);
    host.setBlock(12, 62, 12, physics.fireStateAt(12, 62, 12));
    CHECK(blocks.type(level.block(12, 62, 12)).name == "fire");
    host.setBlock(12, 61, 12, air);
    const BlockPhysics::Reader at = [&](int x, int y, int z) { return level.block(x, y, z); };
    CHECK(physics.afterNeighborChange(level.block(12, 62, 12), 12, 62, 12, at) == air);
}

// How long blocks take to dig and what they drop, from the game's tags and loot tables.
void test_mining_real(const std::filesystem::path& dataDir, const BlockRegistry& blocks, const ItemRegistry& items) {
    const BlockTags tags(dataDir / "data" / "minecraft" / "tags" / "block", blocks);
    const MiningRules mining(blocks, tags);
    const BlockLoot loot(dataDir, blocks);
    const auto b = [&](const char* text) {
        const auto s = blocks.parse(text);
        CHECK(s.has_value());
        return s.value_or(Block::Air);
    };
    const auto stack = [&](const char* name) {
        const auto item = items.find(name);
        CHECK(item.has_value());
        return item ? ItemStack(*item, 1) : ItemStack();
    };
    const ItemStack hand, wooden = stack("wooden_pickaxe"), iron = stack("iron_pickaxe"), diamond = stack("diamond_pickaxe"), shears = stack("shears");
    const auto near = [](float a, float c) { return std::fabs(a - c) < 1.0e-5f * std::max(1.0f, std::fabs(c)); };
    // Tags follow other tags: #minecraft:logs holds oak logs through #minecraft:oak_logs.
    CHECK(tags.contains("#minecraft:logs", static_cast<std::size_t>(*blocks.findBlock("oak_log"))));
    CHECK(tags.contains("minecraft:mineable/pickaxe", static_cast<std::size_t>(*blocks.findBlock("stone"))));
    CHECK(!tags.contains("#minecraft:mineable/pickaxe", static_cast<std::size_t>(*blocks.findBlock("dirt"))));
    // Dirt (hardness 0.5) by hand: no tool is needed, and a shovel is faster. Stone (1.5): the pickaxe is the right tool, by hand it is 100 not 30.
    const Block dirt = b("dirt"), stone = b("stone");
    CHECK(mining.correctForDrops(hand, dirt) && near(mining.progressPerTick(hand, dirt, true, false), 1.0f / 0.5f / 30.0f));
    CHECK(!mining.correctForDrops(hand, stone) && near(mining.progressPerTick(hand, stone, true, false), 1.0f / 1.5f / 100.0f));
    CHECK(mining.correctForDrops(wooden, stone) && near(mining.progressPerTick(wooden, stone, true, false), 2.0f / 1.5f / 30.0f));
    CHECK(near(mining.progressPerTick(iron, stone, true, false), 6.0f / 1.5f / 30.0f));
    CHECK(near(mining.progressPerTick(wooden, stone, false, false), 2.0f / 1.5f / 30.0f / 5.0f));  // in the air: a fifth
    // Iron ore needs a stone pickaxe; a wooden one digs it at its speed but gets no drops. Obsidian wants diamond. Bedrock never breaks; a flower is instant.
    CHECK(!mining.correctForDrops(wooden, b("iron_ore")) && mining.correctForDrops(stack("stone_pickaxe"), b("iron_ore")));
    CHECK(!mining.correctForDrops(iron, b("obsidian")) && mining.correctForDrops(diamond, b("obsidian")));
    CHECK(mining.progressPerTick(diamond, b("bedrock"), true, false) == 0.0f);
    CHECK(mining.progressPerTick(hand, b("poppy"), true, false) >= 1.0f);
    CHECK(destroyStage(0.0f) == -1 && destroyStage(0.05f) == 0 && destroyStage(0.55f) == 5 && destroyStage(0.99f) == 9);
    // Loot: stone gives cobblestone, grass block gives dirt, a log gives itself, leaves give nothing by hand and themselves with shears.
    std::mt19937 random(7);
    const auto only = [&](const std::vector<ItemStack>& drops, const char* name, int count) {
        return drops.size() == 1 && drops[0].item == *items.find(name) && drops[0].count == count;
    };
    CHECK(only(loot.drops(stone, wooden, random), "cobblestone", 1));
    CHECK(only(loot.drops(b("grass_block[snowy=false]"), hand, random), "dirt", 1));
    CHECK(only(loot.drops(b("oak_log[axis=y]"), hand, random), "oak_log", 1));
    CHECK(only(loot.drops(b("oak_leaves[distance=1,persistent=true,waterlogged=false]"), shears, random), "oak_leaves", 1));
    int leafDrops = 0;
    for (int i = 0; i < 400; ++i) leafDrops += loot.drops(b("oak_leaves[distance=1,persistent=true,waterlogged=false]"), hand, random).empty() ? 0 : 1;
    CHECK(leafDrops > 0 && leafDrops < 100);  // a sapling, sticks or an apple now and then (a few in a hundred)
    CHECK(loot.drops(b("bedrock"), diamond, random).empty());
    // A slab: two slabs when it is double.
    CHECK(only(loot.drops(b("oak_slab[type=double,waterlogged=false]"), hand, random), "oak_slab", 2));
    CHECK(only(loot.drops(b("oak_slab[type=bottom,waterlogged=false]"), hand, random), "oak_slab", 1));
}

// Redstone: a lever through dust and a repeater to a lamp, a button's pulse, a door, TNT, a pressure plate and an observer.
void test_redstone_real(const BlockRegistry& blocks) {
    Level level;
    level.putChunk({0, 0}, std::make_shared<Chunk>());
    const Block stone = blocks.find("stone").value(), air = Block::Air;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) level.setBlock(x, 60, z, stone);
    }
    const auto b = [&](const char* text) {
        const auto s = blocks.parse(text);
        CHECK(s.has_value());
        return s.value_or(Block::Air);
    };
    const auto get = [&](int x, int y, int z, const char* property) { return std::string(blocks.get(level.block(x, y, z), property)); };
    Redstone* self = nullptr;
    bool plateInside = false;
    int primed = 0;
    Redstone::Host host;
    host.setBlock = [&](int x, int y, int z, Block block) {
        if (!level.setBlock(x, y, z, block)) return false;
        self->blockChanged(x, y, z);
        return true;
    };
    host.primeTnt = [&](int, int, int) { ++primed; };
    host.entityInside = [&](const Aabb&, bool) { return plateInside; };
    Redstone redstone(level, host, 5);
    self = &redstone;
    const auto put = [&](int x, int y, int z, const char* text) { host.setBlock(x, y, z, b(text)); };

    // A lever on the floor, four pieces of dust, a repeater (its input is on the west, where the dust is) and a lamp on its output side.
    put(8, 61, 2, "redstone_lamp");
    put(7, 61, 2, "repeater[facing=west,delay=1]");
    for (int x = 3; x <= 6; ++x) put(x, 61, 2, "redstone_wire");
    CHECK(get(3, 61, 2, "power") == "0" && get(8, 61, 2, "lit") == "false");
    put(2, 61, 2, "lever[face=floor,facing=east,powered=false]");
    CHECK(redstone.use(2, 61, 2) && get(2, 61, 2, "powered") == "true");  // a click on the lever
    CHECK(get(3, 61, 2, "power") == "15" && get(4, 61, 2, "power") == "14" && get(5, 61, 2, "power") == "13" && get(6, 61, 2, "power") == "12");  // the dust loses one a block
    CHECK(get(7, 61, 2, "powered") == "false");  // the repeater has not answered yet
    for (int i = 0; i < 3; ++i) redstone.tick();
    CHECK(get(7, 61, 2, "powered") == "true" && get(8, 61, 2, "lit") == "true");  // two ticks later it has, and the lamp is on
    CHECK(redstone.use(2, 61, 2) && get(2, 61, 2, "powered") == "false");
    for (int i = 0; i < 12; ++i) redstone.tick();
    CHECK(get(3, 61, 2, "power") == "0" && get(6, 61, 2, "power") == "0" && get(7, 61, 2, "powered") == "false" && get(8, 61, 2, "lit") == "false");

    // A button gives a pulse: 20 ticks for stone.
    put(3, 61, 8, "redstone_lamp");
    put(2, 61, 8, "stone_button[face=floor,facing=north,powered=false]");
    CHECK(redstone.use(2, 61, 8) && get(2, 61, 8, "powered") == "true" && get(3, 61, 8, "lit") == "true");
    for (int i = 0; i < 19; ++i) redstone.tick();
    CHECK(get(2, 61, 8, "powered") == "true");
    for (int i = 0; i < 8; ++i) redstone.tick();
    CHECK(get(2, 61, 8, "powered") == "false" && get(3, 61, 8, "lit") == "false");

    // A door opens with power, both halves, and shuts when it goes.
    put(12, 61, 2, "oak_door[facing=east,half=lower,hinge=left,open=false,powered=false]");
    put(12, 62, 2, "oak_door[facing=east,half=upper,hinge=left,open=false,powered=false]");
    put(12, 61, 3, "redstone_block");
    CHECK(get(12, 61, 2, "open") == "true" && get(12, 62, 2, "open") == "true" && get(12, 61, 2, "powered") == "true");
    host.setBlock(12, 61, 3, air);
    CHECK(get(12, 61, 2, "open") == "false" && get(12, 62, 2, "open") == "false");
    CHECK(redstone.use(12, 61, 2) && get(12, 61, 2, "open") == "true" && get(12, 62, 2, "open") == "true");  // by hand
    put(12, 61, 5, "iron_door[facing=east,half=lower,hinge=left,open=false,powered=false]");
    CHECK(!redstone.use(12, 61, 5));  // an iron door does not answer to a hand

    // TNT next to a block of redstone is lit.
    put(14, 61, 2, "tnt");
    put(14, 61, 3, "redstone_block");
    CHECK(primed == 1 && level.block(14, 61, 2) == air);

    // A pressure plate under a lamp's neighbour: pressed while something stands on it.
    put(4, 61, 11, "redstone_lamp");
    put(5, 61, 11, "oak_pressure_plate[powered=false]");
    plateInside = true;
    redstone.tick();
    CHECK(get(5, 61, 11, "powered") == "true" && get(4, 61, 11, "lit") == "true");
    plateInside = false;
    for (int i = 0; i < 6; ++i) redstone.tick();
    CHECK(get(5, 61, 11, "powered") == "false" && get(4, 61, 11, "lit") == "false");

    // An observer gives a two tick pulse when what it looks at changes.
    put(9, 61, 12, "observer[facing=north,powered=false]");
    CHECK(get(9, 61, 12, "powered") == "false");
    put(9, 61, 11, "stone");
    for (int i = 0; i < 3; ++i) redstone.tick();
    CHECK(get(9, 61, 12, "powered") == "true");
    for (int i = 0; i < 3; ++i) redstone.tick();
    CHECK(get(9, 61, 12, "powered") == "false");

    // A torch is lit, and goes out when the block it stands on is powered (an inverter).
    put(1, 61, 14, "redstone_block");
    put(1, 62, 14, "redstone_torch[lit=true]");
    for (int i = 0; i < 4; ++i) redstone.tick();
    CHECK(get(1, 62, 14, "lit") == "false");  // a block of redstone gives its signal to the torch on it, which goes out
    put(3, 61, 14, "stone");
    put(3, 62, 14, "redstone_torch[lit=true]");
    put(4, 61, 14, "lever[face=wall,facing=east,powered=false]");  // on the east side of the stone block (facing away from it)
    CHECK(redstone.use(4, 61, 14));
    for (int i = 0; i < 4; ++i) redstone.tick();
    CHECK(get(3, 62, 14, "lit") == "false");  // the lever powers the stone block strongly, so the torch on top goes out
}


int main() {
    const auto assets = locateAssets(std::nullopt);
    if (!assets.location) {
        std::printf("world rule tests skipped: no game data found\n");
        return 0;
    }
    const std::filesystem::path dataDir = assets.location->root.parent_path();
    std::string error;
    const std::filesystem::path cache = std::filesystem::temp_directory_path() / "falcon_world_tests_cache";
    const auto blocks = BlockRegistry::loadFromGameData(dataDir, cache, error);
    if (!blocks) {
        std::printf("world rule tests skipped: %s\n", error.c_str());
        return 0;
    }
    const auto items = ItemRegistry::loadFromGameData(dataDir, cache, blocks.get(), error);
    if (!items) {
        std::printf("world rule tests skipped: %s\n", error.c_str());
        return 0;
    }
    installBlockRegistry(blocks);
    installItemRegistry(items);
    test_mining_real(dataDir, *blocks, *items);
    test_block_physics_real(dataDir, *blocks);
    test_commands_real(dataDir, *blocks);
    test_redstone_real(*blocks);
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All world rule tests passed\n";
    return 0;
}
