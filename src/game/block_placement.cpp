#include "falcon/game/block_placement.hpp"

namespace falcon {

Block blockStateForPlacement(const PlacementContext& context) { return context.registry.defaultState(context.blockIndex); }

}  // namespace falcon
