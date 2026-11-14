#include "apex/util/server_address.hpp"

namespace apex {
namespace {

// "" means no port (the default applies); nullopt means the port text is not valid.
std::optional<int> parsePort(std::string_view text) {
    if (text.empty()) return kDefaultServerPort;
    long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
        if (value > 65535) return std::nullopt;
    }
    return static_cast<int>(value);
}

}  // namespace

std::optional<ServerAddress> parseServerAddress(std::string_view text) {
    std::string_view host = text;
    std::string_view portText;

    if (!text.empty() && text.front() == '[') {  // [ipv6] or [ipv6]:port
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        host = text.substr(1, close - 1);
        const std::string_view rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return std::nullopt;
            portText = rest.substr(1);
        }
    } else {
        const std::size_t colon = text.find(':');
        if (colon != std::string_view::npos && text.find(':', colon + 1) == std::string_view::npos) {
            host = text.substr(0, colon);  // exactly one colon: host:port
            portText = text.substr(colon + 1);
        }  // none, or several without brackets: all of it is the host
    }
    if (host.empty()) return std::nullopt;
    const auto port = parsePort(portText);
    if (!port) return std::nullopt;
    return ServerAddress{std::string(host), *port};
}

}  // namespace apex
