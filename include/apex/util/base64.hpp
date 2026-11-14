#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apex {

// Standard Base64 (RFC 4648, "+/" alphabet, "=" padding): what servers.dat stores a server's icon as,
// and what a server's status reply carries its favicon as.
[[nodiscard]] std::string base64Encode(const std::vector<unsigned char>& bytes);
// Decodes `text`; line breaks are ignored (like the game, which strips "\n" from a favicon). Nullopt
// if it holds anything else outside the alphabet, or the length is impossible.
[[nodiscard]] std::optional<std::vector<unsigned char>> base64Decode(std::string_view text);

}  // namespace apex
