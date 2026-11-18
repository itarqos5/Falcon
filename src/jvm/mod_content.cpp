#include "falcon/jvm/mod_content.hpp"

#include "falcon/jvm/vm.hpp"

#include "shim_util.hpp"

#include <algorithm>

namespace falcon::jvm {
namespace {

// The last part of a game constant's name: Rarity.EPIC -> "epic", SoundType.AMETHYST_BLOCK -> "amethyst_block".
std::string constantOf(const Object* o) {
    if (o == nullptr || !o->shim || o->shim->origin.empty()) return {};
    return lowerCaseOf(o->shim->origin.substr(o->shim->origin.find_last_of('.') + 1));
}

const Object* propertiesOf(const Object* thing) {
    const ShimCall* init = firstCall(thing, "<init>");
    if (init == nullptr) return nullptr;
    for (std::size_t i = 0; i < init->raw.size(); ++i) {
        if (init->kinds[i] == 'A' && shimClassHas(init->raw[i].a, "Properties")) return init->raw[i].a;
    }
    return nullptr;
}

void readItemProperties(const Object* props, ModItemDef& d) {
    if (props == nullptr) return;
    for (const ShimCall& c : props->shim->calls) {
        if (c.method == "stacksTo" && !c.raw.empty()) d.stacksTo = std::clamp(c.raw[0].i, 1, 99);
        else if (c.method == "durability" && !c.raw.empty()) d.durability = std::max(0, c.raw[0].i);
        else if (c.method == "fireResistant" || c.method == "fireproof") d.fireResistant = true;
        else if (c.method == "rarity" && !c.raw.empty()) d.rarity = constantOf(c.raw[0].a);
        else if (c.method == "food" && !c.raw.empty() && c.raw[0].a != nullptr && c.raw[0].a->shim) {
            d.food = true;
            for (const ShimCall& f : c.raw[0].a->shim->calls) {
                if (f.method == "nutrition" && !f.raw.empty()) d.nutrition = f.raw[0].i;
                if ((f.method == "saturationModifier" || f.method == "saturationMod" || f.method == "saturation") && !f.raw.empty()) d.saturation = f.raw[0].f;
            }
        }
    }
    if (d.durability > 0) d.stacksTo = 1;
}

void readBlockProperties(Vm& vm, const Object* props, ModBlockDef& d) {
    if (props == nullptr) return;
    for (const ShimCall& c : props->shim->calls) {
        if (c.method == "strength" && !c.raw.empty()) {
            d.hardness = c.raw[0].f;
            d.resistance = c.raw.size() > 1 ? c.raw[1].f : c.raw[0].f;
        } else if ((c.method == "destroyTime" || c.method == "hardness") && !c.raw.empty()) {
            d.hardness = c.raw[0].f;
        } else if ((c.method == "explosionResistance" || c.method == "resistance") && !c.raw.empty()) {
            d.resistance = c.raw[0].f;
        } else if (c.method == "instabreak" || c.method == "breakInstantly") {
            d.instabreak = true;
            d.hardness = 0.0f;
        } else if (c.method == "requiresCorrectToolForDrops" || c.method == "requiresTool") {
            d.requiresTool = true;
        } else if (c.method == "noOcclusion" || c.method == "nonOpaque") {
            d.noOcclusion = true;
        } else if ((c.method == "lightLevel" || c.method == "luminance") && !c.raw.empty() && c.raw[0].a != nullptr) {
            try {  // the lambda answers for a state: ask it once, with a stand-in state
                vm.budget = vm.instructionsRun() + 20'000;
                Value state = valueOfRef(vm.makeShim("net/minecraft/world/level/block/state/BlockState"));
                d.light = std::clamp(vm.callHandler(c.raw[0].a, {"applyAsInt", "apply"}, "A", &state).i, 0, 15);
            } catch (const JavaThrow&) {
            } catch (const BudgetExceeded&) {
            }
        } else if (c.method == "sound" && !c.raw.empty()) {
            const std::string sound = constantOf(c.raw[0].a);
            if (!sound.empty()) d.sound = sound;
        }
    }
}

// Yarn and Mojang name the game's creative tabs a little differently.
std::string tabIdOf(const std::string& group) {
    const std::string name = lowerCaseOf(group);
    if (name == "food_and_drink" || name == "food_and_drinks") return "minecraft:food_and_drinks";
    if (name == "tools" || name == "tools_and_utilities") return "minecraft:tools_and_utilities";
    if (name == "functional" || name == "functional_blocks") return "minecraft:functional_blocks";
    if (name == "natural" || name == "natural_blocks") return "minecraft:natural_blocks";
    if (name == "redstone" || name == "redstone_blocks") return "minecraft:redstone_blocks";
    if (name == "colored_blocks") return "minecraft:colored_blocks";
    if (name == "spawn_eggs") return "minecraft:spawn_eggs";
    return "minecraft:" + name;
}

ModKeyDef readKey(const Object* mapping) {
    ModKeyDef k;
    const ShimCall* init = firstCall(mapping, "<init>");
    if (init == nullptr) return k;
    bool haveKey = false;
    for (std::size_t i = 0; i < init->raw.size(); ++i) {
        const Value v = init->raw[i];
        if (init->kinds[i] == 'I') {
            if (!haveKey) {
                k.key = v.i;
                haveKey = true;
            }
        } else if (init->kinds[i] == 'A' && v.a != nullptr) {
            if (v.a->kind == Kind::String) {
                (k.name.empty() ? k.name : k.category) = v.a->str;
            } else if (v.a->shim && !v.a->str.empty() && shimClassHas(v.a, "Category")) {
                k.category = v.a->str;
            } else if (v.a->shim && v.a->shim->origin.find("MOUSE") != std::string::npos) {
                k.mouse = true;
            }
        }
    }
    return k;
}

}  // namespace

ModContent extractContent(Vm& vm, const std::string& modId) {
    ModContent out;
    out.modId = modId;
    const std::vector<Registration> registrations = vm.capture.registrations;  // (a copy: reading a lambda below can register more)
    // blocks first: a block item names its block
    for (const Registration& reg : registrations) {
        if (reg.registry != "block" || reg.object == nullptr) continue;
        ModBlockDef b;
        b.id = reg.id;
        b.kind = reg.kind;
        readBlockProperties(vm, propertiesOf(reg.object), b);
        out.blocks.push_back(std::move(b));
    }
    for (const Registration& reg : registrations) {
        if (reg.registry != "item" || reg.object == nullptr) continue;
        ModItemDef d;
        d.id = reg.id;
        d.kind = reg.kind;
        d.tab = "minecraft:ingredients";
        if (const ShimCall* init = firstCall(reg.object, "<init>")) {
            for (std::size_t i = 0; i < init->raw.size(); ++i) {  // new BlockItem(block, properties)
                if (init->kinds[i] != 'A' || init->raw[i].a == nullptr) continue;
                for (const Registration& other : registrations) {
                    if (other.registry == "block" && other.object == init->raw[i].a) {
                        d.block = other.id;
                        d.tab = "minecraft:building_blocks";
                    }
                }
            }
        }
        readItemProperties(propertiesOf(reg.object), d);
        out.items.push_back(std::move(d));
    }
    // a block with no item of its own is placed by the item of the same name only when the mod made one: nothing is made up here
    for (const GroupItem& g : vm.capture.groupItems) {
        const std::string id = itemIdOf(vm, g.item);
        for (ModItemDef& item : out.items) {
            if (item.id == id || (!item.block.empty() && item.block == id)) item.tab = tabIdOf(g.group);
        }
    }
    for (const Object* mapping : vm.capture.keyObjects) {
        ModKeyDef k = readKey(mapping);
        if (!k.name.empty()) out.keys.push_back(std::move(k));
    }
    return out;
}

}  // namespace falcon::jvm
