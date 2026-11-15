#pragma once

#include "falcon/assets/nbt.hpp"
#include "falcon/net/bytes.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/world/chunk.hpp"
#include "falcon/world/item_stack.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace falcon::net {

// A tag as it travels in a packet since 1.20.2: the type byte and the payload, with no name for the root.
[[nodiscard]] NbtTag readNetworkNbt(Reader& r);

// What a translation key says (the language of the player); without one a key is shown as it is.
void setComponentTranslator(std::function<std::string(const std::string&)> translate);

// The text of a component written as JSON ({"text":"..."}, {"translate":"..."}, a string, a list), as plain text; the text itself when it is not JSON.
[[nodiscard]] std::string componentJsonToPlain(const std::string& json);

// A text component (a plain string tag or a compound: text, translate, color, bold, italic, underlined, strikethrough, extra) as coloured runs of text.
[[nodiscard]] StyledText componentToStyled(const NbtTag& component);
[[nodiscard]] StyledText readComponent(Reader& r);  // (reads the tag first)

// The chunk of a level_chunk_with_light packet, from just after the chunk position: heightmaps, then the sections of blocks and biomes. `stateCount` is how many block
// states there are (a palette of more than eight bits is the state's number as it is). Null and `error` when the data does not read.
[[nodiscard]] std::shared_ptr<Chunk> decodeChunk(Reader& r, int stateCount, std::string& error);

// The numbers (in the game's data component type registry) of the components of an item that Falcon can read; -1 for one the game's data does not have.
// A component that is not one of these has no layout known here and cannot be stepped over.
struct ComponentIds {
    int damage = -1, repairCost = -1, maxStackSize = -1, maxDamage = -1, unbreakable = -1, customName = -1, itemName = -1, enchantments = -1, glintOverride = -1,
        tooltipDisplay = -1, customData = -1;
};

// An item stack as the network writes it (ItemStack.OPTIONAL_STREAM_CODEC): the count, the item's number, then the components that differ from the item's own.
// `known` turns false when a component that cannot be read was met: the stack is then what was read of it, and the reader is not after it.
[[nodiscard]] ItemStack readItemStack(Reader& r, const ComponentIds& ids, bool& known);
// An LpVec3, the low-precision vector the game sends for velocities and for the point of a click on an entity: read, and written the way the game writes it.
void readLpVec3(Reader& r, double& x, double& y, double& z);
void writeLpVec3(Writer& w, double x, double y, double z);

// One offer of a villager (MerchantOffer): what it costs (one or two things), what it gives, whether it is used up, and what makes the price move.
struct TradeOffer {
    ItemStack costA, costB, result;  // costB is empty for a trade of one thing
    bool outOfStock = false;
    int uses = 0, maxUses = 0, xp = 0, specialPriceDiff = 0, demand = 0;
    float priceMultiplier = 0.0f;
};
// What ClientboundMerchantOffersPacket says about a villager's window.
struct TradeOffers {
    int containerId = 0;
    std::vector<TradeOffer> offers;
    int level = 0, xp = 0;  // how far the villager is in its trade levels
    bool showProgress = false, canRestock = false;
};
// A trade offer as the network writes it. False when a component that cannot be read is in it (nothing more of the packet can then be read).
[[nodiscard]] bool readTradeOffer(Reader& r, const ComponentIds& ids, TradeOffer& out);

// The same the other way, without components (what a creative slot says to the server).
void writeItemStack(Writer& w, const ItemStack& stack);

}  // namespace falcon::net
