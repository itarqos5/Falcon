#pragma once

#include "falcon/assets/nbt.hpp"
#include "falcon/net/bytes.hpp"
#include "falcon/text/styled_text.hpp"
#include "falcon/world/chunk.hpp"

#include <functional>
#include <memory>
#include <string>

namespace falcon::net {

// A tag as it travels in a packet since 1.20.2: the type byte and the payload, with no name for the root.
[[nodiscard]] NbtTag readNetworkNbt(Reader& r);

// What a translation key says (the language of the player); without one a key is shown as it is.
void setComponentTranslator(std::function<std::string(const std::string&)> translate);

// A text component (a plain string tag or a compound: text, translate, color, bold, italic, underlined, strikethrough, extra) as coloured runs of text.
[[nodiscard]] StyledText componentToStyled(const NbtTag& component);
[[nodiscard]] StyledText readComponent(Reader& r);  // (reads the tag first)

// The chunk of a level_chunk_with_light packet, from just after the chunk position: heightmaps, then the sections of blocks and biomes. `stateCount` is how many block
// states there are (a palette of more than eight bits is the state's number as it is). Null and `error` when the data does not read.
[[nodiscard]] std::shared_ptr<Chunk> decodeChunk(Reader& r, int stateCount, std::string& error);

}  // namespace falcon::net
