#include "falcon/game/block_placement.hpp"

#include "falcon/world/block_render.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace falcon {

namespace {

constexpr const char* kFaceName[6] = {"east", "west", "up", "down", "south", "north"};  // by Face: the side of a block

bool endsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool hasProperty(const BlockType& type, const char* name) {
    return std::any_of(type.properties.begin(), type.properties.end(), [&](const BlockProperty& p) { return p.name == name; });
}

bool hasValue(const BlockType& type, const char* property, const char* value) {
    for (const BlockProperty& p : type.properties) {
        if (p.name == property) return std::find(p.values.begin(), p.values.end(), value) != p.values.end();
    }
    return false;
}

// Direction.fromYRot: 0 degrees looks south (+Z), 90 west, 180 north, 270 east.
int horizontalFace(float yaw) {
    const int quadrant = static_cast<int>(std::floor(yaw / 90.0f + 0.5f)) & 3;
    static constexpr int kByQuadrant[4] = {kSouth, kWest, kNorth, kEast};
    return kByQuadrant[quadrant];
}

int opposite(int face) { return face ^ 1; }  // east/west, up/down, south/north are pairs

// Direction.getNearestLookingDirection: the way the player looks, up and down included.
int lookingFace(float yaw, float pitch) {
    if (pitch > 60.0f) return kDown;   // (positive pitch looks down)
    if (pitch < -60.0f) return kUp;
    return horizontalFace(yaw);
}

int clockwise(int face) {  // Direction.getClockWise (about Y)
    switch (face) {
        case kNorth: return kEast;
        case kEast: return kSouth;
        case kSouth: return kWest;
        default: return kNorth;
    }
}

}  // namespace

Block blockStateForPlacement(const PlacementContext& context) {
    const BlockRegistry& registry = context.registry;
    const BlockType& type = registry.type(context.blockIndex);
    const std::string& name = type.name;
    Block state = registry.defaultState(context.blockIndex);
    const int clicked = context.hit.face;
    const bool sideClicked = clicked != kUp && clicked != kDown;
    const double cursorY = context.cursor[1];
    const int player = horizontalFace(context.yaw);
    const auto set = [&](const char* property, const char* value) { state = registry.with(state, property, value); };

    // StandingAndWallBlockItem: a torch used on the side of a block hangs there as the wall torch (facing away from the wall); on top of one it stands.
    const bool torch = endsWith(name, "torch") && name.find("wall_") == std::string::npos;
    if (torch && sideClicked) {
        // torch -> wall_torch, soul_torch -> soul_wall_torch, redstone_torch -> redstone_wall_torch
        const std::string wall = name.substr(0, name.size() - 5) + "wall_torch";
        if (const auto block = registry.findBlock(wall)) return registry.with(registry.defaultState(static_cast<int>(*block)), "facing", kFaceName[clicked]);
    }

    if (hasProperty(type, "axis") && !endsWith(name, "portal")) {  // logs, pillars, chains, hay: along the face that was clicked
        set("axis", clicked == kEast || clicked == kWest ? "x" : clicked == kUp || clicked == kDown ? "y" : "z");
        return state;
    }
    if (endsWith(name, "_stairs")) {  // StairBlock: faces the player, upside down from the lower half of a side or from underneath
        set("facing", kFaceName[player]);
        set("half", clicked == kDown || (sideClicked && cursorY > 0.5) ? "top" : "bottom");
        return state;
    }
    if (endsWith(name, "_slab") && hasProperty(type, "type")) {  // SlabBlock: the half of the block that was clicked
        set("type", clicked == kDown || (sideClicked && cursorY > 0.5) ? "top" : "bottom");
        return state;
    }
    if (endsWith(name, "_trapdoor")) {  // TrapDoorBlock
        if (sideClicked) {
            set("facing", kFaceName[clicked]);
            set("half", cursorY > 0.5 ? "top" : "bottom");
        } else {
            set("facing", kFaceName[opposite(player)]);
            set("half", clicked == kUp ? "bottom" : "top");
        }
        return state;
    }
    if (endsWith(name, "_door") && hasProperty(type, "hinge")) {  // DoorBlock: faces the player; the hinge is on the side the cursor is nearer to
        set("facing", kFaceName[player]);
        set("half", "lower");
        const double along = (player == kNorth || player == kSouth) ? context.cursor[0] : context.cursor[2];
        const bool towardPositive = player == kNorth || player == kEast;  // (which end of the cross axis is the "left" one seen from the front)
        set("hinge", (along < 0.5) == towardPositive ? "right" : "left");
        return state;
    }
    if (endsWith(name, "_fence_gate")) {
        set("facing", kFaceName[player]);
        return state;
    }
    if ((name == "lever" || endsWith(name, "_button")) && hasProperty(type, "face")) {  // FaceAttachedHorizontalDirectionalBlock
        if (sideClicked) {
            set("face", "wall");
            set("facing", kFaceName[clicked]);
        } else {
            set("face", clicked == kUp ? "floor" : "ceiling");
            set("facing", kFaceName[player]);
        }
        return state;
    }
    if (name == "repeater" || name == "comparator") {  // facing the player: the input is on the player's side
        set("facing", kFaceName[opposite(player)]);
        return state;
    }
    if (name == "anvil" || name == "chipped_anvil" || name == "damaged_anvil") {
        set("facing", kFaceName[clockwise(player)]);
        return state;
    }
    if (name == "ladder" && sideClicked) {
        set("facing", kFaceName[clicked]);
        return state;
    }
    if (name == "end_rod" || name == "lightning_rod") {
        set("facing", kFaceName[clicked]);
        return state;
    }
    if ((endsWith(name, "lantern")) && hasProperty(type, "hanging")) {
        set("hanging", clicked == kDown ? "true" : "false");
        return state;
    }
    if (hasProperty(type, "facing")) {
        if (hasValue(type, "facing", "up")) {  // pistons, dispensers, droppers, observers, barrels: the face is toward the player
            set("facing", kFaceName[opposite(lookingFace(context.yaw, context.pitch))]);
        } else {  // furnaces, chests, pumpkins ...: the front faces the player
            set("facing", kFaceName[opposite(player)]);
        }
        return state;
    }
    if (hasProperty(type, "horizontal_axis")) {
        set("horizontal_axis", player == kEast || player == kWest ? "x" : "z");
    }
    return state;
}

}  // namespace falcon
