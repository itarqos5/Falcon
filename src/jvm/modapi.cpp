// Where the platform jar comes from (modapi/falcon-modapi.jar: Falcon's own Java classes for the game and the mod loader's API, see modapi/build.ps1).
#include "falcon/jvm/modapi.hpp"

#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace falcon::jvm {

std::shared_ptr<const ZipArchive> modApiJar() {
    static std::once_flag once;
    static std::shared_ptr<const ZipArchive> jar;
    std::call_once(once, [] {
        std::vector<std::filesystem::path> candidates;
        if (const char* env = std::getenv("FALCON_MODAPI_JAR")) candidates.emplace_back(env);
#ifdef FALCON_MODAPI_DEV_JAR
        candidates.emplace_back(FALCON_MODAPI_DEV_JAR);  // (the one in the source tree: for runs from a build folder)
#endif
        for (const auto& path : candidates) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) continue;
            if (auto zip = ZipArchive::open(path)) {
                jar = std::make_shared<const ZipArchive>(std::move(*zip));
                return;
            }
        }
    });
    return jar;
}

}  // namespace falcon::jvm
