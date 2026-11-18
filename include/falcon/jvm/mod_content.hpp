#pragma once

#include <string>
#include <vector>

namespace falcon::jvm {

class Vm;

// What a mod added to the game, read from what its start-up code registered (the stand-ins kept every call of the constructors and builders): facts, not code. The client makes the
// items, blocks and keys from these with its own code; what a mod's Java methods would do (a block's onUse, an item's use) is not run.
struct ModItemDef {
    std::string id;     // "examplemod:ruby"
    std::string kind;   // the mod's class ("RubyItem", "BlockItem")
    std::string block;  // a block item: the block it places ("examplemod:ruby_block"), else empty
    std::string tab;    // the creative tab it is listed in ("minecraft:ingredients")
    int stacksTo = 64;
    int durability = 0;
    bool fireResistant = false;
    bool food = false;
    int nutrition = 0;
    float saturation = 0.0f;
    std::string rarity;  // "epic" ... empty for common
};

struct ModBlockDef {
    std::string id;
    std::string kind;
    float hardness = 0.0f;  // Properties.strength / destroyTime (a plain Properties.of() breaks at once)
    float resistance = 0.0f;
    int light = 0;          // Properties.lightLevel(state -> n)
    bool requiresTool = false;
    bool noOcclusion = false;  // glass-like: does not hide the faces behind it
    bool instabreak = false;
    std::string sound = "stone";  // the game's sound set ("stone", "wood", "amethyst_block", ...)
};

// A key the mod asked for (KeyMapping): its translation key, the key it starts on and the category it is listed in.
struct ModKeyDef {
    std::string name;      // "key.examplemod.open"
    std::string category;  // "key.categories.examplemod" or "key.category.examplemod.main"
    int key = -1;          // a GLFW key code (the same numbers the game uses), or a mouse button
    bool mouse = false;
};

struct ModContent {
    std::string modId;
    std::vector<ModItemDef> items;
    std::vector<ModBlockDef> blocks;
    std::vector<ModKeyDef> keys;
    [[nodiscard]] bool empty() const { return items.empty() && blocks.empty() && keys.empty(); }
};

// Reads the registrations the VM captured (items, blocks, key mappings and the creative tabs they were put into) into facts.
[[nodiscard]] ModContent extractContent(Vm& vm, const std::string& modId);

}  // namespace falcon::jvm
