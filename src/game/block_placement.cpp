#include "falcon/game/block_placement.hpp"

#include "falcon/world/block_render.hpp"

#include <string>

namespace falcon {

Block blockStateForPlacement(const PlacementContext& context) {
    const BlockType& type = context.registry.type(context.blockIndex);
    // StandingAndWallBlockItem: a torch used on the side of a block hangs there as the wall torch (facing away from the wall); on top of one it stands.
    const std::string& name = type.name;
    const bool torch = name.size() >= 5 && name.compare(name.size() - 5, 5, "torch") == 0 && name.find("wall_") == std::string::npos;
    if (torch && context.hit.face != kUp && context.hit.face != kDown) {
        // torch -> wall_torch, soul_torch -> soul_wall_torch, redstone_torch -> redstone_wall_torch
        const std::string wall = name.substr(0, name.size() - 5) + "wall_torch";
        if (const auto block = context.registry.findBlock(wall)) {
            static constexpr const char* kFacing[6] = {"east", "west", "up", "down", "south", "north"};  // by Face: the side that was clicked
            return context.registry.with(context.registry.defaultState(static_cast<int>(*block)), "facing", kFacing[context.hit.face]);
        }
    }
    return context.registry.defaultState(context.blockIndex);
}

}  // namespace falcon
