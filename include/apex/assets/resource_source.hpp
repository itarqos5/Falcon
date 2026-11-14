#pragma once

#include "apex/assets/resource_location.hpp"
#include "apex/assets/zip_archive.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace apex {

// One resource pack's files. A location like minecraft:textures/gui/title/minecraft.png is
// <pack>/assets/minecraft/textures/gui/title/minecraft.png inside the pack. Reading is thread-safe.
class ResourceSource {
public:
    virtual ~ResourceSource() = default;
    [[nodiscard]] virtual bool contains(const ResourceLocation& loc) const = 0;
    [[nodiscard]] virtual std::optional<std::vector<unsigned char>> read(
        const ResourceLocation& loc) const = 0;
};

// A pack that is a folder.
[[nodiscard]] std::shared_ptr<const ResourceSource> makeDirectoryPack(std::filesystem::path packRoot);
// A pack that is a .zip (see ZipArchive: nothing is loaded until a file is asked for).
[[nodiscard]] std::shared_ptr<const ResourceSource> makeZipPack(ZipArchive zip);

}  // namespace apex
