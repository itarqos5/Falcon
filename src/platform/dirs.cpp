#include "falcon/platform/dirs.hpp"

#include <cstdlib>

namespace falcon {

namespace {

// An environment variable that is set to something.
std::optional<std::filesystem::path> environmentPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

}  // namespace

std::optional<std::filesystem::path> minecraftDir() {
#ifdef _WIN32
    if (const auto appData = environmentPath("APPDATA")) return *appData / ".minecraft";
    return std::nullopt;
#else
    if (const auto home = environmentPath("HOME")) return *home / ".minecraft";
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> falconDataDir() {
#ifdef _WIN32
    if (const auto local = environmentPath("LOCALAPPDATA")) return *local / "Falcon";
    return std::nullopt;
#else
    // The XDG rule: $XDG_DATA_HOME when it is an absolute path, else ~/.local/share.
    if (const auto data = environmentPath("XDG_DATA_HOME"); data && data->is_absolute()) return *data / "Falcon";
    if (const auto home = environmentPath("HOME")) return *home / ".local" / "share" / "Falcon";
    return std::nullopt;
#endif
}

}  // namespace falcon
