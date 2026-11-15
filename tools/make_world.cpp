// Makes the folder of a new world, the way the Create New World screen does (level.dat and session.lock), without any screen: for scripted runs (the CI builds, the benchmarks)
// that need a world to open with --play.
//
//   falcon_make_world <saves folder> <world folder name> <seed> [survival|creative]
#include "falcon/world/new_world.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fputs("usage: falcon_make_world <saves folder> <world folder name> <seed> [survival|creative]\n", stderr);
        return 2;
    }
    falcon::NewWorldSettings settings;
    settings.name = argv[2];
    if (argc > 4 && std::strcmp(argv[4], "creative") == 0) settings.mode = falcon::NewWorldMode::Creative;
    const std::int64_t seed = std::strtoll(argv[3], nullptr, 10);
    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code ec;
    std::filesystem::create_directories(argv[1], ec);
    std::string error;
    if (!falcon::createWorldFolder(argv[1], argv[2], settings, seed, now, error)) {
        std::fprintf(stderr, "cannot make the world: %s\n", error.c_str());
        return 1;
    }
    std::printf("made %s\n", (std::filesystem::path(argv[1]) / argv[2]).string().c_str());
    return 0;
}
