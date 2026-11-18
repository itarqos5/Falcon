#pragma once

#include "falcon/assets/zip_archive.hpp"

#include <memory>

namespace falcon::jvm {

// Falcon's Java classes for the game, the mod loader's API and the parts of the JDK the VM has no native class for. Null when the jar cannot be found (the mods then run against stand-ins only).
[[nodiscard]] std::shared_ptr<const ZipArchive> modApiJar();

}  // namespace falcon::jvm
