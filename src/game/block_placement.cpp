#include "apex/game/block_placement.hpp"

namespace apex {

Block blockStateForPlacement(const PlacementContext& context) { return context.registry.defaultState(context.blockIndex); }

}  // namespace apex
