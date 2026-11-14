#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace falcon {

inline constexpr int kDefaultServerPort = 25565;

struct ServerAddress {
    std::string host;
    int port = kDefaultServerPort;
};

// Splits what the player typed into a host and a port: "host", "host:port", "[ipv6]" and
// "[ipv6]:port" (a bare IPv6 address with several colons is all host). The port defaults to 25565.
// Nullopt when the host is empty, a bracket is unbalanced or the port is not a number in 0..65535.
[[nodiscard]] std::optional<ServerAddress> parseServerAddress(std::string_view text);

// Whether the Done / Join Server buttons of the server screens are enabled for `text`.
[[nodiscard]] inline bool isValidServerAddress(std::string_view text) { return parseServerAddress(text).has_value(); }

}  // namespace falcon
